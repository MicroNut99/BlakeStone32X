/* PC test: renders one frame with the same renderer and writes a PPM. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/render.h"

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static uint8_t tilemap[4096], cmap[16384], fb[320 * 224];
static rwork_t w0, w1;

int main(int argc, char **argv)
{
	FILE *f = fopen(argv[1], "rb");
	long n; uint8_t *blob; bshead_t h; uint32_t *hp = (uint32_t *)&h; int i;
	int pixw = atoi(argv[3]), rows = atoi(argv[4]), low = atoi(argv[5]), floors = atoi(argv[6]);
	int ang = argc > 7 ? atoi(argv[7]) : -1;
	view_t v;
	fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
	blob = malloc(n); fread(blob, 1, n, f); fclose(f);
	for (i = 0; i < 20; i++) hp[i] = be32(blob + i * 4);
	memcpy(h.name, blob + 80, 16);
	if (h.magic != BS_MAGIC) { printf("STOP MAGIC %08x\n", h.magic); return 1; }
	printf("R_Init %d\n", R_Init(blob, &h, tilemap, cmap));
	memset(&v, 0, sizeof v);
	v.pitch = 320; v.floors = floors;
	v.fb = fb + 8 * 320 + (320 - pixw) / 2;
	R_SetupView(&v, pixw, rows, low);
	R_SetCamera(&v, (h.startx << 16) + 0x8000, (h.starty << 16) + 0x8000, ang < 0 ? (int)h.startang : ang);
	R_Render(&v, 0, v.cols / 2, &w0);
	R_Render(&v, v.cols / 2, v.cols, &w1);
	f = fopen(argv[2], "wb");
	fprintf(f, "P6 320 224 255\n");
	for (i = 0; i < 320 * 224; i++) fwrite(blob + h.ofs_palette + fb[i] * 3, 1, 3, f);
	fclose(f);
	return 0;
}
