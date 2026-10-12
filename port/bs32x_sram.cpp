/* BlakeStone32X: what the cart's battery save RAM keeps (32 KB).
   0x0000  "BLAKESTONE32X 1"
   0x0010  config (sound, controls, switches)
   0x0800  high scores
   0x0C00  32X settings (starting lives)
   0x0C40  the saved mission
   Every region starts with 8 bytes: 'B','5', u16 size, u32 checksum
   (big-endian), then the data. A region with a bad checksum is empty. */
#include <cstdint>
#include <cstring>
#include "bs32x.h"

namespace {

struct Region {
    int offset;
    int cap;    /* with the 8 byte header */
};

const Region regions[] = {
    { 0x0010, 0x07F0 },
    { 0x0800, 0x0400 },
    { 0x0C00, 0x0040 },
    { 0x0C40, 0x8000 - 0x0C40 },
};

const char magic[16] = "BLAKESTONE32X 1";
bool checked = false;

uint32_t sum(uint32_t s, const uint8_t* p, int n)
{
    for (int i = 0; i < n; ++i) {
        s = s * 31u + p[i] + 1u;
    }
    return s;
}

void check_header()
{
    if (checked) {
        return;
    }
    checked = true;
    char head[16];
    bs32x_sram_raw_read(0, head, 16);
    if (std::memcmp(head, magic, 16) != 0) {
        /* new or foreign save RAM: mark every region empty */
        static const uint8_t zero[8] = {};
        for (const auto& r : regions) {
            bs32x_sram_raw_write(r.offset, zero, 8);
        }
        bs32x_sram_raw_write(0, magic, 16);
    }
}

bool header(int region, int* size, uint32_t* check)
{
    check_header();
    if (region < 0 || region >= static_cast<int>(sizeof(regions) / sizeof(regions[0]))) {
        return false;
    }
    uint8_t h[8];
    bs32x_sram_raw_read(regions[region].offset, h, 8);
    int n = (h[2] << 8) | h[3];
    if (h[0] != 'B' || h[1] != '5' || n <= 0 || n > regions[region].cap - 8) {
        return false;
    }
    *size = n;
    *check = (uint32_t(h[4]) << 24) | (uint32_t(h[5]) << 16) | (uint32_t(h[6]) << 8) | h[7];
    return true;
}

} // namespace

int bs32x_sram_capacity(int region)
{
    return regions[region].cap - 8;
}

int bs32x_sram_read(int region, void* buf, int cap)
{
    int size = 0;
    uint32_t check = 0;
    if (!header(region, &size, &check) || size > cap) {
        return -1;
    }
    uint8_t* p = static_cast<uint8_t*>(buf);
    bs32x_sram_raw_read(regions[region].offset + 8, p, size);
    if (sum(0, p, size) != check) {
        return -1;
    }
    return size;
}

int bs32x_sram_peek(int region, void* buf, int len)
{
    int size = 0;
    uint32_t check = 0;
    if (!header(region, &size, &check)) {
        return -1;
    }
    bs32x_sram_raw_read(regions[region].offset + 8, buf, len < size ? len : size);
    return size;
}

bool bs32x_sram_write(int region, const void* data, int size)
{
    check_header();
    if (size <= 0 || size > bs32x_sram_capacity(region)) {
        return false;
    }
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t check = sum(0, p, size);
    const Region& r = regions[region];
    /* empty first, so a write cut short (power off) reads as empty */
    static const uint8_t zero[2] = {};
    bs32x_sram_raw_write(r.offset, zero, 2);
    bs32x_sram_raw_write(r.offset + 8, p, size);
    uint8_t h[8] = {
        'B', '5',
        static_cast<uint8_t>(size >> 8), static_cast<uint8_t>(size),
        static_cast<uint8_t>(check >> 24), static_cast<uint8_t>(check >> 16),
        static_cast<uint8_t>(check >> 8), static_cast<uint8_t>(check),
    };
    bs32x_sram_raw_write(r.offset + 2, h + 2, 6);
    bs32x_sram_raw_write(r.offset, h, 2);
    return true;
}

void bs32x_sram_erase(int region)
{
    check_header();
    static const uint8_t zero[2] = {};
    bs32x_sram_raw_write(regions[region].offset, zero, 2);
}
