/* BlakeStone32X: 32X entry point for the full game.
   Primary SH2: hardware setup, C++ constructors, then the BStone game.
   Secondary SH2: idle for now (it renders in the speed test; later the game). */
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "marshw.h"

extern int bs32x_game_main(int argc, char *argv[]);
extern void bs32x_text(int x, int y, const char *s);

/* C++ constructors (.ctors list made by the linker script) */
extern void (*__CTOR_LIST__[])(void);

static void run_constructors(void)
{
    long n = (long)__CTOR_LIST__[0];
    for (long i = n; i >= 1; --i) {
        __CTOR_LIST__[i]();
    }
}

/* heap for malloc: from the end of bss up to 12 KB below the primary stack */
extern char end;
static char *heap_top = &end;
#define HEAP_LIMIT ((char *)0x0603C400)

void *_sbrk(int incr)
{
    char *prev = heap_top;
    if (heap_top + incr > HEAP_LIMIT) {
        errno = ENOMEM;
        return (void *)-1;
    }
    heap_top += incr;
    return prev;
}

unsigned bs32x_heap_used(void)
{
    return (unsigned)(heap_top - &end);
}

int main(void)
{
    static char arg0[] = "bstone";
    static char *argv[] = { arg0, 0 };

    Mars_Init();
    Mars_InitVideo(224);
    SH2_WDT_WTCSR_TCNT = 0x5A00;   /* WDT count = 0 */
    SH2_WDT_WTCSR_TCNT = 0xA53E;   /* WDT on, clock / 4096 (as D32XR) */

    {
        extern void bs32x_vblank(void);
        mars_vblank_hook = bs32x_vblank;
    }
    run_constructors();
    bs32x_game_main(1, argv);
    for (;;) {
    }
    return 0;
}

void secondary(void)
{
    for (;;) {
    }
}
