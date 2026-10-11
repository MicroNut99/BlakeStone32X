/* BlakeStone32X: faster 3D view drawing.
   The game's ray caster still finds the walls, but the wall columns are
   only recorded; then both SH2s draw: the walls (one CPU the even columns,
   the other the odd ones) and the floor and ceiling (even and odd rows),
   with the hand-written loops of r_asm.s. On the PC test build the same code
   runs on one CPU with C copies of the loops, so its frames can be compared
   with the game's own drawing. */
#include "3d_def.h"
#include "bs32x_render.h"

extern int viewwidth;
extern int viewheight;
extern int bufferofs;
extern int vga_scale;
extern int halfheight;
extern int* basedist;
extern int* planeylookup;
extern int* mirrorofs;
extern fixed psin;
extern fixed pcos;
extern uint8_t planepics[8192];
extern int shade_max;
extern int normalshade;
extern uint8_t* vga_memory;
extern const uint8_t* postsource;
extern int postx;
extern int postheight;
extern const uint8_t* shadingtable;
extern const uint8_t* lightsource;
extern const uint8_t colormap[16896];
extern int bounceOffset;
extern bool useBounceOffset;
extern int maxscaleshl2;

void SetPlaneViewSize();
fixed FixedMul(fixed a, fixed b);

/* colormap with every 256-byte row rotated by 128: row + 128 indexed by a
   sign-extended byte gives the same colour as the row indexed by the byte */
static const uint8_t colormap_rot[16896] = {
#include "bs32x_colormap_rot.inc"
};

bool bs32x_fast_render = true;

