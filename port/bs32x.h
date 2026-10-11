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
unsigned char *bs32x_vid_vga_memory(void);          /* emulated VGA memory (256 KB address space) */
void bs32x_vid_set_palette(const unsigned char *pal); /* 768 bytes, 6-bit VGA values */
void bs32x_vid_present(int displayofs);              /* show page at displayofs */
void bs32x_vid_wait_vbl(int vbls);                   /* 70 Hz VGA vblanks */
void bs32x_in_update(void);                          /* platform: read pad, call bs32x_in_key */
void bs32x_in_key(int scan_code, bool is_pressed, char ascii);
/* files: read-only game data and saved files */
bool bs32x_file_open_read(const char *name, const void **data, long *size);
void bs32x_file_save(const char *name, const void *data, long size);
void bs32x_log(const char *message);
/* sound: digitized PCM (8-bit unsigned, ~7 kHz) and music */
bool bs32x_snd_play_pcm(int index, int priority, const void *data, int size, int actor_index, int channel);
bool bs32x_snd_is_playing(void);
bool bs32x_snd_is_channel_playing(int channel);
bool bs32x_snd_play_music(int index, const void *data, int size);
void bs32x_snd_stop_all(void);
void bs32x_snd_stop_music(void);
void bs32x_snd_set_volume(int which, int value);
#include <memory>
#include <string>
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
