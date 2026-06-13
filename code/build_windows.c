#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define assert(cond) do { if (cond) {} else {char msg[] = __FILE__ ":" STRINGIFY(__LINE__) ":1: error: assertion failure\n"; WriteFile((HANDLE)STD_OUTPUT_HANDLE, msg, sizeof(msg) - 1, 0, 0); __debugbreak();} } while (0)
#define assertHR(hr) assert(SUCCEEDED(hr))

#include "build.c"

#include <d3dcompiler.h>
#pragma comment (lib, "d3dcompiler")

//
// SECTION Files IO
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

//
// SECTION Process creation
//

static HANDLE globalProcessHandles[128];
static u64 globalProcessHandlesCount;
static void executeCommandLine(Str cmd) {
    writeToStdout(cmd);
    writeToStdout(STR("\n"));
    STARTUPINFOA startupInfo = {.cb = sizeof(startupInfo)};
    PROCESS_INFORMATION procInfo = {};
    BOOL CreateProcessResult = CreateProcessA(0, cmd.ptr, 0, 0, TRUE, 0, 0, 0, &startupInfo, &procInfo);
    assert(CreateProcessResult);
    assert(globalProcessHandlesCount <= carrayCount(globalProcessHandles));
    globalProcessHandles[globalProcessHandlesCount++] = procInfo.hProcess;
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

    struct {AsepriteDataFile* ptr; i64 len, cap;} dataFiles = arenaAllocDynarr(arena, AsepriteDataFile, 1024);
    {
        WIN32_FIND_DATAA findData = {};
        HANDLE findHandle = FindFirstFileA("data/*.aseprite", &findData);
        assert(findHandle != INVALID_HANDLE_VALUE);
        do {
            Str filefullname = strfmt(arena, "%s", findData.cFileName);
            Str path = strfmt(arena, "data/%.*s", LIT(filefullname));
            u8slice fileContent = readEntireFile(arena, path);
            AsepriteDataFile file = {.fullname = filefullname, .content = fileContent};
            dynarrpush(&dataFiles, file);
        } while (FindNextFileA(findHandle, &findData));
    }

    struct {PlatformShader* ptr; i64 len, cap;} platformShaders = arenaAllocDynarr(arena, PlatformShader, 1024);
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
                Str fileNameNoExt = strslice(shaderFilename, 0, shaderFilename.len - (sizeof(".hlsl") - 1));
                Str enumLabel = strfmt(arena, "%.*s_%.*s", LIT(fileNameNoExt), LIT(entryPoint));
                PlatformShader platformShader = {.label = enumLabel, .content = shaderDataOg};
                dynarrpush(&platformShaders, platformShader);
            }

        } while (FindNextFileA(findHandle, &findData));
    }

    Platform platform = {.writeEntireFile = writeEntireFile, .executeCommandLine = executeCommandLine};
    build(arena, (DataFiles*)&dataFiles, (PlatformShaders*)&platformShaders, STR("game_windows"), platform);

    DWORD WaitForMultibpleObjectsResult = WaitForMultipleObjects(globalProcessHandlesCount, globalProcessHandles, TRUE, INFINITE);
    assert(WaitForMultibpleObjectsResult >= WAIT_OBJECT_0 && WaitForMultibpleObjectsResult < globalProcessHandlesCount - 1);
    for (u64 index = 0; index < globalProcessHandlesCount; index++) {
        DWORD exitCode = 0;
        BOOL GetExitCodeProcessResult = GetExitCodeProcess(globalProcessHandles[index], &exitCode);
        assert(GetExitCodeProcessResult);
        assert(exitCode == 0);
    }

    writeToStdout(strfmt(arena, "finished in %.1fms\n", getMsFromStart(&timer)));
    return 0;
}