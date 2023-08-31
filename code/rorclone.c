#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wgnu-variable-sized-type-not-at-end"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define assert(cond) do { if (cond) {} else __debugbreak(); } while (0)
#define assertHR(hr) assert(SUCCEEDED(hr))
#define STR(x) ((Str){x, sizeof(x) - 1})
#define LIT(x) (int)x.len, x.ptr
#define unused(x) ((x) = (x))
#define arrayCount(x) (sizeof(x) / sizeof((x)[0]))
#define arrpush(arr, val) assert((arr).len < (arr).cap); (arr).ptr[(arr).len++] = (val)
#define arenaAllocArray(arena, type, count) ((type*)arenaAlloc((arena), sizeof(type) * (count)))
#define tempMemoryBlock(arena_) for (TempMemory _temp_ = beginTempMemory(arena_); _temp_.arena; endTempMemory(&_temp_))

#define STB_SPRINTF_STATIC
#define STB_SPRINTF_IMPLEMENTATION
#include "stb_sprintf.h"

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

//
// SECTION String
//

typedef struct Str {
    char* ptr;
    i64 len;
} Str;

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
// SECTION Misc
//

typedef struct V2 {
    f32 x, y;
} V2;

typedef struct Rect {
    V2 topleft;
    V2 dim;
} Rect;

typedef struct Texture {
    u32* pixels;
    i32 w, h;
} Texture;

//
// SECTION Platform
//

// NOTE(khvorov) D3D11 initialisation code is based on this
// https://gist.github.com/mmozeiko/5e727f845db182d468a34d524508ad5f

#pragma comment (lib, "gdi32")
#pragma comment (lib, "user32")
#pragma comment (lib, "dxguid")
#pragma comment (lib, "d3d11")

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgidebug.h>

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

static u8arr readEntireFile(Str path, Arena* arena) {
    Str path0 = {};
    tempMemoryBlock(arena) {path0 = strfmt(arena, "%.*s", LIT(path));}

    HANDLE hfile = CreateFileA(
        path0.ptr,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        0
    );
    assert(hfile != INVALID_HANDLE_VALUE);

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

static ID3D11Buffer* d3d11CreateDynBuffer(ID3D11Device* device, i64 size, D3D11_BIND_FLAG flag) {
    D3D11_BUFFER_DESC desc = {
        .ByteWidth = size,
        .Usage = D3D11_USAGE_DYNAMIC,
        .BindFlags = flag,
        .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
    };

    ID3D11Buffer* result = 0;
    HRESULT ID3D11Device_CreateBufferResult = ID3D11Device_CreateBuffer(device, &desc, 0, &result);
    assertHR(ID3D11Device_CreateBufferResult);

    return result;
}

static void fatalError(const char* message) {
    MessageBoxA(NULL, message, "Error", MB_ICONEXCLAMATION);
    ExitProcess(0);
}

static LRESULT CALLBACK windowProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wparam, lparam);
}

