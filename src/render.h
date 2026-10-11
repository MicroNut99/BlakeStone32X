/* BlakeStone32X - Blake Stone style raycaster (walls + textured floors/ceilings)
   Portable C: runs on the 32X SH2s and in the PC host test. */
#ifndef RENDER_H
#define RENDER_H
#include <stdint.h>

#define MAPSIZE   64
#define MAXCOLS   320
#define MAXROWS   224

/* data blob header, big-endian on disk (SH2 is big-endian) */
typedef struct {
	uint32_t magic;          /* 'BS32' */
	uint32_t version;
	uint32_t numwalls;       /* VSWAP chunks before sprites */
	uint32_t mapnum;
	uint32_t startx, starty; /* tile */
	uint32_t startang;       /* 0..1023, 0 = east, 256 = north */
	uint32_t ceilpic, floorpic;
	uint32_t nsd, sm;        /* Blake normalshade_div, shade_max */
	uint32_t ofs_palette;    /* 768 bytes, 8-bit RGB */
	uint32_t ofs_colormap;   /* 64 * 256 */
	uint32_t ofs_tilemap;    /* 64*64 bytes, [y*64+x], 0 = open, else wall tile */
	uint32_t ofs_walldir;    /* numwalls u32 offsets, 0 = none */
	uint32_t size;
	uint32_t white, black, ceilflat, floorflat;
	char     name[16];
} bshead_t;

#define BS_MAGIC 0x42533332u

typedef struct {
	int32_t px, py;          /* 16.16 tiles */
	int32_t dirx, diry;      /* 16.16 unit vector */
	int32_t planex, planey;  /* 16.16 camera plane */
	int cols, rows;          /* render columns (pixels/2 in low detail), rows */
	int lowdetail;
	int floors;
	uint8_t *fb;             /* top-left pixel of the view */
	int pitch;               /* bytes per framebuffer line */
} view_t;

/* per-CPU work state */
typedef struct {
	int16_t height[MAXCOLS];
	int16_t spanstart[MAXROWS / 2 + 2];
	int (*timer)(void);      /* set on the master only: times walls and planes */
	int t_walls, t_planes;
} rwork_t;

/* shared resources, set by R_Init */
extern const uint8_t *r_tilemap;
extern const uint8_t *r_colormap;
extern const uint8_t *r_walls[256];
extern uint8_t r_planepics[8192];
extern uint8_t r_ceilflat, r_floorflat;

int  R_Init(const uint8_t *blob, const bshead_t *h, uint8_t *tilemap_ram, uint8_t *colormap_ram);
void R_SetupView(view_t *v, int pixw, int rows, int lowdetail);  /* builds tables (not hot) */
void R_SetCamera(view_t *v, int32_t px, int32_t py, int ang);
void R_Render(const view_t *v, int x0, int x1, rwork_t *w);

int32_t R_FixedMul(int32_t a, int32_t b);
int32_t R_FixedDiv(int32_t a, int32_t b);

extern const int32_t r_sintab[1024 + 256];
#define R_SIN(a) r_sintab[(a) & 1023]
#define R_COS(a) r_sintab[((a) & 1023) + 256]

#endif
