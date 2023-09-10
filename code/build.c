#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wgnu-variable-sized-type-not-at-end"

#include <stdint.h>
#include <stddef.h>

#define STB_SPRINTF_STATIC
#define STB_SPRINTF_IMPLEMENTATION
#include "stb_sprintf.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#define Byte 1
#define Kilobyte 1024 * Byte
#define Megabyte 1024 * Kilobyte
#define Gigabyte 1024 * Megabyte

#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)
#define assert(cond) do { if (cond) {} else {writeToStdout(STR(__FILE__ ":" STRINGIFY(__LINE__) ":1: error: assertion failure\n")); __debugbreak();} } while (0)
#define STR(x) ((Str){x, sizeof(x) - 1})
#define LIT(x) (int)x.len, x.ptr
#define unused(x) ((x) = (x))
#define arrayCount(x) (sizeof(x) / sizeof((x)[0]))
#define arrpush(arr, val) assert((arr).len < (arr).cap); (arr).ptr[(arr).len++] = (val)
#define arenaAllocArray(arena, type, count) ((type*)arenaAlloc((arena), sizeof(type) * (count)))
#define tempMemoryBlock(arena_) for (TempMemory _temp_ = beginTempMemory(arena_); _temp_.arena; endTempMemory(&_temp_))

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

typedef struct Str {
    char* ptr;
    i64 len;
} Str;

static void writeToStdout(Str str) {
    WriteFile((HANDLE)STD_OUTPUT_HANDLE, str.ptr, str.len, 0, 0);
}

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

STBSP__ATTRIBUTE_FORMAT(2,3)
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

static Str strslice(Str str, i64 start, i64 end) {return (Str) {str.ptr + start, end - start};}
static char capitalize(char ch) {return ch - ('a' - 'A');}

typedef struct StrBuilder {
    char* ptr;
    i64 len;
    i64 cap;
    Str currentName;
} StrBuilder;

STBSP__ATTRIBUTE_FORMAT(2,3)
static void builderfmt(StrBuilder* builder, char* fmt, ...) {
    char* out = builder->ptr + builder->len;
    i64 size = builder->cap - builder->len;

    va_list va;
    va_start(va, fmt);
    int printResult = stbsp_vsnprintf(out, size, fmt, va);
    va_end(va);

    builder->len += printResult;
}

static void builderEnumBegin(StrBuilder* builder, Str name) {
    builderfmt(builder, "typedef enum %.*s {\n", LIT(name));
    builder->currentName = name;
}

static void builderEnumAdd(StrBuilder* builder, Str name) {
    builderfmt(builder, "    %.*s_%.*s,\n", LIT(builder->currentName), LIT(name));
}

static void builderEnumEnd(StrBuilder* builder) {
    builderfmt(builder, "} %.*s;\n\n", LIT(builder->currentName));
    builder->currentName = (Str) {};
}

static void builderTableBegin(StrBuilder* builder, Str type, Str name, Str entryCount) {
    builderfmt(builder, "static const %.*s %.*s[%.*s] = {\n", LIT(type), LIT(name), LIT(entryCount));
}

static void builderTableAdd(StrBuilder* builder, Str key, Str value) {
    builderfmt(builder, "    [%.*s] = %.*s,\n", LIT(key), LIT(value));
}

static void builderTableEnd(StrBuilder* builder) {
    builderfmt(builder, "};\n\n");
}

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

static void writeEntireFile(Arena* arena, Str path, void* ptr, i64 len) {
    HANDLE hfile = 0;
    tempMemoryBlock(arena) {
        Str path0 = strfmt(arena, "%.*s", LIT(path));

        hfile = CreateFileA(
            path0.ptr,
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            0,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            0
        );
        assert(hfile != INVALID_HANDLE_VALUE);
    }

    DWORD bytesWritten = 0;
    BOOL WriteFileResult = WriteFile(hfile, ptr, len, &bytesWritten, 0);
    assert(WriteFileResult);
    assert(bytesWritten == len);

    CloseHandle(hfile);
}


