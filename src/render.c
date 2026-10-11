/* BlakeStone32X - Blake Stone style raycaster
   Walls: Wolf3D/Blake grid DDA, 64x64 column-major VSWAP walls.
   Floors/ceilings: Blake's span method (d3_d2.cpp MapRow), ceiling and floor
   share one texture coordinate per pixel (interleaved planepics).
   Shading: Blake's colormap, shade = shade_max - 63*height/normalshade.

   Shade tables are stored rotated by 128 and pointed at their middle, so a
   sign-extended texel byte indexes them directly (no extu.b on the SH2).
   On the 32X the hi-detail inner loops are hand-written SH2 (r_asm.s);
   the PC host build uses the C versions below with the same behaviour. */
#include <string.h>
#include "render.h"

#ifdef MARS
#define RAMCODE __attribute__((section(".sdata"), aligned(16), optimize("O2")))
extern int FixedMul2(int a, int b);
extern int FixedDiv(int a, int b);
#define FMUL(a, b) FixedMul2((a), (b))
#define FDIV(a, b) FixedDiv((a), (b))
extern void R_SpanPairsAsm(uint16_t *c_end, uint16_t *f_end, int pairs, uint32_t xy_last,
	uint32_t step_neg, const uint8_t *pp, const uint8_t *sh);
extern void R_ColAsm(uint8_t *dst, int count, int32_t frac, int32_t step,
	const uint8_t *src, const uint8_t *sh, int pitch);
extern void R_SpanLoAsm(uint16_t *c_end, uint16_t *f_end, int count, uint32_t xy_last,
	uint32_t step_neg, const uint8_t *pp, const uint16_t *sh16);
extern void R_ColAsm16(uint16_t *dst, int count, int32_t frac, int32_t step,
	const uint8_t *src, const uint16_t *sh16, int pitch);
#else
#define RAMCODE
static int32_t FMUL(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }
static int32_t FDIV(int32_t a, int32_t b)
{
	int64_t q;
	if (b == 0) return a < 0 ? (int32_t)0x80000000 : 0x7FFFFFFF;
	q = ((int64_t)a * 65536) / b;
	if (q > 0x7FFFFFFF) return 0x7FFFFFFF;
	if (q < -0x7FFFFFFF) return (int32_t)0x80000000;
	return (int32_t)q;
}

/* C versions of the SH2 loops in r_asm.s (same arguments, same order of work) */
#define TIDX(xy) ((((xy) >> 3) & 0x1F80) | (((xy) >> 25) & 0x7E))
static void R_SpanPairsAsm(uint16_t *c, uint16_t *f, int pairs, uint32_t xy,
	uint32_t step, const uint8_t *pp, const uint8_t *sh)
{
	do {
		unsigned i = TIDX(xy), j;
		int8_t cr = (int8_t)pp[i], fr = (int8_t)pp[i + 1], cl, fl;
		xy += step;
		j = TIDX(xy);
		cl = (int8_t)pp[j]; fl = (int8_t)pp[j + 1];
		xy += step;
		*--c = (uint16_t)((sh[cl] << 8) | sh[cr]);
		*--f = (uint16_t)((sh[fl] << 8) | sh[fr]);
	} while (--pairs);
}

static void R_ColAsm(uint8_t *d, int n, int32_t frac, int32_t step,
	const uint8_t *src, const uint8_t *sh, int pitch)
{
	do {
		*d = sh[(int8_t)src[(frac >> 16) & 63]];
		d += pitch;
		frac += step;
	} while (--n);
}

static void R_SpanLoAsm(uint16_t *c, uint16_t *f, int n, uint32_t xy,
	uint32_t step, const uint8_t *pp, const uint16_t *sh16)
{
	do {
		unsigned i = TIDX(xy);
		xy += step;
		*--c = sh16[(int8_t)pp[i]];
		*--f = sh16[(int8_t)pp[i + 1]];
	} while (--n);
}

static void R_ColAsm16(uint16_t *d, int n, int32_t frac, int32_t step,
	const uint8_t *src, const uint16_t *sh16, int pitch)
{
	do {
		*d = sh16[(int8_t)src[(frac >> 16) & 63]];
		d = (uint16_t *)((uint8_t *)d + pitch);
		frac += step;
	} while (--n);
}
#endif

#define TEXIDX(xy) ((((xy) >> 3) & 0x1F80) | (((xy) >> 25) & 0x7E))

int32_t R_FixedMul(int32_t a, int32_t b) { return FMUL(a, b); }
int32_t R_FixedDiv(int32_t a, int32_t b) { return FDIV(a, b); }

