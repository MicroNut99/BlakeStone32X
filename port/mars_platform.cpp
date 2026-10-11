/* BlakeStone32X: 32X platform layer for the BStone game code.
   Video: one VGA page (320x208) in SDRAM, copied to the 32X frame buffer
   on present; latches come from cart ROM (LATCHES.BS6).
   Time: TimeCount runs at 70 Hz from the 60 Hz vblank.
   Input: control pad mapped to the game's keys.
   Files: read-only file table in cart ROM ("BSFS"). */
#include <cstring>
#include <cstdlib>
#include "id_heads.h"

extern "C" {
#include "marshw.h"
}

extern "C" void bs32x_vblank(void);
extern "C" void bs32x_text(int x, int y, const char* s);
extern "C" void bs32x_text_clear(void);
extern "C" void bs32x_music_tick(void);

namespace {

uint8_t vga_page[320 * 208] __attribute__((aligned(16)));
uint8_t pal8[768] __attribute__((aligned(4)));
volatile unsigned vbl_acc = 0;
char last_log[64];

/* ---------- ROM file table ---------- */

struct RomEntry {
    char name[16];
    uint32_t offset;   /* from the table start */
    uint32_t size;
};

const uint8_t* romfs = nullptr;
int romfs_count = 0;

uint32_t be32(const uint8_t* p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

void romfs_find()
{
    if (romfs) {
        return;
    }
    /* the table follows the program, on a 4 KB boundary */
    for (uint32_t a = 0x02000000; a < 0x02400000; a += 0x1000) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(a);
        if (p[0] == 'B' && p[1] == 'S' && p[2] == 'F' && p[3] == 'S' &&
            p[4] == '3' && p[5] == '2' && p[6] == 'X' && p[7] == '!') {
            romfs = p;
            romfs_count = static_cast<int>(be32(p + 8));
            return;
        }
    }
}

bool name_eq(const char* a, const char* b)
{
    /* case-insensitive, ignores any directory part of b */
    const char* s = std::strrchr(b, '/');
    if (s) {
        b = s + 1;
    }
    for (int i = 0; i < 16; ++i) {
        char ca = a[i];
        char cb = b[i];
        if (cb >= 'a' && cb <= 'z') {
            cb = static_cast<char>(cb - 32);
        }
        if (ca != cb) {
            return false;
        }
        if (ca == '\0') {
            return true;
        }
    }
    return true;
}

/* ---------- pad ---------- */

int pad_prev = 0;

struct PadKey {
    int mask;
    ScanCode code;
    char ascii;
};

/* A fire / select, B use (open), C strafe, START menu (Esc),
   X run, Y next weapon, Z status (Tab), MODE Enter */
const PadKey pad_keys[] = {
    {SEGA_CTRL_UP, ScanCode::sc_up_arrow, 0},
    {SEGA_CTRL_DOWN, ScanCode::sc_down_arrow, 0},
    {SEGA_CTRL_LEFT, ScanCode::sc_left_arrow, 0},
    {SEGA_CTRL_RIGHT, ScanCode::sc_right_arrow, 0},
    {SEGA_CTRL_A, ScanCode::sc_control, 0},
    {SEGA_CTRL_A, ScanCode::sc_return, 13},
    {SEGA_CTRL_A, ScanCode::sc_y, 'y'},
    {SEGA_CTRL_B, ScanCode::sc_space, ' '},
    {SEGA_CTRL_C, ScanCode::sc_alt, 0},
    {SEGA_CTRL_START, ScanCode::sc_escape, 27},
    {SEGA_CTRL_X, ScanCode::sc_left_shift, 0},
    {SEGA_CTRL_Z, ScanCode::sc_tab, 9},
    {SEGA_CTRL_MODE, ScanCode::sc_return, 13},
};

} // namespace

int bstone_main(int argc, char* argv[]);

extern "C" int bs32x_game_main(int argc, char* argv[])
{
    return bstone_main(argc, argv);
}

/* called from the primary SH2 vblank interrupt (60 Hz NTSC / 50 Hz PAL) */
extern "C" void bs32x_vblank(void)
{
    unsigned hz = mars_refresh_hz ? mars_refresh_hz : 60;
    vbl_acc += 70;
    while (vbl_acc >= hz) {
        vbl_acc -= hz;
        ++TimeCount;
    }
}

unsigned bs32x_random_seed(void)
{
    return mars_vblank_count;
}

static void wait_ticks(int ticks)
{
    uint32_t end = TimeCount + static_cast<uint32_t>(ticks);
    while (static_cast<int32_t>(TimeCount - end) < 0) {
    }
}

void bs32x_sleep_ms(int ms)
{
    int ticks = (ms * 70) / 1000;
    wait_ticks(ticks > 0 ? ticks : 1);
}

unsigned char* bs32x_vid_vga_memory(void)
{
    return vga_page;
}

unsigned char* bs32x_vid_latch_memory(int* writable)
{
    const void* data = nullptr;
    long size = 0;
    *writable = 0;
    if (!bs32x_file_open_read("LATCHES.BS6", &data, &size)) {
        ::Quit("LATCHES.BS6 missing from the cart.");
    }
    return static_cast<unsigned char*>(const_cast<void*>(data));
}

void bs32x_dump_latches(void)
{
}

void bs32x_vid_set_palette(const unsigned char* pal)
{
    for (int i = 0; i < 768; ++i) {
        unsigned c = pal[i] & 63;
        pal8[i] = static_cast<uint8_t>((c << 2) | (c >> 4));
    }
    Mars_SetPalette(pal8);
}

