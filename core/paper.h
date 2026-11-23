/* paper - the picture for e-paper, 4 greys, 2 bits a pixel.
 * two ways a picture comes here (paper_inks says which):
 *  - the radar's pages, drawn as light on dark glass: e-paper is ink on white, so this prints
 *    the negative, the light by steps (paper.c) as white, light grey, dark grey or black
 *  - the paper's own main screen (ui_paper.c), drawn in the inks below, one channel each */
#pragma once
#include "gfx.h"

#define PAPER_STRIDE ((GFX_W + 3) / 4)      // bytes a row, 4 pixels a byte

/* one strip of 0x00RRGGBB light -> fb, which points at the first byte of the whole
 * frame. row cv->y0 + r goes to fb + (cv->y0 + r) * PAPER_STRIDE.
 * leftmost pixel in the top 2 bits. code 3 = white, 2 = light grey, 1 = dark grey,
 * 0 = black, the same as ViewOwl's mono4gray frames */
void paper_pack(const canvas_t *cv, uint8_t *fb);

/* 1: this frame is drawn in the P_* inks (ui_paper.c), 0: the radar's light, printed as its negative.
 * ui_frame sets it, before the frame's first strip */
extern int paper_inks;

/* 1: the paper's sheet is new (another map, another range): the panel should print it with a full
 * refresh, its greys too, and not draw it on top in black and white. ui_frame sets it, the firmware
 * takes it (main.c: disp_full_next) */
extern int paper_new_sheet;

/* 1: a new screen waits for its map (another look, another range): the firmware draws nothing meanwhile, the glass
 * keeps the old screen till the new one is printed whole once. a white sheet drawn on top of it first was a long
 * refresh for nothing ("перерисовывать все, а это долго и не нужно"). ui_frame sets it */
extern int paper_hold;

/* the sheet's tones (ui_paper.c): 0 the 4 inks and black and white patterns, 1 more tones from
 * patterns of two inks (an experiment). the console sets it (main.c: OWLOS PAPER n) */
extern int paper_style;

/* the inks: a channel each, and paper.c reads which is over half. a line or a letter drawn soft over
 * another ink (gfx and font draw edges half covered) so comes out as one of the two, never as a grey
 * that neither is: black over the light grey water is black or water. the edges are crisp, and what
 * moves over the map stays black and white, which the partial refresh can draw */
#define P_WHITE RGB(0, 0, 0)
#define P_BLACK RGB(255, 0, 0)
#define P_DARK  RGB(0, 255, 0)
#define P_LIGHT RGB(0, 0, 255)

/* soft edges, for what is printed whole (canvas_t.soft): ink over under, k of 256 of the pixel covered, as one
 * of the 4 inks a step between them; a line w wide, soft where the canvas is, else gfx_line's */
uint32_t paper_soft(uint32_t under, uint32_t ink, int k);
// a lightness 0 (black) .. 1 (white) at pixel x, y: an ordered dither of the two inks either side, for gradients
uint32_t paper_tone(float g, int x, int y);
void paper_line(canvas_t *cv, float x0, float y0, float x1, float y1, float w, uint32_t ink);
void paper_disc(canvas_t *cv, float cx, float cy, float r, uint32_t ink);
void paper_ring(canvas_t *cv, float cx, float cy, float r, float w, uint32_t ink);

/* the keys' strip at the right edge (ui_paper.c), a third of the height for each key: what is drawn on
 * the paper keeps left of it */
#define PAPER_KEYS_W 28

// how the panel really shows the 4 codes, 0..255 (measured by ViewOwl), for the preview
extern const uint8_t PAPER_LEVEL[4];

/* the two greys of the panel. drawing with exactly these lights gives a clean flat grey,
 * anything in between gets dithered. the 4.2"'s were 50 and 220 (ViewOwl measured them), its grey
 * waveform now puts them nearer the middle (epd.c LUT_4GRAY): these two are read off a photo */
#if RADAR_DISPLAY == DISPLAY_EPD_400x300
#define PAPER_DARK  95
#define PAPER_LIGHT 185
#else
#define PAPER_DARK  60
#define PAPER_LIGHT 140
#endif
#define INK_GD  RGB(255 - PAPER_DARK, 255 - PAPER_DARK, 255 - PAPER_DARK)      // ViewOwl's GD, dark grey
#define INK_GL  RGB(255 - PAPER_LIGHT, 255 - PAPER_LIGHT, 255 - PAPER_LIGHT)   // ViewOwl's GL, light grey
#define INK_BK  RGB(255, 255, 255)                                             // black ink
