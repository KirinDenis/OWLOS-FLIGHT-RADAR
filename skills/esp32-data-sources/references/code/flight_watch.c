// the planes to follow, see watch.h
#include "watch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char upper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }

static int id_char(char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'; }

// "OE-IJW" and "OEIJW" are the same registration, "eju39nj" the same callsign: the dashes are left
// out and the case is not looked at
static int same_id(const char *a, const char *b)
{
    for (;;) {
        while (*a == '-') a++;
        while (*b == '-') b++;
        if (upper(*a) != upper(*b)) return 0;
        if (!*a) return 1;
        a++;
        b++;
    }
}

static int hex6(const char *s)
{
    int n = 0;
    for (; s[n]; n++) if (!((s[n] >= '0' && s[n] <= '9') || (s[n] >= 'A' && s[n] <= 'F'))) return 0;
    return n == 6;
}

int watch_set(watchlist_t *l, const char *text)
{
    watchlist_t was = *l;
    memset(l, 0, sizeof *l);
    while (*text && l->n < WATCH_MAX) {
        char id[WATCH_ID];
        int k = 0;
        while (*text && !id_char(upper(*text))) text++;          // commas, spaces, anything else between
        while (*text && id_char(upper(*text))) {
            if (k < WATCH_ID - 1) id[k++] = upper(*text);
            text++;
        }
        id[k] = 0;
        if (!k) continue;
        int twice = 0;
        for (int i = 0; i < l->n; i++) twice |= !strcmp(l->w[i].id, id);
        if (twice) continue;
        watch_t *w = &l->w[l->n++];
        for (int i = 0; i < was.n; i++) if (!strcmp(was.w[i].id, id)) *w = was.w[i];   // known: kept as it was
        memcpy(w->id, id, sizeof id);
    }
    return l->n;
}

void watch_text(const watchlist_t *l, char *out, int cap)
{
    int n = 0;
    out[0] = 0;
    for (int i = 0; i < l->n && n < cap - 1; i++) n += snprintf(out + n, (size_t)(cap - n), "%s%s", i ? ", " : "", l->w[i].id);
}

int watch_is(const watch_t *w, const plane_t *p)
{
    if (!w->id[0]) return 0;
    if (p->flight[0] && same_id(w->id, p->flight)) return 1;
    if (p->reg[0] && same_id(w->id, p->reg)) return 1;
    return hex6(w->id) && strtoul(w->id, NULL, 16) == p->icao;
}

int watch_count(const watchlist_t *l, int state)
{
    int n = 0;
    for (int i = 0; i < l->n; i++) n += l->w[i].state == state;
    return n;
}

int watch_near(const watchlist_t *l)
{
    int n = 0;
    for (int i = 0; i < l->n; i++) n += l->w[i].near;
    return n;
}

// a network had it: in the air or on the ground, round home or not. what happened (WATCH_NEWS_*)
static int heard(watch_t *w, const plane_t *p, int near, uint32_t now, char *say, int cap)
{
    int was = w->state, came = near && !w->near;
    w->state = p->ground ? WATCH_GROUND : WATCH_AIR;
    w->heard = now;
    w->near = (uint8_t)near;
    if (came) {
        snprintf(say, (size_t)cap, "%s IN RANGE", w->id);
        return WATCH_NEWS_RANGE;
    }
    if (w->state == WATCH_AIR && was != WATCH_AIR) {
        snprintf(say, (size_t)cap, "%s IS FLYING", w->id);
        return WATCH_NEWS_UP;
    }
    if (w->state == WATCH_GROUND && was == WATCH_AIR) {
        snprintf(say, (size_t)cap, "%s LANDED", w->id);
        return WATCH_NEWS_DOWN;
    }
    return WATCH_NEWS_NONE;
}

int watch_sky(watchlist_t *l, const sky_t *s, uint32_t now, char *say, int cap, uint32_t *pick)
{
    int news = WATCH_NEWS_NONE;
    for (int i = 0; i < l->n; i++) {
        watch_t *w = &l->w[i];
        const plane_t *p = NULL;
        for (int k = 0; k < s->n && !p; k++) if (watch_is(w, &s->p[k])) p = &s->p[k];
        if (!p) { w->near = 0; continue; }                         // gone from round home: the lookups take it
        int n = heard(w, p, 1, now, say, cap);
        if (n == WATCH_NEWS_RANGE) *pick = p->icao;
        if (n > news) news = n;
    }
    return news;
}

// a was asked about before b. never asked: before anything
static int older(const watch_t *a, const watch_t *b)
{
    if (!a->checked || !b->checked) return !a->checked && b->checked;
    return (int32_t)(a->checked - b->checked) < 0;
}

int watch_next(const watchlist_t *l)
{
    int best = -1;
    for (int i = 0; i < l->n; i++) {
        if (l->w[i].near) continue;
        if (best < 0 || older(&l->w[i], &l->w[best])) best = i;
    }
    return best;
}

/* what it may be, by its shape, in the order to ask. a dash: a registration, callsigns and
 * addresses have none. six hex digits: an address first, but ACA123 is a callsign too. the rest:
 * a callsign first, then a registration (N123AB has no dash) */
static int kinds(const watch_t *w, uint8_t *k)
{
    if (strchr(w->id, '-')) { k[0] = WATCH_REG; return 1; }
    if (hex6(w->id)) { k[0] = WATCH_HEX; k[1] = WATCH_CALLSIGN; return 2; }
    k[0] = WATCH_CALLSIGN;
    k[1] = WATCH_REG;
    return 2;
}

int watch_kind(const watch_t *w)
{
    if (w->kind != WATCH_ANY) return w->kind;
    uint8_t k[2];
    int n = kinds(w, k);
    return k[w->tries % n];
}

int watch_answer(watch_t *w, int kind, const plane_t *p, uint32_t now, char *say, int cap)
{
    w->checked = now ? now : 1;
    if (p) {
        w->kind = (uint8_t)kind;
        w->tries = 0;
        return heard(w, p, 0, now, say, cap);
    }
    uint8_t k[2];
    if (w->kind == WATCH_ANY && ++w->tries < kinds(w, k)) return WATCH_NEWS_NONE;   // the next kind it may be, next time
    w->tries = 0;
    // not heard: a flight over the sea is out of reach for a while. gone only after WATCH_GONE_MS,
    // so it is not said to be flying again when it comes back in reach
    if (w->state == WATCH_UNKNOWN || now - w->heard > WATCH_GONE_MS) w->state = WATCH_GONE;
    return WATCH_NEWS_NONE;
}