const uint8_t *r_tilemap;
const uint8_t *r_colormap;
const uint8_t *r_walls[256];
uint8_t r_planepics[8192] __attribute__((aligned(16)));
uint8_t r_ceilflat, r_floorflat;

static int r_nsd = 1, r_sm = 36;
static int32_t r_camx[MAXCOLS];
static int32_t r_rowdist[MAXROWS / 2 + 2];
static const uint8_t *r_shadeptr[MAXROWS + 1];   /* middle (+128) of a rotated shade table */
static const uint16_t *r_shadeptr16[MAXROWS + 1]; /* same, each entry = colour * 0x0101 (two pixels) */
static uint16_t r_cmap16[64 * 256] __attribute__((aligned(16)));
static int32_t r_halff;

#define PLANELEN 0xC000   /* tan(fov/2) = 0.75 */

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

int R_Init(const uint8_t *blob, const bshead_t *h, uint8_t *tilemap_ram, uint8_t *colormap_ram)
{
	unsigned i, k;
	const uint8_t *dir = blob + h->ofs_walldir;
	const uint8_t *cm = blob + h->ofs_colormap;
	const uint8_t *c, *f;

	memcpy(tilemap_ram, blob + h->ofs_tilemap, MAPSIZE * MAPSIZE);
	/* rotated: row[k] = colormap[(k - 128) & 255], used as (row + 128)[signed texel] */
	for (i = 0; i < 64; i++)
		for (k = 0; k < 256; k++) {
			uint8_t c8 = cm[i * 256 + ((k - 128) & 255)];
			colormap_ram[i * 256 + k] = c8;
			r_cmap16[i * 256 + k] = (uint16_t)(c8 * 0x0101);
		}
	r_tilemap = tilemap_ram;
	r_colormap = colormap_ram;

	for (i = 0; i < 256; i++) {
		uint32_t o = i < h->numwalls ? be32(dir + i * 4) : 0;
		r_walls[i] = o ? blob + o : 0;
	}
	if (!r_walls[0]) return -1;
	for (i = 0; i < 256; i++)
		if (!r_walls[i]) r_walls[i] = r_walls[0];
	if (h->ceilpic >= h->numwalls || h->floorpic >= h->numwalls) return -2;

	c = r_walls[h->ceilpic];
	f = r_walls[h->floorpic];
	for (i = 0; i < 4096; i++) {
		r_planepics[i * 2] = c[i];
		r_planepics[i * 2 + 1] = f[i];
	}
	r_nsd = h->nsd ? (int)h->nsd : 1;
	r_sm = (int)h->sm;
	r_ceilflat = (uint8_t)h->ceilflat;
	r_floorflat = (uint8_t)h->floorflat;
	return 0;
}

void R_SetupView(view_t *v, int pixw, int rows, int detail)
{
	int x, k, cols, half, maxscale, normalshade;
	int lowdetail = detail == DETAIL_LO;

	cols = lowdetail ? pixw / 2 : pixw;
	v->cols = cols;
	v->rows = rows;
	v->lowdetail = lowdetail;
	v->lowplanes = detail == DETAIL_MIX;
	half = rows / 2;

	for (x = 0; x < cols; x++)
		r_camx[x] = (int32_t)((((int64_t)(2 * x + 1 - cols)) << 16) / cols);

	/* focal length in pixels = (pixw/2)/planelen, wall half height = (F/2)/dist */
	r_halff = (int32_t)(((int64_t)pixw << 32) / ((int64_t)4 * PLANELEN));

	for (k = 1; k <= half + 1; k++)
		r_rowdist[k] = (int32_t)(((int64_t)r_halff << 16) / ((k << 16) - 0x8000));
	r_rowdist[0] = r_rowdist[1];

	maxscale = half;
	normalshade = (3 * (maxscale >> 2)) / r_nsd;
	if (normalshade < 1) normalshade = 1;
	for (k = 0; k <= MAXROWS; k++) {
		int i = r_sm - (63 * k) / normalshade;
		if (i < 0) i = 0;
		if (i > 63) i = 63;
		r_shadeptr[k] = r_colormap + (i << 8) + 128;
		r_shadeptr16[k] = r_cmap16 + (i << 8) + 128;
	}
}

void R_SetCamera(view_t *v, int32_t px, int32_t py, int ang)
{
	v->px = px;
	v->py = py;
	v->dirx = R_COS(ang);
	v->diry = -R_SIN(ang);          /* map y grows south, angle 256 = north */
	v->planex = FMUL(-v->diry, PLANELEN);
	v->planey = FMUL(v->dirx, PLANELEN);
}

