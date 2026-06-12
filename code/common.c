#pragma clang diagnostic ignored "-Wunused-function"

#define STB_SPRINTF_STATIC
#define STB_SPRINTF_IMPLEMENTATION
#include "stb_sprintf.h"

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>

#include <immintrin.h>

#define Byte 1
#define Kilobyte 1024 * Byte
#define Megabyte 1024 * Kilobyte
#define Gigabyte 1024 * Megabyte

#ifndef assert
#define assert(cond) do { if (cond) {} else __debugbreak(); } while (0)
#endif

#define unimplemented() __debugbreak()
#define assertStrInArena(str, arena) assert((u64)(str).ptr >= (u64)(arena)->base && (u64)(str).ptr + (u64)(str).len <= (u64)(arena)->base + (u64)(arena)->size);
#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)
#define STR(x) ((Str){x, sizeof(x) - 1})
#define LIT(x) (int)x.len, x.ptr
#define absval(x) ((x) < 0 ? -(x) : x)
#define unused(x) ((x) = (x))
#define carrayCount(x) (sizeof(x) / sizeof((x)[0]))
#define dynarrpush(arr, val) assert((arr)->len < (arr)->cap); (arr)->ptr[(arr)->len++] = (val)
#define dynarrpusharr(arr, val) assert((arr)->len + (val).len <= (arr)->cap); memcpy((arr)->ptr + (arr)->len, (val).ptr, (val).len * sizeof(*(val).ptr)); (arr)->len += (val).len
#define slicefromcarray(carr) {.ptr = carr, .len = carrayCount(carr)}
#define arenaAllocOne(arena, type) (type*)arenaAllocBytes((arena), sizeof(type))
#define arenaAllocAndZeroOne(arena, type) (type*)arenaAllocAndZeroBytes((arena), sizeof(type))
#define arenaAllocArray(arena, type, count) {.ptr = (type*)arenaAllocBytes((arena), sizeof(type) * (count)), .len = (count)}
#define arenaAllocAndZeroArray(arena, type, count) {.ptr = (type*)arenaAllocAndZeroBytes((arena), sizeof(type) * (count)), .len = (count)}
#define arenaAllocDynarr(arena, type, capacity) {.ptr = (type*)arenaAllocBytes((arena), sizeof(type) * (capacity)), .len = 0, .cap = (capacity)}
#define arenaAllocAndZeroDynarr(arena, type, capacity) {.ptr = (type*)arenaAllocAndZeroBytes((arena), sizeof(type) * (capacity)), .len = 0, .cap = (capacity)}
#define tempMemoryBlock(arena_) for (TempMemory _temp_ = beginTempMemory(arena_); _temp_.arena; endTempMemory(&_temp_))

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float f32;
typedef double f64;

typedef struct i8slice {i8* ptr; i64 len;} i8slice;
typedef struct i16slice {i16* ptr; i64 len;} i16slice;
typedef struct i32slice {i32* ptr; i64 len;} i32slice;
typedef struct i64slice {i64* ptr; i64 len;} i64slice;
typedef struct u8slice {u8* ptr; i64 len;} u8slice;
typedef struct u16slice {u16* ptr; i64 len;} u16slice;
typedef struct u32slice {u32* ptr; i64 len;} u32slice;
typedef struct u64slice {u64* ptr; i64 len;} u64slice;
typedef struct f32slice {f32* ptr; i64 len;} f32slice;
typedef struct f64slice {f64* ptr; i64 len;} f64slice;

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

static void* arenaAllocBytes(Arena* arena, i64 size) {
    assert(arenaFreesize(arena) >= size);
    void* result = arenaFreeptr(arena);
    arena->used += size;
    return result;
}

static void* arenaAllocAndZeroBytes(Arena* arena, i64 size) {
    void* ptr = arenaAllocBytes(arena, size);
    memset(ptr, 0, size);
    return ptr;
}