namespace bs32x_r {

struct Post {
    const uint8_t* src;
    const uint8_t* sh;      /* rotated shade row + 128 */
    int16_t x;
    int16_t height;
};

const int max_posts = 168;     /* one CPU casts every second column */

/* frame values shared with the second CPU (written before it starts) */
struct Frame {
    uint8_t* view;          /* top-left pixel of the view in the page */
    int max_height;
    int width;
    int lighting;
    int planes;
    int psin;
    int pcos;
    int viewx;
    int viewy;
    int view_height;        /* viewheight * vga_scale */
    int sprites;
};
Frame frame;

const uint8_t* shade_row(int height)
{
    if ((gamestate.flags & GS_LIGHTING) == 0) {
        return colormap_rot + 128;
    }
    int i = shade_max - bs32x_div(63 * height, normalshade * vga_scale);
    if (i < 0) {
        i = 0;
    } else if (i > 63) {
        i = 63;
    }
    return colormap_rot + i * 256 + 128;
}

#ifdef BS32X_MARS
extern "C" {
void R_SpanPairsAsm(uint16_t* c_end, uint16_t* f_end, int pairs, uint32_t xy_last,
    uint32_t step_neg, const uint8_t* pp, const uint8_t* sh);
void R_ColAsm(uint8_t* dst, int count, int32_t frac, int32_t step,
    const uint8_t* src, const uint8_t* sh, int pitch);
}
#else
/* C copies of the r_asm.s loops for the PC test build */
inline uint8_t shade(const uint8_t* sh, uint8_t t)
{
    return sh[static_cast<int8_t>(t)];
}

void R_SpanPairsAsm(uint16_t* c_end, uint16_t* f_end, int pairs, uint32_t xy,
    uint32_t step_neg, const uint8_t* pp, const uint8_t* sh)
{
    uint8_t* c = reinterpret_cast<uint8_t*>(c_end);
    uint8_t* f = reinterpret_cast<uint8_t*>(f_end);
    for (int i = 0; i < 2 * pairs; ++i) {
        uint32_t idx = ((xy >> 3) & 0x1F80) | ((xy >> 25) & 0x7E);
        xy += step_neg;
        *--c = shade(sh, pp[idx]);
        *--f = shade(sh, pp[idx + 1]);
    }
}

void R_ColAsm(uint8_t* dst, int count, int32_t frac, int32_t step,
    const uint8_t* src, const uint8_t* sh, int pitch)
{
    for (int i = 0; i < count; ++i) {
        *dst = shade(sh, src[(frac >> 16) & 63]);
        frac += step;
        dst += pitch;
    }
}
#endif

void draw_posts(const Post* posts, int count)
{
    const int pitch = vga_width;
    for (int i = 0; i < count; ++i) {
        const Post& p = posts[i];
        if (p.height == 0) {
            continue;
        }
        int step = bs32x_div(32 * 65536, p.height);
        int n = p.height < frame.max_height ? p.height : frame.max_height;
        int half = step / 2;
        uint8_t* center = frame.view + frame.max_height * pitch + p.x;
        /* top half, drawn downwards: texel 31 - ((half + h * step) >> 16) */
        R_ColAsm(center - n * pitch, n, 2097151 - half - (n - 1) * step, step, p.src, p.sh, pitch);
        /* bottom half: texel 32 + ((half + h * step) >> 16) */
        R_ColAsm(center, n, 2097152 + half, step, p.src, p.sh, pitch);
    }
}

/* one floor and ceiling span pair: pixels x1..x2 of the rows for 'height' */
void draw_span(int x1, int x2, int height)
{
    uint8_t* crow = frame.view + planeylookup[height];
    uint8_t* frow = crow + mirrorofs[height];

    int xstep = bs32x_div(frame.psin, 2 * height);
    int ystep = bs32x_div(frame.pcos, 2 * height);
    int length = basedist[height];
    int startxfrac = frame.viewx + FixedMul(length, frame.pcos);
    int startyfrac = frame.viewy - FixedMul(length, frame.psin);
    int prestep = ((frame.width * vga_scale) / 2) - x1;
    int xfrac = startxfrac - (xstep * prestep);
    int yfrac = startyfrac - (ystep * prestep);

    uint32_t step = (static_cast<uint32_t>(ystep) << 16) | (xstep & 0xFFFF);
    uint32_t xy = (static_cast<uint32_t>(yfrac) << 16) | (xfrac & 0xFFFF);
    const uint8_t* sh = frame.lighting ? shade_row(height) : colormap_rot + 128;

    /* odd edges one pixel at a time, the rest two pixels per write
       (the view may start at an odd address, so test the address) */
    const int odd = static_cast<int>(reinterpret_cast<uintptr_t>(crow) & 1);
    if (((x1 + odd) & 1) != 0) {
        uint32_t idx = ((xy >> 3) & 0x1F80) | ((xy >> 25) & 0x7E);
        crow[x1] = sh[static_cast<int8_t>(planepics[idx])];
        frow[x1] = sh[static_cast<int8_t>(planepics[idx + 1])];
        xy += step;
        ++x1;
    }
    if (x1 > x2) {
        return;
    }
    if (((x2 + odd) & 1) == 0) {
        uint32_t xy2 = xy + step * static_cast<uint32_t>(x2 - x1);
        uint32_t idx = ((xy2 >> 3) & 0x1F80) | ((xy2 >> 25) & 0x7E);
        crow[x2] = sh[static_cast<int8_t>(planepics[idx])];
        frow[x2] = sh[static_cast<int8_t>(planepics[idx + 1])];
        --x2;
    }
    if (x1 > x2) {
        return;
    }
    int pairs = (x2 - x1 + 1) / 2;
    uint32_t xy_last = xy + step * static_cast<uint32_t>(x2 - x1);
    R_SpanPairsAsm(reinterpret_cast<uint16_t*>(crow + x2 + 1), reinterpret_cast<uint16_t*>(frow + x2 + 1),
        pairs, xy_last, 0u - step, planepics, sh);
}

/* the game's DrawPlanes span walk; this CPU draws the rows of one parity */
void draw_planes(int parity)
{
    int16_t spanstart[128];
    const int width = frame.width * vga_scale;
    int lastheight = halfheight;
    int x;

    for (x = 0; x < width; ++x) {
        int height = wallheight[x] / 8;
        if (height < lastheight) {
            do {
                spanstart[--lastheight] = static_cast<int16_t>(x);
            } while (lastheight > height);
        } else if (height > lastheight) {
            if (height > halfheight) {
                height = halfheight;
            }
            for (; lastheight < height; ++lastheight) {
                if (lastheight > 0 && (lastheight & 1) == parity) {
                    draw_span(spanstart[lastheight], x - 1, lastheight);
                }
            }
        }
    }
    for (; lastheight < halfheight; ++lastheight) {
        if (lastheight > 0 && (lastheight & 1) == parity) {
            draw_span(spanstart[lastheight], x - 1, lastheight);
        }
    }
}

/* ---------- sprites (the game's generic_scale_shape, reentrant) ---------- */

struct Sprite {
    int16_t xcenter;
    int16_t shapenum;
    int16_t height;
    int8_t lighting;
    int8_t shaded;
    int8_t cloaked;
};

const int max_sprites = 96;
Sprite sprites[max_sprites];
int sprite_count = 0;
bool sprites_recording = false;


/* one post of a shape at column x (the game's generic_scale_masked_post) */
void sprite_post(const Sprite& s, const uint8_t* seg, const uint16_t* srcpost,
    int x, int height, const uint8_t* sh)
{
    const int view_h = frame.view_height;
    int bounce = useBounceOffset ? bounceOffset : 0;
    bounce *= vga_scale;

    int iscale = bs32x_div(64 * 65536, height);
    int screenstep = height << 10;
    int sprtopoffset = (view_h << 15) - (height << 15) + (bounce >> 1);

    int end = bs32x_le16(srcpost++) / 2;
    while (end != 0) {
        int source = bs32x_le16(srcpost++);
        int start = bs32x_le16(srcpost++) / 2;
        source += start;
        source %= 65536;

        int length = end - start;
        int topscreen = sprtopoffset + (screenstep * start);
        int bottomscreen = topscreen + (screenstep * length);
        int yl = (topscreen + 0x10000 - 1) >> 16;
        int yh = (bottomscreen - 1) >> 16;
        if (yh >= view_h) {
            yh = view_h - 1;
        }
        int frac;
        if (yl < 0) {
            frac = iscale * (-yl);
            yl = 0;
        } else {
            frac = 0;
        }

        if (yl <= yh) {
            uint8_t* dst = frame.view + yl * vga_width + x;
            int count = yh - yl + 1;
            if (s.cloaked && s.shaded) {
                const uint8_t* cl = colormap_rot + 16 * 256 + 128;  /* lightsource[0x1000 | pixel] */
                for (int i = 0; i < count; ++i) {
                    *dst = cl[static_cast<int8_t>(*dst)];
                    dst += vga_width;
                }
            } else {
                R_ColAsm(dst, count, frac, iscale, seg + source, sh, vga_width);
            }
        }

        end = bs32x_le16(srcpost++) / 2;
    }
}

/* the columns of one parity of a shape */
void draw_sprite(const Sprite& s, int parity)
{
    const int view_w = frame.width * vga_scale;
    int height = s.height;
    int xcenter = s.xcenter + centerx * (vga_scale - 1);

    const t_compshape* shape = static_cast<const t_compshape*>(PM_GetSpritePage(s.shapenum));
    const uint8_t* seg = reinterpret_cast<const uint8_t*>(shape);
    const int leftpix = bs32x_le16(&shape->leftpix);
    const int rightpix = bs32x_le16(&shape->rightpix);

    int64_t xscale = static_cast<int64_t>(height) << 12;
    int64_t xcent = (static_cast<int64_t>(xcenter) << 20) - (static_cast<int64_t>(height) << 17) + 0x80000;

    int x1 = static_cast<int>((xcent + (leftpix * xscale)) >> 20);
    if (x1 >= view_w) {
        return;
    }
    int x2 = static_cast<int>((xcent + (rightpix * xscale)) >> 20);
    if (x2 < 0) {
        return;
    }
    int screenscale = bs32x_div(256 << 20, height);

    int frac;
    if (x1 < 0) {
        frac = (-x1) * screenscale;
        x1 = 0;
    } else {
        frac = screenscale / 2;
    }
    if (x2 >= view_w) {
        x2 = view_w - 1;
    }

    const uint8_t* sh = colormap_rot + 128;
    if (s.shaded) {
        int i = shade_max - bs32x_div(63 * height, normalshade * 8 * vga_scale) + s.lighting;
        if (i < 0) {
            i = 0;
        } else if (i > 63) {
            i = 63;
        }
        sh = colormap_rot + i * 256 + 128;
    }

    int swidth = rightpix - leftpix;
    if ((x1 & 1) != parity) {
        ++x1;
        frac += screenscale;
    }
    for (; x1 <= x2; x1 += 2, frac += 2 * screenscale) {
        if (wallheight[x1] > height) {
            continue;
        }
        int texturecolumn = frac >> 20;
        if (texturecolumn > swidth) {
            texturecolumn = swidth;
        }
        const uint16_t* linecmds = reinterpret_cast<const uint16_t*>(
            &seg[bs32x_le16(&shape->dataofs[texturecolumn])]);
        sprite_post(s, seg, linecmds, x1, height / 4, sh);
    }
}

void draw_sprites(int parity)
{
    for (int i = 0; i < frame.sprites; ++i) {
        draw_sprite(sprites[i], parity);
    }
}

} // namespace bs32x_r

