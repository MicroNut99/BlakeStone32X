/* BlakeStone32X speed test
   Shows Blake Stone AOG level 1 with walls and textured floors/ceilings,
   rendered by both SH2s, with an fps counter.
   A = floors/ceilings on/off   B = view size   C = detail hi/lo
   START = 2 CPU / 1 CPU        D-pad = walk and turn */
#include <string.h>
#include "marshw.h"
#include "render.h"

#define VERSION   "B1"
#define BLOB_ADDR 0x02040000      /* ROM offset 0x40000, set by the build */
#define FBW       320
#define VIEWTOP   8

#define CMD_NONE   0
#define CMD_RENDER 1
#define CMD_PING   2

extern const uint8_t font8x8[];
extern void CacheControl(int mode);

static bshead_t hdr;
static const uint8_t *blob;
static uint8_t tilemap_ram[MAPSIZE * MAPSIZE];
static uint8_t colormap_ram[64 * 256] __attribute__((aligned(16)));
static uint8_t pal[768];

static view_t view;                              /* master copy */
static view_t shared_view __attribute__((aligned(16)));  /* read by the slave through cache-through */
static rwork_t work_pri, work_sec;

#define UNCACHED(p) ((void *)(((uintptr_t)(p)) | 0x20000000))

/* ---------- text ---------- */

static uint8_t white, black;

static void put_text(uint8_t *fb, int x, int y, const char *s)
{
	for (; *s; s++, x += 8) {
		int c = (unsigned char)*s, r, i;
		const uint8_t *g;
		if (c >= 'a' && c <= 'z') c -= 32;
		if (c < 0x20 || c > 0x7F) c = '?';
		g = font8x8 + (c - 0x20) * 8;
		if (x > FBW - 8) return;
		for (r = 0; r < 8; r++) {
			uint8_t *d = fb + (y + r) * FBW + x;
			for (i = 0; i < 8; i++)
				d[i] = (g[r] & (0x80 >> i)) ? white : black;
		}
	}
}

static char *utoa_(char *p, unsigned v)
{
	char t[12]; int n = 0;
	do { t[n++] = '0' + v % 10; v /= 10; } while (v);
	while (n) *p++ = t[--n];
	*p = 0;
	return p;
}

static char *hex_(char *p, unsigned v, int digits)
{
	while (digits--) { int d = (v >> (digits * 4)) & 15; *p++ = d < 10 ? '0' + d : 'A' + d - 10; }
	*p = 0;
	return p;
}

static char *str_(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }

static uint8_t *fbpix(void) { return (uint8_t *)(&MARS_FRAMEBUFFER + 0x100); }

static void clear_fb(void)
{
	uint32_t *p = (uint32_t *)fbpix();
	int n = FBW * 224 / 4;
	uint32_t c = black * 0x01010101u;
	while (n--) *p++ = c;
}

/* ---------- boot log: every step is a line on screen ---------- */

#define LOGMAX 24
static char logbuf[LOGMAX][41];
static int lognum;

static void log_show(void)
{
	int i, pass;
	for (pass = 0; pass < 2; pass++) {    /* draw into both buffers */
		clear_fb();
		for (i = 0; i < lognum; i++)
			put_text(fbpix(), 8, 8 + i * 9, logbuf[i]);
		Mars_FlipFrameBuffers(1);
	}
}

static void log_line(const char *s)
{
	if (lognum == LOGMAX) { memmove(logbuf[0], logbuf[1], sizeof(logbuf[0]) * (LOGMAX - 1)); lognum--; }
	strncpy(logbuf[lognum], s, 40);
	logbuf[lognum][40] = 0;
	lognum++;
	log_show();
}

static void stop(const char *s)
{
	char b[48];
	str_(str_(b, "STOP "), s);
	log_line(b);
	for (;;) ;
}

/* ---------- slave SH2 ---------- */

void secondary(void)
{
	volatile uint16_t *comm4 = (volatile uint16_t *)&MARS_SYS_COMM4;
	for (;;) {
		int cmd;
		while ((cmd = *comm4) != CMD_RENDER && cmd != CMD_PING) ;
		CacheControl(0x11);  /* purge + enable: see the master's latest data */
		if (cmd == CMD_RENDER) {
			const view_t *v = (const view_t *)UNCACHED(&shared_view);
			view_t lv = *v;
			R_Render(&lv, lv.cols / 2, lv.cols, (rwork_t *)&work_sec);
		}
		*comm4 = CMD_NONE;
	}
}

/* ---------- player ---------- */

static int32_t ppx, ppy;
static int pang;

