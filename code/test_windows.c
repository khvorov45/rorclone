#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define assert(cond) do { if (cond) {} else {char msg[] = __FILE__ ":" STRINGIFY(__LINE__) ": test failed\n"; WriteFile((HANDLE)STD_OUTPUT_HANDLE, msg, sizeof(msg) - 1, 0, 0); __debugbreak();} } while (0)
#include "test.c"

void writeToStdout(Str str) {
    if (str.len > 0) {
        assert(str.ptr[str.len] == '\0');
        WriteFile((HANDLE)STD_OUTPUT_HANDLE, str.ptr, str.len, 0, 0);
    }
}

int main() {
    writeToStdout(STR("Running tests\n"));
    test(writeToStdout);
    return 0;
}
