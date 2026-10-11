/* BlakeStone32X: PWM sound effects (bs32x_sound.c) */
#ifndef BS32X_SOUND_H
#define BS32X_SOUND_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define BS32X_SND_VOICES 4

enum { BS32X_SND_START = 1, BS32X_SND_STOP, BS32X_SND_STOP_ALL, BS32X_SND_VOLUME };

typedef struct {
    int kind;
    int slot;               /* voice */
    const uint8_t* data;    /* first ADPCM record */
    int page;               /* its VSWAP sound page (-1: not a VSWAP sound) */
    int pages;              /* records in the sound */
    int vol;                /* 0..64 */
} Bs32xSndCmd;

void bs32x_sound_start(void);                       /* second CPU: start PWM and DMA */
void bs32x_sound_command(const Bs32xSndCmd* cmd);   /* first CPU */
const uint8_t* bs32x_sound_page(int page);          /* platform: VSWAP sound page record */

#ifdef __cplusplus
}
#endif
#endif
