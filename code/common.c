#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wgnu-variable-sized-type-not-at-end"

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>

#define Byte 1
#define Kilobyte 1024 * Byte
#define Megabyte 1024 * Kilobyte
#define Gigabyte 1024 * Megabyte

#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)
#define STR(x) ((Str){x, sizeof(x) - 1})
#define LIT(x) (int)x.len, x.ptr
#define unused(x) ((x) = (x))
#define arrayCount(x) (sizeof(x) / sizeof((x)[0]))
#define arrpush(arr, val) assert((arr).len < (arr).cap); (arr).ptr[(arr).len++] = (val)
#define arrpusharr(arr, val) assert((arr).len + (val).len <= (arr).cap); memcpy((arr).ptr + (arr).len, (val).ptr, (val).len * sizeof(*(val).ptr)); (arr).len += (val).len
#define arenaAllocArray(arena, type, count) ((type*)arenaAlloc((arena), sizeof(type) * (count)))
#define arenaAllocAndZeroArray(arena, type, count) ((type*)arenaAllocAndZero((arena), sizeof(type) * (count)))
#define tempMemoryBlock(arena_) for (TempMemory _temp_ = beginTempMemory(arena_); _temp_.arena; endTempMemory(&_temp_))

#ifndef assert
#define assert(cond) do { if (cond) {} else __debugbreak(); } while (0)
#endif

#define STB_SPRINTF_STATIC
#define STB_SPRINTF_IMPLEMENTATION
#include "stb_sprintf.h"

typedef int8_t   i8;
typedef uint8_t  u8;
typedef int16_t  i16;
typedef uint16_t u16;
typedef int32_t  i32;
typedef uint32_t u32;
typedef int64_t  i64;
typedef uint64_t u64;
typedef float    f32;
typedef double   f64;

typedef struct {u8* ptr; i64 len;} u8arr;

//
// SECTION Memory
//

typedef struct Arena {
    void* base;
    i64 size;
    i64 used;
    i64 tempCount;
} Arena;
static i64 arenaFreesize(Arena* arena) { return arena->size - arena->used;}
static void* arenaFreeptr(Arena* arena) { return arena->base + arena->used;}

static void* arenaAlloc(Arena* arena, i64 size) {
    assert(arenaFreesize(arena) >= size);
    void* result = arenaFreeptr(arena);
    arena->used += size;
    return result;
}

static void* arenaAllocAndZero(Arena* arena, i64 size) {
    void* ptr = arenaAlloc(arena, size);
    memset(ptr, 0, size);
    return ptr;
}

typedef struct TempMemory {
    i64 usedBefore;
    i64 tempBefore;
    Arena* arena;
} TempMemory;

static TempMemory beginTempMemory(Arena* arena) {
    TempMemory result = {.usedBefore = arena->used, .tempBefore = arena->tempCount, .arena = arena};
    arena->tempCount += 1;
    return result;
}

static void endTempMemory(TempMemory* temp) {
    assert(temp->usedBefore <= temp->arena->used);
    assert(temp->tempBefore == temp->arena->tempCount - 1);
    temp->arena->used = temp->usedBefore;
    temp->arena->tempCount -= 1;
    *temp = (TempMemory) {};
}

static bool memeq(void* ptr1, void* ptr2, i64 len) {
    int memcmpResult = memcmp(ptr1, ptr2, len);
    bool result = memcmpResult == 0;
    return result;
}

//
// SECTION String
//

typedef struct Str {
    char* ptr;
    i64 len;
} Str;
static Str strslice(Str str, i64 start, i64 end) {return (Str) {str.ptr + start, end - start};}

__attribute__((format(printf,2,3)))
static Str strfmt(Arena* arena, char* fmt, ...) {
    char* out = arenaFreeptr(arena);

    va_list va;
    va_start(va, fmt);
    int printResult = stbsp_vsnprintf(out, arenaFreesize(arena), fmt, va);
    va_end(va);

    arena->used += printResult + 1;
    Str result = {out, printResult};
    return result;
}

static bool streq(Str str1, Str str2) {
    bool result = false;
    if (str1.len == str2.len) {
        result = memeq(str1.ptr, str2.ptr, str1.len);
    }
    return result;
}

static bool strstarts(Str str, Str start) {
    bool result = false;
    if (str.len >= start.len) {
        result = memeq(str.ptr, start.ptr, start.len);
    }
    return result;
}

//
// SECTION Misc
//

typedef struct V2 { f32 x, y; } V2;
static V2 v2sub(V2 a, V2 b) {return (V2) {.x = a.x - b.x, .y = a.y - b.y};}

typedef struct Rect {
    V2 topleft;
    V2 dim;
} Rect;

typedef struct AtlasLocation {
    Rect rect;
    V2 offset; // NOTE(khvorov) First frame in every animation has offset (0, 0)
} AtlasLocation;

typedef struct Texture {
    u32* pixels;
    i32 w, h;
} Texture;

typedef struct Animation {
    f32* frameDurationsInMS;
    i32 frameCount;
} Animation;

typedef struct FirstLast {i32 first, last;} FirstLast;

//
// SECTION Platform
//

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define assertHR(hr) assert(SUCCEEDED(hr))

static u8arr readEntireFile(Arena* arena, Str path) {
    HANDLE hfile = 0;
    tempMemoryBlock(arena) {
        Str path0 = strfmt(arena, "%.*s", LIT(path));

        hfile = CreateFileA(
            path0.ptr,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            0,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            0
        );
        assert(hfile != INVALID_HANDLE_VALUE);
    }

    LARGE_INTEGER fileSize = {};
    BOOL GetFileSizeExResult = GetFileSizeEx(hfile, &fileSize);
    assert(GetFileSizeExResult);

    void* fileContent = arenaAllocArray(arena, u8, fileSize.QuadPart);
    DWORD bytesRead = 0;
    BOOL ReadFileResult = ReadFile(hfile, fileContent, fileSize.QuadPart, &bytesRead, 0);
    assert(ReadFileResult);
    assert(bytesRead == fileSize.QuadPart);

    CloseHandle(hfile);

    u8arr result = {fileContent, fileSize.QuadPart};
    return result;
}
