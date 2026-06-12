#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define assert(cond) do { if (cond) {} else {char msg[] = __FILE__ ":" STRINGIFY(__LINE__) ":1: error: assertion failure\n"; WriteFile((HANDLE)STD_OUTPUT_HANDLE, msg, sizeof(msg) - 1, 0, 0); __debugbreak();} } while (0)
#include "bench.c"

void outputProc(Str str) {
    if (str.len > 0) {
        assert(str.ptr[str.len] == '\0');
        OutputDebugStringA(str.ptr);
    }
}

int main() {
    Arena arena_ = {.size = Gigabyte};
    arena_.base = VirtualAlloc(0, arena_.size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    bench(&arena_, outputProc);
}