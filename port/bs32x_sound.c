/* BlakeStone32X: sound effects on the 32X PWM.
   The second SH2 mixes up to 4 voices of 4-bit IMA ADPCM at 7000 Hz (the
   game's digitized sound rate) into a double buffer that its DMA channel 1
   feeds to the PWM; each finished half raises the DMA interrupt, which mixes
   the next one. The first SH2 (the game) sends commands through a small ring
   that both CPUs read and write through the cache-through addresses. */
#include <stdint.h>
#include "32x.h"
#include "marshw.h"
#include "bs32x_sound.h"

#define RATE        7000                   /* sound rate */
#define OUT_RATE    14000                  /* PWM rate: every sound sample twice, the */
                                           /* first one halfway to the next (smoother) */
#define HALF        128                    /* sound samples per DMA block (~55 per second) */
#define VOICES      BS32X_SND_VOICES
#define RING        16

static const int16_t steps[89] = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,
    73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,
    724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,
    4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,
    22385,24623,27086,29794,32767};
static const int8_t idx_adj[16] = {-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};

typedef struct {
    const uint8_t* nib;     /* next nibble byte */
    int left;               /* samples left in this record */
    int next_page;          /* next VSWAP sound page, -1 none */
    int pages_left;
    int pred;
    int idx;
    int phase;              /* 0: low nibble next */
    int vol;                /* 0..64 */
    int active;
} Voice;

static Voice voices[VOICES];
static int32_t snd_buf[2][2 * HALF] __attribute__((aligned(16)));
static int last_out;                        /* last sample of the previous block */
static int buf_idx;
static int center;
static int volume = 64;                     /* sound effects volume 0..64 */

/* commands from the first CPU (accessed through cache-through addresses) */
static Bs32xSndCmd ring[RING] __attribute__((aligned(16)));
static volatile int ring_head;              /* written by the first CPU */
static volatile int ring_tail;              /* written by the second CPU */

#define UNCACHED(p) ((__typeof__(p))((uintptr_t)(p) | 0x20000000))

static void start_record(Voice* v, const uint8_t* rec)
{
    v->left = rec[0] | (rec[1] << 8);
    v->pred = (int16_t)(rec[2] | (rec[3] << 8));
    v->idx = rec[4];
    v->nib = rec + 6;
    v->phase = 0;
}

static void run_commands(void)
{
    volatile int* head = UNCACHED(&ring_head);
    volatile int* tail = UNCACHED(&ring_tail);
    Bs32xSndCmd* r = UNCACHED(&ring[0]);
    int t = *tail;
    while (t != *head) {
        const Bs32xSndCmd* c = &r[t];
        if (c->kind == BS32X_SND_STOP_ALL) {
            int i;
            for (i = 0; i < VOICES; ++i) {
                voices[i].active = 0;
            }
        } else if (c->kind == BS32X_SND_VOLUME) {
            volume = c->vol;
        } else if (c->slot >= 0 && c->slot < VOICES) {
            Voice* v = &voices[c->slot];
            if (c->kind == BS32X_SND_STOP) {
                v->active = 0;
            } else {
                start_record(v, c->data);
                v->next_page = c->page >= 0 ? c->page + 1 : -1;
                v->pages_left = c->pages - 1;
                v->vol = c->vol;
                v->active = v->left > 0;
            }
        }
        t = (t + 1) & (RING - 1);
    }
    *tail = t;
}