static void RAMCODE R_Span(const view_t *v, int k, int xa, int xb, int32_t psx, int32_t psy)
{
	const int half = v->rows >> 1;
	const int pitch = v->pitch;
	int n = xb - xa + 1;
	int32_t d, cx, fx, fy, sx, sy;
	uint32_t xy, xystep;
	const uint8_t *sh, *pp;

	if (n <= 0) return;

	if (!v->floors) {
		if (v->lowdetail) {
			uint16_t *c = (uint16_t *)(v->fb + (half - k) * pitch) + xa;
			uint16_t *f = (uint16_t *)(v->fb + (half + k - 1) * pitch) + xa;
			uint16_t cv = r_ceilflat * 0x0101, fv = r_floorflat * 0x0101;
			do { *c++ = cv; *f++ = fv; } while (--n);
		} else {
			uint8_t *c = v->fb + (half - k) * pitch + xa;
			uint8_t *f = v->fb + (half + k - 1) * pitch + xa;
			uint8_t cv = r_ceilflat, fv = r_floorflat;
			uint16_t *c16, *f16;
			int pairs;
			if ((uintptr_t)c & 1) { *c++ = cv; *f++ = fv; n--; }
			c16 = (uint16_t *)c; f16 = (uint16_t *)f;
			for (pairs = n >> 1; pairs > 0; pairs--) { *c16++ = cv * 0x0101; *f16++ = fv * 0x0101; }
			if (n & 1) { *(uint8_t *)c16 = cv; *(uint8_t *)f16 = fv; }
		}
		return;
	}

	d = r_rowdist[k];
	cx = r_camx[xa];
	fx = v->px + FMUL(d, v->dirx + FMUL(v->planex, cx));
	fy = v->py + FMUL(d, v->diry + FMUL(v->planey, cx));
	sx = FMUL(d, psx);
	sy = FMUL(d, psy);
	xy = ((uint32_t)fy << 16) | ((uint32_t)fx & 0xFFFF);
	xystep = ((uint32_t)sy << 16) | ((uint32_t)sx & 0xFFFF);
	sh = r_shadeptr[k];
	pp = r_planepics;

	if (v->lowdetail) {
		/* LO: one texel per column, a column is two pixels */
		uint16_t *c = (uint16_t *)(v->fb + (half - k) * pitch) + xa;
		uint16_t *f = (uint16_t *)(v->fb + (half + k - 1) * pitch) + xa;
		R_SpanLoAsm(c + n, f + n, n, xy + (uint32_t)(n - 1) * xystep, (uint32_t)0 - xystep,
			pp, r_shadeptr16[k]);
	} else if (v->lowplanes) {
		/* MIX: full-resolution columns, one texel per aligned pixel pair */
		uint8_t *c = v->fb + (half - k) * pitch + xa;
		uint8_t *f = v->fb + (half + k - 1) * pitch + xa;
		int pairs;
		if ((uintptr_t)c & 1) {
			unsigned i = TEXIDX(xy);
			xy += xystep;
			*c++ = sh[(int8_t)pp[i]];
			*f++ = sh[(int8_t)pp[i + 1]];
			n--;
		}
		pairs = n >> 1;
		if (pairs) {
			uint32_t st2 = xystep << 1;
			R_SpanLoAsm((uint16_t *)c + pairs, (uint16_t *)f + pairs, pairs,
				xy + (uint32_t)(pairs - 1) * st2, (uint32_t)0 - st2, pp, r_shadeptr16[k]);
			xy += (uint32_t)pairs * st2;
		}
		if (n & 1) {
			unsigned i = TEXIDX(xy);
			c[n - 1] = sh[(int8_t)pp[i]];
			f[n - 1] = sh[(int8_t)pp[i + 1]];
		}
	} else {
		/* pixel pairs as 16-bit writes, drawn right to left by the SH2 loop */
		uint8_t *c = v->fb + (half - k) * pitch + xa;
		uint8_t *f = v->fb + (half + k - 1) * pitch + xa;
		int pairs;
		if ((uintptr_t)c & 1) {
			unsigned i = TEXIDX(xy);
			xy += xystep;
			*c++ = sh[(int8_t)pp[i]];
			*f++ = sh[(int8_t)pp[i + 1]];
			n--;
		}
		pairs = n >> 1;
		if (pairs) {
			R_SpanPairsAsm((uint16_t *)c + pairs, (uint16_t *)f + pairs, pairs,
				xy + (uint32_t)(2 * pairs - 1) * xystep, (uint32_t)0 - xystep, pp, sh);
			xy += (uint32_t)(2 * pairs) * xystep;
		}
		if (n & 1) {
			unsigned i = TEXIDX(xy);
			c[n - 1] = sh[(int8_t)pp[i]];
			f[n - 1] = sh[(int8_t)pp[i + 1]];
		}
	}
}

