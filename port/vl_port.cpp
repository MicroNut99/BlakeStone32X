/* BlakeStone32X: id_vl replacement.
   Drawing functions are BStone v1.0.0's id_vl.cpp unchanged (GPL-2.0+);
   display, palette upload and timing go to the platform layer (bs32x_vid_*). */
#include "id_heads.h"

int bufferofs;
int displayofs;
int* ylookup = nullptr;
bool screenfaded;
uint8_t* vga_memory = nullptr;
uint8_t* vga_palette = nullptr;
int vga_scale = 1;
int vga_width = vga_ref_width;
int vga_height = vga_ref_height;
int vga_area = vga_ref_width * vga_ref_height;
int screen_x = 0;
int screen_y = 0;
int screen_width = vga_ref_width;
int screen_height = vga_ref_height;
bool sdl_is_windowed = false;
bool vid_fix_par = true;

uint8_t palette1[256][3];
uint8_t palette2[256][3];

// BlakeStone32X memory model: the game's three VGA pages share one buffer
// (vl_page_bytes), latches (offsets from FREESTART) live in their own memory,
// read-only cart ROM on the 32X.
static const int vl_page_bytes = 4 * SCREENSIZE;      // 320 x 208
static const int vl_latch_start = 4 * FREESTART;
static uint8_t* vl_latch_mem = nullptr;
static bool vl_latch_writable = false;

static inline int vl_fold(
    int offset)
{
    if (offset < vl_latch_start) {
        while (offset >= vl_page_bytes) {
            offset -= vl_page_bytes;
        }
    }
    return offset;
}

static inline uint8_t* vl_ptr(
    int offset)
{
    if (offset >= vl_latch_start) {
        return vl_latch_mem + (offset - vl_latch_start);
    }
    return ::vga_memory + offset;
}

void VL_WaitVBL(
    uint32_t vbls)
{
    bs32x_vid_wait_vbl(static_cast<int>(vbls));
}

void VL_Startup()
{
}

void VL_Shutdown()
{
}

void VL_SetVGAPlaneMode()
{
    if (!::vga_memory) {
        ::vga_memory = bs32x_vid_vga_memory();
        int writable = 0;
        vl_latch_mem = bs32x_vid_latch_memory(&writable);
        vl_latch_writable = (writable != 0);
    }

    if (!::vga_palette) {
        ::vga_palette = new uint8_t[::vga_palette_size];
    }

    std::uninitialized_fill_n(::vga_palette, ::vga_palette_size, 0);
    ::VL_SetLineWidth(40);
}

void VL_SetPalette(
    int first,
    int count,
    const uint8_t* palette,
    bool refresh_screen)
{
    std::uninitialized_copy_n(palette, 3 * count, &::vga_palette[3 * first]);
    bs32x_vid_set_palette(::vga_palette);

    if (refresh_screen) {
        ::VL_RefreshScreen();
    }
}

void VL_RefreshScreen()
{
    bs32x_vid_present(::displayofs);
}

// Copies the buffer page to the display page, then shows it (as BStone's soft_update_screen)
void VH_UpdateScreen()
{
    if (::vl_get_offset(::displayofs) != ::vl_get_offset(::bufferofs)) {
        std::uninitialized_copy_n(
            vl_ptr(::vl_get_offset(::bufferofs)),
            ::vga_area,
            vl_ptr(::vl_get_offset(::displayofs)));
    }

    bs32x_vid_present(::displayofs);
}

void VL_SetLineWidth(
    int width)
{
    static_cast<void>(width);

    delete [] ::ylookup;
    ::ylookup = new int[::vga_height];

    for (int i = 0; i < ::vga_height; ++i) {
        ::ylookup[i] = i * ::vga_width;
    }
}

void VL_FillPalette(
    uint8_t red,
    uint8_t green,
    uint8_t blue)
{
    for (int i = 0; i < 256; ++i) {
        ::vga_palette[(3 * i) + 0] = red;
        ::vga_palette[(3 * i) + 1] = green;
        ::vga_palette[(3 * i) + 2] = blue;
    }

    ::VL_SetPalette(0, 255, ::vga_palette);
}

void VL_GetPalette(
    int first,
    int count,
    uint8_t* palette)
{
    int offset = 3 * first;
    int size = 3 * count;

    std::uninitialized_copy_n(
        &::vga_palette[offset],
        size,
        palette);
}

