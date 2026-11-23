// a PNG, row by row, see png_decode.h
#include "png_decode.h"
#include <stdlib.h>
#include <string.h>

#define WIN_PIECE  4096u                      /* the window in pieces the heap always has */
#define WIN_PIECES (32768u / WIN_PIECE)
#define ROW_MAX    (1 + PNG_MAX_W * 4)        /* the filter byte, then RGBA at most */

/* a Huffman code the canonical way (RFC 1951): how many codes of each length, then the
 * symbols in code order. decoding walks the lengths a bit at a time */
typedef struct { int16_t count[16], symbol[288]; } huff_t;

typedef struct {
    // the file
    png_read_fn rd;
    void       *rctx;
    uint8_t     in[1024];
    int         pos, len;
    uint32_t    idat_left;                    /* bytes of the IDAT chunk not read yet */
    // the inflater
    uint32_t    bitbuf;
    int         bitcnt, bad;
    uint8_t    *win[WIN_PIECES];              /* what came out, the last 32 KB */
    uint32_t    out;                          /* bytes out so far */
    huff_t      lencode, distcode;
    // the rows
    png_info_t  info;
    png_row_fn  row;
    void       *ctx;
    uint8_t     rows[2][ROW_MAX];             /* this row and the one above: filters need it */
    int         cur, filled, rowlen, y;
} st_t;

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* --- the file: bytes, chunks --- */

// make sure n bytes wait in in[] (n <= its size), 0 if the file ends first
static int need(st_t *s, int n)
{
    if (s->len - s->pos >= n) return 1;
    memmove(s->in, s->in + s->pos, (size_t)(s->len - s->pos));
    s->len -= s->pos;
    s->pos = 0;
    while (s->len < n) {
        int r = s->rd(s->rctx, s->in + s->len, (int)sizeof s->in - s->len);
        if (r <= 0) return 0;
        s->len += r;
    }
    return 1;
}

static int skip(st_t *s, uint32_t n)
{
    while (n) {
        if (s->pos == s->len && !need(s, 1)) return 0;
        uint32_t k = (uint32_t)(s->len - s->pos);
        if (k > n) k = n;
        s->pos += (int)k;
        n -= k;
    }
    return 1;
}

// the next byte of the compressed picture: it runs on from one IDAT chunk into the next
static int idat_byte(st_t *s)
{
    while (!s->idat_left) {
        if (!skip(s, 4) || !need(s, 8)) return -1;     // the CRC of the one before, the next one's head
        if (memcmp(s->in + s->pos + 4, "IDAT", 4)) return -1;
        s->idat_left = be32(s->in + s->pos);
        s->pos += 8;
    }
    if (s->pos == s->len && !need(s, 1)) return -1;
    s->idat_left--;
    return s->in[s->pos++];
}

/* --- the rows: each byte that comes out of the inflater --- */

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

// undo a row's filter: each byte was stored as the difference from its neighbours
static int unfilter(uint8_t *c, const uint8_t *p, int n, int bpp, int f)
{
    switch (f) {
    case 0: break;
    case 1: for (int i = bpp; i < n; i++) c[i] = (uint8_t)(c[i] + c[i - bpp]); break;
    case 2: for (int i = 0; i < n; i++) c[i] = (uint8_t)(c[i] + p[i]); break;
    case 3: for (int i = 0; i < n; i++) c[i] = (uint8_t)(c[i] + (((i >= bpp ? c[i - bpp] : 0) + p[i]) >> 1)); break;
    case 4:
        for (int i = 0; i < n; i++)
            c[i] = (uint8_t)(c[i] + paeth(i >= bpp ? c[i - bpp] : 0, p[i], i >= bpp ? p[i - bpp] : 0));
        break;
    default: return 0;
    }
    return 1;
}