void RAMCODE R_Render(const view_t *v, int x0, int x1, rwork_t *w)
{
	const int rows = v->rows, half = rows >> 1, pitch = v->pitch;
	const int32_t px = v->px, py = v->py;
	const uint8_t *tm = r_tilemap;
	int x, k, last, t0 = 0, t1 = 0;
	int32_t psx, psy;

	if (w->timer) t0 = w->timer();

	for (x = x0; x < x1; x++) {
		int32_t cx = r_camx[x];
		int32_t rdx = v->dirx + FMUL(v->planex, cx);
		int32_t rdy = v->diry + FMUL(v->planey, cx);
		int mapx = px >> 16, mapy = py >> 16;
		int32_t adx = rdx < 0 ? -rdx : rdx, ady = rdy < 0 ? -rdy : rdy;
		int32_t ddx = adx < 0x100 ? 0x1000000 : FDIV(0x10000, adx);
		int32_t ddy = ady < 0x100 ? 0x1000000 : FDIV(0x10000, ady);
		int32_t sdx, sdy, perp, hfix, step, frac, wx;
		int stepx, stepy, side, t, h, y0, y1, n, tx, pic;
		const uint8_t *src, *sh;

		if (ddx > 0x1000000) ddx = 0x1000000;
		if (ddy > 0x1000000) ddy = 0x1000000;
		if (rdx < 0) { stepx = -1; sdx = FMUL(px & 0xFFFF, ddx); }
		else         { stepx = 1;  sdx = FMUL(0x10000 - (px & 0xFFFF), ddx); }
		if (rdy < 0) { stepy = -1; sdy = FMUL(py & 0xFFFF, ddy); }
		else         { stepy = 1;  sdy = FMUL(0x10000 - (py & 0xFFFF), ddy); }

		for (;;) {
			if (sdx < sdy) { sdx += ddx; mapx += stepx; side = 0; }
			else           { sdy += ddy; mapy += stepy; side = 1; }
			if ((unsigned)mapx >= MAPSIZE || (unsigned)mapy >= MAPSIZE) { t = 1; break; }
			t = tm[(mapy << 6) + mapx];
			if (t) break;
		}

		if (side == 0) {
			perp = sdx - ddx;
			if (perp < 0x800) perp = 0x800;
			wx = py + FMUL(perp, rdy);
			tx = (wx >> 10) & 63;
			if (rdx < 0) tx = 63 - tx;
			pic = (t - 1) * 2 + 1;
		} else {
			perp = sdy - ddy;
			if (perp < 0x800) perp = 0x800;
			wx = px + FMUL(perp, rdx);
			tx = (wx >> 10) & 63;
			if (rdy > 0) tx = 63 - tx;
			pic = (t - 1) * 2;
		}

		hfix = FDIV(r_halff, perp);
		h = hfix >> 16;
		w->height[x] = (int16_t)(h > half ? half : h);
		if (h <= 0) continue;

		src = r_walls[pic & 255] + (tx << 6);
		sh = r_shadeptr[h > MAXROWS ? MAXROWS : h];
		step = FDIV(32 << 16, hfix);
		y0 = half - h; if (y0 < 0) y0 = 0;
		y1 = half + h; if (y1 > rows) y1 = rows;
		n = y1 - y0;
		frac = (32 << 16) + (y0 - half) * step + (step >> 1);
		if (frac < 0) frac = 0;

		if (v->lowdetail) {
			R_ColAsm16((uint16_t *)(v->fb + y0 * pitch) + x, n, frac, step, src,
				r_shadeptr16[h > MAXROWS ? MAXROWS : h], pitch);
		} else {
			R_ColAsm(v->fb + y0 * pitch + x, n, frac, step, src, sh, pitch);
		}
	}

	if (w->timer) t1 = w->timer();

	/* floor/ceiling spans (Blake DrawPlanes), only where walls leave rows open */
	psx = FMUL(v->planex, r_camx[1] - r_camx[0]);
	psy = FMUL(v->planey, r_camx[1] - r_camx[0]);
	last = half;
	for (x = x0; x < x1; x++) {
		int h = w->height[x];
		if (h > last) {
			for (k = last + 1; k <= h; k++)
				R_Span(v, k, w->spanstart[k], x - 1, psx, psy);
		} else if (h < last) {
			for (k = h + 1; k <= last; k++)
				w->spanstart[k] = (int16_t)x;
		}
		last = h;
	}
	for (k = last + 1; k <= half; k++)
		R_Span(v, k, w->spanstart[k], x1 - 1, psx, psy);

	if (w->timer) {
		int t2 = w->timer();
		w->t_walls = t1 - t0;
		w->t_planes = t2 - t1;
	}
}