//
// SECTION Aseprite
//

#pragma pack(push)
#pragma pack(1)

typedef enum AseChunkType: u16 {
    AseChunkType_OldPalette = 0x0004,
    AseChunkType_Layer = 0x2004,
    AseChunkType_Cel = 0x2005,
    AseChunkType_ColorProfile = 0x2007,
    AseChunkType_Palette = 0x2019,
} AseChunkType;

typedef struct Fixed16x16 {
    u16 p1;
    u16 p2;
} Fixed16x16;

typedef enum AseColorProfileType: u16 {
    AseColorProfileType_None = 0,
    AseColorProfileType_sRGB = 1,
    AseColorProfileType_ICC = 2,
} AseColorProfileType;

typedef struct AseChunkColorProfile {
    AseColorProfileType type;
    u16 flags;
    Fixed16x16 gamma;
    u8 reserved[8];
    struct {u32 len; void* ptr;} icc;
} AseChunkColorProfile;

typedef struct AseString {
    u16 len;
    char str[];
} AseString;

typedef enum AseLayerFlag {
    AseLayerFlag_Visible = 1,
    AseLayerFlag_Editable = 2,
    AseLayerFlag_LockMovement = 4,
    AseLayerFlag_Background = 8,
    AseLayerFlag_PreferLinkedCels = 16,
    AseLayerFlag_LayerGroupShouldBeDisplayedCollapsed = 32,
    AseLayerFlag_LayerIsAReferenceLayer = 64,
} AseLayerFlag;
typedef u16 AseLayerFlags;

typedef enum AseLayerType: u16 {
    AseLayerType_Normal = 0,
    AseLayerType_Group = 1,
    AseLayerType_Tilemap = 2,
} AseLayerType;

typedef struct AseChunkLayer {
    AseLayerFlags flags;
    AseLayerType type;
    u16 childLevel;
    u16 defaultWidthIgnored;
    u16 defaultHeightIgnored;
    u16 blendMode;
    u8 opacity;
    u8 future[3];
    AseString name;
    u16 tilesetIndex;
} AseChunkLayer;

typedef enum AseCelType: u16 {
    AseCelType_Raw = 0,
    AseCelType_Linked = 1,
    AseCelType_CompressedImage = 2,
    AseCelType_CompressedTilemap = 3,
} AseCelType;

typedef struct AseChunkCel {
    u16 index;
    i16 posX;
    i16 posY;
    u8 opacity;
    AseCelType type;
    i16 zIndexOffset;
    u8 future[5];
    u16 width;
    u16 height;
    u8 compressed[];
} AseChunkCel;

typedef struct AseChunk {
    u32 size;
    AseChunkType type;
    union {
        AseChunkColorProfile colorProfile;
        AseChunkLayer layer;
        AseChunkCel cel;
    };
} AseChunk;

typedef struct AseFrame {
    u32 bytes;
    u16 magic; // 0xF1FA
    u16 chunkCountOld;
    u16 frameDurationMS;
    u8 forFuture[2];
    u32 chunksCountNew;
    AseChunk chunks[];
} AseFrame;

typedef struct AseFile {
    u32 fileSize;
    u16 magic; // 0xA5E0
    u16 frameCount;
    u16 width;
    u16 height;
    u16 bitsPerPixel;
    u32 flags;
    u16 msBetweenFramesDepricated;
    u32 zero1;
    u32 zero2;
    u8 palleteEntryIndexForTransparentColor;
    u8 ignore[3];
    u16 colorCount; // 0 means 256 for old sprites
    u8 pixelWidth;
    u8 pixelHeight;
    i16 gridXPos;
    i16 gridYPos;
    u16 gridWidth;
    u16 gridHeight;
    u8 forFuture[84];
    AseFrame frames[];
} AseFile;