// https://devblogs.microsoft.com/oldnewthing/20100412-00/?p=14353
void toggleFullscreen(HWND hwnd, WINDOWPLACEMENT* wpPrev) {
    DWORD dwStyle = GetWindowLong(hwnd, GWL_STYLE);

    if (dwStyle & WS_OVERLAPPEDWINDOW) {
        MONITORINFO mi = { .cbSize = sizeof(mi) };
        if (GetWindowPlacement(hwnd, wpPrev) && GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLong(hwnd, GWL_STYLE, dwStyle & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(
                hwnd, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED
            );
        }
    } else {
        SetWindowLong(hwnd, GWL_STYLE, dwStyle | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, wpPrev);
        SetWindowPos(
            hwnd, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED
        );
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previnstance, LPSTR cmdline, int cmdshow) {
    unused(previnstance);
    unused(cmdline);
    unused(cmdshow);

    Arena perm_ = {};
    Arena scratch_ = {};
    struct {Arena* perm; Arena* scratch;} memory = {&perm_, &scratch_};
    {
        i64 size = 1 * 1024 * 1024 * 1024;
        void* ptr = VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        assert(ptr);
        memory.perm->base = ptr;
        memory.perm->size = size / 4;
        memory.scratch->base = ptr + memory.perm->size;
        memory.scratch->size = size - memory.perm->size;
    }

    if (false) benchAlignment(memory.scratch);

    struct {
        HWND hwnd;
        DWORD w, h;
        WINDOWPLACEMENT wpPrev;
    } window = {.wpPrev = {.length = sizeof(WINDOWPLACEMENT)}};

    {
        WNDCLASSEXW windowClass = {
            .cbSize = sizeof(windowClass),
            .lpfnWndProc = windowProc,
            .hInstance = instance,
            .hIcon = LoadIcon(NULL, IDI_APPLICATION),
            .hCursor = LoadCursor(NULL, IDC_ARROW),
            .lpszClassName = L"rorclone_window_class",
        };

        {
            ATOM atom = RegisterClassExW(&windowClass);
            assert(atom && "Failed to register window class");
        }

        // NOTE(khvorov) According to Martins:
        // WS_EX_NOREDIRECTIONBITMAP flag here is needed to fix ugly bug with Windows 10
        // when window is resized and DXGI swap chain uses FLIP presentation model
        window.hwnd = CreateWindowExW(
            WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP,
            windowClass.lpszClassName, L"rorclone",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            NULL, NULL, windowClass.hInstance, NULL
        );
        assert(window.hwnd && "Failed to create window");

        toggleFullscreen(window.hwnd, &window.wpPrev);
    }

    typedef struct RectInstance {
        Rect screen;
        Rect tex;
    } RectInstance;

    typedef struct CBuffer {
        V2 windowDim;
        V2 texDim;
        V2 cameraPos;
        f32 cameraHalfSpanX;
        f32 cameraHeightOverWidth;
    } CBuffer;

    struct {
        ID3D11Device* device;
        ID3D11DeviceContext* context;
        IDXGISwapChain1* swapChain;
        struct {ID3D11Buffer* buf; struct {RectInstance* ptr; i64 len; i64 cap;} storage;} rects;
        struct {ID3D11Buffer* buf; CBuffer storage;} cbuffer;
        ID3D11InputLayout* layout;
        ID3D11VertexShader* vshader;
        ID3D11PixelShader* pshader;
        ID3D11ShaderResourceView* textureView;
        ID3D11SamplerState* sampler;
        ID3D11BlendState* blendState;
        ID3D11RasterizerState* rasterizerState;
        ID3D11RenderTargetView* rtView;
    } d3d11 = {};

    {
        UINT flags = 0;
        #ifndef NDEBUG
            flags |= D3D11_CREATE_DEVICE_DEBUG;
        #endif
        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
        HRESULT D3D11CreateDeviceResult = D3D11CreateDevice(
            NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, levels, arrayCount(levels),
            D3D11_SDK_VERSION, &d3d11.device, NULL, &d3d11.context
        );
        assertHR(D3D11CreateDeviceResult);
    }

    #ifndef NDEBUG
    {
        ID3D11InfoQueue* info;
        ID3D11Device_QueryInterface(d3d11.device, &IID_ID3D11InfoQueue, (void**)&info);
        ID3D11InfoQueue_SetBreakOnSeverity(info, D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        ID3D11InfoQueue_SetBreakOnSeverity(info, D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
        ID3D11InfoQueue_SetBreakOnSeverity(info, D3D11_MESSAGE_SEVERITY_WARNING, TRUE);
        ID3D11InfoQueue_Release(info);
    }

    {
        HMODULE dxgiDebug = LoadLibraryA("dxgidebug.dll");
        if (dxgiDebug != NULL)
        {
            HRESULT (WINAPI *dxgiGetDebugInterface)(REFIID riid, void** ppDebug);
            *(FARPROC*)&dxgiGetDebugInterface = GetProcAddress(dxgiDebug, "DXGIGetDebugInterface");

            IDXGIInfoQueue* dxgiInfo;
            assertHR(dxgiGetDebugInterface(&IID_IDXGIInfoQueue, (void**)&dxgiInfo));
            IDXGIInfoQueue_SetBreakOnSeverity(dxgiInfo, DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            IDXGIInfoQueue_SetBreakOnSeverity(dxgiInfo, DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, TRUE);
            IDXGIInfoQueue_SetBreakOnSeverity(dxgiInfo, DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING, TRUE);
            IDXGIInfoQueue_Release(dxgiInfo);
        }
    }
    #endif

    {
        IDXGIDevice* dxgiDevice = 0;
        HRESULT ID3D11Device_QueryInterfaceResult = ID3D11Device_QueryInterface(d3d11.device, &IID_IDXGIDevice, (void**)&dxgiDevice);
        assertHR(ID3D11Device_QueryInterfaceResult);

        IDXGIAdapter* dxgiAdapter = 0;
        HRESULT IDXGIDevice_GetAdapterResult = IDXGIDevice_GetAdapter(dxgiDevice, &dxgiAdapter);
        assertHR(IDXGIDevice_GetAdapterResult);

        IDXGIFactory2* factory = 0;
        HRESULT IDXGIAdapter_GetParentResult = IDXGIAdapter_GetParent(dxgiAdapter, &IID_IDXGIFactory2, (void**)&factory);
        assertHR(IDXGIAdapter_GetParentResult);

        DXGI_SWAP_CHAIN_DESC1 desc = {
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .SampleDesc = { 1, 0 },
            .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
            .BufferCount = 2,
            .Scaling = DXGI_SCALING_NONE,
            .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        };

        HRESULT IDXGIFactory2_CreateSwapChainForHwndResult = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown*)d3d11.device, window.hwnd, &desc, NULL, NULL, &d3d11.swapChain);
        assertHR(IDXGIFactory2_CreateSwapChainForHwndResult);

        IDXGIFactory_MakeWindowAssociation(factory, window.hwnd, DXGI_MWA_NO_ALT_ENTER);

        IDXGIFactory2_Release(factory);
        IDXGIAdapter_Release(dxgiAdapter);
        IDXGIDevice_Release(dxgiDevice);
    }

    {
        d3d11.rects.storage.cap = 1024;
        d3d11.rects.buf = d3d11CreateDynBuffer(d3d11.device, sizeof(*d3d11.rects.storage.ptr) * d3d11.rects.storage.cap, D3D11_BIND_VERTEX_BUFFER);
        d3d11.rects.storage.ptr = arenaAllocArray(memory.perm, RectInstance, d3d11.rects.storage.cap);
    }

    tempMemoryBlock(memory.scratch) {
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            {"SCREEN_POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(RectInstance, screen.topleft), D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"SCREEN_DIM", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(RectInstance, screen.dim),     D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"TEX_POS",    0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(RectInstance, tex.topleft),    D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"TEX_DIM",    0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(RectInstance, tex.dim),        D3D11_INPUT_PER_INSTANCE_DATA, 1},
        };        

        u8arr shadervs = readEntireFile(STR("data/rorclone.hlsl_vs.bin"), memory.scratch);
        u8arr shaderps = readEntireFile(STR("data/rorclone.hlsl_ps.bin"), memory.scratch);

        HRESULT ID3D11Device_CreateVertexShaderResult = ID3D11Device_CreateVertexShader(d3d11.device, shadervs.ptr, shadervs.len, NULL, &d3d11.vshader);
        assertHR(ID3D11Device_CreateVertexShaderResult);
        HRESULT ID3D11Device_CreatePixelShaderResult = ID3D11Device_CreatePixelShader(d3d11.device, shaderps.ptr, shaderps.len, NULL, &d3d11.pshader);
        assertHR(ID3D11Device_CreatePixelShaderResult);
        HRESULT ID3D11Device_CreateInputLayoutResult = ID3D11Device_CreateInputLayout(d3d11.device, desc, arrayCount(desc), shadervs.ptr, shadervs.len, &d3d11.layout);
        assertHR(ID3D11Device_CreateInputLayoutResult);
    }

    struct {Rect* ptr; i64 len;} atlasLocations = {};
    tempMemoryBlock(memory.scratch) {

        struct { Texture* ptr; i32 len; i32 cap; } textures = {.cap = 100};
        textures.ptr = arenaAllocArray(memory.scratch, Texture, textures.cap);
        {
            WIN32_FIND_DATAA findData = {};
            HANDLE findHandle = FindFirstFileA("data/*.aseprite", &findData);
            assert(findHandle != INVALID_HANDLE_VALUE);
            do {
                char* name = findData.cFileName;
                Str path = strfmt(memory.scratch, "data/%s", name);
                u8arr fileContent = readEntireFile(path, memory.scratch);

                AseFile* ase = (AseFile*)fileContent.ptr;
                assert(ase->magic == 0xA5E0);
                AseFrame* frame = ase->frames;
                for (i32 frameIndex = 0; frameIndex < ase->frameCount; frameIndex++) {
                    assert(frame->magic == 0xF1FA);
                    AseChunk* chunk = frame->chunks;
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
                                u32 nonDataSize = sizeof(u32) + sizeof(AseChunkType) + sizeof(AseChunkCel);
                                assert(chunk->size >= nonDataSize);
                                void* compressedData = (void*)chunk + nonDataSize;
                                u32 compressedDataSize = chunk->size - nonDataSize;
                                texture.w = chunk->cel.width;
                                texture.h = chunk->cel.height;
                                i32 pixelsInTex = texture.w * texture.h;
                                texture.pixels = arenaAllocArray(memory.scratch, u32, pixelsInTex);
                                i32 bytesInTex = pixelsInTex * sizeof(u32);
                                int decodeResult = stbi_zlib_decode_buffer((char*)texture.pixels, bytesInTex, compressedData, compressedDataSize);
                                assert(decodeResult == bytesInTex);
                                arrpush(textures, texture);
                            } break;

                            case AseChunkType_Palette:
                            case AseChunkType_OldPalette:
                                break;

                            default: assert(!"unimplemented"); break;
                        }

                        {
                            void* temp = chunk;
                            temp += chunk->size;
                            chunk = temp;
                        }
                    }
                    frame = (AseFrame*)chunk;
                }
            } while (FindNextFileA(findHandle, &findData));
        }

        Texture* texturesTrimmed = arenaAllocArray(memory.scratch, Texture, textures.len);
        for (i32 texInd = 0; texInd < textures.len; texInd++) {
            Texture* texture = textures.ptr + texInd;
            i32 left = texture->w;
            i32 right = 0;
            i32 top = texture->h;
            i32 bottom = 0;
            for (i32 row = 0; row < texture->h; row++) {
                for (i32 col = 0; col < texture->w; col++) {
                    u32 px = texture->pixels[row * texture->w + col];
                    if (px) {
                        left = min(left, col);
                        right = max(right, col);
                        top = min(top, row);
                        bottom = max(bottom, row);
                    }
                }
            }

            Texture* trimmed = texturesTrimmed + texInd;
            trimmed->pixels = texture->pixels + top * texture->w + left;
            trimmed->w = right - left + 1;
            trimmed->h = bottom - top + 1;
        }

        stbrp_rect* rectsToPack = arenaAllocArray(memory.scratch, stbrp_rect, textures.len);
        for (i32 texInd = 0; texInd < textures.len; texInd++) {
            Texture* texture = texturesTrimmed + texInd;
            stbrp_rect* rect = rectsToPack + texInd;
            rect->id = texInd;
            rect->w = texture->w + 1;
            rect->h = texture->h + 1;
        }

        {
            struct {stbrp_node* ptr; i32 len;} nodes = {.len = 4096};
            nodes.ptr = arenaAllocArray(memory.scratch, stbrp_node, nodes.len);
            stbrp_context ctx = {};
            stbrp_init_target(&ctx, nodes.len, INT_MAX, nodes.ptr, nodes.len);
            int allRectsPacked = stbrp_pack_rects(&ctx, rectsToPack, textures.len);
            assert(allRectsPacked);
        }

        Texture atlas = {};
        for (i32 texInd = 0; texInd < textures.len; texInd++) {
            stbrp_rect* rect = rectsToPack + texInd;
            assert(rect->was_packed);
            atlas.w = max(atlas.w, rect->x + rect->w);
            atlas.h = max(atlas.h, rect->y + rect->h);
        }
        atlas.w += 1;
        atlas.h += 1;
        atlas.pixels = arenaAllocArray(memory.scratch, u32, atlas.w * atlas.h);

        atlasLocations.len = textures.len;
        atlasLocations.ptr = arenaAllocArray(memory.perm, Rect, atlasLocations.len);
        for (i32 texInd = 0; texInd < textures.len; texInd++) {
            stbrp_rect* rect = rectsToPack + texInd;
            Texture* texture = textures.ptr + rect->id;
            Texture* trimmed = texturesTrimmed + rect->id;

            for (i32 texRow = 0; texRow < trimmed->h; texRow++) {
                u32* src = trimmed->pixels + texRow * texture->w;
                u32* dest = atlas.pixels + (texRow + (rect->y + 1)) * atlas.w + (rect->x + 1);
                memcpy(dest, src, trimmed->w * sizeof(u32));
            }

            Rect atlasLocation = {{rect->x + 1, rect->y + 1}, {rect->w - 1, rect->h - 1}};
            atlasLocations.ptr[texInd] = atlasLocation;
        }

        D3D11_TEXTURE2D_DESC desc = {
            .Width = atlas.w,
            .Height = atlas.h,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            .SampleDesc = { 1, 0 },
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
        };

        D3D11_SUBRESOURCE_DATA data = {
            .pSysMem = atlas.pixels,
            .SysMemPitch = atlas.w * sizeof(u32),
        };

        ID3D11Texture2D* texture;
        ID3D11Device_CreateTexture2D(d3d11.device, &desc, &data, &texture);
        ID3D11Device_CreateShaderResourceView(d3d11.device, (ID3D11Resource*)texture, NULL, &d3d11.textureView);
        ID3D11Texture2D_Release(texture);

        d3d11.cbuffer.storage.texDim = (V2) {atlas.w, atlas.h};
    }

    {
        D3D11_SAMPLER_DESC desc = {
            .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
            .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
            .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
            .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
        };
        ID3D11Device_CreateSamplerState(d3d11.device, &desc, &d3d11.sampler);
    }

    {
        D3D11_BLEND_DESC desc = {
            .RenderTarget[0] = {
                .BlendEnable = TRUE,
                .SrcBlend = D3D11_BLEND_SRC_ALPHA,
                .DestBlend = D3D11_BLEND_INV_SRC_ALPHA,
                .BlendOp = D3D11_BLEND_OP_ADD,
                .SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA,
                .DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA,
                .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL,
            },
        };
        ID3D11Device_CreateBlendState(d3d11.device, &desc, &d3d11.blendState);
    }

    {
        D3D11_RASTERIZER_DESC desc = {
            .FillMode = D3D11_FILL_SOLID,
            .CullMode = D3D11_CULL_NONE,
        };
        ID3D11Device_CreateRasterizerState(d3d11.device, &desc, &d3d11.rasterizerState);
    }

    {
        d3d11.cbuffer.buf = d3d11CreateDynBuffer(d3d11.device, sizeof(CBuffer), D3D11_BIND_CONSTANT_BUFFER);
    }

    ShowWindow(window.hwnd, SW_SHOWDEFAULT);

    d3d11.cbuffer.storage.cameraHalfSpanX = 10;

    // TODO(khvorov) Better way to move by pixels
    {
        RECT rect = {};
        GetClientRect(window.hwnd, &rect);
        DWORD windowWidth = rect.right - rect.left;
        f32 windowHalfWidth = (f32)windowWidth / 2.0f;
        f32 WUPerPixel = d3d11.cbuffer.storage.cameraHalfSpanX / windowHalfWidth;

        arrpush(d3d11.rects.storage, ((RectInstance) {.screen = {{4 + WUPerPixel, 1}, {4, 4}}, .tex = atlasLocations.ptr[0]}));
        arrpush(d3d11.rects.storage, ((RectInstance) {.screen = {{-2, -3}, {5, 5}}, .tex = atlasLocations.ptr[1]}));
    }

    for (;;) {
        assert(memory.scratch->used == 0);
        assert(memory.scratch->tempCount == 0);
        assert(memory.perm->tempCount == 0);

        for (MSG msg = {}; PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);) {
            switch (msg.message) {
                case WM_MOUSEMOVE:
                case WM_KEYDOWN:
                case WM_KEYUP:
                case WM_SYSKEYUP:
                    break;

                case WM_SYSKEYDOWN: {
                    switch (msg.wParam) {
                        case VK_RETURN: toggleFullscreen(window.hwnd, &window.wpPrev); break;
                        case VK_F4: goto breakmainloop; break;
                    }
                } break;

                case WM_QUIT: goto breakmainloop; break;
                default: TranslateMessage(&msg); DispatchMessageW(&msg); break;
            }
        }

        {
            u8 keyboard[256];
            GetKeyboardState(keyboard);
            u8 downMask = (1 << 7);

            f32 deltaX = 0.1f;
            if (keyboard[VK_LEFT] & downMask) {
                d3d11.rects.storage.ptr[0].screen.topleft.x -= deltaX;
            }
            if (keyboard[VK_RIGHT] & downMask) {
                d3d11.rects.storage.ptr[0].screen.topleft.x += deltaX;
            }

            d3d11.cbuffer.storage.cameraPos = (V2) {0, 0};
        }

        {
            RECT rect = {};
            GetClientRect(window.hwnd, &rect);
            DWORD windowWidth = rect.right - rect.left;
            DWORD windowHeight = rect.bottom - rect.top;

            if (d3d11.rtView == NULL || windowWidth != window.w || windowHeight != window.h) {
                if (d3d11.rtView) {
                    ID3D11DeviceContext_ClearState(d3d11.context);
                    ID3D11RenderTargetView_Release(d3d11.rtView);
                    d3d11.rtView = NULL;
                }

                if (windowWidth != 0 && windowHeight != 0) {
                    HRESULT IDXGISwapChain1_ResizeBuffersResult = IDXGISwapChain1_ResizeBuffers(d3d11.swapChain, 0, windowWidth, windowHeight, DXGI_FORMAT_UNKNOWN, 0);
                    if (FAILED(IDXGISwapChain1_ResizeBuffersResult)) {
                        fatalError("Failed to resize swap chain!");
                    }

                    ID3D11Texture2D* backbuffer = 0;
                    IDXGISwapChain1_GetBuffer(d3d11.swapChain, 0, &IID_ID3D11Texture2D, (void**)&backbuffer);

                    // https://gamedev.net/forums/topic/670546-d3d12srgb-buffer-format-for-swap-chain/5243987/
                    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
                    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                    ID3D11Device_CreateRenderTargetView(d3d11.device, (ID3D11Resource*)backbuffer, &rtvDesc, &d3d11.rtView);
                    ID3D11Texture2D_Release(backbuffer);

                    D3D11_TEXTURE2D_DESC depthDesc = {
                        .Width = windowWidth,
                        .Height = windowHeight,
                        .MipLevels = 1,
                        .ArraySize = 1,
                        .Format = DXGI_FORMAT_D32_FLOAT,
                        .SampleDesc = { 1, 0 },
                        .Usage = D3D11_USAGE_DEFAULT,
                        .BindFlags = D3D11_BIND_DEPTH_STENCIL,
                    };

                    ID3D11Texture2D* depth;
                    ID3D11Device_CreateTexture2D(d3d11.device, &depthDesc, NULL, &depth);
                    ID3D11Texture2D_Release(depth);
                }

                window.w = windowWidth;
                window.h = windowHeight;
            }
        }

        d3d11.cbuffer.storage.windowDim = (V2) {window.w, window.h};
        d3d11.cbuffer.storage.cameraHeightOverWidth = (f32)window.h / (f32)window.w;

        if (d3d11.rtView) {
            {
                D3D11_MAPPED_SUBRESOURCE mapped = {};
                d3d11.context->lpVtbl->Map(d3d11.context, (ID3D11Resource*)d3d11.rects.buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
                memcpy(mapped.pData, d3d11.rects.storage.ptr, d3d11.rects.storage.len * sizeof(*d3d11.rects.storage.ptr));
                d3d11.context->lpVtbl->Unmap(d3d11.context, (ID3D11Resource*)d3d11.rects.buf, 0);
            }

            {
                D3D11_MAPPED_SUBRESOURCE mapped = {};
                d3d11.context->lpVtbl->Map(d3d11.context, (ID3D11Resource*)d3d11.cbuffer.buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
                memcpy(mapped.pData, &d3d11.cbuffer.storage, sizeof(d3d11.cbuffer.storage));
                d3d11.context->lpVtbl->Unmap(d3d11.context, (ID3D11Resource*)d3d11.cbuffer.buf, 0);
            }

            D3D11_VIEWPORT viewport = {
                .TopLeftX = 0,
                .TopLeftY = 0,
                .Width = (FLOAT)window.w,
                .Height = (FLOAT)window.h,
                .MinDepth = 0,
                .MaxDepth = 1,
            };

            FLOAT color[] = {0.01, 0.02, 0.03, 0};
            ID3D11DeviceContext_ClearRenderTargetView(d3d11.context, d3d11.rtView, color);

            ID3D11DeviceContext_IASetInputLayout(d3d11.context, d3d11.layout);
            ID3D11DeviceContext_IASetPrimitiveTopology(d3d11.context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            {
                UINT stride = sizeof(RectInstance);
                UINT offset = 0;
                ID3D11DeviceContext_IASetVertexBuffers(d3d11.context, 0, 1, &d3d11.rects.buf, &stride, &offset);
            }

            d3d11.context->lpVtbl->VSSetConstantBuffers(d3d11.context, 0, 1, &d3d11.cbuffer.buf);
            d3d11.context->lpVtbl->PSSetConstantBuffers(d3d11.context, 0, 1, &d3d11.cbuffer.buf);

            ID3D11DeviceContext_VSSetShader(d3d11.context, d3d11.vshader, NULL, 0);

            ID3D11DeviceContext_RSSetViewports(d3d11.context, 1, &viewport);
            ID3D11DeviceContext_RSSetState(d3d11.context, d3d11.rasterizerState);

            ID3D11DeviceContext_PSSetSamplers(d3d11.context, 0, 1, &d3d11.sampler);
            ID3D11DeviceContext_PSSetShaderResources(d3d11.context, 0, 1, &d3d11.textureView);
            ID3D11DeviceContext_PSSetShader(d3d11.context, d3d11.pshader, NULL, 0);

            ID3D11DeviceContext_OMSetBlendState(d3d11.context, d3d11.blendState, NULL, ~0U);
            ID3D11DeviceContext_OMSetRenderTargets(d3d11.context, 1, &d3d11.rtView, 0);

            ID3D11DeviceContext_DrawInstanced(d3d11.context, 4, d3d11.rects.storage.len, 0, 0);
        }

        {
            BOOL vsync = TRUE;
            HRESULT IDXGISwapChain1_PresentResult = IDXGISwapChain1_Present(d3d11.swapChain, vsync ? 1 : 0, 0);
            if (IDXGISwapChain1_PresentResult == DXGI_STATUS_OCCLUDED) {
                if (vsync) {
                    Sleep(10);
                }
            } else if (FAILED(IDXGISwapChain1_PresentResult)) {
                fatalError("Failed to present swap chain! Device lost?");
            }
        }
    }
    breakmainloop:

    d3d11.device->lpVtbl->Release(d3d11.device);
    d3d11.context->lpVtbl->Release(d3d11.context);
    d3d11.swapChain->lpVtbl->Release(d3d11.swapChain);
    d3d11.rects.buf->lpVtbl->Release(d3d11.rects.buf);
    d3d11.cbuffer.buf->lpVtbl->Release(d3d11.cbuffer.buf);
    d3d11.layout->lpVtbl->Release(d3d11.layout);
    d3d11.vshader->lpVtbl->Release(d3d11.vshader);
    d3d11.pshader->lpVtbl->Release(d3d11.pshader);
    d3d11.textureView->lpVtbl->Release(d3d11.textureView);
    d3d11.sampler->lpVtbl->Release(d3d11.sampler);
    d3d11.blendState->lpVtbl->Release(d3d11.blendState);
    d3d11.rasterizerState->lpVtbl->Release(d3d11.rasterizerState);
    if (d3d11.rtView) d3d11.rtView->lpVtbl->Release(d3d11.rtView);

    return 0;
}
