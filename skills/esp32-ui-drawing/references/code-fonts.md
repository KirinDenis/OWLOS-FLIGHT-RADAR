# Fonts, complete

`core/font.h`, `core/font.c` (with the whole 3x5 table) and `tools/FontBake` from
OWLOS-FLIGHT-RADAR. Runs on hardware. The baked `font_*.c` files are not copied here, they are
generated: rebuild them with the lines in fonts-and-font-baking.md.

## font.h

```c
/* text, two kinds of fonts:
 * MICRO - 3x5 HUD font from my OWL FLY game (4x6 cell, 1 bit, caps/digits/few signs, int scale).
 *   used for instrument text: ring labels, degrees, channels, status lines.
 * AA fonts (font_*.c) - baked from TTF by tools/FontBake, 4 bit coverage. for network names,
 *   wich can be anything UTF-8: lower case, Slovak, Cyrillic... */
#pragma once
#include "gfx.h"

typedef struct {
    uint16_t cp;            /* Unicode code point */
    uint8_t  w, h;          /* ink box */
    int8_t   xo, yo;        /* offset from pen, yo is from line top */
    uint8_t  adv;           /* pen advance */
    uint8_t  pad;
    uint32_t off;           /* offset in bitmap. rows of ceil(w/2) bytes, high nibble first */
} glyph_t;

typedef struct {
    const glyph_t *glyphs;  /* sorted by cp, for binary search */
    uint16_t       count;
    uint8_t        line_h;  /* line height */
    uint8_t        ascent;  /* baseline, counted from line top */
    const uint8_t *bitmap;
} font_t;

extern const font_t FONT_SMALL;   // names in list and labels
extern const font_t FONT_MEDIUM;  // name of the tracked network
extern const font_t FONT_LARGE;   // big numbers
extern const font_t FONT_HUGE;    // target signal strength, full screen
// e-paper dashboard: DejaVu Sans Bold, read from across the room like ViewOwl's plates
extern const font_t FONT_PAPER_S;  // ~10 px, small labels
extern const font_t FONT_PAPER_M;  // ~13 px, names
extern const font_t FONT_PAPER_L;  // ~20 px, title, dBm
extern const font_t FONT_PAPER_XL; // ~34 px, the one big number
extern const font_t FONT_PAPER_XXL;// ~63 px, digits, minus and "~ M" only: the target's dBm

// next code point from UTF-8 string, bad bytes give '?'
uint32_t utf8_next(const char **s);

// x = pen, y = line top. returns advance in pixels
int font_text(canvas_t *cv, const font_t *f, int x, int y, const char *utf8, uint32_t c, int a);
int font_width(const font_t *f, const char *utf8);
// draws max maxw pixels, a cut string fades out on the right
int font_text_clip(canvas_t *cv, const font_t *f, int x, int y, const char *utf8, uint32_t c, int a, int maxw);

// MICRO: 3x5 glyph in a 4x6 cell, scale is integer
#define MICRO_ADV 4
#define MICRO_H   6
int micro_text(canvas_t *cv, int x, int y, const char *s, uint32_t c, int a, int scale);
static inline int micro_width(const char *s, int scale)
{
    int n = 0;
    while (*s++) n++;
    return n ? (n * MICRO_ADV - 1) * scale : 0;
}
```

## font.c