void VL_FadeOut(
    int start,
    int end,
    int red,
    int green,
    int blue,
    int steps)
{
    int orig;
    int delta;

    ::VL_GetPalette(0, 256, &::palette1[0][0]);

    std::uninitialized_copy_n(
        &::palette1[0][0],
        768,
        &::palette2[0][0]);

    //
    // fade through intermediate frames
    //
    for (int i = 0; i < steps; ++i) {
        auto origptr = &::palette1[start][0];
        auto newptr = &::palette2[start][0];

        for (int j = start; j <= end; ++j) {
            orig = *origptr++;
            delta = red - orig;
            *newptr++ = static_cast<uint8_t>(orig + ((delta * i) / steps));
            orig = *origptr++;
            delta = green - orig;
            *newptr++ = static_cast<uint8_t>(orig + ((delta * i) / steps));
            orig = *origptr++;
            delta = blue - orig;
            *newptr++ = static_cast<uint8_t>(orig + ((delta * i) / steps));
        }

        ::VL_SetPalette(0, 256, &::palette2[0][0]);
        ::VL_WaitVBL(1);
    }

    //
    // final color
    //
    ::VL_FillPalette(
        static_cast<uint8_t>(red),
        static_cast<uint8_t>(green),
        static_cast<uint8_t>(blue));

    ::VL_WaitVBL(1);

    screenfaded = true;
}

void VL_FadeIn(
    int start,
    int end,
    const uint8_t* palette,
    int steps)
{
    ::VL_GetPalette(0, 256, &::palette1[0][0]);

    std::uninitialized_copy_n(
        &::palette1[0][0],
        768,
        &::palette2[0][0]);

    start *= 3;
    end = (end * 3) + 2;

    //
    // fade through intermediate frames
    //
    for (int i = 0; i < steps; ++i) {
        for (int j = start; j <= end; ++j) {
            int delta = palette[j] - ::palette1[0][j];

            ::palette2[0][j] =
                static_cast<uint8_t>(::palette1[0][j] + ((delta * i) / steps));
        }

        ::VL_SetPalette(0, 256, &::palette2[0][0]);
        ::VL_WaitVBL(1);
    }

    //
    // final color
    //
    ::VL_SetPalette(0, 256, palette);
    ::VL_WaitVBL(1);

    screenfaded = false;
}

void VL_SetPaletteIntensity(
    int start,
    int end,
    const uint8_t* palette,
    int intensity)
{
    auto cmap = &::palette1[0][0] + (start * 3);

    intensity = 63 - intensity;

    for (int loop = start; loop <= end; ++loop) {
        int red = (*palette++) - intensity;

        if (red < 0) {
            red = 0;
        }

        *cmap++ = static_cast<uint8_t>(red);

        int green = *palette++ - intensity;

        if (green < 0) {
            green = 0;
        }

        *cmap++ = static_cast<uint8_t>(green);

        int blue = *palette++ - intensity;

        if (blue < 0) {
            blue = 0;
        }

        *cmap++ = static_cast<uint8_t>(blue);
    }

    ::VL_SetPalette(start, end - start + 1, &::palette1[0][0]);
}

// (the port always runs with vga_scale 1)
uint8_t* vl_page_ptr(
    int x,
    int y)
{
    return vl_ptr(::vl_get_offset(::bufferofs, x, y));
}

void VL_Plot(
    int x,
    int y,
    uint8_t color)
{
    *vl_page_ptr(x, y) = color;
}

void VL_Hlin(
    int x,
    int y,
    int width,
    uint8_t color)
{
    ::VL_Bar(x, y, width, 1, color);
}

void VL_Vlin(
    int x,
    int y,
    int height,
    uint8_t color)
{
    ::VL_Bar(x, y, 1, height, color);
}

void VL_Bar(
    int x,
    int y,
    int width,
    int height,
    uint8_t color)
{
    width *= ::vga_scale;
    height *= ::vga_scale;

    int offset = ::vl_get_offset(::bufferofs, x, y);

    if (x == 0 && width == ::vga_width) {
        int count = height * ::vga_width;
        std::uninitialized_fill_n(vl_ptr(offset), count, color);
    } else {
        for (int i = 0; i < height; ++i) {
            std::uninitialized_fill_n(vl_ptr(offset), width, color);
            offset += vga_width;
        }
    }
}

