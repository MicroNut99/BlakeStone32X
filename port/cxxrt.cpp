/* BlakeStone32X: the bits of the C++ runtime the game needs (no libstdc++ on the 32X).
   Allocation goes to newlib malloc; library "throw" helpers stop with a message. */
#include <cstdlib>
#include <cstddef>
#include <new>
#include <malloc.h>

void bs32x_fatal(const char* what);
extern "C" unsigned bs32x_heap_used(void);

static void out_of_memory(std::size_t n)
{
    // "OUT OF MEMORY <request> HEAP <used>"
    static char msg[80] = "OUT OF MEMORY ";
    char* p = msg + 14;
    auto put = [&p](unsigned v) {
        char t[12]; int k = 0;
        do { t[k++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
        while (k) *p++ = t[--k];
    };
    put(static_cast<unsigned>(n));
    for (const char* s = " HEAP "; *s; ) *p++ = *s++;
    put(bs32x_heap_used());
    struct mallinfo mi = mallinfo();      // in use / free inside the heap
    for (const char* s = " USE "; *s; ) *p++ = *s++;
    put(static_cast<unsigned>(mi.uordblks));
    for (const char* s = " FREE "; *s; ) *p++ = *s++;
    put(static_cast<unsigned>(mi.fordblks));
    *p = '\0';
    bs32x_fatal(msg);
}

void* operator new(std::size_t n)
{
    void* p = std::malloc(n ? n : 1);
    if (!p) {
        out_of_memory(n);
    }
    return p;
}

void* operator new[](std::size_t n)
{
    return operator new(n);
}

void operator delete(void* p) noexcept
{
    std::free(p);
}

void operator delete[](void* p) noexcept
{
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}

namespace std {
void __throw_length_error(const char*) { bs32x_fatal("length_error"); }
void __throw_logic_error(const char*) { bs32x_fatal("logic_error"); }
void __throw_out_of_range(const char*) { bs32x_fatal("out_of_range"); }
void __throw_out_of_range_fmt(const char*, ...) { bs32x_fatal("out_of_range"); }
void __throw_invalid_argument(const char*) { bs32x_fatal("invalid_argument"); }
void __throw_bad_alloc() { bs32x_fatal("bad_alloc"); }
void __throw_bad_array_new_length() { bs32x_fatal("bad_array_new_length"); }
void __throw_bad_function_call() { bs32x_fatal("bad_function_call"); }
void __glibcxx_assert_fail(const char*, int, const char*, const char* cond) noexcept { bs32x_fatal(cond); }
}

extern "C" {
void* __dso_handle = nullptr;
int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }
void __cxa_pure_virtual() { bs32x_fatal("pure virtual call"); }
}