```c
#include "font.h"
#include <string.h>

/* --- MICRO, the HUD font from my OWL FLY game ---
 * first 41 glyphs are resource 1 of CITY.DAT, copied byte for byte.
 * 6 rows per glyph, bits 7..5 are the 3 columns. signs after '.' are new,
 * drawn for the radar in the same 3x5 grid */
static const uint8_t MICRO[][6] = {
    {0xE0,0xA0,0xA0,0xA0,0xE0,0x00}, /* '0' */
    {0x40,0xC0,0x40,0x40,0xE0,0x00}, /* '1' */
    {0xE0,0x20,0xE0,0x80,0xE0,0x00}, /* '2' */
    {0xE0,0x20,0xE0,0x20,0xE0,0x00}, /* '3' */
    {0xA0,0xA0,0xE0,0x20,0x20,0x00}, /* '4' */
    {0xE0,0x80,0xE0,0x20,0xE0,0x00}, /* '5' */
    {0xE0,0x80,0xE0,0xA0,0xE0,0x00}, /* '6' */
    {0xE0,0x20,0x40,0x40,0x40,0x00}, /* '7' */
    {0xE0,0xA0,0xE0,0xA0,0xE0,0x00}, /* '8' */
    {0xE0,0xA0,0xE0,0x20,0xE0,0x00}, /* '9' */
    {0x40,0xA0,0xE0,0xA0,0xA0,0x00}, /* 'A' */
    {0xC0,0xA0,0xC0,0xA0,0xC0,0x00}, /* 'B' */
    {0x60,0x80,0x80,0x80,0x60,0x00}, /* 'C' */
    {0xC0,0xA0,0xA0,0xA0,0xC0,0x00}, /* 'D' */
    {0xE0,0x80,0xC0,0x80,0xE0,0x00}, /* 'E' */
    {0xE0,0x80,0xC0,0x80,0x80,0x00}, /* 'F' */
    {0x60,0x80,0xA0,0xA0,0x60,0x00}, /* 'G' */
    {0xA0,0xA0,0xE0,0xA0,0xA0,0x00}, /* 'H' */
    {0xE0,0x40,0x40,0x40,0xE0,0x00}, /* 'I' */
    {0x20,0x20,0x20,0xA0,0x40,0x00}, /* 'J' */
    {0xA0,0xC0,0x80,0xC0,0xA0,0x00}, /* 'K' */
    {0x80,0x80,0x80,0x80,0xE0,0x00}, /* 'L' */
    {0xA0,0xE0,0xA0,0xA0,0xA0,0x00}, /* 'M' */
    {0xE0,0xA0,0xA0,0xA0,0xA0,0x00}, /* 'N' */
    {0xE0,0xA0,0xA0,0xA0,0xE0,0x00}, /* 'O' */
    {0xE0,0xA0,0xE0,0x80,0x80,0x00}, /* 'P' */
    {0xE0,0xA0,0xA0,0xE0,0x20,0x00}, /* 'Q' */
    {0xC0,0xA0,0xC0,0xA0,0xA0,0x00}, /* 'R' */
    {0xE0,0x80,0xE0,0x20,0xE0,0x00}, /* 'S' */
    {0xE0,0x40,0x40,0x40,0x40,0x00}, /* 'T' */
    {0xA0,0xA0,0xA0,0xA0,0xE0,0x00}, /* 'U' */
    {0xA0,0xA0,0xA0,0xA0,0x40,0x00}, /* 'V' */
    {0xA0,0xA0,0xA0,0xE0,0xA0,0x00}, /* 'W' */
    {0xA0,0xA0,0x40,0xA0,0xA0,0x00}, /* 'X' */
    {0xA0,0xA0,0x40,0x40,0x40,0x00}, /* 'Y' */
    {0xE0,0x20,0x40,0x80,0xE0,0x00}, /* 'Z' */
    {0x00,0x40,0xE0,0x40,0x00,0x00}, /* '+' */
    {0x00,0x00,0xE0,0x00,0x00,0x00}, /* '-' */
    {0x00,0x40,0x00,0x40,0x00,0x00}, /* ':' */
    {0xA0,0x20,0x40,0x80,0xA0,0x00}, /* '%' */
    {0x00,0x00,0x00,0x00,0x40,0x00}, /* '.' */
    /* --- drawn for RadarScan --- */
    {0x20,0x20,0x40,0x80,0x80,0x00}, /* '/' */
    {0x40,0xA0,0x40,0x00,0x00,0x00}, /* '*' as a degree sign */
    {0x20,0x40,0x80,0x40,0x20,0x00}, /* '<' */
    {0x80,0x40,0x20,0x40,0x80,0x00}, /* '>' */
    {0x00,0xE0,0x00,0xE0,0x00,0x00}, /* '=' */
    {0x40,0x80,0x80,0x80,0x40,0x00}, /* '(' */
    {0x40,0x20,0x20,0x20,0x40,0x00}, /* ')' */
    {0x40,0x40,0x40,0x00,0x40,0x00}, /* '!' */
    {0xE0,0x20,0x60,0x00,0x40,0x00}, /* '?' */
    {0x00,0x00,0x00,0x00,0x40,0x80}, /* ',' */
    {0x40,0x40,0x00,0x00,0x00,0x00}, /* '\'' */
    {0xC0,0x80,0x80,0x80,0xC0,0x00}, /* '[' */
    {0x60,0x20,0x20,0x20,0x60,0x00}, /* ']' */
    {0x00,0x00,0x00,0x00,0xE0,0x00}, /* '_' */
    {0xA0,0xE0,0xA0,0xE0,0xA0,0x00}, /* '#' */
    {0x00,0x00,0xC0,0x60,0x00,0x00}, /* '~' */
    {0x40,0x40,0x40,0x40,0x40,0x00}, /* '|' */
    {0x40,0xE0,0x00,0x00,0x00,0x00}, /* '^' */
};

static int micro_index(unsigned char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 10;   // no lower case, use caps
    static const char signs[] = "+-:%./*<>=()!?,'[]_#~|^";
    const char *p = strchr(signs, ch);
    return (p && ch) ? 36 + (int)(p - signs) : -1;      // careful: strchr finds the '\0' too
}

CORE_HOT int micro_text(canvas_t *cv, int x, int y, const char *s, uint32_t c, int a, int scale)
{
    int x0 = x;
    if (!gfx_touches(cv, y, y + MICRO_H * scale)) return micro_width(s, scale);   // not in this strip, just measure
    for (; *s; s++, x += MICRO_ADV * scale) {
        int gi = micro_index((unsigned char)*s);
        if (gi < 0) continue;
        const uint8_t *g = MICRO[gi];
        for (int r = 0; r < 6; r++) {
            uint8_t bits = g[r];
            if (!bits) continue;
            for (int sy = 0; sy < scale; sy++) {
                uint32_t *row = gfx_row(cv, y + r * scale + sy);
                if (!row) continue;
                for (int col = 0; col < 3; col++) {
                    if (!(bits & (0x80 >> col))) continue;
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + col * scale + sx;
                        if (px >= 0 && px < GFX_W)
                            row[px] = a >= 256 ? c : gfx_mix(row[px], c, (uint32_t)a);
                    }
                }
            }
        }
    }
    return x - x0 - scale;   // no gap after the last char
}

/* --- UTF-8 and the baked fonts --- */

uint32_t utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t c = *p++;
    int more = 0;
    if (c < 0x80) more = 0;
    else if ((c & 0xE0) == 0xC0) { c &= 0x1F; more = 1; }
    else if ((c & 0xF0) == 0xE0) { c &= 0x0F; more = 2; }
    else if ((c & 0xF8) == 0xF0) { c &= 0x07; more = 3; }
    else { *s = (const char *)p; return '?'; }
    while (more--) {
        if ((*p & 0xC0) != 0x80) { *s = (const char *)p; return '?'; }
        c = (c << 6) | (*p++ & 0x3F);
    }
    *s = (const char *)p;
    return c;
}

static const glyph_t *find_glyph(const font_t *f, uint32_t cp)
{
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uint32_t m = f->glyphs[mid].cp;
        if (m == cp) return &f->glyphs[mid];
        if (m < cp) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

static const glyph_t *glyph_or_fallback(const font_t *f, uint32_t cp)
{
    const glyph_t *g = find_glyph(f, cp);
    if (!g && cp >= 0x80) g = find_glyph(f, '?');   // emoji and such
    return g;
}

/* text loop for all baked fonts. limit = first column that must stay clear (or big number).
 * NOTE: cut names fade out over the last FADE px instead of "...", at six pixels
 * ellipsis looks like underscore and "My_Flat_" could be a real network name */
#define FADE 12
CORE_HOT static int draw_text(canvas_t *cv, const font_t *f, int x, int y, const char *utf8, uint32_t c, int a,
                     int limit, int fade)
{
    int x0 = x;
    int hit = gfx_touches(cv, y, y + f->line_h);
    while (*utf8) {
        const glyph_t *g = glyph_or_fallback(f, utf8_next(&utf8));
        if (!g) continue;
        if (x + g->xo >= limit) break;
        if (hit && g->w) {
            int stride = (g->w + 1) >> 1;
            const uint8_t *bits = f->bitmap + g->off;
            for (int r = 0; r < g->h; r++) {
                uint32_t *row = gfx_row(cv, y + g->yo + r);
                if (!row) continue;
                const uint8_t *b = bits + r * stride;
                for (int col = 0; col < g->w; col++) {
                    unsigned v = (b[col >> 1] >> ((col & 1) ? 0 : 4)) & 15;
                    if (!v) continue;
                    int px = x + g->xo + col;
                    if (px < 0 || px >= GFX_W || px >= limit) continue;
                    int k = (a * (int)(v * 17 + 1)) >> 8;   // v*17+1: 15 -> 256
                    // paper: the soft edges stay, paper.c rounds them to the panel's two greys
                    // (no dither), that is the halftone smoothing. no fade there, see font_text_clip
                    if (!RADAR_EPAPER && fade && px > limit - FADE) k = k * (limit - px) / FADE;
                    if (k > 0) row[px] = gfx_mix(row[px], c, (uint32_t)k);
                }
            }
        }
        x += g->adv;
    }
    return (x < limit ? x : limit) - x0;
}

int font_text(canvas_t *cv, const font_t *f, int x, int y, const char *utf8, uint32_t c, int a)
{
    return draw_text(cv, f, x, y, utf8, c, a, 1 << 20, 0);   // no limit
}

int font_text_clip(canvas_t *cv, const font_t *f, int x, int y, const char *utf8, uint32_t c, int a, int maxw)
{
    int cut = font_width(f, utf8) > maxw;
    if (RADAR_EPAPER && cut) {
        // paper can't fade, so whole letters and an ellipsis. at these sizes it can't pass for "_"
        int ew = font_width(f, "\xE2\x80\xA6");
        int w = 0;
        const char *s = utf8;
        while (*s) {                                  // letters that fit before the ellipsis
            const char *at = s;
            const glyph_t *g = glyph_or_fallback(f, utf8_next(&s));
            if (g && w + g->adv > maxw - ew) { s = at; break; }
            if (g) w += g->adv;
        }
        draw_text(cv, f, x, y, utf8, c, a, x + w, 0);
        draw_text(cv, f, x + w, y, "\xE2\x80\xA6", c, a, 1 << 20, 0);
        return w + ew;
    }
    return draw_text(cv, f, x, y, utf8, c, a, x + maxw, cut);
}

int font_width(const font_t *f, const char *utf8)
{
    int w = 0;
    while (*utf8) {
        const glyph_t *g = glyph_or_fallback(f, utf8_next(&utf8));
        if (g) w += g->adv;
    }
    return w;
}
```