/* one ray caster per CPU: the game's code (tools/gen_ray_inc.py) with its
   own state in each namespace */
extern int focaltx;
extern int focalty;
extern int xpartialup;
extern int xpartialdown;
extern int ypartialup;
extern int ypartialdown;
extern int midangle;
extern int viewtx;
extern int viewty;
extern uint16_t DoorJamsShade[];
extern uint16_t DoorJams[];

namespace ray0 {
using namespace bs32x_r;
#include "bs32x_ray_state_fwd.inc"
#include "bs32x_ray.inc"
#include "bs32x_ray_state.inc"
}

namespace ray1 {
using namespace bs32x_r;
#include "bs32x_ray_state_fwd.inc"
#include "bs32x_ray.inc"
#include "bs32x_ray_state.inc"
}

/* the second CPU's part of the frame */
void bs32x_render_second(void)
{
    ray1::walls(1);
    bs32x_sync_second(3, 4);        /* all of wallheight[] is there */
    if (bs32x_r::frame.planes) {
        bs32x_r::draw_planes(1);
    }
    bs32x_sync_second(5, 6);        /* floors done, sprite list ready */
    bs32x_r::draw_sprites(1);
}

/* the game's WallRefresh set-up for this view */
static void wall_setup()
{
    viewangle = player->angle;
    midangle = viewangle * (FINEANGLES / ANGLES);
    viewsin = sintable[viewangle];
    viewcos = costable[viewangle];
    viewx = player->x - FixedByFrac(focallength, viewcos);
    viewy = player->y + FixedByFrac(focallength, viewsin);

    focaltx = viewx >> TILESHIFT;
    focalty = viewy >> TILESHIFT;

    viewtx = player->x >> TILESHIFT;
    viewty = player->y >> TILESHIFT;

    xpartialdown = viewx & (TILEGLOBAL - 1);
    xpartialup = static_cast<uint16_t>(TILEGLOBAL - xpartialdown);
    ypartialdown = viewy & (TILEGLOBAL - 1);
    ypartialup = static_cast<uint16_t>(TILEGLOBAL - ypartialdown);
}

