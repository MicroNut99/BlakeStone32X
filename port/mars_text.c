/* BlakeStone32X: 8x8 text straight into the 32X frame buffer, for stop screens.
   Draws into both buffers so the text stays up. 16-bit writes (Fusion skips
   byte writes of colour 0). Sets colour 0 black and 255 white. */
#include <stdint.h>
#include <string.h>
#include "marshw.h"

extern const uint8_t font8x8[];

static uint8_t stop_pal[768];

static void put(int x, int y, const char *s)
{
    uint8_t *fb = (uint8_t *)(&MARS_FRAMEBUFFER + 0x100);
    for (; *s && x <= 312; s++, x += 8) {
        int c = (unsigned char)*s, r;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c < 0x20 || c > 0x7F) c = '?';
        const uint8_t *g = font8x8 + (c - 0x20) * 8;
        for (r = 0; r < 8; r++) {
            uint16_t *d = (uint16_t *)(fb + (y + r) * 320 + x);
            unsigned gr = g[r], i;
            for (i = 0; i < 4; i++) {
                unsigned hi = (gr & 0x80) ? 255 : 0;
                unsigned lo = (gr & 0x40) ? 255 : 0;
                d[i] = (uint16_t)((hi << 8) | lo);
                gr <<= 2;
            }
        }
    }
}

static char lines[24][41];
static int ys[24];
static int nlines;

void bs32x_text_clear(void)
{
    nlines = 0;
}

void bs32x_text(int x, int y, const char *s)
{
    int pass;
    if (nlines < 24) {
        strncpy(lines[nlines], s, 40);
        lines[nlines][40] = 0;
        ys[nlines] = y;
        nlines++;
    }
    memset(stop_pal, 0, sizeof(stop_pal));
    stop_pal[765] = stop_pal[766] = stop_pal[767] = 255;
    Mars_SetPalette(stop_pal);
    for (pass = 0; pass < 2; pass++) {
        uint32_t *p = (uint32_t *)(&MARS_FRAMEBUFFER + 0x100);
        int n = 320 * 224 / 4, i;
        while (n--) *p++ = 0;
        for (i = 0; i < nlines; i++)
            put(x, ys[i], lines[i]);
        Mars_FlipFrameBuffers(1);
    }
}
