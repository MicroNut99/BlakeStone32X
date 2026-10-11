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

/* menus and screens: A / C / START choose, B / MODE back */
const PadKey ui_keys[] = {
    {SEGA_CTRL_UP, ScanCode::sc_up_arrow, 0},
    {SEGA_CTRL_DOWN, ScanCode::sc_down_arrow, 0},
    {SEGA_CTRL_LEFT, ScanCode::sc_left_arrow, 0},
    {SEGA_CTRL_RIGHT, ScanCode::sc_right_arrow, 0},
    {SEGA_CTRL_A, ScanCode::sc_return, 13},
    {SEGA_CTRL_C, ScanCode::sc_return, 13},
    {SEGA_CTRL_START, ScanCode::sc_return, 13},
    {SEGA_CTRL_B, ScanCode::sc_escape, 27},
    {SEGA_CTRL_MODE, ScanCode::sc_escape, 27},
};

/* yes/no questions: A / C / START yes, B / MODE no */
const PadKey confirm_keys[] = {
    {SEGA_CTRL_A, ScanCode::sc_y, 'y'},
    {SEGA_CTRL_C, ScanCode::sc_y, 'y'},
    {SEGA_CTRL_START, ScanCode::sc_y, 'y'},
    {SEGA_CTRL_B, ScanCode::sc_n, 'n'},
    {SEGA_CTRL_MODE, ScanCode::sc_n, 'n'},
};

/* playing: A open/use, B fire, C strafe, X run, Y next weapon,
   Z / MODE status, START menu */
const PadKey play_keys[] = {
    {SEGA_CTRL_UP, ScanCode::sc_up_arrow, 0},
    {SEGA_CTRL_DOWN, ScanCode::sc_down_arrow, 0},
    {SEGA_CTRL_LEFT, ScanCode::sc_left_arrow, 0},
    {SEGA_CTRL_RIGHT, ScanCode::sc_right_arrow, 0},
    {SEGA_CTRL_B, ScanCode::sc_control, 0},
    {SEGA_CTRL_A, ScanCode::sc_space, ' '},
    {SEGA_CTRL_C, ScanCode::sc_alt, 0},
    {SEGA_CTRL_X, ScanCode::sc_left_shift, 0},
    {SEGA_CTRL_Z, ScanCode::sc_tab, 9},
    {SEGA_CTRL_MODE, ScanCode::sc_tab, 9},
    {SEGA_CTRL_START, ScanCode::sc_escape, 27},
};

struct PadMap {
    const PadKey* keys;
    int count;
};

const PadMap pad_maps[3] = {
    {ui_keys, int(sizeof(ui_keys) / sizeof(ui_keys[0]))},
    {play_keys, int(sizeof(play_keys) / sizeof(play_keys[0]))},
    {confirm_keys, int(sizeof(confirm_keys) / sizeof(confirm_keys[0]))},
};

int in_mode = BS32X_IN_UI;

const int dpad_mask = SEGA_CTRL_UP | SEGA_CTRL_DOWN | SEGA_CTRL_LEFT | SEGA_CTRL_RIGHT;

} // namespace

extern "C" {
uint8_t bs32x_pal8[768] __attribute__((aligned(4)));
}
#define pal8 bs32x_pal8

int bstone_main(int argc, char* argv[]);

void bs32x_sound_init(void);