/* copy the page to the 32X back buffer (rows 0..199 -> lines 12..211) and flip */
void bs32x_vid_present(int displayofs)
{
    const uint32_t* src = reinterpret_cast<const uint32_t*>(
        vga_page + ((4 * displayofs) % (320 * 208)));
    uint32_t* dst = reinterpret_cast<uint32_t*>(0x24000200 + 12 * 320);   /* frame buffer pixels */
    int n = 320 * 200 / 4;
    do {
        *dst++ = *src++;
    } while (--n);
    Mars_FlipFrameBuffers(1);
}

void bs32x_vid_wait_vbl(int vbls)
{
    wait_ticks(vbls);
}

void bs32x_in_update(void)
{
    /* the 68000 sends the pad once per vblank; read it once per vblank too
       (reading clears the accumulated value) */
    static unsigned last_vbl = ~0u;
    static int pad_now = 0;
    bs32x_music_tick();

    if (mars_vblank_count != last_vbl) {
        last_vbl = mars_vblank_count;
        int v = Mars_ReadController(0);
        pad_now = v < 0 ? 0 : v;
    }

    int pad = pad_now;
    int changed = pad ^ pad_prev;
    if (changed) {
        for (const auto& k : pad_keys) {
            if (changed & k.mask) {
                bool down = (pad & k.mask) != 0;
                bs32x_in_key(static_cast<int>(k.code), down, down ? k.ascii : 0);
            }
        }
        pad_prev = pad;
    }
}

bool bs32x_file_open_read(const char* name, const void** data, long* size)
{
    romfs_find();
    if (!romfs) {
        return false;
    }
    const uint8_t* e = romfs + 16;
    for (int i = 0; i < romfs_count; ++i, e += 24) {
        if (name_eq(reinterpret_cast<const char*>(e), name)) {
            *data = romfs + be32(e + 16);
            *size = static_cast<long>(be32(e + 20));
            return true;
        }
    }
    return false;
}

void bs32x_file_save(const char*, const void*, long)
{
    /* no save RAM yet: config and saved games are not kept */
}

bool bs32x_is_rom(const void* p)
{
    uintptr_t a = reinterpret_cast<uintptr_t>(p) & 0xDFFFFFFF;   /* also the cache-through alias */
    return a >= 0x02000000 && a < 0x02400000;
}

void bs32x_log(const char* message)
{
    std::strncpy(last_log, message, sizeof(last_log) - 1);
    last_log[sizeof(last_log) - 1] = '\0';
}

bool bs32x_snd_play_pcm(int, int, const void*, int, int, int)
{
    return false;
}

bool bs32x_snd_is_playing(void)
{
    return false;
}

bool bs32x_snd_is_channel_playing(int)
{
    return false;
}

static uint32_t music_start = 0;
static bool music_on = false;

bool bs32x_snd_play_music(int, const void*, int)
{
    music_start = TimeCount;
    music_on = true;
    return true;
}

void bs32x_snd_stop_all(void)
{
}

void bs32x_snd_stop_music(void)
{
    music_on = false;
}

void bs32x_snd_set_volume(int, int)
{
}

/* no music player yet: report the song as played once after 8 seconds */
extern "C" void bs32x_music_tick(void)
{
    extern bool sqPlayedOnce;
    if (music_on && TimeCount - music_start > 70 * 8) {
        sqPlayedOnce = true;
    }
}

bool bs32x_archive_error = false;

void bs32x_archive_fail(const char* what)
{
    bs32x_archive_error = true;
    bs32x_log(what);
}

/* last stop: show the reason on screen */
static void stop_screen(const char* title, const char* text)
{
    bs32x_text_clear();
    bs32x_text(8, 8, title);
    bs32x_text(8, 20, text);
    bs32x_text(8, 36, "PRESS RESET");
    for (;;) {
    }
}

static char* hex8(char* p, uint32_t v)
{
    for (int i = 7; i >= 0; --i) {
        int d = (v >> (i * 4)) & 15;
        *p++ = static_cast<char>(d < 10 ? '0' + d : 'A' + d - 10);
    }
    *p = '\0';
    return p;
}

extern "C" void bs32x_exception(int kind, unsigned pc, unsigned pr)
{
    static const char* const names[] = {
        "?", "ILLEGAL INSTRUCTION", "INVALID SLOT", "ADDRESS ERROR", "DMA ADDRESS ERROR", "TRAPA",
    };
    char line[48];
    char* p = line;
    for (const char* s = "PC "; *s; ) {
        *p++ = *s++;
    }
    p = hex8(p, pc);
    for (const char* s = " PR "; *s; ) {
        *p++ = *s++;
    }
    hex8(p, pr);
    bs32x_text_clear();
    bs32x_text(8, 8, "STOP SH2 EXCEPTION");
    bs32x_text(8, 20, names[(kind >= 1 && kind <= 5) ? kind : 0]);
    bs32x_text(8, 32, line);
    bs32x_text(8, 44, last_log[0] ? last_log : "(no log)");
    bs32x_text(8, 60, "PRESS RESET");
    for (;;) {
    }
}

void bs32x_fatal(const char* what)
{
    stop_screen("STOP FATAL", what);
}

extern "C" void _exit(int code)
{
    static_cast<void>(code);
    stop_screen("STOP GAME QUIT", last_log[0] ? last_log : "(no message)");
}

extern "C" void abort(void)
{
    stop_screen("STOP ABORT", last_log[0] ? last_log : "(no message)");
}
