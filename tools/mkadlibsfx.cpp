// BlakeStone32X: AdLib sound effects for the cart.
// The 32X has no AdLib chip, so every AdLib-only sound effect (one without a
// digitized version) is played once here through BStone's own AdLib decoder
// and the DOSBox OPL emulator, and stored as 4-bit IMA ADPCM at 7000 Hz (the rate of the game's digitized sounds)
// in ADLIBSFX.BS6.
// Format (little-endian): "ASX1", u32 count, u32 offset[count] (0 = none),
// then records as in VSWAP.BS6 sound pages: u16 samples, s16 first sample,
// u8 step index, u8 0, nibbles (low first).
// usage: mkadlibsfx GAMEDIR EXT DIGIMAP_SOURCE OUTFILE
//   DIGIMAP_SOURCE: bstone/jm_free.cpp (its wolfdigimap lists the digitized ones)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "bstone_adlib_sfx_decoder.h"

static std::vector<uint8_t> load(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", path.c_str());
        std::exit(1);
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static std::string text(const std::string& path)
{
    std::ifstream f(path);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

static const int steps[89] = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,
    118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,
    1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,
    5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,
    27086,29794,32767};
static const int idx_adj[16] = {-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};

static std::vector<uint8_t> adpcm(const std::vector<int>& pcm)
{
    std::vector<uint8_t> out;
    int n = static_cast<int>(pcm.size());
    int pred = n ? pcm[0] : 0;
    int idx = 0;
    out.push_back(n & 255);
    out.push_back((n >> 8) & 255);
    out.push_back(pred & 255);
    out.push_back((pred >> 8) & 255);
    out.push_back(0);
    out.push_back(0);
    std::vector<int> nib;
    for (int i = 1; i < n; ++i) {
        int step = steps[idx];
        int diff = pcm[i] - pred;
        int code = 0;
        if (diff < 0) {
            code = 8;
            diff = -diff;
        }
        int delta = step >> 3;
        if (diff >= step) { code |= 4; diff -= step; delta += step; }
        if (diff >= (step >> 1)) { code |= 2; diff -= step >> 1; delta += step >> 1; }
        if (diff >= (step >> 2)) { code |= 1; delta += step >> 2; }
        pred = (code & 8) ? pred - delta : pred + delta;
        if (pred < -32768) pred = -32768;
        if (pred > 32767) pred = 32767;
        idx += idx_adj[code];
        if (idx < 0) idx = 0;
        if (idx > 88) idx = 88;
        nib.push_back(code);
    }
    if (nib.size() & 1) {
        nib.push_back(0);
    }
    for (size_t i = 0; i < nib.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(nib[i] | (nib[i + 1] << 4)));
    }
    return out;
}

int main(int argc, char** argv)
{
    if (argc < 5) {
        std::fprintf(stderr, "usage: mkadlibsfx GAMEDIR EXT DIGIMAP_SOURCE OUTFILE\n");
        return 1;
    }
    std::string dir = argv[1], ext = argv[2];
    auto hed = load(dir + "/AUDIOHED." + ext);
    auto aud = load(dir + "/AUDIOT." + ext);
    auto off = [&hed](int i) {
        return static_cast<uint32_t>(hed[4 * i] | (hed[4 * i + 1] << 8) | (hed[4 * i + 2] << 16) | (hed[4 * i + 3] << 24));
    };

    // sound names in order (audio.h enum) and the digitized ones (jm_free.cpp)
    std::string src_dir = std::string(argv[3]);
    std::string audio_h = text(src_dir.substr(0, src_dir.rfind('/') + 1) + "audio.h");
    std::map<std::string, int> sound_num;
    {
        std::regex name_re("const int16_t ([A-Z0-9_]+SND) = ([0-9]+);");
        for (std::sregex_iterator it(audio_h.begin(), audio_h.end(), name_re), end; it != end; ++it) {
            sound_num[(*it)[1]] = std::atoi(std::string((*it)[2]).c_str());
        }
    }
    std::set<int> digitized;
    {
        std::string js = text(argv[3]);
        size_t a = js.find("wolfdigimap[]");
        size_t b = js.find("LASTSOUND", a);
        std::string body = js.substr(a, b - a);
        std::regex pair_re("([A-Z0-9_]+SND)\\s*,\\s*[0-9]+");
        for (std::sregex_iterator it(body.begin(), body.end(), pair_re), end; it != end; ++it) {
            auto f = sound_num.find((*it)[1]);
            if (f != sound_num.end()) {
                digitized.insert(f->second);
            }
        }
    }
    std::fprintf(stderr, "%zu sound names, %zu digitized\n", sound_num.size(), digitized.size());

    const int count = 100;            // NUMSOUNDS
    const int first = 100;            // STARTADLIBSOUNDS
    const int rate = 7000;           // the game's digitized sound rate
    std::vector<uint8_t> out(8 + 4 * count, 0);
    out[0] = 'A'; out[1] = 'S'; out[2] = 'X'; out[3] = '1';
    out[4] = count;
    int stored = 0;
    for (int s = 0; s < count; ++s) {
        if (digitized.count(s)) {
            continue;
        }
        uint32_t a = off(first + s), b = off(first + s + 1);
        if (b <= a + 23) {
            continue;
        }
        bstone::AdlibSfxDecoder dec;
        if (!dec.initialize(&aud[a], static_cast<int>(b - a), rate)) {
            continue;
        }
        std::vector<int> pcm;
        int16_t buf[1024];
        int got;
        while ((got = dec.decode(1024, buf)) > 0 && pcm.size() < 60000) {
            pcm.insert(pcm.end(), buf, buf + got);
        }
        // trim the silent tail
        while (!pcm.empty() && std::abs(pcm.back()) < 64) {
            pcm.pop_back();
        }
        if (pcm.empty()) {
            continue;
        }
        auto rec = adpcm(pcm);
        while (out.size() & 3) {
            out.push_back(0);
        }
        uint32_t at = static_cast<uint32_t>(out.size());
        out[8 + 4 * s] = at & 255;
        out[8 + 4 * s + 1] = (at >> 8) & 255;
        out[8 + 4 * s + 2] = (at >> 16) & 255;
        out[8 + 4 * s + 3] = at >> 24;
        out.insert(out.end(), rec.begin(), rec.end());
        ++stored;
    }
    std::ofstream f(argv[4], std::ios::binary);
    f.write(reinterpret_cast<const char*>(out.data()), out.size());
    std::printf("ADLIBSFX %d sounds, %zu bytes\n", stored, out.size());
    return 0;
}