extern "C" int bs32x_game_main(int argc, char* argv[])
{
    bs32x_sound_init();
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

/* ---------- second SH2 ---------- */
/* COMM4: 1 = draw your half of the 3D view, 2 = show the page,
   7 = start the sound (PWM and DMA), 0 = done */

#include "bs32x_render.h"
#include "bs32x_sound.h"

namespace {

const uint32_t* present_src = nullptr;   /* page rows to show (set before a job 2) */

/* copy the page rows to the 32X back buffer (lines 12..211) and ask for the
   flip at the next vblank; the copy waits for the previous flip first,
   because until then the back buffer is still on screen */
void copy_and_flip(const uint32_t* src)
{
    Mars_WaitFrameBuffersFlip();
    uint32_t* dst = reinterpret_cast<uint32_t*>(0x24000200 + 12 * 320);
    int n = 320 * 200 / 16;
    do {
        uint32_t a = src[0];
        uint32_t b = src[1];
        uint32_t c = src[2];
        uint32_t d = src[3];
        dst[0] = a;
        dst[1] = b;
        dst[2] = c;
        dst[3] = d;
        src += 4;
        dst += 4;
    } while (--n);
    Mars_FlipFrameBuffers(0);
}

bool second_busy = false;

} // namespace

extern "C" void bs32x_second_loop(void)
{
    volatile uint16_t* comm4 = reinterpret_cast<volatile uint16_t*>(&MARS_SYS_COMM4);
    for (;;) {
        int cmd;
        while ((cmd = *comm4) != 1 && cmd != 2 && cmd != 7) {
        }
        CacheControl(SH2_CCTL_CP | SH2_CCTL_CE);   /* see the first CPU's latest data */
        if (cmd == 1) {
            bs32x_render_second();
        } else if (cmd == 7) {
            bs32x_sound_start();
        } else {
            copy_and_flip(present_src);
        }
        *comm4 = 0;
    }
}

void bs32x_second_cpu_wait(void)
{
    if (!second_busy) {
        return;
    }
    volatile uint16_t* comm4 = reinterpret_cast<volatile uint16_t*>(&MARS_SYS_COMM4);
    while (*comm4 != 0) {
    }
    second_busy = false;
    CacheControl(SH2_CCTL_CP | SH2_CCTL_CE);       /* see what the second CPU did */
}

/* start the sound effects on the second CPU (once, at start-up) */
void bs32x_sound_init(void)
{
    bs32x_second_cpu_wait();
    second_busy = true;
    MARS_SYS_COMM4 = 7;
    bs32x_second_cpu_wait();
}

bool bs32x_second_cpu_start(void)
{
    bs32x_second_cpu_wait();
    second_busy = true;
    MARS_SYS_COMM4 = 1;
    return true;
}

bool bs32x_present_async = false;

/* the two CPUs meet during a frame: COMM4 3 = second CPU's walls done,
   4 = go on with the floor and ceiling, 5 = its floors done, 6 = go on with
   the sprites */
void bs32x_sync_first(int ready, int go)
{
    volatile uint16_t* comm4 = reinterpret_cast<volatile uint16_t*>(&MARS_SYS_COMM4);
    while (*comm4 != ready) {
    }
    CacheControl(SH2_CCTL_CP | SH2_CCTL_CE);       /* see what the second CPU wrote */
    *comm4 = static_cast<uint16_t>(go);
}

void bs32x_sync_second(int ready, int go)
{
    volatile uint16_t* comm4 = reinterpret_cast<volatile uint16_t*>(&MARS_SYS_COMM4);
    *comm4 = static_cast<uint16_t>(ready);
    while (*comm4 != go) {
    }
    CacheControl(SH2_CCTL_CP | SH2_CCTL_CE);
}

extern "C" {
volatile uint32_t bs32x_present_count;   /* screens shown (for speed tests) */
}

/* show the page (rows 0..199 -> lines 12..211); during play the second CPU
   does it while the first goes on with the next frame */
void bs32x_vid_present(int displayofs)
{
    ++bs32x_present_count;
    const uint32_t* src = reinterpret_cast<const uint32_t*>(
        vga_page + ((4 * displayofs) % (320 * 208)));
    bs32x_second_cpu_wait();
    if (bs32x_present_async) {
        present_src = src;
        second_busy = true;
        MARS_SYS_COMM4 = 2;
        return;
    }
    copy_and_flip(src);
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
        const PadMap& m = pad_maps[in_mode];
        for (int i = 0; i < m.count; ++i) {
            const PadKey& k = m.keys[i];
            if (changed & k.mask) {
                bool down = (pad & k.mask) != 0;
                bs32x_in_key(static_cast<int>(k.code), down, down ? k.ascii : 0);
            }
        }
        if (in_mode == BS32X_IN_PLAY && (changed & pad & SEGA_CTRL_Y)) {
            bs32x_next_weapon = true;
        }
        pad_prev = pad;
    }
}