static bool dual = false;

/* walls (and floor and ceiling when 'planes') of the 3D view, on both CPUs */
void bs32x_render_frame(bool planes)
{
    using bs32x_r::frame;

    wall_setup();

    if (planes) {
        if (((viewheight / 2) * vga_scale) != halfheight) {
            SetPlaneViewSize();
        }
        psin = viewsin;
        if (psin < 0) {
            psin = -(psin & 0xFFFF);
        }
        pcos = viewcos;
        if (pcos < 0) {
            pcos = -(pcos & 0xFFFF);
        }
    }

    frame.view = vga_memory + vl_get_offset(bufferofs);
    frame.max_height = (viewheight / 2) * vga_scale;
    frame.width = viewwidth;
    frame.lighting = (gamestate.flags & GS_LIGHTING) != 0;
    frame.planes = planes;
    frame.psin = psin;
    frame.pcos = pcos;
    frame.viewx = viewx;
    frame.viewy = viewy;
    frame.view_height = viewheight * vga_scale;
    frame.sprites = 0;

    dual = bs32x_second_cpu_start();
    if (dual) {
        ray0::walls(0);
        bs32x_sync_first(3, 4);     /* both halves of wallheight[] are done */
        if (planes) {
            bs32x_r::draw_planes(0);
        }
    } else {
        ray0::walls(0);
        ray1::walls(1);
        if (planes) {
            bs32x_r::draw_planes(0);
            bs32x_r::draw_planes(1);
        }
    }
    /* DrawScaleds now only lists the sprites, bs32x_render_sprites draws them */
    bs32x_r::sprite_count = 0;
    bs32x_r::sprites_recording = true;
}

bool bs32x_sprite_record(int xcenter, int shapenum, int height, int lighting, bool shaded)
{
    using namespace bs32x_r;
    if (!sprites_recording) {
        return false;
    }
    /* the game's early-outs */
    if ((height / 2) > (maxscaleshl2 * vga_scale) || ((height / 2) == 0)) {
        return true;
    }
    if (sprite_count < max_sprites) {
        Sprite& s = sprites[sprite_count++];
        s.xcenter = static_cast<int16_t>(xcenter);
        s.shapenum = static_cast<int16_t>(shapenum);
        s.height = static_cast<int16_t>(height);
        s.lighting = static_cast<int8_t>(lighting);
        s.shaded = shaded ? 1 : 0;
        s.cloaked = cloaked_shape ? 1 : 0;
    }
    return true;
}

/* after DrawScaleds: both CPUs draw the listed sprites */
void bs32x_render_sprites(void)
{
    using namespace bs32x_r;
    if (!sprites_recording) {
        return;
    }
    sprites_recording = false;
    frame.sprites = sprite_count;
    if (dual) {
        bs32x_sync_first(5, 6);     /* the second CPU's floors are done; it reads the list now */
        draw_sprites(0);
        bs32x_second_cpu_wait();
    } else {
        draw_sprites(0);
        draw_sprites(1);
    }
}
