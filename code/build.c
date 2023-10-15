#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define assert(cond) do { if (cond) {} else {char msg[] = __FILE__ ":" STRINGIFY(__LINE__) ":1: error: assertion failure\n"; WriteFile((HANDLE)STD_OUTPUT_HANDLE, msg, sizeof(msg) - 1, 0, 0); __debugbreak();} } while (0)
#define assertHR(hr) assert(SUCCEEDED(hr))

#include "common.c"

#include <stdlib.h>

#include <d3dcompiler.h>
#pragma comment (lib, "d3dcompiler")

#define STBRP_STATIC
#define STBRP_ASSERT(x) assert(x)
#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rectpack.h"

#define STBI_NO_JPEG
#define STBI_NO_PNG
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_SUPPORT_ZLIB
#define STB_IMAGE_STATIC
#define STBI_ASSERT(x) assert(x)
#define STB_IMAGE_IMPLEMENTATION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include "stb_image.h"
#pragma clang diagnostic pop

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

static Texture aseDecodeTextureFromCel(Arena* arena, AseChunk* chunk) {
    Texture texture = {};
    assert(chunk->cel.type == AseCelType_CompressedImage);
    u32 compressedDataSize = chunk->size - offsetof(AseChunk, cel.compressed);
    texture.w = chunk->cel.width;
    texture.h = chunk->cel.height;
    i32 pixelsInTex = texture.w * texture.h;
    texture.pixels = arenaAllocArray(arena, u32, pixelsInTex);
    i32 bytesInTex = pixelsInTex * sizeof(u32);
    int decodeResult = stbi_zlib_decode_buffer((char*)texture.pixels, bytesInTex, (char*)chunk->cel.compressed, compressedDataSize);
    assert(decodeResult == bytesInTex);
    return texture;
}

//
// SECTION Misc
//

typedef enum LayerTypeInfo {
    LayerTypeInfo_None,
    LayerTypeInfo_Collision,
} LayerTypeInfo;

static V2 adjustRefpoint(V2 refpoint, V2 firstFrameArtOffset) {
    V2 result = {refpoint.x - firstFrameArtOffset.x + 1, firstFrameArtOffset.y - refpoint.y - 1};
    return result;
}

static char capitalize(char ch) {return ch - ('a' - 'A');}

typedef struct StrBuilder {
    char* ptr;
    i64 len;
    i64 cap;
    Str currentName;
    i64 addCount;
} StrBuilder;

__attribute__((format(printf,2,3)))
static void strbuilderfmt(StrBuilder* builder, char* fmt, ...) {
    char* out = builder->ptr + builder->len;
    i64 size = builder->cap - builder->len;

    va_list va;
    va_start(va, fmt);
    int printResult = stbsp_vsnprintf(out, size, fmt, va);
    va_end(va);

    builder->len += printResult;
}

static void strbuilderEnumBegin(StrBuilder* builder, Str name) {
    strbuilderfmt(builder, "typedef enum %.*s {\n", LIT(name));
    builder->currentName = name;
    builder->addCount = 0;
}

static void strbuilderEnumAdd(StrBuilder* builder, Str name) {
    strbuilderfmt(builder, "    %.*s_%.*s,\n", LIT(builder->currentName), LIT(name));
    builder->addCount += 1;
}

static void strbuilderEnumEnd(StrBuilder* builder) {
    strbuilderfmt(builder, "} %.*s;\n\n", LIT(builder->currentName));
    builder->currentName = (Str) {};
    builder->addCount = 0;
}

static void strbuilderTableBegin(StrBuilder* builder, Str type, Str name, Str entryCount) {
    strbuilderfmt(builder, "static const %.*s %.*s[%.*s] = {\n", LIT(type), LIT(name), LIT(entryCount));
    builder->addCount = 0;
}

static void builderTableAdd(StrBuilder* builder, Str key, Str value) {
    strbuilderfmt(builder, "    [%.*s] = %.*s,\n", LIT(key), LIT(value));
    builder->addCount += 1;
}

static void strbuilderTableEnd(StrBuilder* builder) {
    strbuilderfmt(builder, "};\n\n");
    builder->addCount = 0;
}

typedef struct BinBuilder {
    u8* ptr;
    i64 len;
    i64 cap;
} BinBuilder;

#define binWrite(Bin, Val) binWrite(Bin, &(Val), sizeof(Val))
static void binWrite_(BinBuilder* bin, void* ptr, i64 len) {
    assert(bin->cap - bin->len >= len);
    memcpy(bin->ptr + bin->len, ptr, len);
    bin->len += len;
}

typedef struct PtrFix {
    Str arrName;
    Str elName;
    Str elParentName;
    i32 len;
} PtrFix;

typedef struct AssetDataBuilder {
    BinBuilder* bin;
    StrBuilder* str;
    i32 currentIndLevel;
    struct {PtrFix* ptr; i32 len, cap;} ptrfixes;
} AssetDataBuilder;

static void assetIndent(AssetDataBuilder* datab) {
    for (i32 ind = 0; ind < datab->currentIndLevel; ind++) {
        strbuilderfmt(datab->str, "    ");
    }
}

static void assetBeginData(AssetDataBuilder* datab) {
    strbuilderfmt(datab->str, "#pragma pack(push)\n#pragma pack(1)\ntypedef struct AssetData {\n");
    datab->currentIndLevel += 1;
}

static void assetEndData(AssetDataBuilder* datab) {
    strbuilderfmt(datab->str, "} AssetData;\n#pragma pack(pop)\n\n");
    datab->currentIndLevel -= 1;
}

static void assetBeginStruct(AssetDataBuilder* datab) {
    assetIndent(datab);
    strbuilderfmt(datab->str, "struct {\n");
    datab->currentIndLevel += 1;
}

static void assetEndStruct(AssetDataBuilder* datab, Str name) {
    datab->currentIndLevel -= 1;
    assetIndent(datab);
    strbuilderfmt(datab->str, "} %.*s;\n", LIT(name));
}

#define assetAddField(Datab, Name, Val) assetAddField_(Datab, STR(Name), &(Val), sizeof(Val))
static void assetAddField_(AssetDataBuilder* datab, Str name, void* data, i64 dataLen) {
    assetIndent(datab);
    strbuilderfmt(datab->str, "%.*s;\n", LIT(name));
    binWrite_(datab->bin, data, dataLen);
}