/* one sample of a voice (the first sample of a record is its header value) */
static inline int voice_sample(Voice* v)
{
    int s = v->pred;
    if (--v->left <= 0) {
        if (v->pages_left > 0 && v->next_page >= 0) {
            const uint8_t* rec = bs32x_sound_page(v->next_page);
            ++v->next_page;
            --v->pages_left;
            if (rec) {
                start_record(v, rec);
                return s;
            }
        }
        v->active = 0;
        return s;
    }
    {
        int b = *v->nib;
        int code;
        int step = steps[v->idx];
        int delta = step >> 3;
        if (v->phase) {
            code = b >> 4;
            ++v->nib;
        } else {
            code = b & 15;
        }
        v->phase ^= 1;
        if (code & 4) delta += step;
        if (code & 2) delta += step >> 1;
        if (code & 1) delta += step >> 2;
        if (code & 8) {
            v->pred -= delta;
            if (v->pred < -32768) v->pred = -32768;
        } else {
            v->pred += delta;
            if (v->pred > 32767) v->pred = 32767;
        }
        v->idx += idx_adj[code];
        if (v->idx < 0) v->idx = 0;
        if (v->idx > 88) v->idx = 88;
    }
    return s;
}

static void mix(int32_t* out)
{
    static int acc[HALF];       /* (static: the second CPU's stack is small) */
    int i, k;
    for (i = 0; i < HALF; ++i) {
        acc[i] = 0;
    }
    for (k = 0; k < VOICES; ++k) {
        Voice* v = &voices[k];
        int vol;
        if (!v->active) {
            continue;
        }
        vol = (v->vol * volume) >> 6;       /* 0..64 */
        for (i = 0; i < HALF && v->active; ++i) {
            acc[i] += voice_sample(v) * vol;
        }
    }
    for (i = 0; i < HALF; ++i) {
        /* 16-bit * 64 >> 12 = +-512 per full voice (PWM centre ~820) */
        int s = (acc[i] >> 12) + center;
        int m;
        if (s < 1) {
            s = 1;
        } else if (s > 2 * center - 1) {
            s = 2 * center - 1;
        }
        m = (last_out + s) >> 1;
        out[2 * i] = (m << 16) | m;
        out[2 * i + 1] = (s << 16) | s;
        last_out = s;
    }
}

static void dma_done(void)
{
    /* play the block mixed last time, then mix the other one */
    SH2_DMA_SAR1 = (uintptr_t)snd_buf[buf_idx];
    SH2_DMA_TCR1 = 2 * HALF;
    SH2_DMA_CHCR1 = 0x18E5;     /* dest fixed, src incr, long, DREQ, interrupt, enable */
    buf_idx ^= 1;
    run_commands();
    mix(snd_buf[buf_idx]);
}

/* on the second CPU */
void bs32x_sound_start(void)
{
    int cycle;
    SH2_DMA_SAR1 = 0;
    SH2_DMA_DAR1 = (uint32_t)&MARS_PWM_STEREO;  /* a long sets left and right */
    SH2_DMA_TCR1 = 0;
    SH2_DMA_CHCR1 = 0;
    SH2_DMA_DRCR1 = 0;
    SH2_DMA_DMAOR = 1;
    SH2_DMA_VCR1 = 66;                                  /* DMA interrupt vector */
    SH2_INT_IPRA = (SH2_INT_IPRA & 0xF0FF) | 0x0400;    /* DMA interrupt priority 4 */

    if (MARS_VDP_DISPMODE & MARS_NTSC_FORMAT) {
        cycle = (23011361 + OUT_RATE / 2) / OUT_RATE;
    } else {
        cycle = (22801467 + OUT_RATE / 2) / OUT_RATE;
    }
    center = cycle / 2;
    last_out = center;
    Mars_InitPWM(OUT_RATE, 1, cycle);
    Mars_SetSecDMA1Callback(&dma_done);

    buf_idx = 0;
    mix(snd_buf[0]);
    dma_done();
}

/* on the first CPU: queue a command (dropped if the ring is full) */
void bs32x_sound_command(const Bs32xSndCmd* cmd)
{
    volatile int* head = UNCACHED(&ring_head);
    volatile int* tail = UNCACHED(&ring_tail);
    int h = *head;
    int n = (h + 1) & (RING - 1);
    if (n == *tail) {
        return;
    }
    UNCACHED(&ring[0])[h] = *cmd;
    *head = n;
}
