// Extracts assets from source files and packs them into an asset file the game uses
// NOTE: All packed textures have a 1px border around them, as a result there is a 2px gap between all textures
// Compiles the game, the tests and the bench

#include "common.c"

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

#define STBRP_STATIC
#define STBRP_ASSERT(x) assert(x)
#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rectpack.h"

//
// SECTION Aseprite
//

static_assert(true); // NOTE: otherwise clang may complain
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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-variable-sized-type-not-at-end"
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
#pragma clang diagnostic pop

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
    texture.pixels = ((u32slice) arenaAllocArray(arena, u32, pixelsInTex)).ptr;
    i32 bytesInTex = pixelsInTex * sizeof(u32);
    int decodeResult = stbi_zlib_decode_buffer((char*)texture.pixels, bytesInTex, (char*)chunk->cel.compressed, compressedDataSize);
    assert(decodeResult == bytesInTex);
    return texture;
}

//
// SECTION Raw asset processing
//

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

static int fileInfoStageCmp(const void* val1_, const void* val2_) {
    FileInfoStage* val1 = ((FileInfoStage*)val1_);
    FileInfoStage* val2 = ((FileInfoStage*)val2_);
    int result = val1->index - val2->index;
    if (result == 0) {
        result = val1->variant - val2->variant;
    }
    return result;
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

typedef struct CornerInfo {
    bool isCorner;
    V2i pos;
    V2i nextDir;
} CornerInfo;

static CornerInfo getCornerInfo(i32 rowEdge, i32 colEdge, i32 canvasPitch, Texture canvas) {
    i32 pxIndexTopLeft = (rowEdge - 1) * canvasPitch + (colEdge - 1);
    i32 pxIndexTopRight = pxIndexTopLeft + 1;
    i32 pxIndexBottomLeft = pxIndexTopLeft + canvasPitch;
    i32 pxIndexBottomRight = pxIndexBottomLeft + 1;

    u32 pxValueTopLeft = canvas.pixels[pxIndexTopLeft];
    u32 pxValueTopRight = canvas.pixels[pxIndexTopRight];
    u32 pxValueBottomLeft = canvas.pixels[pxIndexBottomLeft];
    u32 pxValueBottomRight = canvas.pixels[pxIndexBottomRight];

    bool pxFilledTopLeft = pxValueTopLeft != 0;
    bool pxFilledTopRight = pxValueTopRight != 0;
    bool pxFilledBottomLeft = pxValueBottomLeft != 0;
    bool pxFilledBottomRight = pxValueBottomRight != 0;

    bool isCornerTopLeft = (pxFilledBottomLeft == pxFilledBottomRight && pxFilledBottomLeft == pxFilledTopRight) && (pxFilledBottomLeft != pxFilledTopLeft);
    bool isCornerTopRight = (pxFilledBottomLeft == pxFilledBottomRight && pxFilledBottomLeft == pxFilledTopLeft) && (pxFilledBottomLeft != pxFilledTopRight);
    bool isCornerBottomLeft = (pxFilledTopLeft == pxFilledTopRight && pxFilledTopLeft == pxFilledBottomRight) && (pxFilledTopLeft != pxFilledBottomLeft);
    bool isCornerBottomRight = (pxFilledTopLeft == pxFilledTopRight && pxFilledTopLeft == pxFilledBottomLeft) && (pxFilledTopLeft != pxFilledBottomRight);
    bool isCorner = isCornerBottomLeft || isCornerTopLeft || isCornerBottomRight || isCornerTopRight;

    // TODO(khvorov) Do we need to handle double corner situations?
    assert(isCornerTopLeft + isCornerTopRight + isCornerBottomLeft + isCornerBottomRight <= 1);

    CornerInfo result = {};
    if (isCorner) {
        result.isCorner = true;
        result.pos = (V2i) {colEdge, rowEdge};

        if (isCornerBottomRight) {
            if (!pxFilledBottomRight) {
                result.nextDir.y = 1;
            } else {
                result.nextDir.x = 1;
            }
        } else if (isCornerBottomLeft) {
            if (!pxFilledBottomLeft) {
                result.nextDir.x = -1;
            } else {
                result.nextDir.y = 1;
            }
        } else if (isCornerTopLeft) {
            if (!pxFilledTopLeft) {
                result.nextDir.y = -1;
            } else {
                result.nextDir.x = -1;
            }
        } else if (isCornerTopRight) {
            if (!pxFilledTopRight) {
                result.nextDir.x = 1;
            } else {
                result.nextDir.y = -1;
            }
        }
    }
    return result;
}

typedef enum LayerTypeInfo {
    LayerTypeInfo_None,
    LayerTypeInfo_Collision,
} LayerTypeInfo;

static V2 adjustRefpoint(V2 refpoint, V2 firstFrameArtOffset) {
    V2 result = {refpoint.x - firstFrameArtOffset.x + 1, firstFrameArtOffset.y - refpoint.y - 1};
    return result;
}

typedef struct AssetStructBuilder {
    StrBuilder strb;
    Str currentName;
    i64 addCount;
} AssetStructBuilder;

static void strbuilderEnumBegin(AssetStructBuilder* builder, Str name) {
    append(&builder->strb, "typedef enum %.*s {\n", LIT(name));
    builder->currentName = name;
    builder->addCount = 0;
}

static void strbuilderEnumAdd(AssetStructBuilder* builder, Str name) {
    append(&builder->strb, "    %.*s_%.*s,\n", LIT(builder->currentName), LIT(name));
    builder->addCount += 1;
}

static void strbuilderEnumEnd(AssetStructBuilder* builder) {
    append(&builder->strb, "} %.*s;\n\n", LIT(builder->currentName));
    builder->currentName = (Str) {};
    builder->addCount = 0;
}

static void strbuilderTableBegin(AssetStructBuilder* builder, Str type, Str name, Str entryCount) {
    append(&builder->strb, "static const %.*s %.*s[%.*s] = {\n", LIT(type), LIT(name), LIT(entryCount));
    builder->addCount = 0;
}

static void builderTableAdd(AssetStructBuilder* builder, Str key, Str value) {
    append(&builder->strb, "    [%.*s] = %.*s,\n", LIT(key), LIT(value));
    builder->addCount += 1;
}

static void strbuilderTableEnd(AssetStructBuilder* builder) {
    append(&builder->strb, "};\n\n");
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
    AssetStructBuilder* str;
    i32 currentIndLevel;
    struct {PtrFix* ptr; i32 len, cap;} ptrfixes;
} AssetDataBuilder;

static void assetIndent(AssetDataBuilder* datab) {
    for (i32 ind = 0; ind < datab->currentIndLevel; ind++) {
        append(&datab->str->strb, "    ");
    }
}

static void assetBeginData(AssetDataBuilder* datab) {
    append(&datab->str->strb, "#pragma pack(push)\n#pragma pack(1)\ntypedef struct AssetData {\n");
    datab->currentIndLevel += 1;
}

static void assetEndData(AssetDataBuilder* datab) {
    append(&datab->str->strb, "} AssetData;\n#pragma pack(pop)\n\n");
    datab->currentIndLevel -= 1;
}

static void assetBeginStruct(AssetDataBuilder* datab) {
    assetIndent(datab);
    append(&datab->str->strb, "struct {\n");
    datab->currentIndLevel += 1;
}

static void assetEndStruct(AssetDataBuilder* datab, Str name) {
    datab->currentIndLevel -= 1;
    assetIndent(datab);
    append(&datab->str->strb, "} %.*s;\n", LIT(name));
}

#define assetAddField(Datab, Name, Val) assetAddField_(Datab, STR(Name), &(Val), sizeof(Val))
static void assetAddField_(AssetDataBuilder* datab, Str name, void* data, i64 dataLen) {
    assetIndent(datab);
    append(&datab->str->strb, "%.*s;\n", LIT(name));
    binWrite_(datab->bin, data, dataLen);
}

#define assetAddArrField(Datab, Name, Data, Count) assetAddArrField_(Datab, STR(Name), Data, sizeof(*Data), Count)
static void assetAddArrField_(AssetDataBuilder* datab, Str name, void* data, i64 elementSize, i32 elementCount) {
    assetIndent(datab);
    append(&datab->str->strb, "%.*s[%d];\n", LIT(name), elementCount);
    binWrite_(datab->bin, data, elementCount * elementSize);
}

static void assetBeginProc(AssetDataBuilder* datab) {
    append(&datab->str->strb, "static void assetDataAfterLoad(AssetData* adata) {\n");
    datab->currentIndLevel += 1;
}

static void assetEndProc(AssetDataBuilder* datab) {
    append(&datab->str->strb, "}\n");
    datab->currentIndLevel -= 1;
}

static void assetAddPtrFixLoop(AssetDataBuilder* datab, i32 count, Str arrName, Str elName, Str elParentName) {
    assetIndent(datab);
    // NOTE(khvorov) Pointers are offsets (in elements) from the base of allData, so make them into actual pointers by adding the base of allData
    append(&datab->str->strb,
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
    assetAddArrField_(datab, strfmt(arena, "%.*s allData", LIT(dataType)), allDataPtr, allDataElementSize, allDataElementCount);
    assetAddArrField_(datab, strfmt(arena, "%.*sslice elements", LIT(dataType)), arrPtr, arrElementSize, arrElementCount);
    assetEndStruct(datab, name);
    dynarrpush(&datab->ptrfixes, ((PtrFix){name, STR("elements"), STR("allData"), arrElementCount}));
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
    assetAddArrField_(datab, strfmt(arena, "%.*s allData", LIT(dataType)), allDataPtr, allDataElementSize, allDataElementCount);
    assetAddArrField_(datab, strfmt(arena, "%.*sslice midData", LIT(dataType)), midDataPtr, midDataElementSize, midDataElementCount);
    assetAddArrField_(datab, strfmt(arena, "%.*ssliceslice elements", LIT(dataType)), arrPtr, arrElementSize, arrElementCount);
    assetEndStruct(datab, name);
    dynarrpush(&datab->ptrfixes, ((PtrFix){name, STR("elements"), STR("midData"), arrElementCount}));
    dynarrpush(&datab->ptrfixes, ((PtrFix){name, STR("midData"), STR("allData"), midDataElementCount}));
}
