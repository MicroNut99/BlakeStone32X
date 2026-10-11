/* BlakeStone32X: where the floors you have left are kept.
   The game keeps the state of every floor you leave (about 37 KB each) so
   it is there when you come back by elevator. The 32X RAM can't hold that,
   so each state is packed (LZSS, the format of tools/mkmusic.py) and kept in
   the unused part of the two 32X frame buffers (about 56 KB each, past the
   224 screen lines). While a floor is packed or unpacked the VGA page RAM is
   used as work space (the platform puts the screen into both frame buffers
   first, so the page can be put back after). */
#include <cstdint>
#include <cstring>
#include "bs32x.h"
#include "bs32x_levels.h"

namespace {

struct Entry {
    int key;            /* level index, -1 free */
    int bank;
    int offset;         /* in the bank's store */
    int csize;          /* packed size (rounded up to 4) */
    int rsize;          /* unpacked size */
};

const int max_entries = 24;
Entry entries[max_entries];
int used[2];
bool initialized = false;

void init()
{
    if (initialized) {
        return;
    }
    initialized = true;
    for (auto& e : entries) {
        e.key = -1;
    }
    used[0] = used[1] = 0;
}

Entry* find(int key)
{
    for (auto& e : entries) {
        if (e.key == key) {
            return &e;
        }
    }
    return nullptr;
}

/* frame buffer memory takes 16- and 32-bit writes only (a byte write of 0 is
   lost on some emulators), so all copies are in 32-bit words */
void copy32(void* dst, const void* src, int bytes)
{
    uint32_t* d = static_cast<uint32_t*>(dst);
    const uint32_t* s = static_cast<const uint32_t*>(src);
    for (int n = bytes >> 2; n > 0; --n) {
        *d++ = *s++;
    }
}

void remove_entry(Entry* e)
{
    int size = 0;
    uint8_t* store = bs32x_bank_access(e->bank, &size);
    int end = e->offset + e->csize;
    /* close the gap: move the later data down */
    copy32(store + e->offset, store + end, used[e->bank] - end);
    for (auto& o : entries) {
        if (o.key >= 0 && o.bank == e->bank && o.offset > e->offset) {
            o.offset -= e->csize;
        }
    }
    used[e->bank] -= e->csize;
    e->key = -1;
}

/* ---------- LZSS ---------- */

const int hash_sets = 2048;     /* 2 ways each: 8 KB of work space */

inline int hash3(const uint8_t* p)
{
    uint32_t h = (static_cast<uint32_t>(p[0]) << 16) | (p[1] << 8) | p[2];
    h *= 2654435761u;
    return static_cast<int>(h >> 21);   /* 11 bits */
}

/* returns the packed size, or -1 if it did not fit in out_cap */
int pack(const uint8_t* in, int n, uint8_t* out, int out_cap, uint16_t* table)
{
    for (int i = 0; i < hash_sets * 2; ++i) {
        table[i] = 0xFFFF;
    }
    int o = 0;
    int flags_at = -1;
    int bit = 8;
    auto flag = [&](int v) -> bool {
        if (bit == 8) {
            if (o >= out_cap) {
                return false;
            }
            flags_at = o;
            out[o++] = 0;
            bit = 0;
        }
        if (v) {
            out[flags_at] |= static_cast<uint8_t>(1 << bit);
        }
        ++bit;
        return true;
    };
    auto insert = [&](int pos) {
        if (pos + 3 <= n && pos < 0xFFFF) {
            uint16_t* set = &table[hash3(in + pos) * 2];
            set[1] = set[0];
            set[0] = static_cast<uint16_t>(pos);
        }
    };
    int i = 0;
    while (i < n) {
        int best_len = 0;
        int best_dist = 0;
        if (i + 3 <= n && i < 0xFFFF) {
            const uint16_t* set = &table[hash3(in + i) * 2];
            for (int w = 0; w < 2; ++w) {
                int p = set[w];
                if (p == 0xFFFF || i - p > 32767 || p >= i) {
                    continue;
                }
                int mx = n - i < 256 ? n - i : 256;
                int l = 0;
                while (l < mx && in[p + l] == in[i + l]) {
                    ++l;
                }
                if (l > best_len) {
                    best_len = l;
                    best_dist = i - p;
                }
            }
        }
        if (best_len >= 4) {
            if (!flag(1) || o + 3 > out_cap) {
                return -1;
            }
            int d = best_dist - 1;
            out[o++] = static_cast<uint8_t>(d >> 8);
            out[o++] = static_cast<uint8_t>(d);
            out[o++] = static_cast<uint8_t>(best_len - 1);
            for (int k = 0; k < best_len; ++k) {
                insert(i + k);
            }
            i += best_len;
        } else {
            if (!flag(0) || o + 1 > out_cap) {
                return -1;
            }
            insert(i);
            out[o++] = in[i++];
        }
    }
    if (!flag(1) || o + 3 > out_cap) {
        return -1;
    }
    out[o++] = 0;
    out[o++] = 0;
    out[o++] = 0;          /* length 1: the end */
    return o;
}

/* returns the unpacked size, or -1 */
int unpack(const uint8_t* in, uint8_t* out, int out_cap)
{
    int o = 0;
    for (;;) {
        int idb = *in++;
        for (int b = 0; b < 8; ++b, idb >>= 1) {
            if (idb & 1) {
                int pos = (in[0] << 8) | in[1];
                int len = in[2] + 1;
                in += 3;
                if (len == 1) {
                    return o;
                }
                int s = o - pos - 1;
                if (s < 0 || o + len > out_cap) {
                    return -1;
                }
                for (int k = 0; k < len; ++k) {
                    out[o + k] = out[s + k];
                }
                o += len;
            } else {
                if (o >= out_cap) {
                    return -1;
                }
                out[o++] = *in++;
            }
        }
    }
}

} // namespace