static Arena arenaFromArena(Arena* arena, i64 size) {
    Arena result = {
        .base = arenaAllocBytes(arena, size),
        .size = size,
    };
    return result;
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

static void keepTempMemory(TempMemory* temp) {
    assert(temp->usedBefore <= temp->arena->used);
    assert(temp->tempBefore == temp->arena->tempCount - 1);
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

static char capitalize(char ch) {return ch - ('a' - 'A');}

typedef struct Str {
    char* ptr;
    i64 len;
} Str;

typedef struct Strs {
    Str* ptr;
    i64 len;
} Strs;

static Str strslice(Str str, i64 start, i64 end) {return (Str) {str.ptr + start, end - start};}

typedef struct StrBuilder {
    char* ptr;
    i64 len;
    i64 cap;
} StrBuilder;

static void append_(StrBuilder* builder, char* fmt, va_list args) {
    char* out = builder->ptr + builder->len;
    i64 size = builder->cap - builder->len;
    int printResult = stbsp_vsnprintf(out, size, fmt, args);
    builder->len += printResult;
}

__attribute__((format(printf,2,3)))
static void append(StrBuilder* builder, char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    append_(builder, fmt, va);
    va_end(va);
}

static Str strfmt_(Arena* arena, char* fmt, va_list args) {
    char* out = arenaFreeptr(arena);
    int printResult = stbsp_vsnprintf(out, arenaFreesize(arena), fmt, args);
    arena->used += printResult + 1; // NOTE: null terminator
    Str result = {out, printResult};
    return result;
}

__attribute__((format(printf,2,3)))
static Str strfmt(Arena* arena, char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    Str result = strfmt_(arena, fmt, va);
    va_end(va);
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

static bool strends(Str str, Str end) {
    bool result = false;
    if (str.len >= end.len) {
        result = memeq(str.ptr + str.len - end.len, end.ptr, end.len);
    }
    return result;
}

//
// SECTION Misc
//

#define lerp(From, To, By) _Generic((From), f32: lerpf32, V2: v2lerp)(From, To, By)

static f32 lerpf32(f32 from, f32 to, f32 by) {return from * (1 - by) + to * by;}
static f32 square(f32 x) {return x * x;}

static f32 squareRoot(f32 x) {
    __m128 x128 = _mm_set1_ps(x);
    __m128 result128 = _mm_sqrt_ss(x128);
    f32 result = _mm_cvtss_f32(result128);
    return result;
}

typedef struct V2i { i32 x, y; } V2i;
typedef struct V2 { f32 x, y; } V2;
typedef struct V2slice {V2* ptr; i64 len;} V2slice;
typedef struct V2sliceslice {V2slice* ptr; i64 len;} V2sliceslice;
static V2 v2fromf32(f32 x) {return (V2) {x, x};}
static V2 v2add(V2 a, V2 b) {return (V2) {.x = a.x + b.x, .y = a.y + b.y};}
static V2 v2sub(V2 a, V2 b) {return (V2) {.x = a.x - b.x, .y = a.y - b.y};}
static V2 v2scale(V2 v, f32 by) {return (V2) {.x = v.x * by, .y = v.y * by};}
static V2 v2lerp(V2 v1, V2 v2, f32 by) {return (V2) {lerp(v1.x, v2.x, by), lerp(v1.y, v2.y, by)};}
static f32 v2dot(V2 v1, V2 v2) {return v1.x * v2.x + v1.y * v2.y;}
static f32 v2outer(V2 v1, V2 v2) {return v1.x * v2.y - v1.y * v2.x;}
static bool v2eq(V2 v1, V2 v2) {return v1.x == v2.x && v1.y == v2.y;}
static bool v2ieq(V2i v1, V2i v2) {return v1.x == v2.x && v1.y == v2.y;}
static f32 v2len(V2 v) {return squareRoot(square(v.x) + square(v.y));}
static V2 v2normalize(V2 v) {return v2scale(v, 1 / v2len(v));}
static V2 v2xyquaterturn(V2 v) {return (V2) {-v.y, v.x};}

typedef union V4 {
    struct {f32 x, y, z, w;};
    struct {f32 r, g, b, a;};
} V4;

typedef struct Rect {V2 topleft, dim;} Rect;
static Rect rectShrink(Rect rect, f32 by) {return (Rect) {.topleft = v2add(rect.topleft, v2fromf32(by)), .dim = v2sub(rect.dim, v2fromf32(by * 2))};}
static Rect rectTranslate(Rect rect, V2 by) {return (Rect) {.topleft = v2add(rect.topleft, by), .dim = rect.dim};}

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

typedef struct CollisionLine {
    V2 p1, p2;
} CollisionLine;
