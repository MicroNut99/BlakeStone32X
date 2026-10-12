/* BlakeStone32X port glue shared by the BStone game code. */
#ifndef BS32X_H
#define BS32X_H
#include "SDL.h"
/* exceptions are off on the 32X: archive (save/load) errors set a flag instead */
extern bool bs32x_archive_error;
void bs32x_archive_fail(const char *what);
void bs32x_fatal(const char *what);
/* platform services (32X or PC test build) */
unsigned bs32x_random_seed(void);
void bs32x_sleep_ms(int ms);
unsigned char *bs32x_vid_vga_memory(void);          /* one VGA page: 320 x 208 bytes */
unsigned char *bs32x_vid_latch_memory(int *writable); /* latch area (status bar pics) */
void bs32x_dump_latches(void);                       /* PC build: save the latch area for the cart */
void bs32x_vid_set_palette(const unsigned char *pal); /* 768 bytes, 6-bit VGA values */
void bs32x_vid_present(int displayofs);              /* show page at displayofs */
void bs32x_vid_wait_vbl(int vbls);                   /* 70 Hz VGA vblanks */
/* 32-bit signed divide: the SH2's divide unit on the 32X */
static inline int bs32x_div(int a, int b)
{
#ifdef BS32X_MARS
    volatile int* const divu = reinterpret_cast<volatile int*>(0xFFFFFF00);
    divu[0] = b;            /* DVSR */
    divu[1] = a;            /* DVDNT: starts the divide */
    return divu[1];         /* waits for the quotient */
#else
    return a / b;
#endif
}
unsigned char* vl_page_ptr(int x, int y);            /* vl_port: pixel of the draw page */
void bs32x_in_update(void);                          /* platform: read pad, call bs32x_in_key */
void bs32x_in_key(int scan_code, bool is_pressed, char ascii);
/* pad meaning: 0 menus/screens, 1 playing, 2 yes/no question; returns the old mode */
enum { BS32X_IN_UI = 0, BS32X_IN_PLAY = 1, BS32X_IN_CONFIRM = 2 };
int bs32x_set_input_mode(int mode);
bool bs32x_pad_idle(void);                           /* no pad button held */
extern volatile bool bs32x_next_weapon;                /* Y pressed while playing */
#ifdef __cplusplus
struct Bs32xInputMode {                               /* sets a pad meaning for one scope */
    int old;
    explicit Bs32xInputMode(int mode) : old(bs32x_set_input_mode(mode)) {}
    ~Bs32xInputMode() { bs32x_set_input_mode(old); }
};
#endif
/* files: read-only game data and saved files */
bool bs32x_file_open_read(const char *name, const void **data, long *size);
void bs32x_file_save(const char *name, const void *data, long size);
void bs32x_log(const char *message);
bool bs32x_is_rom(const void *p);                    /* data in cart ROM (never freed) */
extern "C" int bs32x_lz4_decode(const unsigned char *src, int src_len, unsigned char *dst, int dst_len);
/* sound: digitized PCM (8-bit unsigned, ~7 kHz) and music */
bool bs32x_snd_play_pcm(int index, int priority, const void *data, int size, int actor_index, int channel);
bool bs32x_snd_play_adlib(int sound_index, int priority, int actor_index, int channel);
bool bs32x_snd_is_playing(void);
bool bs32x_snd_is_channel_playing(int channel);
bool bs32x_snd_play_music(int index, const void *data, int size);
void bs32x_snd_stop_all(void);
void bs32x_snd_stop_music(void);
void bs32x_snd_set_volume(int which, int value);
#include <memory>
#include <string>
/* endian-neutral, alignment-safe little-endian reads (the SH2 is big-endian
   and faults on unaligned 16/32-bit loads) */
static inline unsigned short bs32x_le16(const void *p)
{
    const unsigned char *b = static_cast<const unsigned char *>(p);
    return static_cast<unsigned short>(b[0] | (b[1] << 8));
}
static inline unsigned bs32x_le32(const void *p)
{
    const unsigned char *b = static_cast<const unsigned char *>(p);
    return b[0] | (b[1] << 8) | (b[2] << 16) | (static_cast<unsigned>(b[3]) << 24);
}

/* two uppercase hex digits, as FormatString() << setw(2) << setfill('0') << hex << uppercase */
static inline std::string bs32x_hex2(int value)
{
    static const char digits[] = "0123456789ABCDEF";
    std::string s(2, '0');
    s[0] = digits[(value >> 4) & 15];
    s[1] = digits[value & 15];
    return s;
}
#include <deque>
#include <list>
#include <algorithm>
#include <array>
#include <cstddef>
#endif
