/*
BStone: A Source port of
Blake Stone: Aliens of Gold and Blake Stone: Planet Strike

Copyright (c) 1992-2013 Apogee Entertainment, LLC
Copyright (c) 2013-2015 Boris I. Bendovsky (bibendovsky@hotmail.com)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the
Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/


#include "id_heads.h"


void VL_LatchToScreen(
    int source,
    int width,
    int height,
    int x,
    int y);

void IN_StartAck();
bool IN_CheckAck();
void CalcTics();
void ForceUpdateStatusBar();

pictabletype* pictable;
pictabletype* picmtable;

int16_t px;
int16_t py;
uint8_t fontcolor;
uint8_t backcolor;
int16_t fontnumber;

bool allcaps = false;


void VW_DrawPropString(
    const char* string)
{
    fontstruct* font =
        static_cast<fontstruct*>(grsegs[STARTFONT + fontnumber]);

    int height = static_cast<int16_t>(bs32x_le16(&font->height));

    int string_length = static_cast<int>(strlen(string));

    for (int c = 0; c < string_length; ++c) {
        uint8_t ch = string[c];
        int width = font->width[ch];

        const uint8_t* source =
            (reinterpret_cast<const uint8_t*>(font)) + bs32x_le16(&font->location[ch]);

#ifdef BS32X
        uint8_t* dst = vl_page_ptr(px, py);
        const uint8_t color = fontcolor;

        for (int h = 0; h < height; ++h) {
            for (int w = 0; w < width; ++w) {
                if (source[w] != 0) {
                    dst[w] = color;
                }
            }

            source += width;
            dst += vga_width;
        }
#else
        for (int w = 0; w < width; ++w) {
            for (int h = 0; h < height; ++h) {
                if (source[h * width] != 0) {
                    VL_Plot(px + w, py + h, fontcolor);
                }
            }

            ++source;
        }

#endif

        px = static_cast<int16_t>(px + width);
    }
}

void VWL_MeasureString(
    const char* string,
    int* width,
    int* height,
    fontstruct* font)
{
    *height = static_cast<int16_t>(bs32x_le16(&font->height));

    for (*width = 0; string[0] != '\0'; ++string) {
        // proportional width
        *width += font->width[static_cast<uint8_t>(*string)];
    }
}

void VW_MeasurePropString(
    const char* string,
    int* width,
    int* height)
{
    VWL_MeasureString(
        string,
        width,
        height,
        static_cast<fontstruct*>(grsegs[STARTFONT + fontnumber]));
}


/*
=============================================================================

 Double buffer management routines

=============================================================================
*/

void VWB_DrawTile8(
    int x,
    int y,
    int tile)
{
    LatchDrawChar(x, y, tile);
}

void VWB_DrawPic(
    int x,
    int y,
    int chunknum)
{
    int picnum = chunknum - STARTPICS;
    int width = pictable[picnum].width;
    int height = pictable[picnum].height;

    VL_MemToScreen(
        static_cast<const uint8_t*>(grsegs[chunknum]),
        width,
        height,
        x & (~7),
        y);
}

void VWB_DrawMPic(
    int x,
    int y,
    int chunknum)
{
    int picnum = chunknum - STARTPICS;
    int width = pictable[picnum].width;
    int height = pictable[picnum].height;

    VL_MaskMemToScreen(
        static_cast<const uint8_t*>(grsegs[chunknum]),
        width,
        height,
        x,
        y,
        255);
}

void VWB_DrawPropString(
    const char* string)
{
    VW_DrawPropString(string);
}

void VWB_Bar(
    int x,
    int y,
    int width,
    int height,
    uint8_t color)
{
    VW_Bar(x, y, width, height, color);
}

void VWB_Plot(
    int x,
    int y,
    uint8_t color)
{
    VW_Plot(x, y, color);
}

void VWB_Hlin(
    int x1,
    int x2,
    int y,
    uint8_t color)
{
    VW_Hlin(x1, x2, y, color);
}

void VWB_Vlin(
    int y1,
    int y2,
    int x,
    uint8_t color)
{
    VW_Vlin(y1, y2, x, color);
}


/*
=============================================================================

 WOLFENSTEIN STUFF

=============================================================================
*/

