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
#include "id_heads.h"

namespace {

std::vector<uint8_t> vga(256 * 1024);
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
    int music;
    int period;
    int key;
    char ascii;
};
std::vector<UntilTap> untils;

std::vector<ScriptEvent> script;
size_t script_pos = 0;

std::map<std::string, std::vector<uint8_t>> files;

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
            int music = 0, period = 0;
            if (std::sscanf(line + 1, "%d %d %31s", &music, &period, name) == 3) {
                for (auto& k : key_names) {
                    if (std::strcmp(k.name, name) == 0) {
                        untils.push_back({music, period, static_cast<int>(k.code), k.ascii});
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
    const uint8_t* src = &vga[4 * displayofs];
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

void bs32x_vid_set_palette(const unsigned char* pal)
{
    std::memcpy(palette, pal, 768);
}

void bs32x_vid_present(int displayofs)
{
    init();
    if (shots_all || shots.count(frame_no)) {
        write_frame(displayofs);
    }
    ++frame_no;
    TimeCount += 2;
    if (frame_no >= max_frames) {
        std::fprintf(stderr, "BS32X host: %d frames done\n", frame_no);
        std::exit(0);
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

bool bs32x_file_open_read(const char* name, const void** data, long* size)
{
    std::string key = upper(name);
    auto it = files.find(key);
    if (it == files.end()) {
        const char* dir = std::getenv("BS32X_DATA");
        std::string path = std::string(dir ? dir : ".") + "/" + key;
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) {
            return false;
        }
        std::vector<uint8_t> buf;
        uint8_t tmp[65536];
        size_t n;
        while ((n = std::fread(tmp, 1, sizeof(tmp), f)) > 0) {
            buf.insert(buf.end(), tmp, tmp + n);
        }
        std::fclose(f);
        it = files.emplace(key, std::move(buf)).first;
    }
    *data = it->second.data();
    *size = static_cast<long>(it->second.size());
    return true;
}

void bs32x_file_save(const char* name, const void* data, long size)
{
    std::string key = upper(name);
    auto p = static_cast<const uint8_t*>(data);
    files[key].assign(p, p + size);
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
