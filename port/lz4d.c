/* BlakeStone32X: LZ4 block decoder (format written by tools/lz4c.py). */
#include <stdint.h>
#include <string.h>

int bs32x_lz4_decode(const uint8_t *src, int src_len, uint8_t *dst, int dst_len)
{
    const uint8_t *s = src, *s_end = src + src_len;
    uint8_t *d = dst, *d_end = dst + dst_len;

    while (s < s_end) {
        unsigned tok = *s++;
        unsigned len = tok >> 4;

        if (len == 15) {
            unsigned b;
            do { b = *s++; len += b; } while (b == 255);
        }
        if (d + len > d_end) return -1;
        memcpy(d, s, len);
        d += len;
        s += len;
        if (s >= s_end) break;

        unsigned dist = s[0] | (s[1] << 8);
        s += 2;
        unsigned mlen = tok & 15;
        if (mlen == 15) {
            unsigned b;
            do { b = *s++; mlen += b; } while (b == 255);
        }
        mlen += 4;
        if (dist == 0 || d - dst < (int)dist || d + mlen > d_end) return -1;
        {
            const uint8_t *m = d - dist;
            while (mlen--) *d++ = *m++;
        }
    }
    return (int)(d - dst);
}