#define assetAddArrField(Datab, Name, Data, Count) assetAddArrField_(Datab, STR(Name), Data, sizeof(*Data), Count)
static void assetAddArrField_(AssetDataBuilder* datab, Str name, void* data, i64 elementSize, i32 elementCount) {
    assetIndent(datab);
    strbuilderfmt(datab->str, "%.*s[%d];\n", LIT(name), elementCount);
    binWrite_(datab->bin, data, elementCount * elementSize);
}

static void assetBeginProc(AssetDataBuilder* datab) {
    strbuilderfmt(datab->str, "static void assetDataAfterLoad(AssetData* adata) {\n");
    datab->currentIndLevel += 1;
}

static void assetEndProc(AssetDataBuilder* datab) {
    strbuilderfmt(datab->str, "}\n");
    datab->currentIndLevel -= 1;
}

static void assetAddPtrFixLoop(AssetDataBuilder* datab, i32 count, Str arrName, Str elName, Str elParentName) {
    assetIndent(datab);
    // NOTE(khvorov) Pointers are offsets (in elements) from the base of allData, so make them into actual pointers by adding the base of allData
    strbuilderfmt(datab->str,
        "for (u32 ind = 0; ind < %d; ind++) {"
        "adata->%.*s.%.*s[ind].ptr = adata->%.*s.%.*s + (u64)adata->%.*s.%.*s[ind].ptr;"
        "}\n", count, LIT(arrName), LIT(elName), LIT(arrName), LIT(elParentName), LIT(arrName), LIT(elName)
    );
}

typedef struct ArrType {void* ptr;} ArrType;
static void convertPtrsToIndices(void* arrPtr, i64 arrElementSize, i64 arrElementCount, void* allDataPtr, i64 allDataElementSize) {
    for (i64 arrElIndex = 0; arrElIndex < arrElementCount; arrElIndex++) {
        ArrType* arr = (ArrType*)(arrPtr + (arrElIndex * arrElementSize));
        assert(arr->ptr >= allDataPtr);
        arr->ptr = (void*)(((u64)arr->ptr - (u64)allDataPtr) / allDataElementSize);
    }
}

#define assetAddArrOfArr(Arena, Datab, Name, DataType, Arr, Data) assetAddArrOfArr_(Arena, Datab, STR(Name), STR(DataType), (Arr).ptr, sizeof(*(Arr).ptr), (Arr).len, (Data).ptr, sizeof(*(Data).ptr), (Data).len)
static void assetAddArrOfArr_(Arena* arena, AssetDataBuilder* datab, Str name, Str dataType, void* arrPtr, i64 arrElementSize, i64 arrElementCount, void* allDataPtr, i64 allDataElementSize, i64 allDataElementCount) {
    convertPtrsToIndices(arrPtr, arrElementSize, arrElementCount, allDataPtr, allDataElementSize);
    assetBeginStruct(datab);
    assetAddArrField_(datab, strfmt(arena, "%*s allData", LIT(dataType)), allDataPtr, allDataElementSize, allDataElementCount);
    assetAddArrField_(datab, strfmt(arena, "%*sarr elements", LIT(dataType)), arrPtr, arrElementSize, arrElementCount);
    assetEndStruct(datab, name);
    arrpush(datab->ptrfixes, ((PtrFix){name, STR("elements"), STR("allData"), arrElementCount}));
}

#define assetAddArrOfArrOfArr(Arena, Datab, Name, DataType, Arr, MidArr, Data) \
    assetAddArrOfArrOfArr_(Arena, Datab, STR(Name), STR(DataType), (Arr).ptr, sizeof(*(Arr).ptr), (Arr).len, (MidArr).ptr, sizeof(*(MidArr).ptr), (MidArr).len, (Data).ptr, sizeof(*(Data).ptr), (Data).len)