static void put(st_t *s, int b)
{
    s->win[(s->out / WIN_PIECE) % WIN_PIECES][s->out % WIN_PIECE] = (uint8_t)b;
    s->out++;
    if (s->y >= s->info.h) return;                     // past the picture: the stream's tail
    uint8_t *c = s->rows[s->cur];
    c[s->filled++] = (uint8_t)b;
    if (s->filled < s->rowlen) return;
    if (!unfilter(c + 1, s->rows[s->cur ^ 1] + 1, s->rowlen - 1, s->info.bpp, c[0])) { s->bad = 1; return; }
    s->row(s->ctx, &s->info, s->y++, c + 1);
    s->cur ^= 1;
    s->filled = 0;
}

/* --- inflate (RFC 1951) --- */

static int bits(st_t *s, int n)
{
    uint32_t v = s->bitbuf;
    while (s->bitcnt < n) {
        int b = idat_byte(s);
        if (b < 0) { s->bad = 1; return 0; }
        v |= (uint32_t)b << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = v >> n;
    s->bitcnt -= n;
    return (int)(v & ((1u << n) - 1));
}

static int decode(st_t *s, const huff_t *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= bits(s, 1);
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;                                         // no such code
}

// lengths -> code. 0 complete, > 0 some codes unused (allowed), < 0 too many: broken
static int construct(huff_t *h, const uint8_t *length, int n)
{
    int16_t offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[length[i]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= 15; len++) {
        left = (left << 1) - h->count[len];
        if (left < 0) return -1;
    }
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = (int16_t)(offs[len] + h->count[len]);
    for (int i = 0; i < n; i++) if (length[i]) h->symbol[offs[length[i]]++] = (int16_t)i;
    return left;
}

static const uint16_t LBASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                                    67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t  LEXT[29]  = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t DBASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t  DEXT[30]  = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

// a block's literals and copies, till its end code
static int codes(st_t *s)
{
    for (;;) {
        int sym = decode(s, &s->lencode);
        if (sym < 0 || s->bad) return 0;
        if (sym < 256) {
            put(s, sym);
        } else if (sym == 256) {
            return 1;
        } else {
            sym -= 257;
            if (sym >= 29) return 0;
            int len = LBASE[sym] + bits(s, LEXT[sym]);
            int ds = decode(s, &s->distcode);
            if (ds < 0 || ds >= 30) return 0;
            uint32_t dist = DBASE[ds] + (uint32_t)bits(s, DEXT[ds]);
            if (s->bad || dist > s->out) return 0;
            while (len--) {                            // the copy may overlap what it makes: byte by byte
                uint32_t at = s->out - dist;
                put(s, s->win[(at / WIN_PIECE) % WIN_PIECES][at % WIN_PIECE]);
            }
        }
        if (s->bad) return 0;
    }
}

static int stored(st_t *s)
{
    s->bitbuf = 0;                                     // to the next whole byte
    s->bitcnt = 0;
    int b[4];
    for (int i = 0; i < 4; i++) if ((b[i] = idat_byte(s)) < 0) return 0;
    int n = b[0] | b[1] << 8;
    if ((n ^ (b[2] | b[3] << 8)) != 0xFFFF) return 0;
    while (n--) {
        int c = idat_byte(s);
        if (c < 0) return 0;
        put(s, c);
    }
    return !s->bad;
}

static int fixed(st_t *s)
{
    uint8_t l[288];
    for (int i = 0; i < 288; i++) l[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
    construct(&s->lencode, l, 288);
    for (int i = 0; i < 30; i++) l[i] = 5;
    construct(&s->distcode, l, 30);
    return codes(s);
}

static int dynamic(st_t *s)
{
    static const uint8_t ORDER[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    uint8_t l[288 + 30];
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (s->bad || nlen > 286 || ndist > 30) return 0;
    memset(l, 0, 19);
    for (int i = 0; i < ncode; i++) l[ORDER[i]] = (uint8_t)bits(s, 3);
    if (construct(&s->lencode, l, 19) != 0) return 0;  // the code of the code lengths must be complete
    for (int i = 0; i < nlen + ndist; ) {
        int sym = decode(s, &s->lencode);
        if (sym < 0 || s->bad) return 0;
        if (sym < 16) { l[i++] = (uint8_t)sym; continue; }
        int v = 0, rep;
        if (sym == 16) {
            if (!i) return 0;
            v = l[i - 1];
            rep = 3 + bits(s, 2);
        } else {
            rep = sym == 17 ? 3 + bits(s, 3) : 11 + bits(s, 7);
        }
        if (i + rep > nlen + ndist) return 0;
        while (rep--) l[i++] = (uint8_t)v;
    }
    if (!l[256]) return 0;                             // no end code
    if (construct(&s->lencode, l, nlen) < 0 || construct(&s->distcode, l + nlen, ndist) < 0) return 0;
    return codes(s);
}

static int inflate(st_t *s)
{
    int cmf = idat_byte(s), flg = idat_byte(s);        // the zlib wrapper: deflate, no preset dictionary
    if (cmf < 0 || flg < 0 || (cmf & 15) != 8 || (cmf * 256 + flg) % 31 || (flg & 32)) return 0;
    int last;
    do {
        last = bits(s, 1);
        int type = bits(s, 2), ok;
        if (s->bad) return 0;
        if (type == 0) ok = stored(s);
        else if (type == 1) ok = fixed(s);
        else if (type == 2) ok = dynamic(s);
        else ok = 0;
        if (!ok) return 0;
    } while (!last && s->y < s->info.h);
    return 1;                                          // its checksum is not read: the rows are already out
}

/* --- the file --- */

int png_decode(png_read_fn rd, void *rctx, png_head_fn head, png_row_fn row, void *ctx)
{
    static const uint8_t SIG[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    static const int8_t BPP[7] = { 1, 0, 3, 1, 2, 0, 4 };
    st_t *s = calloc(1, sizeof *s);
    if (!s) return PNG_NO_MEMORY;
    int res = PNG_BAD;
    for (unsigned i = 0; i < WIN_PIECES; i++)
        if (!(s->win[i] = malloc(WIN_PIECE))) { res = PNG_NO_MEMORY; goto done; }
    s->rd = rd;
    s->rctx = rctx;
    s->row = row;
    s->ctx = ctx;
    png_info_t *in = &s->info;
    if (!need(s, 8) || memcmp(s->in, SIG, 8)) goto done;
    s->pos = 8;
    for (;;) {
        if (!need(s, 8)) goto done;
        uint32_t len = be32(s->in + s->pos);
        char type[5];
        memcpy(type, s->in + s->pos + 4, 4);
        type[4] = 0;
        s->pos += 8;
        if (!strcmp(type, "IHDR")) {
            if (len != 13 || !need(s, 13)) goto done;
            const uint8_t *h = s->in + s->pos;
            in->w = (int)be32(h);
            in->h = (int)be32(h + 4);
            in->type = h[9];
            if (h[8] != 8 || in->type > 6 || !BPP[in->type] || h[12] != 0) goto done;   // 8 bits, not interlaced
            if (in->w < 1 || in->w > PNG_MAX_W || in->h < 1) goto done;
            in->bpp = BPP[in->type];
            s->rowlen = 1 + in->w * in->bpp;
            if (!skip(s, 13 + 4)) goto done;
        } else if (!strcmp(type, "PLTE")) {
            if (len > 768 || len % 3 || !need(s, (int)len)) goto done;
            memcpy(in->pal, s->in + s->pos, len);
            in->npal = (int)len / 3;
            if (!skip(s, len + 4)) goto done;
        } else if (!strcmp(type, "IDAT")) {
            if (!s->rowlen || (in->type == 3 && !in->npal)) goto done;
            if (head && !head(ctx, in)) goto done;
            s->idat_left = len;
            if (inflate(s) && s->y == in->h) res = PNG_OK;
            goto done;                                 // what follows the picture is not needed
        } else if (!strcmp(type, "IEND")) {
            goto done;
        } else if (!skip(s, len + 4)) {
            goto done;
        }
    }
done:
    for (unsigned i = 0; i < WIN_PIECES; i++) free(s->win[i]);
    free(s);
    return res;
}