#pragma pack(pop)

//
// SECTION Main
//

#include <stdlib.h>

typedef struct FileInfo {
    struct {Str entity, animation, file;} names;
    AseFile* content;
} FileInfo;

static int fileInfoCmp(const void* val1_, const void* val2_) {
    Str val1 = ((FileInfo*)val1_)->names.file;
    Str val2 = ((FileInfo*)val2_)->names.file;
    int result = strncmp(val1.ptr, val2.ptr, min(val1.len, val2.len));
    return result;
}

void benchAlignment(Arena* arena) {
    // NOTE(khvorov) Did not see any difference in speed with aligned vs unalligned array

    i64 misalignments[] = {0, 1};
    for (u64 misalignmentIndex = 0; misalignmentIndex < arrayCount(misalignments); misalignmentIndex++) tempMemoryBlock(arena) {
        // NOTE(khvorov) Assume aligned
        assert(((u64)arenaFreeptr(arena) & 3) == 0);

        i64 thisMisalign = misalignments[misalignmentIndex];
        arena->used += thisMisalign;

        i64 floatsToAlloc[] = {100, 1000, 10000, 100000};
        for (i64 floatsToAllocIndex = 0; floatsToAllocIndex < (i64)arrayCount(floatsToAlloc); floatsToAllocIndex++) {
            i64 floatCount = floatsToAlloc[floatsToAllocIndex];
            u64 minDiff = UINT64_MAX;
            u64 maxDiff = 0;

            i64 timesToRun = 10000;
            f32 sumOfSums = 0;

            for (i64 runIndex = 0; runIndex < timesToRun; runIndex++) tempMemoryBlock(arena) {
                u64 before = __rdtsc();

                f32* arr = arenaAllocArray(arena, f32, floatCount);
                for (i64 floatIndex = 0; floatIndex < floatCount; floatIndex++) {
                    arr[floatIndex] = (f32)floatIndex;
                }
                f32 sum = 0;
                for (i64 floatIndex = 0; floatIndex < floatCount; floatIndex++) {
                    sum += arr[floatIndex];
                }

                sumOfSums += sum;

                u64 after = __rdtsc();
                u64 diff = after - before;

                minDiff = min(minDiff, diff);
                maxDiff = max(maxDiff, diff);
            }

            {
                Str out = strfmt(arena, "mis: %llu floatCount: %llu, sum: %f, min: %llu, max: %llu\n", thisMisalign, floatCount, sumOfSums, minDiff, maxDiff);
                OutputDebugStringA(out.ptr);
            }
        }
    }
}

