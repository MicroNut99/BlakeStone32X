/* BlakeStone32X PC test platform (headless).
   - game files from $BS32X_DATA (case-insensitive names)
   - virtual 70 Hz clock: advances when the game sleeps or presents
   - input script from $BS32X_SCRIPT, times in 70 Hz ticks of the virtual clock:
       "<tick> <+|-><key>", "m<music> <ticks after it started> <+|-><key>",
       "u<music> <period> <key>" (tap until that music starts); key names below
   - every presented frame N in $BS32X_SHOTS ("10,50,200" or "all") is written
     to $BS32X_OUT/frame_NNNNN.ppm; the run ends after $BS32X_FRAMES frames */
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <sys/mman.h>
#include "id_heads.h"
#include "bs32x_render.h"

namespace {

std::vector<uint8_t> vga(320 * 208);
std::vector<uint8_t> latch(64 * 1024);
uint8_t palette[768];
int frame_no = 0;
int max_frames = 2000;
std::set<int> shots;
bool shots_all = false;
std::string out_dir = ".";

struct ScriptEvent {
    int frame;      // frame number, or offset after the anchor
    int anchor;     // -1: absolute; else music index whose start is the anchor
    int key;
    bool down;
    char ascii;
};

int music_start_frame[64];

// "u<music> <period> <key>": tap key every period frames until that music starts
struct UntilTap {
    int music;      // stop when this music starts
    int period;
    int key;
    char ascii;
    int after;      // start only after this music started (+period), -1: from the beginning
};
std::vector<UntilTap> untils;

std::vector<ScriptEvent> script;
size_t script_pos = 0;

struct RoFile {
    const uint8_t* p;
    size_t n;
    const uint8_t* data() const { return p; }
    size_t size() const { return n; }
    bool empty() const { return n == 0; }
};
std::map<std::string, RoFile> files;

std::string upper(const char* s)
{
    std::string r(s);
    for (auto& c : r) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    // strip directories
    auto p = r.find_last_of("/\\");
    if (p != std::string::npos) {
        r = r.substr(p + 1);
    }
    return r;
}

struct KeyName {
    const char* name;
    ScanCode code;
    char ascii;
};

const KeyName key_names[] = {
    {"up", ScanCode::sc_up_arrow, 0}, {"down", ScanCode::sc_down_arrow, 0},
    {"left", ScanCode::sc_left_arrow, 0}, {"right", ScanCode::sc_right_arrow, 0},
    {"fire", ScanCode::sc_control, 0}, {"use", ScanCode::sc_space, ' '},
    {"strafe", ScanCode::sc_alt, 0}, {"run", ScanCode::sc_left_shift, 0},
    {"enter", ScanCode::sc_return, 13}, {"esc", ScanCode::sc_escape, 27},
    {"y", ScanCode::sc_y, 'y'}, {"n", ScanCode::sc_n, 'n'},
    {"1", ScanCode::sc_1, '1'}, {"2", ScanCode::sc_2, '2'}, {"3", ScanCode::sc_3, '3'},
    {"tab", ScanCode::sc_tab, 9},
};

void load_script()
{
    const char* path = std::getenv("BS32X_SCRIPT");
    if (!path) {
        return;
    }
    FILE* f = std::fopen(path, "r");
    if (!f) {
        return;
    }
    char line[128];
    while (std::fgets(line, sizeof(line), f)) {
        int fr = 0;
        int anchor = -1;
        char name[32] = {};
        if (line[0] == 'u') {
            int music = 0, period = 0, after = -1;
            if (std::sscanf(line + 1, "%d %d %31s %d", &music, &period, name, &after) >= 3) {
                for (auto& k : key_names) {
                    if (std::strcmp(k.name, name) == 0) {
                        untils.push_back({music, period, static_cast<int>(k.code), k.ascii, after});
                    }
                }
            }
            continue;
        }
        if (line[0] == 'm') {
            // "m<music> <offset> <key>": relative to the start of that music
            if (std::sscanf(line + 1, "%d %d %31s", &anchor, &fr, name) != 3) {
                continue;
            }
        } else if (std::sscanf(line, "%d %31s", &fr, name) != 2) {
            continue;
        }
        bool down = name[0] != '-';
        const char* n = (name[0] == '+' || name[0] == '-') ? name + 1 : name;
        for (auto& k : key_names) {
            if (std::strcmp(k.name, n) == 0) {
                script.push_back({fr, anchor, static_cast<int>(k.code), down, k.ascii});
            }
        }
    }
    std::fclose(f);
}

void write_frame(int displayofs)
{
    char path[512];
    std::snprintf(path, sizeof(path), "%s/frame_%05d.ppm", out_dir.c_str(), frame_no);
    FILE* f = std::fopen(path, "wb");
    if (!f) {
        return;
    }
    std::fprintf(f, "P6 320 200 255\n");
    const uint8_t* src = &vga[(4 * displayofs) % (320 * 208)];
    for (int i = 0; i < 320 * 200; ++i) {
        const uint8_t* p = &palette[3 * src[i]];
        uint8_t rgb[3] = {
            static_cast<uint8_t>((p[0] << 2) | (p[0] >> 4)),
            static_cast<uint8_t>((p[1] << 2) | (p[1] >> 4)),
            static_cast<uint8_t>((p[2] << 2) | (p[2] >> 4)),
        };
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

bool initialized = false;

void init()
{
    if (initialized) {
        return;
    }
    initialized = true;
    for (auto& m : music_start_frame) {
        m = -1;
    }
    if (std::getenv("BS32X_ORIG")) {
        ::bs32x_fast_render = false;     /* the game's own 3D drawing */
    }
    if (const char* s = std::getenv("BS32X_FRAMES")) {
        max_frames = std::atoi(s);
    }
    if (const char* s = std::getenv("BS32X_OUT")) {
        out_dir = s;
    }
    if (const char* s = std::getenv("BS32X_SHOTS")) {
        if (std::strcmp(s, "all") == 0) {
            shots_all = true;
        } else {
            std::string str(s);
            size_t p = 0;
            while (p < str.size()) {
                shots.insert(std::atoi(str.c_str() + p));
                p = str.find(',', p);
                if (p == std::string::npos) {
                    break;
                }
                ++p;
            }
        }
    }
    load_script();
}

} // namespace

static uint32_t music_start = 0;
static bool music_on = false;

unsigned bs32x_random_seed(void)
{
    return 0;
}

void bs32x_sleep_ms(int ms)
{
    init();
    int ticks = (ms * 70) / 1000;
    TimeCount += ticks > 0 ? ticks : 1;
}

unsigned char* bs32x_vid_vga_memory(void)
{
    return vga.data();
}

unsigned char* bs32x_vid_latch_memory(int* writable)
{
    *writable = 1;
    return latch.data();
}

// writes the latch area built by LoadLatchMem: this file goes into the 32X cart
void bs32x_dump_latches(void)
{
    extern int destoff;
    const char* path = std::getenv("BS32X_DUMP_LATCHES");
    if (!path) {
        return;
    }
    int size = 4 * (destoff - 80 * 208 * 3);
    FILE* f = std::fopen(path, "wb");
    if (f) {
        std::fwrite(latch.data(), 1, static_cast<size_t>(size), f);
        std::fclose(f);
        std::fprintf(stderr, "LATCHES %d bytes -> %s\n", size, path);
    }
}

void bs32x_vid_set_palette(const unsigned char* pal)
{
    std::memcpy(palette, pal, 768);
}

void bs32x_dump_grsizes(void);

void bs32x_vid_present(int displayofs)
{
    init();
    if (frame_no == 0) {
        bs32x_dump_grsizes();
        extern void bs32x_dump_grexp(void);
        bs32x_dump_grexp();
    }
    if (shots_all || shots.count(frame_no)) {
        write_frame(displayofs);
    }
    ++frame_no;
    TimeCount += 2;
    if (frame_no >= max_frames) {
        std::fprintf(stderr, "BS32X host: %d frames done\n", frame_no);
        extern void bs32x_mem_report(void);
        bs32x_mem_report();
        std::fflush(stderr);
    std::_Exit(0);
    }
}

void bs32x_vid_wait_vbl(int vbls)
{
    TimeCount += vbls;
}

void bs32x_in_update(void)
{
    init();
    TimeCount += 1;   /* polling loops must see time pass on the virtual clock */
    extern bool sqPlayedOnce;
    if (music_on && TimeCount - music_start > 70 * 8) {
        sqPlayedOnce = true;   /* no music player on the test build: pretend it looped */
    }
    for (auto& u : untils) {
        if (music_start_frame[u.music & 63] >= 0 || u.period <= 0) {
            continue;
        }
        if (u.after >= 0 && (music_start_frame[u.after & 63] < 0 ||
            static_cast<int>(TimeCount) < music_start_frame[u.after & 63] + 2000))
        {
            continue;
        }
        int ph = static_cast<int>(TimeCount) % u.period;
        if (ph == 0) {
            bs32x_in_key(u.key, true, u.ascii);
        } else if (ph == 4) {
            bs32x_in_key(u.key, false, 0);
        }
    }

    while (script_pos < script.size()) {
        const auto& e = script[script_pos];
        int at = e.frame;
        if (e.anchor >= 0) {
            if (music_start_frame[e.anchor & 63] < 0) {
                break;
            }
            at += music_start_frame[e.anchor & 63];
        }
        if (at > static_cast<int>(TimeCount)) {
            break;
        }
        ++script_pos;
        std::fprintf(stderr, "KEY %s%d tick %u frame %d\n", e.down ? "+" : "-", e.key, TimeCount, frame_no);
        bs32x_in_key(e.key, e.down, e.down ? e.ascii : 0);
    }
}

volatile bool bs32x_next_weapon = false;

/* one CPU: the fast renderer draws both halves itself */
bool bs32x_second_cpu_start(void)
{
    return false;
}

void bs32x_second_cpu_wait(void)
{
}

bool bs32x_present_async = false;

void bs32x_sync_first(int, int)
{
}

void bs32x_sync_second(int, int)
{
}

int bs32x_set_input_mode(int mode)
{
    static int cur = 0;
    int old = cur;
    cur = mode;
    return old;
}

bool bs32x_file_open_read(const char* name, const void** data, long* size)
{
    extern bool mem_ignore;
    std::string key = upper(name);
    auto it = files.find(key);
    mem_ignore = true;
    if (it == files.end()) {
        const char* dir = std::getenv("BS32X_DATA");
        std::string path = std::string(dir ? dir : ".") + "/" + key;
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) {
            mem_ignore = false;
            return false;
        }
        std::vector<uint8_t> buf;
        uint8_t tmp[65536];
        size_t n;
        while ((n = std::fread(tmp, 1, sizeof(tmp), f)) > 0) {
            buf.insert(buf.end(), tmp, tmp + n);
        }
        std::fclose(f);
        /* read-only copy, like cart ROM: a write by the game crashes here */
        size_t len = (buf.size() + 8 + 4095) & ~size_t(4095);
        void* m = mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        std::memcpy(m, buf.data(), buf.size());
        mprotect(m, len, PROT_READ);
        it = files.emplace(key, RoFile{static_cast<const uint8_t*>(m), buf.size()}).first;
    }
    mem_ignore = false;
    *data = it->second.data();
    *size = static_cast<long>(it->second.size());
    return true;
}

bool bs32x_is_rom(const void* p)
{
    auto a = static_cast<const uint8_t*>(p);
    for (auto& kv : files) {
        const auto& v = kv.second;
        if (!v.empty() && a >= v.data() && a <= v.data() + v.size()) {
            return true;
        }
    }
    return false;
}

void bs32x_file_save(const char* name, const void* data, long size)
{
    /* like the cart: nothing is kept */
    static_cast<void>(name);
    static_cast<void>(data);
    static_cast<void>(size);
}

void bs32x_log(const char* message)
{
    std::fprintf(stderr, "LOG: %s\n", message);
}

bool bs32x_snd_play_pcm(int index, int, const void*, int size, int, int)
{
    std::fprintf(stderr, "SND pcm %d (%d bytes) frame %d\n", index, size, frame_no);
    return true;
}

bool bs32x_snd_is_playing(void)
{
    return false;
}

bool bs32x_snd_is_channel_playing(int)
{
    return false;
}

bool bs32x_snd_play_music(int index, const void*, int)
{
    music_start = TimeCount;
    music_on = true;
    if (music_start_frame[index & 63] < 0) {
        music_start_frame[index & 63] = static_cast<int>(TimeCount);
    }
    std::fprintf(stderr, "MUSIC %d tick %u frame %d\n", index, TimeCount, frame_no);
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

bool bs32x_archive_error = false;

void bs32x_archive_fail(const char* what)
{
    bs32x_archive_error = true;
    std::fprintf(stderr, "archive error: %s\n", what);
}

void bs32x_fatal(const char* what)
{
    std::fprintf(stderr, "FATAL: %s\n", what);
    std::exit(1);
}

// ---- memory accounting for the 32X budget (PC test build only) ----
#include <new>
static size_t mem_now = 0, mem_peak = 0;
struct LiveBlock { size_t size; void* site; };
static std::map<void*, LiveBlock>* live = nullptr;
static std::vector<std::pair<size_t, void*>> peak_snapshot;
static std::vector<int> peak_grsegs;
static uint32_t peak_tick = 0;
static bool in_track = false;
bool mem_ignore = false;   // file cache = cart ROM on the 32X, not counted
static void* track_alloc(size_t n, void* site)
{
    auto p = static_cast<size_t*>(std::malloc(n + 16));
    p[0] = n;
    p[1] = (mem_ignore || in_track) ? 0 : 1;
    if (p[1] && n >= 4096 && std::getenv("BS32X_MEMLOG")) {
        std::fprintf(stderr, "NEW %zu from %p live-before %zu\n", n, site, mem_now);
    }
    if (p[1] && live && !in_track && std::getenv("BS32X_MEMDUMP_AT") &&
        n == static_cast<size_t>(std::atoi(std::getenv("BS32X_MEMDUMP_AT")))) {
        std::fprintf(stderr, "MEMDUMP before %zu: live %zu\n", n, mem_now);
        for (auto& kv : *live) {
            if (kv.second.size >= 256) {
                std::fprintf(stderr, "  %zu from %p\n", kv.second.size, kv.second.site);
            }
        }
    }
    if (p[1]) {
        mem_now += n;
        if (std::getenv("BS32X_MEMPEAK")) {
            in_track = true;
            if (!live) {
                live = new std::map<void*, LiveBlock>();
            }
            (*live)[p + 2] = {n, site};
            if (mem_now > mem_peak) {
                peak_snapshot.clear();
                for (auto& kv : *live) {
                    peak_snapshot.push_back({kv.second.size, kv.second.site});
                }
                extern std::vector<void*> grsegs;
                peak_grsegs.clear();
                for (size_t g = 0; g < grsegs.size(); ++g) {
                    if (grsegs[g]) {
                        auto it = live->find(grsegs[g]);
                        peak_grsegs.push_back(static_cast<int>(g));
                        peak_grsegs.push_back(it != live->end() ? static_cast<int>(it->second.size) : -1);
                    }
                }
                peak_tick = TimeCount;
            }
            in_track = false;
        }
        if (mem_now > mem_peak) {
            mem_peak = mem_now;
        }
    }
    return p + 2;
}
void* operator new(size_t n)
{
    return track_alloc(n, __builtin_return_address(0));
}
void* operator new[](size_t n)
{
    return track_alloc(n, __builtin_return_address(0));
}
void operator delete(void* q) noexcept
{
    if (q) {
        auto p = static_cast<size_t*>(q) - 2;
        if (p[1]) {
            mem_now -= p[0];
            if (live && !in_track) {
                in_track = true;
                live->erase(q);
                in_track = false;
            }
        }
        std::free(p);
    }
}
void operator delete[](void* p) noexcept
{
    operator delete(p);
}
void operator delete(void* p, size_t) noexcept
{
    operator delete(p);
}
void operator delete[](void* p, size_t) noexcept
{
    operator delete(p);
}
void bs32x_mem_report(void)
{
    extern int destoff;
    std::fprintf(stderr, "MEM heap live %zu peak %zu, latch bytes %d\n",
        mem_now, mem_peak, 4 * (destoff - 80 * 208 * 3));
    std::fprintf(stderr, "PEAKTICK %u grsegs:", peak_tick);
    for (size_t g = 0; g + 1 < peak_grsegs.size(); g += 2) {
        std::fprintf(stderr, " %d:%d", peak_grsegs[g], peak_grsegs[g + 1]);
    }
    std::fprintf(stderr, "\n");
    std::map<void*, std::pair<size_t, int>> by_site;
    for (auto& b : peak_snapshot) {
        by_site[b.second].first += b.first;
        by_site[b.second].second += 1;
    }
    for (auto& kv : by_site) {
        if (kv.second.first >= 1024) {
            std::fprintf(stderr, "PEAK %zu bytes in %d blocks from %p\n", kv.second.first, kv.second.second, kv.first);
        }
    }
}


// ---- PC build tool: expanded size of every graphics chunk ----
void bs32x_dump_grsizes(void)
{
    const char* path = std::getenv("BS32X_GRSIZES");
    if (!path) {
        return;
    }
    extern std::vector<void*> grsegs;
    extern int ca_gr_last_expanded_size;
    FILE* f = std::fopen(path, "w");
    for (int c = 0; c < NUMCHUNKS; ++c) {
        if (grsegs[c]) {
            std::fprintf(f, "%d cached\n", c);
            continue;
        }
        ca_gr_last_expanded_size = 0;
        CA_CacheGrChunk(static_cast<int16_t>(c));
        std::fprintf(f, "%d %d\n", c, grsegs[c] ? ca_gr_last_expanded_size : -1);
        if (grsegs[c]) {
            UNCACHEGRCHUNK(c);
        }
    }
    std::fclose(f);
    std::fflush(stderr);
    std::_Exit(0);
}


// ---- PC build tool: pre-expanded graphics chunks for the cart (GREXP.BS6) ----
// fonts, pictures, tiles and the extern chunks (texts, palettes, demos), except
// chunks of 60000 bytes or more (full screens).
// Format (little-endian): "GRX1", u32 count, u32 offset[count] (0 = not stored), data.
int32_t GRFILEPOS(int16_t c);

void bs32x_dump_grexp(void)
{
    const char* path = std::getenv("BS32X_DUMP_GREXP");
    if (!path) {
        return;
    }
    extern std::vector<void*> grsegs;
    extern int ca_gr_last_expanded_size;
    std::vector<uint8_t> out(8 + 4 * NUMCHUNKS, 0);
    auto put32 = [&out](size_t at, uint32_t v) {
        out[at] = v & 255; out[at + 1] = (v >> 8) & 255; out[at + 2] = (v >> 16) & 255; out[at + 3] = v >> 24;
    };
    out[0] = 'G'; out[1] = 'R'; out[2] = 'X'; out[3] = '1';
    put32(4, static_cast<uint32_t>(NUMCHUNKS));
    int stored = 0;
    for (int c = STARTFONT; c < NUMCHUNKS; ++c) {
        if (grsegs[c] || GRFILEPOS(c) < 0) {
            continue;
        }
        ca_gr_last_expanded_size = 0;
        CA_CacheGrChunk(static_cast<int16_t>(c));
        if (!grsegs[c]) {
            continue;
        }
        int size = ca_gr_last_expanded_size;
        if (size > 0 && size < 60000) {
            while (out.size() & 3) {
                out.push_back(0);
            }
            put32(8 + 4 * c, static_cast<uint32_t>(out.size()));
            auto p = static_cast<const uint8_t*>(grsegs[c]);
            size_t at = out.size();
            out.insert(out.end(), p, p + size);
            out.insert(out.end(), 4, 0);
            if (c >= STARTEXTERNS) {
                // texts are used from ROM: do now what the text presenter
                // does to them in memory (control codes in capitals, a 0
                // after the "^XX" end mark)
                for (size_t i = at; i + 2 < at + size; ++i) {
                    if (out[i] == '^') {
                        out[i + 1] = static_cast<uint8_t>(std::toupper(out[i + 1]));
                        out[i + 2] = static_cast<uint8_t>(std::toupper(out[i + 2]));
                    }
                }
                // (after the last one: a chunk may hold several messages)
                for (size_t i = at + size; i-- > at + 2; ) {
                    if (out[i - 2] == '^' && out[i - 1] == 'X' && out[i] == 'X') {
                        out[i + 1] = 0;
                        break;
                    }
                }
            }
            ++stored;
        }
        UNCACHEGRCHUNK(c);
    }
    FILE* f = std::fopen(path, "wb");
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    std::fprintf(stderr, "GREXP %d chunks, %zu bytes -> %s\n", stored, out.size(), path);
    std::fflush(stderr);
    std::_Exit(0);
}