void LatchDrawPic(
    int x,
    int y,
    int picnum)
{
    int wide = pictable[picnum - STARTPICS].width;
    int height = pictable[picnum - STARTPICS].height;
    int source = latchpics[2 + picnum - LATCHPICS_LUMP_START];

    VL_LatchToScreen(source, wide / 4, height, x * 8, y);
}

int destoff;

void LoadLatchMem()
{
#ifdef BS32X
    // BlakeStone32X: on the 32X the latch area is prebuilt in cart ROM
    // (made by the PC test build); only the offsets are computed here.
    int latch_writable = 0;
    static_cast<void>(bs32x_vid_latch_memory(&latch_writable));
#endif

    int picnum = 0;

    //
    // tile 8s
    //
    latchpics[picnum++] = freelatch;
    destoff = freelatch;

#ifdef BS32X
    if (latch_writable) {
#endif
    CA_CacheGrChunk(STARTTILE8);
    const uint8_t* src = static_cast<const uint8_t*>(grsegs[STARTTILE8]);

    for (int i = 0; i < NUMTILE8; ++i) {
        VL_MemToLatch(src, 8, 8, destoff);
        src += 64;
        destoff += 16;
    }

    UNCACHEGRCHUNK(STARTTILE8);
#ifdef BS32X
    } else {
        destoff += 16 * NUMTILE8;
    }
#endif

    //
    // pics
    //
    ++picnum;

    for (int16_t i = LATCHPICS_LUMP_START; i <= LATCHPICS_LUMP_END; ++i) {
        latchpics[picnum++] = destoff;
        int width = pictable[i - STARTPICS].width;
        int height = pictable[i - STARTPICS].height;

#ifdef BS32X
        if (latch_writable) {
#endif
        CA_CacheGrChunk(i);
        VL_MemToLatch(static_cast<const uint8_t*>(grsegs[i]),
                      width, height, destoff);
        UNCACHEGRCHUNK(i);
#ifdef BS32X
        }
#endif

        destoff += (width / 4) * height;
    }

#ifdef BS32X
    bs32x_dump_latches();
#endif
}

extern ControlInfo c;

// Returns true if aborted
bool FizzleFade(
    int source,
    int dest,
    int width,
    int height,
    int frames,
    bool abortable)
{
    auto rndval = 1;
    auto pixperframe = 64000 / frames;
    auto remain_pixels = 64000 - (frames * pixperframe);
    auto src_offset = ::vl_get_offset(source);
    auto dst_offset = ::vl_get_offset(dest);
    auto frame = 0;

    ::IN_StartAck();

    ::TimeCount = 0;
    ::LastScan = ScanCode::sc_none;

    auto finished = false;
    auto is_aborted = false;
    auto do_full_copy = false;

    while (!finished) {
        if (abortable && ::IN_CheckAck()) {
            is_aborted = true;
            do_full_copy = true;
        }

        if (!do_full_copy) {
            auto pixel_count = pixperframe + remain_pixels;

            remain_pixels = 0;

            for (auto p = 0; p < pixel_count; ++p) {
                auto x = (rndval >> 8) & 0xFFFF;
                auto y = ((rndval & 0xFF) - 1) & 0xFF;

                auto carry = ((rndval & 1) != 0);

                rndval >>= 1;

                if (carry) {
                    rndval ^= 0x00012000;
                }

                if (x > width || y > height) {
                    continue;
                }

                auto pixel_offset = vga_scale * ((y * vga_width) + x);

                for (auto dy = 0; dy < vga_scale; ++dy) {
                    for (auto dx = 0; dx < vga_scale; ++dx) {
                        auto offset = pixel_offset + dx;

                        ::vga_memory[dst_offset + offset] =
                            ::vga_memory[src_offset + offset];
                    }

                    pixel_offset += vga_width;
                }

                if (rndval == 1) {
                    do_full_copy = true;
                }
            }
        } else {
            finished = true;

            std::uninitialized_copy_n(
                &vga_memory[src_offset],
                (::vga_scale * width) * (::vga_scale * height),
                &vga_memory[dst_offset]);
        }

        ::VL_RefreshScreen();

        ++frame;

        ::CalcTics();
    }

    return is_aborted;
}