void bs32x_levels_clear(void)
{
    init();
    for (auto& e : entries) {
        e.key = -1;
    }
    used[0] = used[1] = 0;
}

bool bs32x_level_has(int key)
{
    init();
    return find(key) != nullptr;
}

void bs32x_level_remove(int key)
{
    init();
    if (Entry* e = find(key)) {
        remove_entry(e);
    }
}

/* data: the level state in the scratch area; work: the rest of it */
bool bs32x_level_put(int key, const uint8_t* data, int size, uint8_t* work, int work_size)
{
    init();
    bs32x_level_remove(key);

    uint16_t* table = reinterpret_cast<uint16_t*>(work);
    uint8_t* out = work + hash_sets * 2 * 2;
    int out_cap = work_size - hash_sets * 2 * 2 - 4;
    int csize = pack(data, size, out, out_cap, table);
    if (csize < 0) {
        bs32x_log("level state did not pack");
        return false;
    }
    while (csize & 3) {
        out[csize++] = 0;
    }

    Entry* e = find(-1);
    if (!e) {
        return false;
    }
    int bank = -1;
    for (int b = 0; b < 2; ++b) {
        int cap = 0;
        bs32x_bank_size(b, &cap);
        if (cap - used[b] >= csize && (bank < 0 || used[b] < used[bank])) {
            bank = b;
        }
    }
    if (bank < 0) {
        bs32x_log("no room for the level state");
        return false;
    }
    int cap = 0;
    uint8_t* store = bs32x_bank_access(bank, &cap);
    copy32(store + used[bank], out, csize);
    e->key = key;
    e->bank = bank;
    e->offset = used[bank];
    e->csize = csize;
    e->rsize = size;
    used[bank] += csize;
    return true;
}

/* unpack into dst; returns its size or -1 */
int bs32x_level_get(int key, uint8_t* dst, int cap)
{
    init();
    Entry* e = find(key);
    if (!e) {
        return -1;
    }
    int size = 0;
    const uint8_t* store = bs32x_bank_access(e->bank, &size);
    int n = unpack(store + e->offset, dst, cap);
    return n == e->rsize ? n : -1;
}

int bs32x_levels_used(void)
{
    init();
    return used[0] + used[1];
}