static void assetAddArrOfArrOfArr_(
    Arena* arena, AssetDataBuilder* datab, Str name, Str dataType,
    void* arrPtr, i64 arrElementSize, i64 arrElementCount,
    void* midDataPtr, i64 midDataElementSize, i64 midDataElementCount,
    void* allDataPtr, i64 allDataElementSize, i64 allDataElementCount
) {
    convertPtrsToIndices(arrPtr, arrElementSize, arrElementCount, midDataPtr, midDataElementSize);
    convertPtrsToIndices(midDataPtr, midDataElementSize, midDataElementCount, allDataPtr, allDataElementSize);
    assetBeginStruct(datab);
    assetAddArrField_(datab, strfmt(arena, "%*s allData", LIT(dataType)), allDataPtr, allDataElementSize, allDataElementCount);
    assetAddArrField_(datab, strfmt(arena, "%*sarr midData", LIT(dataType)), midDataPtr, midDataElementSize, midDataElementCount);
    assetAddArrField_(datab, strfmt(arena, "%*sarrarr elements", LIT(dataType)), arrPtr, arrElementSize, arrElementCount);
    assetEndStruct(datab, name);
    arrpush(datab->ptrfixes, ((PtrFix){name, STR("elements"), STR("midData"), arrElementCount}));
    arrpush(datab->ptrfixes, ((PtrFix){name, STR("midData"), STR("allData"), midDataElementCount}));
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

static void writeToStdout(Str msg) {WriteFile((HANDLE)STD_OUTPUT_HANDLE, msg.ptr, msg.len, 0, 0);}

typedef struct FileInfoStage {
    i32 index, variant;
    AseFile* content;
} FileInfoStage;

typedef struct FileInfoEntity {
    struct {Str entity, animation, file;} names;
    AseFile* content;
} FileInfoEntity;

static int fileInfoEntityCmp(const void* val1_, const void* val2_) {
    Str val1 = ((FileInfoEntity*)val1_)->names.file;
    Str val2 = ((FileInfoEntity*)val2_)->names.file;
    int result = strncmp(val1.ptr, val2.ptr, min(val1.len, val2.len));
    return result;
}

static u64 getClock(void) {
    LARGE_INTEGER buildProgramStart = {};
    QueryPerformanceCounter(&buildProgramStart);
    return buildProgramStart.QuadPart;
}

static f32 getMsFrom(u64 before, LARGE_INTEGER freqPerSec) {
    u64 now = getClock();
    u64 diff = now - before;
    f32 ms = (f32)diff / (f32)freqPerSec.QuadPart * 1000.0f;
    return ms;
}

static u64 parseUint(Str str) {
    u64 result = 0;
    u64 scale = 1;
    for (i32 ind = str.len - 1; ind >= 0; ind--) {
        char ch = str.ptr[ind];
        assert(ch <= '9' && ch >= '0');
        u64 chNumber = ch - '0';
        u64 chNumberScaled = chNumber * scale;
        result += chNumberScaled;
        scale *= 10;
    }
    return result;
}

//
// SECTION Bench
//

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

//
// SECTION Main
//

int main() {
    u64 buildProgramStart = getClock();

    LARGE_INTEGER performanceFrequencyPerSec = {};
    QueryPerformanceFrequency(&performanceFrequencyPerSec);

    Arena arena_ = {.size = Gigabyte};
    arena_.base = VirtualAlloc(0, arena_.size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(arena_.base);
    Arena* arena = &arena_;

    if (false) benchAlignment(arena);

    StrBuilder strbuilder_ = {.cap = 20 * Megabyte};
    strbuilder_.ptr = arenaAllocArray(arena, char, strbuilder_.cap);
    StrBuilder* strbuilder = &strbuilder_;

    strbuilderfmt(strbuilder, "// generated by build.c, do not edit by hand\n\n");

    struct {FileInfoEntity* ptr; i64 len, cap;} fileInfosEntities = {.cap = 1024};
    fileInfosEntities.ptr = arenaAllocArray(arena, FileInfoEntity, fileInfosEntities.cap);
    struct {FileInfoStage* ptr; i64 len, cap;} fileInfosStages = {.cap = 1024};
    fileInfosStages.ptr = arenaAllocArray(arena, FileInfoStage, fileInfosStages.cap);
    {
        WIN32_FIND_DATAA findData = {};
        HANDLE findHandle = FindFirstFileA("data/*.aseprite", &findData);
        assert(findHandle != INVALID_HANDLE_VALUE);

        do {
            Str filefull = strfmt(arena, "%s", findData.cFileName);

            Str path = strfmt(arena, "data/%.*s", LIT(filefull));
            u8arr fileContent = readEntireFile(arena, path);

            AseFile* ase = (AseFile*)fileContent.ptr;
            assert(ase->magic == 0xA5E0);

            Str filename = strslice(filefull, 0, filefull.len - (sizeof(".aseprite") - 1));
            bool isStage = strstarts(filename, STR("stage"));
            if (isStage) {
                FileInfoStage info = {.content = ase};
                Str numbers = strslice(filename, STR("stage").len, filename.len);
                Str indexStr = {};
                Str variantStr = {};
                bool found = false;
                for (i32 ind = 0; ind < numbers.len; ind++) {
                    char ch = numbers.ptr[ind];
                    if (ch == '_') {
                        indexStr = strslice(numbers, 0, ind);
                        variantStr = strslice(numbers, ind + 1, numbers.len);
                        found = true;
                        break;
                    }
                }
                assert(found);
                info.index = parseUint(indexStr);
                info.variant = parseUint(variantStr);
                arrpush(fileInfosStages, info);
            } else {
                FileInfoEntity info = {.content = ase, .names.file = filefull};
                for (i32 ind = 0; ind < filename.len; ind++) {
                    char ch = filename.ptr[ind];
                    if (ch == '_') {
                        info.names.entity = strslice(filename, 0, ind);
                        info.names.entity.ptr[0] = capitalize(info.names.entity.ptr[0]);
                        info.names.animation = strslice(filename, ind + 1, filename.len);
                        info.names.animation.ptr[0] = capitalize(info.names.animation.ptr[0]);
                        break;
                    }
                }
                arrpush(fileInfosEntities, info);
            }

        } while (FindNextFileA(findHandle, &findData));
    }

    qsort(fileInfosEntities.ptr, fileInfosEntities.len, sizeof(*fileInfosEntities.ptr), fileInfoEntityCmp);

    struct {Str* ptr; i64 len, cap;} entityNamesDedup = {.cap = fileInfosEntities.len};
    entityNamesDedup.ptr = arenaAllocArray(arena, Str, fileInfosEntities.len);
    for (i64 ind = 0; ind < fileInfosEntities.len; ind++) {
        FileInfoEntity* info = fileInfosEntities.ptr + ind;
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

    {
        strbuilderEnumBegin(strbuilder, STR("EntityID"));
        for (i64 ind = 0; ind < entityNamesDedup.len; ind++) {strbuilderEnumAdd(strbuilder, entityNamesDedup.ptr[ind]);}
        strbuilderEnumAdd(strbuilder, STR("Count"));
        strbuilderEnumEnd(strbuilder);
    }

    Str* animationNames = arenaAllocArray(arena, Str, fileInfosEntities.len);

    strbuilderEnumBegin(strbuilder, STR("AnimationID"));
    for (i64 ind = 0; ind < fileInfosEntities.len; ind++) {
        FileInfoEntity info = fileInfosEntities.ptr[ind];
        Str animationName = strfmt(arena, "%.*s_%.*s", LIT(info.names.entity), LIT(info.names.animation));
        strbuilderEnumAdd(strbuilder, animationName);
        animationNames[ind] = animationName;
    }
    strbuilderEnumAdd(strbuilder, STR("Count"));
    strbuilderEnumEnd(strbuilder);

    strbuilderEnumBegin(strbuilder, STR("AtlasID"));
    strbuilderEnumAdd(strbuilder, STR("Whitepx"));
    strbuilderEnumAdd(strbuilder, STR("Font"));
    for (i64 ind = 0; ind < fileInfosEntities.len; ind++) {
        FileInfoEntity info = fileInfosEntities.ptr[ind];
        Str animationName = animationNames[ind];
        for (int frameIndex = 0; frameIndex < info.content->frameCount; frameIndex++) {
            Str atlasName = strfmt(arena, "%.*s_frame%d", LIT(animationName), frameIndex + 1);
            strbuilderEnumAdd(strbuilder, atlasName);
        }
    }
    i32 totalAtlasTextureCount = strbuilder->addCount;
    strbuilderEnumAdd(strbuilder, STR("Count"));
    strbuilderEnumEnd(strbuilder);

    strbuilderTableBegin(strbuilder, STR("int"), STR("globalFirstAtlasID"), STR("EntityID_Count"));
    {
        Str currentEntity = {};
        for (i64 ind = 0; ind < fileInfosEntities.len; ind++) {
            FileInfoEntity info = fileInfosEntities.ptr[ind];
            if (!streq(currentEntity, info.names.entity)) {
                Str key = strfmt(arena, "EntityID_%.*s", LIT(info.names.entity));
                Str value = strfmt(arena, "AtlasID_%.*s_%.*s_frame1", LIT(info.names.entity), LIT(info.names.animation));
                builderTableAdd(strbuilder, key, value);
                currentEntity = info.names.entity;
            }
        }
    }
    strbuilderTableEnd(strbuilder);

    strbuilderTableBegin(strbuilder, STR("int"), STR("globalAnimationCumulativeFrameCounts"), STR("AnimationID_Count"));
    {
        i32 currentCumulativeCount = 0;
        Str currentEntity = {};
        for (i64 ind = 0; ind < fileInfosEntities.len; ind++) {
            FileInfoEntity info = fileInfosEntities.ptr[ind];
            if (!streq(currentEntity, info.names.entity)) {
                currentCumulativeCount = 0;
                currentEntity = info.names.entity;
            }

            Str animationName = animationNames[ind];
            Str key = strfmt(arena, "AnimationID_%.*s", LIT(animationName));
            Str value = strfmt(arena, "%d", currentCumulativeCount);
            builderTableAdd(strbuilder, key, value);

            currentCumulativeCount += info.content->frameCount;
        }
    }
    strbuilderTableEnd(strbuilder);

    struct {Texture* ptr; i64 len, cap;} atlasTextures = {.cap = totalAtlasTextureCount};
    atlasTextures.ptr = arenaAllocArray(arena, Texture, atlasTextures.cap);

    {
        u32 whitePxTexData[] = {0xffff'ffff};
        Texture whitePxTex = {.w = 1, .h = 1, .pixels = whitePxTexData};
        arrpush(atlasTextures, whitePxTex);
    }

    struct {i32 glyphCount, glyphW, glyphH, gapW; Texture tex;} font = {.glyphCount = 128, .glyphW = 8, .glyphH = 16, .gapW = 2};
    {
        font.tex.w = font.glyphW * font.glyphCount + (font.glyphCount - 1) * font.gapW;
        font.tex.h = font.glyphH;
        font.tex.pixels = arenaAllocArray(arena, u32, font.tex.w * font.tex.h);
        {
            // Taken from https://github.com/nakst/luigi/blob/main/luigi.h
            // Taken from https://commons.wikimedia.org/wiki/File:Codepage-437.png
            // Public domain.

            const uint64_t _uiFont[] = {
                0x0000000000000000UL, 0x0000000000000000UL, 0xBD8181A5817E0000UL, 0x000000007E818199UL, 0xC3FFFFDBFF7E0000UL, 0x000000007EFFFFE7UL, 0x7F7F7F3600000000UL, 0x00000000081C3E7FUL,
                0x7F3E1C0800000000UL, 0x0000000000081C3EUL, 0xE7E73C3C18000000UL, 0x000000003C1818E7UL, 0xFFFF7E3C18000000UL, 0x000000003C18187EUL, 0x3C18000000000000UL, 0x000000000000183CUL,
                0xC3E7FFFFFFFFFFFFUL, 0xFFFFFFFFFFFFE7C3UL, 0x42663C0000000000UL, 0x00000000003C6642UL, 0xBD99C3FFFFFFFFFFUL, 0xFFFFFFFFFFC399BDUL, 0x331E4C5870780000UL, 0x000000001E333333UL,
                0x3C666666663C0000UL, 0x0000000018187E18UL, 0x0C0C0CFCCCFC0000UL, 0x00000000070F0E0CUL, 0xC6C6C6FEC6FE0000UL, 0x0000000367E7E6C6UL, 0xE73CDB1818000000UL, 0x000000001818DB3CUL,
                0x1F7F1F0F07030100UL, 0x000000000103070FUL, 0x7C7F7C7870604000UL, 0x0000000040607078UL, 0x1818187E3C180000UL, 0x0000000000183C7EUL, 0x6666666666660000UL, 0x0000000066660066UL,
                0xD8DEDBDBDBFE0000UL, 0x00000000D8D8D8D8UL, 0x6363361C06633E00UL, 0x0000003E63301C36UL, 0x0000000000000000UL, 0x000000007F7F7F7FUL, 0x1818187E3C180000UL, 0x000000007E183C7EUL,
                0x1818187E3C180000UL, 0x0000000018181818UL, 0x1818181818180000UL, 0x00000000183C7E18UL, 0x7F30180000000000UL, 0x0000000000001830UL, 0x7F060C0000000000UL, 0x0000000000000C06UL,
                0x0303000000000000UL, 0x0000000000007F03UL, 0xFF66240000000000UL, 0x0000000000002466UL, 0x3E1C1C0800000000UL, 0x00000000007F7F3EUL, 0x3E3E7F7F00000000UL, 0x0000000000081C1CUL,
                0x0000000000000000UL, 0x0000000000000000UL, 0x18183C3C3C180000UL, 0x0000000018180018UL, 0x0000002466666600UL, 0x0000000000000000UL, 0x36367F3636000000UL, 0x0000000036367F36UL,
                0x603E0343633E1818UL, 0x000018183E636160UL, 0x1830634300000000UL, 0x000000006163060CUL, 0x3B6E1C36361C0000UL, 0x000000006E333333UL, 0x000000060C0C0C00UL, 0x0000000000000000UL,
                0x0C0C0C0C18300000UL, 0x0000000030180C0CUL, 0x30303030180C0000UL, 0x000000000C183030UL, 0xFF3C660000000000UL, 0x000000000000663CUL, 0x7E18180000000000UL, 0x0000000000001818UL,
                0x0000000000000000UL, 0x0000000C18181800UL, 0x7F00000000000000UL, 0x0000000000000000UL, 0x0000000000000000UL, 0x0000000018180000UL, 0x1830604000000000UL, 0x000000000103060CUL,
                0xDBDBC3C3663C0000UL, 0x000000003C66C3C3UL, 0x1818181E1C180000UL, 0x000000007E181818UL, 0x0C183060633E0000UL, 0x000000007F630306UL, 0x603C6060633E0000UL, 0x000000003E636060UL,
                0x7F33363C38300000UL, 0x0000000078303030UL, 0x603F0303037F0000UL, 0x000000003E636060UL, 0x633F0303061C0000UL, 0x000000003E636363UL, 0x18306060637F0000UL, 0x000000000C0C0C0CUL,
                0x633E6363633E0000UL, 0x000000003E636363UL, 0x607E6363633E0000UL, 0x000000001E306060UL, 0x0000181800000000UL, 0x0000000000181800UL, 0x0000181800000000UL, 0x000000000C181800UL,
                0x060C183060000000UL, 0x000000006030180CUL, 0x00007E0000000000UL, 0x000000000000007EUL, 0x6030180C06000000UL, 0x00000000060C1830UL, 0x18183063633E0000UL, 0x0000000018180018UL,
                0x7B7B63633E000000UL, 0x000000003E033B7BUL, 0x7F6363361C080000UL, 0x0000000063636363UL, 0x663E6666663F0000UL, 0x000000003F666666UL, 0x03030343663C0000UL, 0x000000003C664303UL,
                0x66666666361F0000UL, 0x000000001F366666UL, 0x161E1646667F0000UL, 0x000000007F664606UL, 0x161E1646667F0000UL, 0x000000000F060606UL, 0x7B030343663C0000UL, 0x000000005C666363UL,
                0x637F636363630000UL, 0x0000000063636363UL, 0x18181818183C0000UL, 0x000000003C181818UL, 0x3030303030780000UL, 0x000000001E333333UL, 0x1E1E366666670000UL, 0x0000000067666636UL,
                0x06060606060F0000UL, 0x000000007F664606UL, 0xC3DBFFFFE7C30000UL, 0x00000000C3C3C3C3UL, 0x737B7F6F67630000UL, 0x0000000063636363UL, 0x63636363633E0000UL, 0x000000003E636363UL,
                0x063E6666663F0000UL, 0x000000000F060606UL, 0x63636363633E0000UL, 0x000070303E7B6B63UL, 0x363E6666663F0000UL, 0x0000000067666666UL, 0x301C0663633E0000UL, 0x000000003E636360UL,
                0x18181899DBFF0000UL, 0x000000003C181818UL, 0x6363636363630000UL, 0x000000003E636363UL, 0xC3C3C3C3C3C30000UL, 0x00000000183C66C3UL, 0xDBC3C3C3C3C30000UL, 0x000000006666FFDBUL,
                0x18183C66C3C30000UL, 0x00000000C3C3663CUL, 0x183C66C3C3C30000UL, 0x000000003C181818UL, 0x0C183061C3FF0000UL, 0x00000000FFC38306UL, 0x0C0C0C0C0C3C0000UL, 0x000000003C0C0C0CUL,
                0x1C0E070301000000UL, 0x0000000040607038UL, 0x30303030303C0000UL, 0x000000003C303030UL, 0x0000000063361C08UL, 0x0000000000000000UL, 0x0000000000000000UL, 0x0000FF0000000000UL,
                0x0000000000180C0CUL, 0x0000000000000000UL, 0x3E301E0000000000UL, 0x000000006E333333UL, 0x66361E0606070000UL, 0x000000003E666666UL, 0x03633E0000000000UL, 0x000000003E630303UL,
                0x33363C3030380000UL, 0x000000006E333333UL, 0x7F633E0000000000UL, 0x000000003E630303UL, 0x060F0626361C0000UL, 0x000000000F060606UL, 0x33336E0000000000UL, 0x001E33303E333333UL,
                0x666E360606070000UL, 0x0000000067666666UL, 0x18181C0018180000UL, 0x000000003C181818UL, 0x6060700060600000UL, 0x003C666660606060UL, 0x1E36660606070000UL, 0x000000006766361EUL,
                0x18181818181C0000UL, 0x000000003C181818UL, 0xDBFF670000000000UL, 0x00000000DBDBDBDBUL, 0x66663B0000000000UL, 0x0000000066666666UL, 0x63633E0000000000UL, 0x000000003E636363UL,
                0x66663B0000000000UL, 0x000F06063E666666UL, 0x33336E0000000000UL, 0x007830303E333333UL, 0x666E3B0000000000UL, 0x000000000F060606UL, 0x06633E0000000000UL, 0x000000003E63301CUL,
                0x0C0C3F0C0C080000UL, 0x00000000386C0C0CUL, 0x3333330000000000UL, 0x000000006E333333UL, 0xC3C3C30000000000UL, 0x00000000183C66C3UL, 0xC3C3C30000000000UL, 0x0000000066FFDBDBUL,
                0x3C66C30000000000UL, 0x00000000C3663C18UL, 0x6363630000000000UL, 0x001F30607E636363UL, 0x18337F0000000000UL, 0x000000007F63060CUL, 0x180E181818700000UL, 0x0000000070181818UL,
                0x1800181818180000UL, 0x0000000018181818UL, 0x18701818180E0000UL, 0x000000000E181818UL, 0x000000003B6E0000UL, 0x0000000000000000UL, 0x63361C0800000000UL, 0x00000000007F6363UL,
            };

            memset(font.tex.pixels, 0, font.tex.w * font.tex.h * sizeof(*font.tex.pixels));
            for (i32 glyphIndex = 0; glyphIndex < font.glyphCount; glyphIndex++) {
                u8* glyphBitmap = (u8*)(_uiFont + (glyphIndex * 2));
                for (i32 glyphByteIndex = 0; glyphByteIndex < 16; glyphByteIndex++) {
                    u8 glyphByte = glyphBitmap[glyphByteIndex];
                    for (u8 bitIndex = 0; bitIndex < 8; bitIndex++) {
                        u8 mask = 1 << bitIndex;
                        if (glyphByte & mask) {
                            i32 texIndex = glyphByteIndex * font.tex.w + glyphIndex * font.glyphW + glyphIndex * font.gapW + bitIndex;
                            font.tex.pixels[texIndex] = 0xFFFF'FFFF;
                        }
                    }
                }
            }
        }

        arrpush(atlasTextures, font.tex);
    }

    AtlasLocation* atlasLocations = arenaAllocAndZeroArray(arena, AtlasLocation, totalAtlasTextureCount);
    struct {f32* ptr; i64 len, cap;} allAnimationDurations = {.cap = 4096};
    allAnimationDurations.ptr = arenaAllocArray(arena, f32, allAnimationDurations.cap);
    struct {Animation* ptr; i32 len;} animations = {.len = fileInfosEntities.len};
    animations.ptr = arenaAllocArray(arena, Animation, animations.len);

    struct {LayerTypeInfo* ptr; i32 len, cap;} layerTypeInfo = {.cap = 1024};
    layerTypeInfo.ptr = arenaAllocArray(arena, LayerTypeInfo, layerTypeInfo.cap);

    struct {Rect* ptr; i32 len, cap;} collisionRects = {.cap = entityNamesDedup.len};
    collisionRects.ptr = arenaAllocArray(arena, Rect, collisionRects.cap);

    for (i32 fileInfoIndex = 0; fileInfoIndex < fileInfosEntities.len; fileInfoIndex++) {
        FileInfoEntity* info = fileInfosEntities.ptr + fileInfoIndex;
        AseFile* ase = info->content;

        AseFrame* frame = ase->frames;
        V2 firstFrameArtOffset = {};

        Animation* animation = animations.ptr + fileInfoIndex;
        animation->frameCount = ase->frameCount;
        animation->frameDurationsInMS = allAnimationDurations.ptr + allAnimationDurations.len;

        layerTypeInfo.len = 0;

        for (i32 frameIndex = 0; frameIndex < ase->frameCount; frameIndex++) {
            assert(frame->magic == 0xF1FA);
            arrpush(allAnimationDurations, frame->frameDurationMS);

            bool foundCollision = false;

            AseChunk* chunk = frame->chunks;
            for (u16 chunkIndex = 0; chunkIndex < frame->chunksCountNew; chunkIndex++) {
                assert(chunk->size >= 6);

                switch (chunk->type) {
                    case AseChunkType_ColorProfile: {
                        assert(chunk->colorProfile.type == AseColorProfileType_sRGB);
                    } break;

                    case AseChunkType_Layer: {
                        assert(chunk->layer.type == AseLayerType_Normal);
                        Str name = {chunk->layer.name.str, chunk->layer.name.len};
                        bool isCollision = streq(name, STR("collision"));
                        if (isCollision) {
                            arrpush(layerTypeInfo, LayerTypeInfo_Collision);
                        } else {
                            arrpush(layerTypeInfo, LayerTypeInfo_None);
                        }
                    } break;

                    case AseChunkType_Cel: {
                        assert(chunk->cel.index < layerTypeInfo.len);
                        LayerTypeInfo thisLayerTypeInfo = layerTypeInfo.ptr[chunk->cel.index];
                        switch (thisLayerTypeInfo) {
                            case LayerTypeInfo_None: {
                                Texture texture = aseDecodeTextureFromCel(arena, chunk);

                                V2 artOffset = (V2) {chunk->cel.posX, chunk->cel.posY};
                                if (frameIndex == 0) {
                                    firstFrameArtOffset = artOffset;
                                } else {
                                    AtlasLocation* thisLoc = atlasLocations + atlasTextures.len;
                                    V2 thisOffset = v2sub(firstFrameArtOffset, artOffset);
                                    thisLoc->offset = thisOffset; // TODO(khvorov) Do we ever need an offset that's not in the top-left?
                                }

                                arrpush(atlasTextures, texture);
                            } break;

                            case LayerTypeInfo_Collision: {
                                foundCollision = true;
                                Rect collision = {{chunk->cel.posX, chunk->cel.posY}, {chunk->cel.width, chunk->cel.height}};
                                arrpush(collisionRects, collision);
                            } break;
                        }
                    } break;

                    case AseChunkType_Palette:
                    case AseChunkType_OldPalette:
                        break;

                    default: assert(!"unimplemented"); break;
                }

                chunk = (void*)chunk + chunk->size;
            }

            if (foundCollision) {
                assert(frameIndex == 0);
                assert(collisionRects.len > 0);
                Rect* lastCollisionRect = collisionRects.ptr + collisionRects.len - 1;
                lastCollisionRect->topleft = adjustRefpoint(lastCollisionRect->topleft, firstFrameArtOffset);
            }

            frame = (AseFrame*)chunk;
        }
    }

    assert(atlasTextures.len == atlasTextures.cap);
    assert(collisionRects.len == collisionRects.cap);

    Texture atlas = {};
    {
        stbrp_rect* rectsToPack = arenaAllocArray(arena, stbrp_rect, atlasTextures.len);
        for (i32 texInd = 0; texInd < atlasTextures.len; texInd++) {
            Texture* texture = atlasTextures.ptr + texInd;
            stbrp_rect* rect = rectsToPack + texInd;
            rect->id = texInd;
            rect->w = texture->w + 2;
            rect->h = texture->h + 2;
        }

        {
            struct {stbrp_node* ptr; i32 len;} nodes = {.len = 4096};
            nodes.ptr = arenaAllocArray(arena, stbrp_node, nodes.len);
            stbrp_context ctx = {};
            stbrp_init_target(&ctx, nodes.len, INT_MAX, nodes.ptr, nodes.len);
            int allRectsPacked = stbrp_pack_rects(&ctx, rectsToPack, atlasTextures.len);
            assert(allRectsPacked);
        }

        for (i32 texInd = 0; texInd < atlasTextures.len; texInd++) {
            stbrp_rect* rect = rectsToPack + texInd;
            assert(rect->was_packed);
            atlas.w = max(atlas.w, rect->x + rect->w);
            atlas.h = max(atlas.h, rect->y + rect->h);
        }
        atlas.pixels = arenaAllocArray(arena, u32, atlas.w * atlas.h);

        for (i32 texInd = 0; texInd < atlasTextures.len; texInd++) {
            stbrp_rect* rect = rectsToPack + texInd;
            Texture* texture = atlasTextures.ptr + rect->id;

            for (i32 texRow = 0; texRow < texture->h; texRow++) {
                u32* src = texture->pixels + texRow * texture->w;
                u32* dest = atlas.pixels + (texRow + (rect->y + 1)) * atlas.w + (rect->x + 1);
                memcpy(dest, src, texture->w * sizeof(u32));
            }

            AtlasLocation* loc = atlasLocations + texInd;
            loc->rect = (Rect) {{rect->x, rect->y}, {rect->w, rect->h}};
        }
    }

    strbuilderEnumBegin(strbuilder, STR("ShaderID"));
    struct {u8arr* ptr; i32 len, cap;} shaders = {.cap = 1024};
    shaders.ptr = arenaAllocArray(arena, u8arr, shaders.cap);
    struct {u8* ptr; i64 len, cap;} allShaderData = {.cap = 50 * Megabyte};
    allShaderData.ptr = arenaAllocArray(arena, u8, allShaderData.cap);
    {
        WIN32_FIND_DATAA findData = {};
        HANDLE findHandle = FindFirstFileA("code/*.hlsl", &findData);
        assert(findHandle != INVALID_HANDLE_VALUE);
        do {
            Str shaderFilename = strfmt(arena, "%s", findData.cFileName);
            Str shaderSrcPath = strfmt(arena, "code/%.*s", LIT(shaderFilename));

            u8arr shaderSrc = readEntireFile(arena, shaderSrcPath);
            Str entryPoints[] = {STR("vs"), STR("ps")};
            UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
            for (u32 entryPointIndex = 0; entryPointIndex < arrayCount(entryPoints); entryPointIndex++) {
                Str entryPoint = entryPoints[entryPointIndex];
                ID3DBlob* vblob = 0;
                ID3DBlob* error = 0;
                Str target = strstarts(entryPoint, STR("vs")) ? STR("vs_5_0") : STR("ps_5_0");
                HRESULT compileResult = D3DCompile(shaderSrc.ptr, shaderSrc.len, shaderSrcPath.ptr, NULL, NULL, entryPoint.ptr, target.ptr, flags, 0, &vblob, &error);
                if (FAILED(compileResult)) {
                    Str message = {ID3D10Blob_GetBufferPointer(error), ID3D10Blob_GetBufferSize(error)};
                    writeToStdout(message);
                    assert(!"failed to compile");
                }
                u8arr shaderDataOg = {ID3D10Blob_GetBufferPointer(vblob), ID3D10Blob_GetBufferSize(vblob)};
                u8arr shaderDataCopy = {allShaderData.ptr + allShaderData.len, shaderDataOg.len};
                arrpusharr(allShaderData, shaderDataOg);
                arrpush(shaders, shaderDataCopy);
                Str fileNameNoExt = strslice(shaderFilename, 0, shaderFilename.len - (sizeof(".hlsl") - 1));
                Str enumLabel = strfmt(arena, "%.*s_%.*s", LIT(fileNameNoExt), LIT(entryPoint));
                strbuilderEnumAdd(strbuilder, enumLabel);
            }

        } while (FindNextFileA(findHandle, &findData));
    }
    strbuilderEnumAdd(strbuilder, STR("Count"));
    strbuilderEnumEnd(strbuilder);

    struct {V2* ptr; i32 len, cap;} collisionPoints = {.cap = 1024};
    collisionPoints.ptr = arenaAllocArray(arena, V2, collisionPoints.cap);

    for (i32 fileInfoIndex = 0; fileInfoIndex < fileInfosStages.len; fileInfoIndex++) {
        FileInfoStage* info = fileInfosStages.ptr + fileInfoIndex;
        AseFile* ase = info->content;
        AseFrame* frame = ase->frames;

        Texture canvas = {.w = ase->width, .h = ase->height};
        canvas.pixels = arenaAllocAndZeroArray(arena, u32, (canvas.w + 2) * (canvas.h + 2));
        i32 canvasPitch = canvas.w + 2;

        // NOTE(khvorov) Fill the border
        {
            i64 bytesInRow = canvasPitch * sizeof(*canvas.pixels);
            memset(canvas.pixels, 0xFF, bytesInRow);
            memset(canvas.pixels + (canvas.h + 1) * canvasPitch, 0xFF, bytesInRow);
            for (i32 rowIndex = 1; rowIndex <= canvas.h; rowIndex++) {
                u32* rowPixels = canvas.pixels + rowIndex * canvasPitch;
                rowPixels[0] = 0xFFFF'FFFF;
                rowPixels[canvas.w + 1] = 0xFFFF'FFFF;
            }
            canvas.pixels = canvas.pixels + canvasPitch + 1;
        }

        for (i32 frameIndex = 0; frameIndex < ase->frameCount; frameIndex++) {
            assert(frame->magic == 0xF1FA);
            AseChunk* chunk = frame->chunks;
            for (u16 chunkIndex = 0; chunkIndex < frame->chunksCountNew; chunkIndex++) {
                assert(chunk->size >= 6);

                switch (chunk->type) {
                    case AseChunkType_Cel: {
                        Texture texture = aseDecodeTextureFromCel(arena, chunk);
                        assert(chunk->cel.posY + texture.h <= canvas.h);
                        assert(chunk->cel.posX + texture.w <= canvas.w);
                        u32* canvasTopleft = canvas.pixels + chunk->cel.posY * canvasPitch + chunk->cel.posX;
                        for (i32 texRow = 0; texRow < texture.h; texRow++) {
                            u32* rowPixels = texture.pixels + texRow * texture.w;
                            u32* canvasRowStart = canvasTopleft + texRow * canvasPitch;
                            memcpy(canvasRowStart, rowPixels, texture.w * sizeof(*texture.pixels));
                        }
                    } break;

                    case AseChunkType_ColorProfile:
                    case AseChunkType_Layer:
                    case AseChunkType_Palette:
                    case AseChunkType_OldPalette:
                        break;

                    default: assert(!"unimplemented"); break;
                }

                chunk = (void*)chunk + chunk->size;
            }            
        }

        // NOTE(khvorov) Write the canvas out to a string for debugging
        if (false) {
            i32 tempStrPitch = (canvas.w + 3);
            i32 tempStrLen = tempStrPitch * (canvas.h + 2);
            char* tempStr = arenaAllocArray(arena, char, tempStrLen);
            memset(tempStr, '+', tempStrLen);
            tempStr = tempStr + tempStrPitch + 1;
            for (i32 rowIndex = -1; rowIndex <= canvas.h; rowIndex++) {
                tempStr[rowIndex * tempStrPitch + canvas.h + 1] = '\n';
                for (i32 colIndex = -1; colIndex <= canvas.w; colIndex++) {
                    i32 pxIndex = rowIndex * canvasPitch + colIndex;
                    u32 pxValue = canvas.pixels[pxIndex];
                    char ch = pxValue ? 'x' : 'o';
                    i32 chIndex = rowIndex * tempStrPitch + colIndex;
                    tempStr[chIndex] = ch;
                }
            }
            writeEntireFile(arena, STR("temp.txt"), tempStr - tempStrPitch - 1, tempStrLen);
        }

        // TODO(khvorov) Figure out how to do walk around all the polygons adding each of their points to the array once in the right order
        for (i32 rowIndex = -1; rowIndex < canvas.h; rowIndex++) {
            for (i32 colIndex = 0; colIndex <= canvas.w; colIndex++) {
                
                // TODO(khvorov) Remove - ignoring the edges of the canvas
                if (rowIndex == -1 || rowIndex == canvas.h - 1 || colIndex == 0 || colIndex == canvas.w) {
                    continue;
                }

                i32 pxIndex = rowIndex * canvasPitch + colIndex;
                u32 pxValue = canvas.pixels[pxIndex];
                u32 pxValueLeft = canvas.pixels[pxIndex - 1];
                u32 pxValueBottom = canvas.pixels[pxIndex + canvasPitch];

                bool pxFilled = pxValue != 0;
                bool pxFilledLeft = pxValueLeft != 0;
                bool pxFilledBottom = pxValueBottom != 0;

                // NOTE(khvorov) Go bottom-up to follow world-space

                f32 pxLeft = colIndex;
                f32 pxBottom = canvas.h - 1 - rowIndex;
                f32 pxTop = pxBottom + 1;

                if (pxFilled != pxFilledLeft) {
                    V2 top = {pxLeft, pxTop};
                    V2 bottom = {pxLeft, pxBottom};

                    if (pxFilled) {
                        arrpush(collisionPoints, bottom);
                        arrpush(collisionPoints, top);
                    } else {
                        arrpush(collisionPoints, top);
                        arrpush(collisionPoints, bottom);
                    }

                    // TODO(khvorov) Remove once a proper edge determination proc is figured out
                    goto tempStopPuttingPointsIn;
                }

                if (pxFilled != pxFilledBottom) {

                }
            }
        }
        tempStopPuttingPointsIn:
    }

    // TODO(khvorov) Pull from art    
    V2arr tempCollisionPolys[] = {
        {collisionPoints.ptr, collisionPoints.len},
    };
    V2arrarr collisionPolys = {tempCollisionPolys, arrayCount(tempCollisionPolys)};

    V2arrarr tempStages[] = {
        {collisionPolys.ptr, collisionPolys.len},
    };
    struct {V2arrarr* ptr; i64 len;} stages = {tempStages, arrayCount(tempStages)};

    BinBuilder binb = {.cap = 50 * Megabyte};
    binb.ptr = arenaAllocArray(arena, u8, binb.cap);

    AssetDataBuilder datab_ = {.bin = &binb, .str = strbuilder, .ptrfixes.cap = 1024};
    datab_.ptrfixes.ptr = arenaAllocArray(arena, PtrFix, datab_.ptrfixes.cap);
    AssetDataBuilder* datab = &datab_;

    assetBeginData(datab);

    assetBeginStruct(datab);
    assetAddField(datab, "int glyphW", font.glyphW);
    assetEndStruct(datab, STR("font"));

    assetBeginStruct(datab);
    assetAddArrField(datab, "Rect collision", collisionRects.ptr, collisionRects.len);
    assetEndStruct(datab, STR("entities"));

    assetBeginStruct(datab);
    assetAddField(datab, "int w", atlas.w);
    assetAddField(datab, "int h", atlas.h);
    assetAddArrField(datab, "unsigned int pixels", atlas.pixels, atlas.w * atlas.h);
    assetAddArrField(datab, "AtlasLocation locations", atlasLocations, totalAtlasTextureCount);
    assetEndStruct(datab, STR("atlas"));

    assetAddArrOfArr(arena, datab, "animations", "f32", animations, allAnimationDurations);
    assetAddArrOfArr(arena, datab, "shaders", "u8", shaders, allShaderData);

    assetAddArrOfArrOfArr(arena, datab, "stages", "V2", stages, collisionPolys, collisionPoints);

    assetEndData(datab);

    assetBeginProc(datab);
    for (i32 ptrfixIndex = 0; ptrfixIndex < datab->ptrfixes.len; ptrfixIndex++) {
        PtrFix ptrfix = datab->ptrfixes.ptr[ptrfixIndex];
        assetAddPtrFixLoop(datab, ptrfix.len, ptrfix.arrName, ptrfix.elName, ptrfix.elParentName);
    }
    assetEndProc(datab);

    writeEntireFile(arena, STR("code/generated.c"), strbuilder->ptr, strbuilder->len);

    {
        Str cmd = strfmt(arena, "clang code/rorclone.c -std=c2x -march=native -Wall -Wextra -g -o build/rorclone.exe");
        writeToStdout(strfmt(arena, "%.*s\n", LIT(cmd)));
        STARTUPINFOA startupInfo = {.cb = sizeof(startupInfo)};
        PROCESS_INFORMATION procInfo = {};
        BOOL CreateProcessResult = CreateProcessA(0, cmd.ptr, 0, 0, TRUE, 0, 0, 0, &startupInfo, &procInfo);
        assert(CreateProcessResult);
        DWORD WaitForSingleObjectResult = WaitForSingleObject(procInfo.hProcess, INFINITE);
        assert(WaitForSingleObjectResult == WAIT_OBJECT_0);
        DWORD exitCode = 0;
        BOOL GetExitCodeProcessResult = GetExitCodeProcess(procInfo.hProcess, &exitCode);
        assert(GetExitCodeProcessResult);
        assert(exitCode == 0);
    }

    writeEntireFile(arena, STR("build/rorclone.dat"), binb.ptr, binb.len);

    writeToStdout(strfmt(arena, "finished in %.1fms\n", getMsFrom(buildProgramStart, performanceFrequencyPerSec)));
    return 0;
}