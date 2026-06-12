#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define assert(cond) do { if (cond) {} else {char msg[] = __FILE__ ":" STRINGIFY(__LINE__) ":1: error: assertion failure\n"; WriteFile((HANDLE)STD_OUTPUT_HANDLE, msg, sizeof(msg) - 1, 0, 0); __debugbreak();} } while (0)
#define assertHR(hr) assert(SUCCEEDED(hr))

#include "build.c"

#include <d3dcompiler.h>
#pragma comment (lib, "d3dcompiler")

//
// SECTION Misc
//

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

static u8slice readEntireFile(Arena* arena, Str path) {
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

    u8slice fileContent = arenaAllocArray(arena, u8, fileSize.QuadPart);
    DWORD bytesRead = 0;
    BOOL ReadFileResult = ReadFile(hfile, fileContent.ptr, fileSize.QuadPart, &bytesRead, 0);
    assert(ReadFileResult);
    assert(bytesRead == fileSize.QuadPart);

    CloseHandle(hfile);

    return fileContent;
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

static void executeCommandLine(Str cmd) {
    writeToStdout(cmd);
    writeToStdout(STR("\n"));
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

//
// SECTION Timer
//

static u64 getClock(void) {
    LARGE_INTEGER buildProgramStart = {};
    BOOL QueryPerformanceCounterResult = QueryPerformanceCounter(&buildProgramStart);
    assert(QueryPerformanceCounterResult != 0);
    return buildProgramStart.QuadPart;
}

typedef struct Timer {
    u64 startTime;
    u64 performanceFrequencyPerSec;
} Timer;

static Timer startTimer() {
    LARGE_INTEGER performanceFrequencyPerSec = {};
    BOOL QueryPerformanceFrequencyResult = QueryPerformanceFrequency(&performanceFrequencyPerSec);
    assert(QueryPerformanceFrequencyResult != 0);
    Timer timer = {
        .performanceFrequencyPerSec = performanceFrequencyPerSec.QuadPart,
        .startTime = getClock(),
    };
    return timer;
}

static f32 getMsFromStart(Timer* timer) {
    u64 now = getClock();
    u64 diff = now - timer->startTime;
    f32 ms = (f32)diff / (f32)timer->performanceFrequencyPerSec * 1000.0f;
    return ms;
}

//
// SECTION Main
//

int main() {
    Timer timer = startTimer();

    Arena arena_ = {.size = Gigabyte};
    arena_.base = VirtualAlloc(0, arena_.size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(arena_.base);
    Arena* arena = &arena_;

    StrBuilder strbuilder_ = {.cap = 20 * Megabyte};
    strbuilder_.ptr = ((Str) arenaAllocArray(arena, char, strbuilder_.cap)).ptr;
    StrBuilder* strbuilder = &strbuilder_;

    strbuilderfmt(strbuilder, "// generated by build program, do not edit by hand\n\n");

    // TODO(khvorov) Split stage art into collision and non-collision

    struct {FileInfoEntity* ptr; i64 len, cap;} fileInfosEntities = arenaAllocDynarr(arena, FileInfoEntity, 1024);
    struct {FileInfoStage* ptr; i64 len, cap;} fileInfosStages = arenaAllocDynarr(arena, FileInfoStage, 1024);
    {
        WIN32_FIND_DATAA findData = {};
        HANDLE findHandle = FindFirstFileA("data/*.aseprite", &findData);
        assert(findHandle != INVALID_HANDLE_VALUE);

        do {
            Str filefull = strfmt(arena, "%s", findData.cFileName);

            Str path = strfmt(arena, "data/%.*s", LIT(filefull));
            u8slice fileContent = readEntireFile(arena, path);

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
                dynarrpush(&fileInfosStages, info);
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
                dynarrpush(&fileInfosEntities, info);
            }

        } while (FindNextFileA(findHandle, &findData));
    }

    qsort(fileInfosEntities.ptr, fileInfosEntities.len, sizeof(*fileInfosEntities.ptr), fileInfoEntityCmp);
    qsort(fileInfosStages.ptr, fileInfosStages.len, sizeof(*fileInfosStages.ptr), fileInfoStageCmp);

    struct {Str* ptr; i64 len, cap;} entityNamesDedup = arenaAllocDynarr(arena, Str, fileInfosEntities.len);;
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
            dynarrpush(&entityNamesDedup, newName);
        }
    }

    {
        strbuilderEnumBegin(strbuilder, STR("EntityID"));
        for (i64 ind = 0; ind < entityNamesDedup.len; ind++) {strbuilderEnumAdd(strbuilder, entityNamesDedup.ptr[ind]);}
        strbuilderEnumAdd(strbuilder, STR("Count"));
        strbuilderEnumEnd(strbuilder);
    }

    {
        strbuilderEnumBegin(strbuilder, STR("StageID"));
        u8slice stageAlreadyPresent = arenaAllocAndZeroArray(arena, u8, fileInfosStages.len);
        for (i64 ind = 0; ind < fileInfosStages.len; ind++) {
            FileInfoStage info = fileInfosStages.ptr[ind];
            u8* alreadyDone = stageAlreadyPresent.ptr + info.index;
            if (!*alreadyDone) {
                *alreadyDone = true;
                Str name = strfmt(arena, "Stage%d", info.index);
                strbuilderEnumAdd(strbuilder, name);
            }
        }
        strbuilderEnumAdd(strbuilder, STR("Count"));
        strbuilderEnumEnd(strbuilder);
    }

    struct {Str* ptr; i64 len;} animationNames = arenaAllocArray(arena, Str, fileInfosEntities.len);

    strbuilderEnumBegin(strbuilder, STR("AnimationID"));
    for (i64 ind = 0; ind < fileInfosEntities.len; ind++) {
        FileInfoEntity info = fileInfosEntities.ptr[ind];
        Str animationName = strfmt(arena, "%.*s_%.*s", LIT(info.names.entity), LIT(info.names.animation));
        strbuilderEnumAdd(strbuilder, animationName);
        animationNames.ptr[ind] = animationName;
    }
    strbuilderEnumAdd(strbuilder, STR("Count"));
    strbuilderEnumEnd(strbuilder);

    strbuilderEnumBegin(strbuilder, STR("AtlasID"));
    strbuilderEnumAdd(strbuilder, STR("Whitepx"));
    strbuilderEnumAdd(strbuilder, STR("Font"));
    for (i64 ind = 0; ind < fileInfosEntities.len; ind++) {
        FileInfoEntity info = fileInfosEntities.ptr[ind];
        Str animationName = animationNames.ptr[ind];
        for (int frameIndex = 0; frameIndex < info.content->frameCount; frameIndex++) {
            Str atlasName = strfmt(arena, "%.*s_frame%d", LIT(animationName), frameIndex + 1);
            strbuilderEnumAdd(strbuilder, atlasName);
        }
    }
    for (i64 ind = 0; ind < fileInfosStages.len; ind++) {
        FileInfoStage info = fileInfosStages.ptr[ind];
        Str atlasName = strfmt(arena, "Stage%d_variant%d", info.index, info.variant);
        strbuilderEnumAdd(strbuilder, atlasName);
    }
    i32 totalAtlasTextureCount = strbuilder->addCount;
    strbuilderEnumAdd(strbuilder, STR("Count"));
    strbuilderEnumEnd(strbuilder);

    strbuilderTableBegin(strbuilder, STR("int"), STR("globalFirstAtlasIDEntities"), STR("EntityID_Count"));
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

    strbuilderTableBegin(strbuilder, STR("int"), STR("globalFirstAtlasIDStages"), STR("StageID_Count"));
    {
        i32 currentStageIndex = -1;
        for (i64 ind = 0; ind < fileInfosStages.len; ind++) {
            FileInfoStage info = fileInfosStages.ptr[ind];
            if (currentStageIndex != info.index) {
                Str key = strfmt(arena, "StageID_Stage%d", info.index);
                Str value = strfmt(arena, "AtlasID_Stage%d_variant%d", info.index, info.variant);
                builderTableAdd(strbuilder, key, value);
                currentStageIndex = info.index;
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

            Str animationName = animationNames.ptr[ind];
            Str key = strfmt(arena, "AnimationID_%.*s", LIT(animationName));
            Str value = strfmt(arena, "%d", currentCumulativeCount);
            builderTableAdd(strbuilder, key, value);

            currentCumulativeCount += info.content->frameCount;
        }
    }
    strbuilderTableEnd(strbuilder);

    strbuilderTableBegin(strbuilder, STR("int"), STR("globalStageVariantCounts"), STR("StageID_Count"));
    {
        i32 currentVariantCount = 0;
        i32 currentStageIndex = -1;
        for (i64 ind = 0; ind < fileInfosStages.len; ind++) {
            FileInfoStage info = fileInfosStages.ptr[ind];
            if (info.index != currentStageIndex || ind == fileInfosStages.len - 1) {
                if (currentStageIndex != -1) {
                    Str key = strfmt(arena, "StageID_Stage%d", currentStageIndex);
                    Str value = strfmt(arena, "%d", ind == fileInfosStages.len - 1 ? currentVariantCount + 1 : currentVariantCount);
                    builderTableAdd(strbuilder, key, value);
                }

                currentVariantCount = 0;
                currentStageIndex = info.index;
            }
            currentVariantCount += 1;
        }
    }
    strbuilderTableEnd(strbuilder);

    struct {Texture* ptr; i64 len, cap;} atlasTextures = arenaAllocDynarr(arena, Texture, totalAtlasTextureCount);

    {
        u32 whitePxTexData[] = {0xffff'ffff};
        Texture whitePxTex = {.w = 1, .h = 1, .pixels = whitePxTexData};
        dynarrpush(&atlasTextures, whitePxTex);
    }

    struct {i32 glyphCount, glyphW, glyphH, gapW; Texture tex;} font = {.glyphCount = 128, .glyphW = 8, .glyphH = 16, .gapW = 2};
    {
        font.tex.w = font.glyphW * font.glyphCount + (font.glyphCount - 1) * font.gapW;
        font.tex.h = font.glyphH;
        font.tex.pixels = ((u32slice) arenaAllocArray(arena, u32, font.tex.w * font.tex.h)).ptr;
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

        dynarrpush(&atlasTextures, font.tex);
    }

    struct {AtlasLocation* ptr; i64 len;} atlasLocations = arenaAllocAndZeroArray(arena, AtlasLocation, totalAtlasTextureCount);
    struct {f32* ptr; i64 len, cap;} allAnimationDurations = arenaAllocDynarr(arena, f32, 4096);;
    struct {Animation* ptr; i32 len;} animations = arenaAllocArray(arena, Animation, fileInfosEntities.len);

    struct {LayerTypeInfo* ptr; i32 len, cap;} layerTypeInfo = arenaAllocDynarr(arena, LayerTypeInfo, 1024);

    struct {Rect* ptr; i32 len, cap;} collisionRects = arenaAllocDynarr(arena, Rect, entityNamesDedup.len);

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
            dynarrpush(&allAnimationDurations, frame->frameDurationMS);

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
                            dynarrpush(&layerTypeInfo, LayerTypeInfo_Collision);
                        } else {
                            dynarrpush(&layerTypeInfo, LayerTypeInfo_None);
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
                                    AtlasLocation* thisLoc = atlasLocations.ptr + atlasTextures.len;
                                    V2 thisOffset = v2sub(firstFrameArtOffset, artOffset);
                                    thisLoc->offset = thisOffset; // TODO(khvorov) Do we ever need an offset that's not in the top-left?
                                }

                                dynarrpush(&atlasTextures, texture);
                            } break;

                            case LayerTypeInfo_Collision: {
                                foundCollision = true;
                                Rect collision = {{chunk->cel.posX, chunk->cel.posY}, {chunk->cel.width, chunk->cel.height}};
                                dynarrpush(&collisionRects, collision);
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

    assert(collisionRects.len == collisionRects.cap);

    struct {V2* ptr; i32 len, cap;} collisionPoints = arenaAllocDynarr(arena, V2, 1024);;
    struct {V2slice* ptr; i32 len, cap;} collisionPolys = arenaAllocDynarr(arena, V2slice, 1024);;
    struct {V2sliceslice* ptr; i64 len, cap;} stages = arenaAllocDynarr(arena, V2sliceslice, 1024);;

    for (i32 fileInfoIndex = 0; fileInfoIndex < fileInfosStages.len; fileInfoIndex++) {
        FileInfoStage* info = fileInfosStages.ptr + fileInfoIndex;
        AseFile* ase = info->content;
        AseFrame* frame = ase->frames;

        Texture canvas = {.w = ase->width, .h = ase->height};
        canvas.pixels = ((u32slice)arenaAllocAndZeroArray(arena, u32, (canvas.w + 2) * (canvas.h + 2))).ptr;
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

        assert(ase->frameCount == 1);
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

                        V2 artOffset = (V2) {chunk->cel.posX, chunk->cel.posY};
                        AtlasLocation* thisLoc = atlasLocations.ptr + atlasTextures.len;
                        thisLoc->offset = artOffset;

                        dynarrpush(&atlasTextures, texture);
                    } break;

                    case AseChunkType_ColorProfile:
                    case AseChunkType_Layer:
                    case AseChunkType_Palette:
                    case AseChunkType_OldPalette:
                        break;

                    default: unimplemented(); break;
                }

                chunk = (void*)chunk + chunk->size;
            }
        }

        // NOTE(khvorov) Write the canvas out to a string for debugging
        if (false) {
            i32 tempStrPitch = (canvas.w + 3);
            i32 tempStrLen = tempStrPitch * (canvas.h + 2);
            char* tempStr = ((Str)arenaAllocArray(arena, char, tempStrLen)).ptr;
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

        V2sliceslice stage = {.ptr = collisionPolys.ptr + collisionPolys.len};
        i64 collisionPolysLenBefore = collisionPolys.len;

        u8slice pixelsTouched = arenaAllocAndZeroArray(arena, u8, (canvas.w + 1) * (canvas.h + 1));
        i32 pixelsTouchedPitch = canvas.w + 1;
        for (bool allPixelsTouched = false; !allPixelsTouched;) {
            allPixelsTouched = true;

            CornerInfo firstCorner = {};
            for (i32 rowEdge = 0; rowEdge <= canvas.h; rowEdge += 1) {
                for (i32 colEdge = 0; colEdge <= canvas.w; colEdge += 1) {
                    u8* touchedBefore = pixelsTouched.ptr + rowEdge * pixelsTouchedPitch + colEdge;
                    if (!*touchedBefore) {
                        *touchedBefore = true;
                        allPixelsTouched = false;

                        CornerInfo cornerInfo = getCornerInfo(rowEdge, colEdge, canvasPitch, canvas);
                        if (cornerInfo.isCorner) {
                            firstCorner = cornerInfo;
                            goto breakFirstCornerSearch;
                        }
                    }
                }
            }
            breakFirstCornerSearch:

            if (firstCorner.isCorner) {
                assert(!allPixelsTouched);

                V2slice collisionPoly = {.ptr = collisionPoints.ptr + collisionPoints.len};
                i32 collisionPointsLenBeforeShapeWalk = collisionPoints.len;
                for (CornerInfo currentCorner = firstCorner;;) {
                    V2 currentCornerPosWorld = {currentCorner.pos.x, canvas.h - currentCorner.pos.y};
                    dynarrpush(&collisionPoints, currentCornerPosWorld);

                    CornerInfo nextCorner;
                    i32 rowEdge = currentCorner.pos.y;
                    i32 colEdge = currentCorner.pos.x;
                    for (;;) {
                        rowEdge += currentCorner.nextDir.y;
                        colEdge += currentCorner.nextDir.x;
                        assert(rowEdge >= 0 && rowEdge <= canvas.h);
                        assert(colEdge >= 0 && colEdge <= canvas.w);

                        u8* touchedBefore = pixelsTouched.ptr + rowEdge * pixelsTouchedPitch + colEdge;
                        if (*touchedBefore) {
                            assert(v2ieq((V2i) {colEdge, rowEdge}, firstCorner.pos));
                            goto breakShapeWalk;
                        } else {
                            *touchedBefore = true;

                            CornerInfo cornerInfo = getCornerInfo(rowEdge, colEdge, canvasPitch, canvas);
                            if (cornerInfo.isCorner) {
                                nextCorner = cornerInfo;
                                goto breakNextCornerSearch;
                            }
                        }
                    }
                    breakNextCornerSearch:
                    assert(nextCorner.isCorner);

                    currentCorner = nextCorner;
                }
                breakShapeWalk:

                collisionPoly.len = collisionPoints.len - collisionPointsLenBeforeShapeWalk;
                dynarrpush(&collisionPolys, collisionPoly);
            }
        }

        stage.len = collisionPolys.len - collisionPolysLenBefore;
        dynarrpush(&stages, stage);
    }

    assert(atlasTextures.len == atlasTextures.cap);

    Texture atlas = {};
    {
        struct {stbrp_rect* ptr; i64 len;} rectsToPack = arenaAllocArray(arena, stbrp_rect, atlasTextures.len);
        for (i32 texInd = 0; texInd < atlasTextures.len; texInd++) {
            Texture* texture = atlasTextures.ptr + texInd;
            stbrp_rect* rect = rectsToPack.ptr + texInd;
            rect->id = texInd;
            rect->w = texture->w + 2;
            rect->h = texture->h + 2;
        }

        {
            struct {stbrp_node* ptr; i32 len;} nodes = arenaAllocArray(arena, stbrp_node, 4096);
            stbrp_context ctx = {};
            stbrp_init_target(&ctx, nodes.len, INT_MAX, nodes.ptr, nodes.len);
            int allRectsPacked = stbrp_pack_rects(&ctx, rectsToPack.ptr, rectsToPack.len);
            assert(allRectsPacked);
        }

        for (i32 texInd = 0; texInd < atlasTextures.len; texInd++) {
            stbrp_rect* rect = rectsToPack.ptr + texInd;
            assert(rect->was_packed);
            atlas.w = max(atlas.w, rect->x + rect->w);
            atlas.h = max(atlas.h, rect->y + rect->h);
        }
        atlas.pixels = ((u32slice) arenaAllocArray(arena, u32, atlas.w * atlas.h)).ptr;

        for (i32 texInd = 0; texInd < atlasTextures.len; texInd++) {
            stbrp_rect* rect = rectsToPack.ptr + texInd;
            Texture* texture = atlasTextures.ptr + rect->id;

            for (i32 texRow = 0; texRow < texture->h; texRow++) {
                u32* src = texture->pixels + texRow * texture->w;
                u32* dest = atlas.pixels + (texRow + (rect->y + 1)) * atlas.w + (rect->x + 1);
                memcpy(dest, src, texture->w * sizeof(u32));
            }

            AtlasLocation* loc = atlasLocations.ptr + texInd;
            loc->rect = (Rect) {{rect->x, rect->y}, {rect->w, rect->h}};
        }
    }

    strbuilderEnumBegin(strbuilder, STR("ShaderID"));
    struct {u8slice* ptr; i32 len, cap;} shaders = arenaAllocDynarr(arena, u8slice, 1024);
    struct {u8* ptr; i64 len, cap;} allShaderData = arenaAllocDynarr(arena, u8, 50 * Megabyte);
    {
        WIN32_FIND_DATAA findData = {};
        HANDLE findHandle = FindFirstFileA("code/*.hlsl", &findData);
        assert(findHandle != INVALID_HANDLE_VALUE);
        do {
            Str shaderFilename = strfmt(arena, "%s", findData.cFileName);
            Str shaderSrcPath = strfmt(arena, "code/%.*s", LIT(shaderFilename));

            u8slice shaderSrc = readEntireFile(arena, shaderSrcPath);
            Str entryPoints[] = {STR("vs"), STR("ps")};
            UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
            for (u32 entryPointIndex = 0; entryPointIndex < carrayCount(entryPoints); entryPointIndex++) {
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
                u8slice shaderDataOg = {ID3D10Blob_GetBufferPointer(vblob), ID3D10Blob_GetBufferSize(vblob)};
                u8slice shaderDataCopy = {allShaderData.ptr + allShaderData.len, shaderDataOg.len};
                dynarrpusharr(&allShaderData, shaderDataOg);
                dynarrpush(&shaders, shaderDataCopy);
                Str fileNameNoExt = strslice(shaderFilename, 0, shaderFilename.len - (sizeof(".hlsl") - 1));
                Str enumLabel = strfmt(arena, "%.*s_%.*s", LIT(fileNameNoExt), LIT(entryPoint));
                strbuilderEnumAdd(strbuilder, enumLabel);
            }

        } while (FindNextFileA(findHandle, &findData));
    }
    strbuilderEnumAdd(strbuilder, STR("Count"));
    strbuilderEnumEnd(strbuilder);

    BinBuilder binb = arenaAllocDynarr(arena, u8, 50 * Megabyte);
    AssetDataBuilder datab_ = {.bin = &binb, .str = strbuilder, .ptrfixes = arenaAllocDynarr(arena, PtrFix, 1024)};
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
    assetAddArrField(datab, "AtlasLocation locations", atlasLocations.ptr, totalAtlasTextureCount);
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

    // NOTE: executable and its data file are assumed to have the same name and lie in the same directory
    // so the executable can find its data file by querying its name and changing the extension to .dat
    Str exename = STR("game_windows");
    writeEntireFile(arena, strfmt(arena, "build/%.*s.dat", LIT(exename)), binb.ptr, binb.len);
    executeCommandLine(strfmt(arena, "clang code/game_windows.c -std=c2x -march=native -Wall -Wextra -g -o build/%.*s.exe", LIT(exename)));

    executeCommandLine(STR("clang code/test_windows.c -std=c2x -march=native -Wall -Wextra -g -o build/test_windows.exe"));
    executeCommandLine(STR("clang code/bench_windows.c -std=c2x -march=native -Wall -Wextra -g -o build/bench_windows.exe"));

    writeToStdout(strfmt(arena, "finished in %.1fms\n", getMsFromStart(&timer)));
    return 0;
}