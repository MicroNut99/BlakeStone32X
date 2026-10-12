/* BlakeStone32X: packed states of the floors you have left (bs32x_levels.cpp) */
#ifndef BS32X_LEVELS_H
#define BS32X_LEVELS_H
#include <cstdint>

const int bs32x_scratch_size = 66560;   /* the VGA page */
const int bs32x_level_max = 40000;      /* largest unpacked level state */

void bs32x_levels_clear(void);
bool bs32x_level_has(int key);
void bs32x_level_remove(int key);
bool bs32x_level_put(int key, const uint8_t* data, int size, uint8_t* work, int work_size);
int bs32x_level_get(int key, uint8_t* dst, int cap);
int bs32x_levels_used(void);
int bs32x_level_keys(int* keys, int max);
int bs32x_level_export(int key, uint8_t* dst, int cap, int* rsize);
bool bs32x_level_import(int key, const uint8_t* data, int csize, int rsize);

/* platform */
uint8_t* bs32x_scratch_begin(void);     /* the page RAM as work space (screen kept elsewhere) */
void bs32x_scratch_end(void);           /* the page back as it was */
uint8_t* bs32x_bank_access(int bank, int* size);    /* store of a frame buffer, made accessible */
void bs32x_bank_size(int bank, int* size);

#endif