static int solid(int32_t x, int32_t y)
{
	int tx = x >> 16, ty = y >> 16;
	if ((unsigned)tx >= MAPSIZE || (unsigned)ty >= MAPSIZE) return 1;
	return r_tilemap[(ty << 6) + tx] != 0;
}

static int blocked(int32_t x, int32_t y)
{
	const int32_t r = 0x4000;
	return solid(x - r, y - r) || solid(x + r, y - r) || solid(x - r, y + r) || solid(x + r, y + r);
}

static void move_player(int buttons, int ticks)
{
	int32_t dx = 0, dy = 0, sp;
	if (ticks > 8) ticks = 8;
	if (buttons & SEGA_CTRL_LEFT)  pang = (pang + 6 * ticks) & 1023;
	if (buttons & SEGA_CTRL_RIGHT) pang = (pang - 6 * ticks) & 1023;
	sp = 0x1400 * ticks;
	if (buttons & SEGA_CTRL_UP)   { dx += R_FixedMul(R_COS(pang), sp); dy -= R_FixedMul(R_SIN(pang), sp); }
	if (buttons & SEGA_CTRL_DOWN) { dx -= R_FixedMul(R_COS(pang), sp); dy += R_FixedMul(R_SIN(pang), sp); }
	if (!blocked(ppx + dx, ppy)) ppx += dx;
	if (!blocked(ppx, ppy + dy)) ppy += dy;
}

/* ---------- main ---------- */

static const short sizes[][2] = { {320, 160}, {288, 144}, {256, 128}, {224, 112}, {192, 96} };
#define NUMSIZES 5