void VL_MemToLatch(
    const uint8_t* source,
    int width,
    int height,
    int dest)
{
    if (!vl_latch_writable) {
        return;   // 32X: latches were built on the PC and are in cart ROM
    }

    int dst_pitch = ::vga_scale * width;
    int base_offset = ::vl_get_offset(dest);

    for (int p = 0; p < 4; ++p) {
        for (int h = 0; h < height; ++h) {
            for (int w = p; w < width; w += 4) {
                uint8_t pixel = *source++;

                int offset = base_offset +
                    ::vga_scale * ((::vga_scale * h * width) + w);

                for (int s = 0; s < ::vga_scale; ++s) {
                    std::uninitialized_fill_n(
                        vl_ptr(offset),
                        ::vga_scale,
                        pixel);

                    offset += dst_pitch;
                }
            }
        }
    }
}

void VL_MemToScreen(
    const uint8_t* source,
    int width,
    int height,
    int x,
    int y)
{
    uint8_t* base = vl_page_ptr(x, y);

    for (int p = 0; p < 4; ++p) {
        uint8_t* row = base + p;
        for (int h = 0; h < height; ++h) {
            for (int w = p; w < width; w += 4) {
                row[w - p] = *source++;
            }
            row += ::vga_width;
        }
    }
}

void VL_MaskMemToScreen(
    const uint8_t* source,
    int width,
    int height,
    int x,
    int y,
    uint8_t mask)
{
    uint8_t* base = vl_page_ptr(x, y);

    for (int p = 0; p < 4; ++p) {
        uint8_t* row = base + p;
        for (int h = 0; h < height; ++h) {
            for (int w = p; w < width; w += 4) {
                uint8_t color = *source++;

                if (color != mask) {
                    row[w - p] = color;
                }
            }
            row += ::vga_width;
        }
    }
}

void VL_ScreenToMem(
    uint8_t* dest,
    int width,
    int height,
    int x,
    int y)
{
    for (int p = 0; p < 4; ++p) {
        for (int h = 0; h < height; ++h) {
            for (int w = p; w < width; w += 4) {
                *dest++ = ::vl_get_pixel(::bufferofs, x + w, y + h);
            }
        }
    }
}

void VL_LatchToScreen(
    int source,
    int width,
    int height,
    int x,
    int y)
{
    int src_pitch = ::vga_scale * 4 * width;
    int src_offset = ::vl_get_offset(source);
    int dst_offset = ::vl_get_offset(::bufferofs, x, y);

    for (int h = 0; h < height; ++h) {
        for (int s = 0; s < ::vga_scale; ++s) {
            std::uninitialized_copy_n(
                vl_ptr(src_offset),
                src_pitch,
                vl_ptr(dst_offset));

            src_offset += src_pitch;
            dst_offset += vga_width;
        }
    }
}

void VL_ScreenToScreen(
    int source,
    int dest,
    int width,
    int height)
{
    source *= 4 * ::vga_scale * ::vga_scale;
    dest *= 4 * ::vga_scale * ::vga_scale;
    width *= 4 * ::vga_scale;
    height *= ::vga_scale;

    auto src_pixels = vl_ptr(vl_fold(source));
    auto dst_pixels = vl_ptr(vl_fold(dest));

    for (int h = 0; h < height; ++h) {
        std::uninitialized_copy_n(
            src_pixels,
            width,
            dst_pixels);

        src_pixels += vga_width;
        dst_pixels += vga_width;
    }
}

void JM_VGALinearFill(
    int start,
    int length,
    uint8_t fill)
{
    int offset = ::vl_get_offset(start);
    int count = 4 * length;

    if (offset < vl_latch_start && offset + count > vl_page_bytes) {
        count = vl_page_bytes - offset;   // pages share one buffer
    }

    std::uninitialized_fill_n(vl_ptr(offset), count, fill);
}

uint8_t vl_get_pixel(
    int base_offset,
    int x,
    int y)
{
    return *vl_ptr(::vl_get_offset(base_offset, x, y));
}

int vl_get_offset(
    int base_offset)
{
    return vl_fold(4 * base_offset);
}

int vl_get_offset(
    int base_offset,
    int x,
    int y)
{
    return vl_fold(4 * base_offset) + (y * ::vga_width) + x;
}
