/* BlakeStone32X: minimal stand-in for SDL2 types used by the BStone game code.
   The 32X and PC-test platform layers implement video/input/sound themselves. */
#ifndef BS32X_SDL_SHIM_H
#define BS32X_SDL_SHIM_H
#include <stdint.h>
#include <stddef.h>
typedef uint8_t  Uint8;
typedef int8_t   Sint8;
typedef uint16_t Uint16;
typedef int16_t  Sint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;
typedef uint64_t Uint64;
typedef int64_t  Sint64;
typedef enum { SDL_FALSE = 0, SDL_TRUE = 1 } SDL_bool;
typedef int32_t SDL_Keycode;
typedef uint16_t SDL_Keymod;
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_RWops;
typedef struct { int x, y, w, h; } SDL_Rect;
#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define SDL_BYTEORDER SDL_BIG_ENDIAN
#else
#define SDL_BYTEORDER SDL_LIL_ENDIAN
#endif
static inline Uint16 SDL_Swap16(Uint16 x) { return (Uint16)((x << 8) | (x >> 8)); }
static inline Uint32 SDL_Swap32(Uint32 x) { return (x << 24) | ((x << 8) & 0xFF0000u) | ((x >> 8) & 0xFF00u) | (x >> 24); }
static inline Uint64 SDL_Swap64(Uint64 x) { return ((Uint64)SDL_Swap32((Uint32)x) << 32) | SDL_Swap32((Uint32)(x >> 32)); }
typedef Uint32 SDL_AudioDeviceID;
static inline int SDL_Init(Uint32) { return 0; }
static inline const char *SDL_GetError(void) { return ""; }
static inline char *SDL_GetPrefPath(const char *, const char *) { return 0; }
static inline char *SDL_GetBasePath(void) { return 0; }
static inline void SDL_free(void *) {}
#define SDL_PRESSED 1
#define SDL_RELEASED 0
#endif