volatile bool bs32x_next_weapon = false;

/* a new pad meaning: let go of the buttons of the old one; a button that is
   still held only counts after it is pressed again (the d-pad carries over) */
int bs32x_set_input_mode(int mode)
{
    int old = in_mode;
    if (mode == old || mode < 0 || mode > 2) {
        return old;
    }
    const PadMap& m = pad_maps[old];
    for (int i = 0; i < m.count; ++i) {
        const PadKey& k = m.keys[i];
        if ((pad_prev & k.mask & ~dpad_mask) != 0) {
            bs32x_in_key(static_cast<int>(k.code), false, 0);
        }
    }
    in_mode = mode;
    bs32x_next_weapon = false;
    return old;
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

/* ---------- sound effects (mixed on the second CPU, bs32x_sound.c) ---------- */

extern uint16_t* DigiList;

namespace {

struct SndSlot {
    int actor;          /* objlist index, -1 none */
    int channel;
    int priority;
    uint32_t end;       /* TimeCount when it has finished */
};

SndSlot snd_slots[BS32X_SND_VOICES];
int snd_volume = 64;

const uint8_t* adlib_sfx = nullptr;
bool adlib_tried = false;

bool slot_busy(const SndSlot& s)
{
    return static_cast<int32_t>(s.end - TimeCount) > 0;
}

/* the voice for a new sound, or -1 */
int pick_slot(int priority, int actor, int channel)
{
    if (actor >= 0) {
        for (int i = 0; i < BS32X_SND_VOICES; ++i) {
            if (snd_slots[i].actor == actor && snd_slots[i].channel == channel && slot_busy(snd_slots[i])) {
                return i;   /* the same actor's voice: the new sound replaces it */
            }
        }
    }
    int best = -1;
    for (int i = 0; i < BS32X_SND_VOICES; ++i) {
        if (!slot_busy(snd_slots[i])) {
            return i;
        }
        if (snd_slots[i].priority <= priority && (best < 0 || snd_slots[i].priority < snd_slots[best].priority)) {
            best = i;
        }
    }
    return best;
}

bool start_sound(const uint8_t* rec, int page, int pages, int samples, int priority, int actor, int channel)
{
    if (!rec || samples <= 0 || snd_volume == 0) {
        return false;
    }
    int slot = pick_slot(priority, actor, channel);
    if (slot < 0) {
        return false;
    }
    SndSlot& s = snd_slots[slot];
    s.actor = actor;
    s.channel = channel;
    s.priority = priority;
    s.end = TimeCount + static_cast<uint32_t>(samples / 100 + 1);   /* 7000 Hz, 70 ticks/s */

    Bs32xSndCmd c;
    c.kind = BS32X_SND_START;
    c.slot = slot;
    c.data = rec;
    c.page = page;
    c.pages = pages;
    c.vol = 64;
    bs32x_sound_command(&c);
    return true;
}

} // namespace

extern "C" const uint8_t* bs32x_sound_page(int page)
{
    return static_cast<const uint8_t*>(PM_GetSoundPage(page));
}

bool bs32x_snd_play_pcm(int index, int priority, const void* data, int size, int actor_index, int channel)
{
    int page = DigiList[2 * index];
    int pages = (size + PMPageSize - 1) / PMPageSize;
    return start_sound(static_cast<const uint8_t*>(data), page, pages, size, priority, actor_index, channel);
}

/* AdLib-only sound effects, made into ADPCM on the PC (ADLIBSFX.BS6) */
bool bs32x_snd_play_adlib(int sound_index, int priority, int actor_index, int channel)
{
    if (!adlib_tried) {
        adlib_tried = true;
        const void* d = nullptr;
        long size = 0;
        if (bs32x_file_open_read("ADLIBSFX.BS6", &d, &size)) {
            adlib_sfx = static_cast<const uint8_t*>(d);
        }
    }
    if (!adlib_sfx || sound_index < 0 || sound_index >= static_cast<int>(bs32x_le32(adlib_sfx + 4))) {
        return false;
    }
    uint32_t at = bs32x_le32(adlib_sfx + 8 + 4 * sound_index);
    if (at == 0) {
        return false;
    }
    const uint8_t* rec = adlib_sfx + at;
    int samples = rec[0] | (rec[1] << 8);
    return start_sound(rec, -1, 1, samples, priority, actor_index, channel);
}

bool bs32x_snd_is_playing(void)
{
    for (const auto& s : snd_slots) {
        if (slot_busy(s)) {
            return true;
        }
    }
    return false;
}

bool bs32x_snd_is_channel_playing(int channel)
{
    for (const auto& s : snd_slots) {
        if (s.actor == 0 && s.channel == channel && slot_busy(s)) {
            return true;
        }
    }
    return false;
}

/* ---------- music: YM2612 VGM songs played by the Genesis side (MUSIC.BS6) ---------- */

static const uint8_t* music_file = nullptr;
static bool music_tried = false;
static uint32_t music_start = 0;
static uint32_t music_ticks = 0;
static bool music_on = false;
static int music_volume = 128;

bool bs32x_snd_play_music(int index, const void*, int)
{
    if (!music_tried) {
        music_tried = true;
        const void* d = nullptr;
        long size = 0;
        if (bs32x_file_open_read("MUSIC.BS6", &d, &size)) {
            music_file = static_cast<const uint8_t*>(d);
        }
    }
    bs32x_snd_stop_music();
    if (!music_file || index < 0 || index >= static_cast<int>(bs32x_le32(music_file + 4))) {
        return false;
    }
    const uint8_t* e = music_file + 8 + 12 * index;
    uint32_t at = bs32x_le32(e);
    uint32_t size = bs32x_le32(e + 4);
    if (at == 0 || size == 0) {
        return false;
    }
    uintptr_t addr = reinterpret_cast<uintptr_t>(music_file + at);
    if ((addr >> 20) != ((addr + size - 1) >> 20)) {
        return false;       /* the Genesis side reads one 1 MB ROM bank */
    }
    music_ticks = bs32x_le32(e + 8);
    music_start = TimeCount;
    music_on = true;
    Mars_SetMusicVolume(static_cast<uint8_t>(music_volume));
    Mars_PlayTrack(0, index + 1, "", static_cast<int>(addr), static_cast<int>(size), 1);
    return true;
}

void bs32x_snd_stop_all(void)
{
    for (auto& s : snd_slots) {
        s.end = TimeCount;
    }
    Bs32xSndCmd c = {};
    c.kind = BS32X_SND_STOP_ALL;
    bs32x_sound_command(&c);
}

void bs32x_snd_stop_music(void)
{
    if (music_on) {
        Mars_StopTrack();
    }
    music_on = false;
}

void bs32x_snd_set_volume(int which, int value)
{
    if (which == 1) {
        /* music: game 0..20 (default 5) -> FM volume 0..255 */
        int v = value * 255 / 10;
        music_volume = v > 255 ? 255 : (v < 0 ? 0 : v);
        if (music_on) {
            Mars_SetMusicVolume(static_cast<uint8_t>(music_volume));
        }
        return;
    }
    if (which != 0) {
        return;
    }
    /* sound effects: game 0..20 (default 10) -> 0..64, full from 12 */
    int v = value * 64 / 12;
    snd_volume = v > 64 ? 64 : (v < 0 ? 0 : v);
    Bs32xSndCmd c = {};
    c.kind = BS32X_SND_VOLUME;
    c.vol = snd_volume;
    bs32x_sound_command(&c);
}

/* the song has played once after its length */
extern "C" void bs32x_music_tick(void)
{
    extern bool sqPlayedOnce;
    if (music_on && TimeCount - music_start >= music_ticks) {
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