int main(void)
{
	char b[64], *p;
	int i, sizeidx = 0, low = 0, floors = 1, twocpu = 1;
	int prev = 0, frames = 0, fps10 = 0, msframe = 0;
	unsigned t_last, t_window, t_frame;
	uint32_t *hp = (uint32_t *)&hdr;

	Mars_Init();
	Mars_InitVideo(224);

	/* temporary palette until the game palette is loaded: 0 black, 1 white */
	memset(pal, 0, sizeof(pal));
	pal[3] = pal[4] = pal[5] = 255;
	white = 1; black = 0;
	Mars_SetPalette(pal);
	Mars_WaitTicks(2);

	log_line("BLAKESTONE32X SPEED TEST " VERSION);

	blob = (const uint8_t *)BLOB_ADDR;
	for (i = 0; i < 20; i++) hp[i] = ((const uint32_t *)blob)[i];   /* SH2 is big-endian like the blob */
	memcpy(hdr.name, blob + 80, 16);
	hdr.name[15] = 0;

	p = str_(b, "DATA ");
	p = hex_(p, BLOB_ADDR, 8);
	p = str_(p, " MAGIC ");
	p = hex_(p, hdr.magic, 8);
	log_line(b);
	if (hdr.magic != BS_MAGIC) stop("DATA MAGIC IS NOT 42533332");

	p = str_(b, "VERSION "); p = utoa_(p, hdr.version);
	p = str_(p, " SIZE "); p = utoa_(p, hdr.size);
	log_line(b);
	if (hdr.version != 1) stop("DATA VERSION IS NOT 1");

	p = str_(b, "WALL PICS "); p = utoa_(p, hdr.numwalls);
	p = str_(p, " MAP "); p = utoa_(p, hdr.mapnum);
	p = str_(p, " "); p = str_(p, hdr.name);
	log_line(b);

	p = str_(b, "START X "); p = utoa_(p, hdr.startx);
	p = str_(p, " Y "); p = utoa_(p, hdr.starty);
	p = str_(p, " ANG "); p = utoa_(p, hdr.startang);
	log_line(b);

	p = str_(b, "CEIL PIC "); p = utoa_(p, hdr.ceilpic);
	p = str_(p, " FLOOR PIC "); p = utoa_(p, hdr.floorpic);
	log_line(b);

	p = str_(b, "SHADE DIV "); p = utoa_(p, hdr.nsd);
	p = str_(p, " MAX "); p = utoa_(p, hdr.sm);
	log_line(b);

	i = R_Init(blob, &hdr, tilemap_ram, colormap_ram);
	p = str_(b, "R_INIT "); p = utoa_(p, i < 0 ? -i : i);
	p = str_(p, i < 0 ? " FAILED" : " OK");
	log_line(b);
	if (i < 0) stop("R_INIT FAILED");

	/* game palette */
	memcpy(pal, blob + hdr.ofs_palette, 768);
	white = (uint8_t)hdr.white;
	black = (uint8_t)hdr.black;
	Mars_SetPalette(pal);
	Mars_WaitTicks(2);
	p = str_(b, "PALETTE WHITE "); p = utoa_(p, white);
	p = str_(p, " BLACK "); p = utoa_(p, black);
	log_line(b);

	/* slave check */
	MARS_SYS_COMM4 = CMD_PING;
	t_last = mars_vblank_count;
	while (MARS_SYS_COMM4 != CMD_NONE && mars_vblank_count - t_last < 120) ;
	p = str_(b, "SLAVE SH2 COMM4 ");
	p = hex_(p, MARS_SYS_COMM4, 4);
	p = str_(p, MARS_SYS_COMM4 == CMD_NONE ? " OK" : " NO REPLY");
	log_line(b);
	if (MARS_SYS_COMM4 != CMD_NONE) stop("SLAVE SH2 DID NOT ANSWER");

	log_line("A FLOORS  B SIZE  C DETAIL  START CPUS");
	log_line("PRESS START");
	for (;;) {
		int bt = Mars_ReadController(0);
		if (bt > 0 && (bt & SEGA_CTRL_START)) break;
		Mars_WaitTicks(1);
	}
	for (;;) {   /* wait for release */
		int bt = Mars_ReadController(0);
		Mars_WaitTicks(2);
		if (bt <= 0 || !(bt & SEGA_CTRL_START)) break;
	}

	ppx = (hdr.startx << 16) + 0x8000;
	ppy = (hdr.starty << 16) + 0x8000;
	pang = hdr.startang & 1023;

	memset(&view, 0, sizeof(view));
	view.pitch = FBW;
	{
		int redo = 2;
		t_last = t_window = mars_vblank_count;

		for (;;) {
			int bt, pressed, ticks, w, h;
			unsigned now;

			if (redo) {   /* view changed: clear both buffers, rebuild tables */
				w = sizes[sizeidx][0]; h = sizes[sizeidx][1];
				R_SetupView(&view, w, h, low);
				view.fb = fbpix() + VIEWTOP * FBW + (FBW - w) / 2;
				clear_fb();
				redo--;
			}
			w = sizes[sizeidx][0]; h = sizes[sizeidx][1];
			view.floors = floors;

			bt = Mars_ReadController(0);
			if (bt < 0) bt = 0;
			pressed = bt & ~prev;
			prev = bt;
			if (pressed & SEGA_CTRL_A) floors ^= 1;
			if (pressed & SEGA_CTRL_B) { sizeidx = (sizeidx + 1) % NUMSIZES; redo = 2; }
			if (pressed & SEGA_CTRL_C) { low ^= 1; redo = 2; }
			if (pressed & SEGA_CTRL_START) twocpu ^= 1;

			now = mars_vblank_count;
			ticks = now - t_last;
			t_last = now;
			move_player(bt, ticks ? ticks : 1);
			R_SetCamera(&view, ppx, ppy, pang);

			/* render */
			t_frame = mars_vblank_count;
			if (twocpu) {
				memcpy(UNCACHED(&shared_view), &view, sizeof(view));
				MARS_SYS_COMM4 = CMD_RENDER;
				R_Render(&view, 0, view.cols / 2, &work_pri);
				while (MARS_SYS_COMM4 != CMD_NONE) ;
			} else {
				R_Render(&view, 0, view.cols, &work_pri);
			}

			/* fps over a 1 second window */
			frames++;
			now = mars_vblank_count;
			if (now - t_window >= 60) {
				fps10 = frames * 600 / (now - t_window);
				msframe = (now - t_window) * 1000 / 60 / frames;
				frames = 0;
				t_window = now;
			}
			(void)t_frame;

			p = str_(b, "FPS "); p = utoa_(p, fps10 / 10); p = str_(p, "."); p = utoa_(p, fps10 % 10);
			p = str_(p, "  MS "); p = utoa_(p, msframe); p = str_(p, "   ");
			put_text(fbpix(), 8, VIEWTOP + 160 + 8, b);
			p = str_(b, "FLOORS "); p = str_(p, floors ? "ON " : "OFF");
			p = str_(p, "  CPUS "); p = utoa_(p, twocpu ? 2 : 1);
			put_text(fbpix(), 8, VIEWTOP + 160 + 18, b);
			p = str_(b, "VIEW "); p = utoa_(p, w); p = str_(p, "X"); p = utoa_(p, h);
			p = str_(p, "  DETAIL "); p = str_(p, low ? "LO" : "HI");
			put_text(fbpix(), 8, VIEWTOP + 160 + 28, b);
			p = str_(b, "X "); p = utoa_(p, ppx >> 16); p = str_(p, " Y "); p = utoa_(p, ppy >> 16);
			p = str_(p, " ANG "); p = utoa_(p, pang); p = str_(p, "   ");
			put_text(fbpix(), 8, VIEWTOP + 160 + 38, b);

			Mars_FlipFrameBuffers(1);
		}
	}
	return 0;
}