## tools/FontBake/FontBake.csproj

Windows only: it uses WPF's `GlyphTypeface` for exact advances and GDI+ for the hinted rasteriser.

```xml
<Project Sdk="Microsoft.NET.Sdk">

  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net8.0-windows</TargetFramework>
    <UseWindowsForms>true</UseWindowsForms>
    <UseWPF>true</UseWPF>
    <Nullable>enable</Nullable>
    <ImplicitUsings>disable</ImplicitUsings>
  </PropertyGroup>

</Project>
```

## tools/FontBake/Program.cs

```csharp
// FontBake - bakes a TTF into a 4-bit antialiased C font for core/font.h
//   FontBake <font.ttf> <NAME> <advance-px> <ranges> <out.c> [sheet.png]
//   advance-px = monospace cell width. em size is picked so the advance is exactly
//                that, glyphs sit on whole pixels and stay crisp
//   ranges = code points, e.g. "32-126,160-383,1024-1119,8230"
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;

static class Program
{
    record Glyph(int Cp, int W, int H, int Xo, int Yo, int Adv, byte[] Bits);

    static int Main(string[] args)
    {
        if (args.Length < 5)
        {
            Console.Error.WriteLine("usage: FontBake <font.ttf> <NAME> <advance-px> <ranges> <out.c> [sheet.png]");
            return 2;
        }
        string ttf = Path.GetFullPath(args[0]);
        string name = args[1];
        int advPx = int.Parse(args[2], CultureInfo.InvariantCulture);
        var cps = ParseRanges(args[3]);
        string outC = args[4];
        string? sheet = args.Length > 5 ? args[5] : null;

        // WPF GlyphTypeface gives exact advances and wich chars the font has, missing ones are skipped
        var gt = new System.Windows.Media.GlyphTypeface(new Uri(ttf));
        if (!gt.CharacterToGlyphMap.TryGetValue('0', out ushort zero))
            throw new Exception("font has no '0'");
        double advEm = gt.AdvanceWidths[zero];
        double em = advPx / advEm;

        using var pfc = new PrivateFontCollection();
        pfc.AddFontFile(ttf);
        using var font = new Font(pfc.Families[0], (float)em, FontStyle.Regular, GraphicsUnit.Pixel);
        var fam = pfc.Families[0];
        double unitsPerEm = fam.GetEmHeight(FontStyle.Regular);
        int ascent = (int)Math.Round(fam.GetCellAscent(FontStyle.Regular) / unitsPerEm * em);
        int descent = (int)Math.Round(fam.GetCellDescent(FontStyle.Regular) / unitsPerEm * em);

        const int Pad = 32, Size = 128;
        using var bmp = new Bitmap(Size, Size, PixelFormat.Format32bppArgb);
        using var g = Graphics.FromImage(bmp);
        g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;   // hinted. white on black, green channel = coverage
        g.SmoothingMode = SmoothingMode.None;
        using var white = new SolidBrush(Color.White);
        var fmt = StringFormat.GenericTypographic;

        var glyphs = new List<Glyph>();
        int top = int.MaxValue, bottom = int.MinValue;
        foreach (int cp in cps)
        {
            if (!gt.CharacterToGlyphMap.TryGetValue(cp, out ushort gi)) continue;
            int adv = (int)Math.Round(gt.AdvanceWidths[gi] * em);
            g.Clear(Color.Black);
            g.DrawString(char.ConvertFromUtf32(cp), font, white, Pad, Pad, fmt);
            int x0 = Size, y0 = Size, x1 = -1, y1 = -1;
            var cov = new byte[Size, Size];
            for (int y = 0; y < Size; y++)
                for (int x = 0; x < Size; x++)
                {
                    byte v = bmp.GetPixel(x, y).G;
                    cov[y, x] = v;
                    if (v >= 9) { x0 = Math.Min(x0, x); x1 = Math.Max(x1, x); y0 = Math.Min(y0, y); y1 = Math.Max(y1, y); }
                }
            if (x1 < 0) { glyphs.Add(new Glyph(cp, 0, 0, 0, 0, adv, Array.Empty<byte>())); continue; }
            int w = x1 - x0 + 1, h = y1 - y0 + 1, stride = (w + 1) / 2;
            var bits = new byte[stride * h];
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                {
                    int q = (cov[y0 + y, x0 + x] * 15 + 127) / 255;
                    bits[y * stride + x / 2] |= (byte)((x & 1) == 0 ? q << 4 : q);
                }
            glyphs.Add(new Glyph(cp, w, h, x0 - Pad, y0 - Pad, adv, bits));
            top = Math.Min(top, y0 - Pad); bottom = Math.Max(bottom, y1 - Pad);
        }

        // shift everything up so the top of H/d/0 lands right under the line top.
        // accents on capitals can stick out above, they get clipped at the line top
        int capTop = glyphs.Where(q => q.Cp == 'H' || q.Cp == 'd' || q.Cp == '0').Min(q => q.Yo);
        int lift = Math.Max(0, capTop - 1);
        int lineH = ascent + descent - lift;
        glyphs = glyphs.Select(q => q with { Yo = q.Yo - lift }).ToList();

        WriteC(outC, name, Path.GetFileName(ttf), em, advPx, lineH, ascent - lift, glyphs);
        if (sheet != null) WriteSheet(sheet, glyphs, lineH, advPx);
        Console.WriteLine($"{name}: {glyphs.Count} glyphs, em {em:F2}px, advance {advPx}, line {lineH}, ascent {ascent - lift}, ink rows {top - lift}..{bottom - lift}");
        return 0;
    }

    static List<int> ParseRanges(string spec)
    {
        var list = new List<int>();
        foreach (var part in spec.Split(',', StringSplitOptions.RemoveEmptyEntries))
        {
            var ab = part.Split('-');
            int a = int.Parse(ab[0], CultureInfo.InvariantCulture);
            int b = ab.Length > 1 ? int.Parse(ab[1], CultureInfo.InvariantCulture) : a;
            for (int c = a; c <= b; c++) list.Add(c);
        }
        return list.Distinct().OrderBy(c => c).ToList();
    }

    static void WriteC(string path, string name, string ttf, double em, int adv, int lineH, int ascent, List<Glyph> glyphs)
    {
        var sb = new StringBuilder();
        sb.Append($"/* Generated by tools/FontBake from {ttf} at {em.ToString("F2", CultureInfo.InvariantCulture)} px (advance {adv}). Do not edit. */\r\n");
        sb.Append("#include \"font.h\"\r\n\r\n");
        sb.Append("static const uint8_t BITS[] = {\r\n");
        int off = 0;
        var offs = new List<int>();
        foreach (var q in glyphs)
        {
            offs.Add(off);
            if (q.Bits.Length == 0) continue;
            sb.Append("    ");
            sb.Append(string.Join(",", q.Bits.Select(b => "0x" + b.ToString("X2"))));
            sb.Append($",  /* U+{q.Cp:X4} */\r\n");
            off += q.Bits.Length;
        }
        if (off == 0) sb.Append("    0\r\n");
        sb.Append("};\r\n\r\nstatic const glyph_t GLYPHS[] = {\r\n");
        for (int i = 0; i < glyphs.Count; i++)
        {
            var q = glyphs[i];
            sb.Append($"    {{ 0x{q.Cp:X4}, {q.W}, {q.H}, {q.Xo}, {q.Yo}, {q.Adv}, 0, {offs[i]} }},\r\n");
        }
        sb.Append("};\r\n\r\n");
        sb.Append($"const font_t FONT_{name} = {{ GLYPHS, {glyphs.Count}, {lineH}, {ascent}, BITS }};\r\n");
        File.WriteAllText(path, sb.ToString(), new UTF8Encoding(false));
    }

    // contact sheet at 4x, white on navy, just to eyeball what was baked
    static void WriteSheet(string path, List<Glyph> glyphs, int lineH, int adv)
    {
        const int Cols = 32, Z = 4;
        int rows = (glyphs.Count + Cols - 1) / Cols;
        int cw = adv + 2, ch = lineH + 2;
        using var bmp = new Bitmap(Cols * cw * Z, rows * ch * Z);
        using (var g = Graphics.FromImage(bmp)) g.Clear(Color.FromArgb(6, 16, 34));
        for (int i = 0; i < glyphs.Count; i++)
        {
            var q = glyphs[i];
            int ox = (i % Cols) * cw + 1, oy = (i / Cols) * ch + 1;
            int stride = (q.W + 1) / 2;
            for (int y = 0; y < q.H; y++)
                for (int x = 0; x < q.W; x++)
                {
                    int v = (q.Bits[y * stride + x / 2] >> ((x & 1) == 0 ? 4 : 0)) & 15;
                    if (v == 0) continue;
                    int px = ox + q.Xo + x, py = oy + q.Yo + y;
                    if (px < 0 || py < 0) continue;
                    int c = v * 17;
                    var col = Color.FromArgb(6 + (220 - 6) * c / 255, 16 + (240 - 16) * c / 255, 34 + (255 - 34) * c / 255);
                    for (int dy = 0; dy < Z; dy++)
                        for (int dx = 0; dx < Z; dx++)
                        {
                            int X = px * Z + dx, Y = py * Z + dy;
                            if (X < bmp.Width && Y < bmp.Height) bmp.SetPixel(X, Y, col);
                        }
                }
        }
        bmp.Save(path, ImageFormat.Png);
    }
}
```