int main() {
    Arena arena_ = {.size = Gigabyte};
    arena_.base = VirtualAlloc(0, arena_.size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(arena_.base);
    Arena* arena = &arena_;

    if (false) benchAlignment(arena);

    StrBuilder builder_ = {.cap = 20 * Megabyte};
    builder_.ptr = arenaAllocArray(arena, char, builder_.cap);
    StrBuilder* builder = &builder_;

    builderfmt(builder, "// generated by build.c, do not edit by hand\n\n");

    struct {FileInfo* ptr; i64 len, cap;} fileInfos = {.cap = 1024};
    fileInfos.ptr = arenaAllocArray(arena, FileInfo, fileInfos.cap);
    {
        WIN32_FIND_DATAA findData = {};
        HANDLE findHandle = FindFirstFileA("data/*.aseprite", &findData);
        assert(findHandle != INVALID_HANDLE_VALUE);

        i64 currentFileIndex = 0;
        do {
            assert(currentFileIndex < fileInfos.cap);
            FileInfo* info = fileInfos.ptr + currentFileIndex;
            info->names.file = strfmt(arena, "%s", findData.cFileName);
            {
                Str filename = strslice(info->names.file, 0, info->names.file.len - (sizeof(".aseprite") - 1));
                for (i32 ind = 0; ind < filename.len; ind++) {
                    char ch = filename.ptr[ind];
                    if (ch == '_') {
                        info->names.entity = strslice(filename, 0, ind);
                        info->names.entity.ptr[0] = capitalize(info->names.entity.ptr[0]);
                        info->names.animation = strslice(filename, ind + 1, filename.len);
                        info->names.animation.ptr[0] = capitalize(info->names.animation.ptr[0]);
                        break;
                    }
                }
            }

            Str path = strfmt(arena, "data/%.*s", LIT(info->names.file));
            u8arr fileContent = readEntireFile(arena, path);

            AseFile* ase = (AseFile*)fileContent.ptr;
            assert(ase->magic == 0xA5E0);
            info->content = ase;

            #if 0
            animation->frameDurationInMS = arenaAllocArray(memory.perm, f32, ase->frameCount);

            AseFrame* frame = ase->frames;
            V2 firstFrameArtOffset = {};

            for (i32 frameIndex = 0; frameIndex < ase->frameCount; frameIndex++) {
                assert(frame->magic == 0xF1FA);
                animation->frameDurationInMS[frameIndex] = frame->frameDurationMS;

                AseChunk* chunk = frame->chunks;
                AtlasID thisID = getAtlasID(thisEntityID, thisAnimationID, frameIndex);
                for (u16 chunkIndex = 0; chunkIndex < frame->chunksCountNew; chunkIndex++) {
                    assert(chunk->size >= 6);

                    switch (chunk->type) {
                        case AseChunkType_ColorProfile: {
                            assert(chunk->colorProfile.type == AseColorProfileType_sRGB);
                        } break;

                        case AseChunkType_Layer: {
                            assert(chunk->layer.type == AseLayerType_Normal);
                        } break;

                        case AseChunkType_Cel: {
                            Texture texture = {};
                            assert(chunk->cel.type == AseCelType_CompressedImage);
                            u32 compressedDataSize = chunk->size - offsetof(AseChunk, cel.compressed);
                            texture.w = chunk->cel.width;
                            texture.h = chunk->cel.height;
                            i32 pixelsInTex = texture.w * texture.h;
                            texture.pixels = arenaAllocArray(memory.scratch, u32, pixelsInTex);
                            i32 bytesInTex = pixelsInTex * sizeof(u32);
                            int decodeResult = stbi_zlib_decode_buffer((char*)texture.pixels, bytesInTex, (char*)chunk->cel.compressed, compressedDataSize);
                            assert(decodeResult == bytesInTex);
                            textures.ptr[thisID] = texture;

                            V2 artOffset = (V2) {chunk->cel.posX, chunk->cel.posY};
                            if (frameIndex == 0) {
                                firstFrameArtOffset = artOffset;
                            } else {
                                AtlasLocation* thisLoc = atlasLocations.ptr + thisID;
                                V2 thisOffset = v2sub(firstFrameArtOffset, artOffset);
                                thisLoc->offset = thisOffset;
                            }
                        } break;

                        case AseChunkType_Palette:
                        case AseChunkType_OldPalette:
                            break;

                        default: assert(!"unimplemented"); break;
                    }

                    chunk = (void*)chunk + chunk->size;
                }
                frame = (AseFrame*)chunk;
            }
            #endif

            currentFileIndex += 1;
        } while (FindNextFileA(findHandle, &findData));

        fileInfos.len = currentFileIndex;
    }

    qsort(fileInfos.ptr, fileInfos.len, sizeof(*fileInfos.ptr), fileInfoCmp);

    {
        struct {Str* ptr; i64 len, cap;} entityNamesDedup = {.cap = fileInfos.len};
        entityNamesDedup.ptr = arenaAllocArray(arena, Str, fileInfos.len);
        for (i64 ind = 0; ind < fileInfos.len; ind++) {
            FileInfo* info = fileInfos.ptr + ind;
            Str newName = info->names.entity;
            bool alreadyPresent = false;
            for (i64 ind = 0; ind < entityNamesDedup.len; ind++) {
                Str existingName = entityNamesDedup.ptr[ind];
                if (streq(newName, existingName)) {
                    alreadyPresent = true;
                    break;
                }
            }
            if (!alreadyPresent) {
                arrpush(entityNamesDedup, newName);
            }
        }

        builderEnumBegin(builder, STR("EntityID"));
        builderEnumAdd(builder, STR("None"));
        for (i64 ind = 0; ind < entityNamesDedup.len; ind++) {builderEnumAdd(builder, entityNamesDedup.ptr[ind]);}
        builderEnumAdd(builder, STR("Count"));
        builderEnumEnd(builder);
    }

    Str* animationNames = arenaAllocArray(arena, Str, fileInfos.len);

    builderEnumBegin(builder, STR("AnimationID"));
    builderEnumAdd(builder, STR("None"));
    for (i64 ind = 0; ind < fileInfos.len; ind++) {
        FileInfo info = fileInfos.ptr[ind];
        Str animationName = strfmt(arena, "%.*s_%.*s", LIT(info.names.entity), LIT(info.names.animation));
        builderEnumAdd(builder, animationName);
        animationNames[ind] = animationName;
    }
    builderEnumAdd(builder, STR("Count"));
    builderEnumEnd(builder);

    builderEnumBegin(builder, STR("AtlasID"));
    builderEnumAdd(builder, STR("Whitepx"));
    builderEnumAdd(builder, STR("Font"));
    for (i64 ind = 0; ind < fileInfos.len; ind++) {
        FileInfo info = fileInfos.ptr[ind];
        Str animationName = animationNames[ind];
        for (int frameIndex = 0; frameIndex < info.content->frameCount; frameIndex++) {
            Str atlasName = strfmt(arena, "%.*s_frame%d", LIT(animationName), frameIndex + 1);
            builderEnumAdd(builder, atlasName);
        }
    }
    builderEnumAdd(builder, STR("Count"));
    builderEnumEnd(builder);

    builderTableBegin(builder, STR("int"), STR("globalFirstAtlasID"), STR("EntityID_Count"));
    {
        Str currentEntity = {};
        for (i64 ind = 0; ind < fileInfos.len; ind++) {
            FileInfo info = fileInfos.ptr[ind];
            if (!streq(currentEntity, info.names.entity)) {
                Str key = strfmt(arena, "EntityID_%.*s", LIT(info.names.entity));
                Str value = strfmt(arena, "AtlasID_%.*s_%.*s_frame1", LIT(info.names.entity), LIT(info.names.animation));
                builderTableAdd(builder, key, value);
                currentEntity = info.names.entity;
            }
        }
    }
    builderTableEnd(builder);

    builderTableBegin(builder, STR("int"), STR("globalAnimationCumulativeFrameCounts"), STR("AnimationID_Count"));
    {
        i32 currentCumulativeCount = 0;
        Str currentEntity = {};
        for (i64 ind = 0; ind < fileInfos.len; ind++) {
            FileInfo info = fileInfos.ptr[ind];
            if (!streq(currentEntity, info.names.entity)) {
                currentCumulativeCount = 0;
                currentEntity = info.names.entity;
            }

            Str animationName = animationNames[ind];
            Str key = strfmt(arena, "AnimationID_%.*s", LIT(animationName));
            Str value = strfmt(arena, "%d", currentCumulativeCount);
            builderTableAdd(builder, key, value);

            currentCumulativeCount += info.content->frameCount;
        }
    }
    builderTableEnd(builder);

    // TODO(khvorov) Move all texture atlas-related work here
    // TODO(khvorov) Move everything in build.bat here

    writeEntireFile(arena, STR("code/generated.c"), builder->ptr, builder->len);
    return 0;
}