#include "common.c"

//
// SECTION Input
//

typedef enum InputKeyID {
    InputKeyID_None,

    InputKeyID_Left,
    InputKeyID_Right,
    InputKeyID_Up,
    InputKeyID_Down,
    InputKeyID_Accelerate,

    InputKeyID_Count,
} InputKeyID;

typedef struct InputKey {
    bool down;
    i32 halfTransitionCount;
} InputKey;

typedef struct Input {
    InputKey keys[InputKeyID_Count];
} Input;

static bool wasPressed(Input* input, InputKeyID id) {
    InputKey* key = input->keys + id;
    bool result = (key->halfTransitionCount > 1) || (key->down && key->halfTransitionCount > 0);
    return result;
}

//
// SECTION Misc
//

typedef union V4 {
    struct {f32 x, y, z, w;};
    struct {f32 r, g, b, a;};
} V4;

#include "generated.c"

static AtlasID getAtlasID(EntityID entity, AnimationID animation, i32 animationFrame) {
    i32 firstAtlasID = globalFirstAtlasIDEntities[entity];
    i32 animationOffset = globalAnimationCumulativeFrameCounts[animation];
    AtlasID id = (AtlasID)(firstAtlasID + animationOffset + animationFrame);
    return id;
}

typedef struct SpriteCommon {
    // NOTE(khvorov) This is the visual top-left of the sprite in the first frame of any animation
    // (first frames of all animations for a given sprite are aligned with each other)
    V2 topleft;
    i32 mirrorX;
} SpriteCommon;

typedef struct Sprite {
    SpriteCommon common;
    EntityID entity;
    AnimationID animationID;
    i32 animationFrame;
    i32 currentAnimationCounterMS;
    bool isGrounded;
} Sprite;

typedef struct SpriteRect {
    SpriteCommon common;
    AtlasLocation texInAtlas;
    V2 fliplines;
} SpriteRect;

typedef struct ScreenRect {
    Rect scr;
    Rect texInAtlas;
    V4 color;
} ScreenRect;

typedef struct Game {
    AssetData* assets;
    struct {Sprite* ptr; i64 len; i64 cap;} sprites;
    struct {ScreenRect* ptr; i64 len; i64 cap;} screenRects;
    f32 spriteScaleMultiplier;
    V2 cameraPos;
    struct {i32 w, h;} window;
} Game;

static void drawGlyph(Game* game, char glyph, V2 topleft, V4 color) {
    i32 glyphXOffset = (i32)glyph * (game->assets->font.glyphW + 2);
    Rect atlas = game->assets->atlas.locations[AtlasID_Font].rect;
    Rect glyphRect = {.topleft = {atlas.topleft.x + glyphXOffset, atlas.topleft.y}, .dim = {game->assets->font.glyphW + 2, atlas.dim.y}};
    arrpush(game->screenRects, ((ScreenRect) {.scr.topleft = topleft, .scr.dim = glyphRect.dim, .texInAtlas = glyphRect, .color = color}));
}

static void drawStr(Game* game, Str str, V2 topleft, V4 color) {
    V2 currentTopleft = topleft;
    for (i64 charIndex = 0; charIndex < str.len; charIndex++) {
        char ch = str.ptr[charIndex];
        drawGlyph(game, ch, currentTopleft, color);
        currentTopleft.x += game->assets->font.glyphW;
    }
}

static V2 worldToScreenV2(V2 pos, V2 cameraPos, f32 spriteScaleMultiplier, i32 windowW, i32 windowH) {
    V2 posCamera = v2sub(pos, cameraPos);
    V2 posPxFromCenter = v2scale(posCamera, spriteScaleMultiplier);
    posPxFromCenter.y *= -1;
    V2 windowHalfDim = {windowW / 2, windowH / 2};
    V2 posScr = v2add(posPxFromCenter, windowHalfDim);
    return posScr;
}

static Rect worldToScreenRect(Rect rect, V2 cameraPos, f32 spriteScaleMultiplier, i32 windowW, i32 windowH) {
    Rect result = {
        .topleft = worldToScreenV2(rect.topleft, cameraPos, spriteScaleMultiplier, windowW, windowH),
        .dim = v2scale(rect.dim, spriteScaleMultiplier),
    };
    return result;
}

static void drawRect(Game* game, Rect rectWorld, V4 color) {
    Rect topleftRectScr = worldToScreenRect(rectWorld, game->cameraPos, game->spriteScaleMultiplier, game->window.w, game->window.h);
    // TODO(khvorov) All locations in the atlas have a transparent border around them. Should they?
    Rect whitePx = rectShrink(game->assets->atlas.locations[AtlasID_Whitepx].rect, 1);
    ScreenRect pointRect = {.scr = topleftRectScr, .texInAtlas = whitePx, .color = color};
    arrpush(game->screenRects, pointRect);
}
static void drawPoint(Game* game, V2 posWorld, V4 color) { drawRect(game, (Rect) {posWorld, v2fromf32(1)}, color);}

//
// SECTION Platform
//

typedef struct CBuffer {
    V2 windowDim;
    V2 atlasDim;
    V2 cameraPos;
    f32 spriteScaleMultiplier;
    u8 pad[4];
} CBuffer;

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

#define assertHR(hr) assert(SUCCEEDED(hr))
#define D3D11Destroy(x) x->lpVtbl->Release(x)

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

typedef struct D3D11VSPS {
    ID3D11InputLayout* layout;
    ID3D11VertexShader* vshader;
    ID3D11PixelShader* pshader;
    ID3D11Buffer* instanceBuf;
} D3D11VSPS;

static D3D11VSPS d3d11CreateVSPS(ID3D11Device* device, D3D11_INPUT_ELEMENT_DESC* desc, i32 descCount, u8arr shadervs, u8arr shaderps, i64 instanceBufSize) {
    D3D11VSPS result = {};
    HRESULT ID3D11Device_CreateVertexShaderResult = ID3D11Device_CreateVertexShader(device, shadervs.ptr, shadervs.len, NULL, &result.vshader);
    assertHR(ID3D11Device_CreateVertexShaderResult);
    HRESULT ID3D11Device_CreateInputLayoutResult = ID3D11Device_CreateInputLayout(device, desc, descCount, shadervs.ptr, shadervs.len, &result.layout);
    assertHR(ID3D11Device_CreateInputLayoutResult);
    HRESULT ID3D11Device_CreatePixelShaderResult = ID3D11Device_CreatePixelShader(device, shaderps.ptr, shaderps.len, NULL, &result.pshader);
    assertHR(ID3D11Device_CreatePixelShaderResult);
    result.instanceBuf = d3d11CreateDynBuffer(device, instanceBufSize, D3D11_BIND_VERTEX_BUFFER);
    return result;
}

static void d3d11DestroyVSPS(D3D11VSPS vsps) {
    D3D11Destroy(vsps.layout);
    D3D11Destroy(vsps.vshader);
    D3D11Destroy(vsps.pshader);
    D3D11Destroy(vsps.instanceBuf);
}

static void d3d11CopyBuf(ID3D11DeviceContext* context, ID3D11Buffer* dest, void* src, i64 size) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    context->lpVtbl->Map(context, (ID3D11Resource*)dest, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, src, size);
    context->lpVtbl->Unmap(context, (ID3D11Resource*)dest, 0);
}

static void d3d11DrawRects(ID3D11DeviceContext* context, D3D11VSPS vsps, UINT rectSize, i64 rectCount) {
    ID3D11DeviceContext_IASetInputLayout(context, vsps.layout);
    {
        UINT stride = rectSize;
        UINT offset = 0;
        ID3D11DeviceContext_IASetVertexBuffers(context, 0, 1, &vsps.instanceBuf, &stride, &offset);
    }
    ID3D11DeviceContext_VSSetShader(context, vsps.vshader, NULL, 0);
    ID3D11DeviceContext_PSSetShader(context, vsps.pshader, NULL, 0);
    ID3D11DeviceContext_DrawInstanced(context, 4, rectCount, 0, 0);
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

    Game game_ = {
        .screenRects.cap = 1024,
        .sprites.cap = 1024,
        .spriteScaleMultiplier = 5.0f,
    };
    Game* game = &game_;
    game->sprites.ptr = arenaAllocArray(memory.perm, Sprite, game->sprites.cap);
    game->screenRects.ptr = arenaAllocArray(memory.perm, ScreenRect, game->screenRects.cap);

    // NOTE(khvorov) Assets
    tempMemoryBlock(memory.scratch) {
        wchar_t* dataPath = 0;
        {
            wchar_t* exepath = arenaFreeptr(memory.scratch);
            DWORD exepathLenInChars = GetModuleFileNameW((HMODULE)instance, exepath, arenaFreesize(memory.scratch) / sizeof(wchar_t));
            memory.scratch->used += exepathLenInChars * sizeof(wchar_t);
            exepath[exepathLenInChars - 3] = 'd';
            exepath[exepathLenInChars - 2] = 'a';
            exepath[exepathLenInChars - 1] = 't';
            exepath[exepathLenInChars] = '\0';
            dataPath = exepath;
        }

        HANDLE hfile = CreateFileW(
            dataPath,
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
        assert(fileSize.QuadPart == sizeof(AssetData));

        game->assets = arenaAllocArray(memory.perm, AssetData, 1);
        DWORD bytesRead = 0;
        BOOL ReadFileResult = ReadFile(hfile, game->assets, fileSize.QuadPart, &bytesRead, 0);
        assert(ReadFileResult);
        assert(bytesRead == fileSize.QuadPart);

        CloseHandle(hfile);

        assetDataAfterLoad(game->assets);
    }

    struct {
        HWND hwnd;
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

    struct {
        ID3D11Device* device;
        ID3D11DeviceContext* context;
        IDXGISwapChain1* swapChain;
        ID3D11Buffer* cbuffer;
        ID3D11ShaderResourceView* textureView;
        ID3D11BlendState* blendState;
        ID3D11RasterizerState* rasterizer;
        ID3D11RenderTargetView* rtView;
        ID3D11SamplerState* sampler;
        struct {
            D3D11VSPS sprite;
            D3D11VSPS screen;
        } rects;
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
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            {"TOPLEFT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(SpriteRect, common.topleft), D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"MIRRORX", 0, DXGI_FORMAT_R32_SINT, 0, offsetof(SpriteRect, common.mirrorX), D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"TEX_POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(SpriteRect, texInAtlas.rect.topleft), D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"TEX_DIM", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(SpriteRect, texInAtlas.rect.dim), D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"OFFSET", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(SpriteRect, texInAtlas.offset), D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"FLIPLINES", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(SpriteRect, fliplines), D3D11_INPUT_PER_INSTANCE_DATA, 1},
        };
        u8arr shadervs = game->assets->shaders.elements[ShaderID_sprite_vs];
        u8arr shaderps = game->assets->shaders.elements[ShaderID_sprite_ps];
        d3d11.rects.sprite = d3d11CreateVSPS(d3d11.device, desc, arrayCount(desc), shadervs, shaderps, sizeof(SpriteRect) * game->sprites.cap);
    }

    {
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            {"TOPLEFT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ScreenRect, scr.topleft), D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"DIM", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ScreenRect, scr.dim), D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"TEX_POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ScreenRect, texInAtlas.topleft), D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"TEX_DIM", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ScreenRect, texInAtlas.dim), D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(ScreenRect, color), D3D11_INPUT_PER_INSTANCE_DATA, 1},
        };
        u8arr shadervs = game->assets->shaders.elements[ShaderID_screen_vs];
        u8arr shaderps = game->assets->shaders.elements[ShaderID_screen_ps];
        d3d11.rects.screen = d3d11CreateVSPS(d3d11.device, desc, arrayCount(desc), shadervs, shaderps, sizeof(ScreenRect) * game->screenRects.cap);
    }

    {
        Texture atlas = { .pixels = game->assets->atlas.pixels, .w = game->assets->atlas.w, .h = game->assets->atlas.h};

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
        ID3D11Device_CreateRasterizerState(d3d11.device, &desc, &d3d11.rasterizer);
    }

    d3d11.cbuffer = d3d11CreateDynBuffer(d3d11.device, sizeof(CBuffer), D3D11_BIND_CONSTANT_BUFFER);

    ShowWindow(window.hwnd, SW_SHOWDEFAULT);

    {
        arrpush(game->sprites, ((Sprite) {.common.topleft = {0, 0}, .entity = EntityID_Commando}));
        // arrpush(game->sprites, ((Sprite) {.common.topleft = {0, -20}, .common.mirrorX = true, .entity = EntityID_Commando}));
        // arrpush(game->sprites, ((Sprite) {.common.topleft = {20, -20}, .entity = EntityID_Lemurian}));
    }

    // TODO(khvorov) Should world space be bottom-up?

    // TODO(khvorov) Killfocus message (Alt+Tab)
    Input input = {};

    LARGE_INTEGER performanceFrequencyPerSecond = {};
    QueryPerformanceFrequency(&performanceFrequencyPerSecond);

    LARGE_INTEGER performanceCounter;
    QueryPerformanceCounter(&performanceCounter);

    for (;;) {
        assert(memory.scratch->used == 0);
        assert(memory.scratch->tempCount == 0);
        assert(memory.perm->tempCount == 0);

        for (i32 keyid = 0; keyid < InputKeyID_Count; keyid++) {
            input.keys[keyid].halfTransitionCount = 0;
        }

        for (MSG msg = {}; PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);) {
            switch (msg.message) {
                case WM_KEYDOWN:
                case WM_KEYUP: {
                    InputKeyID key = InputKeyID_None;
                    switch (msg.wParam) {
                        case VK_LEFT:  key = InputKeyID_Left; break;
                        case VK_RIGHT: key = InputKeyID_Right; break;
                        case VK_UP:    key = InputKeyID_Up; break;
                        case VK_DOWN:  key = InputKeyID_Down; break;
                        case VK_SHIFT: key = InputKeyID_Accelerate; break;
                    }
                    bool down = msg.message == WM_KEYDOWN;
                    input.keys[key].down = down;
                    input.keys[key].halfTransitionCount += 1;
                } break;

                case WM_MOUSEMOVE:
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

        f32 msSinceLastUpdate = 0;
        {
            u64 before = performanceCounter.QuadPart;
            QueryPerformanceCounter(&performanceCounter);
            u64 diff = performanceCounter.QuadPart - before;
            msSinceLastUpdate = (f32)diff / (f32)performanceFrequencyPerSecond.QuadPart * 1000.0f;
        }

        // NOTE(khvorov) Update
        {
            Sprite* sprite = game->sprites.ptr;
            assert(sprite->entity == EntityID_Commando);

            {
                Rect collision = game->assets->entities.collision[sprite->entity];
                V2 currentPos = v2add(sprite->common.topleft, collision.topleft);
                currentPos.y -= collision.dim.y;
                V2 deltaPos = {};
                {
                    f32 deltaX = 0.01f * msSinceLastUpdate;
                    if (input.keys[InputKeyID_Accelerate].down) { deltaX *= 10.0f;}
                    if (input.keys[InputKeyID_Left].down) { deltaPos.x -= deltaX;}
                    if (input.keys[InputKeyID_Right].down) {deltaPos.x += deltaX;}
                    if (input.keys[InputKeyID_Up].down) {deltaPos.y += deltaX;}
                    if (input.keys[InputKeyID_Down].down) {deltaPos.y -= deltaX;}
                }

                for (;;) {
                    bool collided = false;
                    f32 collisionProp = 1.0f;
                    V2 collisionWallUnitV = {};

                    // TODO(khvorov) Get colllision polygons from the correct stage
                    V2arrarr stageCollisionPolygons = game->assets->stages.elements[0];

                    for (i64 polyIndex = 0; polyIndex < stageCollisionPolygons.len; polyIndex++) {
                        V2arr collisionPolygon = stageCollisionPolygons.ptr[polyIndex];
                        for (u32 collisionLineIndex = 0; collisionLineIndex < collisionPolygon.len; collisionLineIndex++) {
                            CollisionLine collisionLine = {collisionPolygon.ptr[collisionLineIndex], collisionPolygon.ptr[(collisionLineIndex + 1) % collisionPolygon.len]};

                            V2 wallVector = v2sub(collisionLine.p2, collisionLine.p1);
                            V2 wallUnitV = v2normalize(wallVector);
                            V2 wallNormal = v2xyquaterturn(wallUnitV);

                            f32 wallShift = v2dot(wallNormal, collision.dim);
                            f32 wallShiftClamped = min(wallShift, 0);
                            V2 wallShiftV = v2scale(wallNormal, absval(wallShiftClamped));

                            V2 wallBound1Og = v2add(collisionLine.p1, wallShiftV);
                            V2 wallBound2Og = v2add(collisionLine.p2, wallShiftV);

                            V2 wallBound1 = {min(wallBound1Og.x, wallBound2Og.x), min(wallBound1Og.y, wallBound2Og.y)};
                            V2 wallBound2 = {max(wallBound1Og.x, wallBound2Og.x), max(wallBound1Og.y, wallBound2Og.y)};

                            f32 wallExt = v2dot(wallUnitV, collision.dim);
                            V2 wallExtV = v2scale(wallUnitV, wallExt);
                            wallBound1 = v2sub(wallBound1, wallExtV);

                            V2 currentToBound1 = v2sub(wallBound1, currentPos);
                            f32 currentDotWallNormal = v2dot(currentToBound1, wallNormal);
                            if (currentDotWallNormal <= 0) {
                                V2 newPos = v2add(currentPos, deltaPos);
                                V2 newToBound1 = v2sub(wallBound1, newPos);
                                f32 newDotWallNormal = v2dot(newToBound1, wallNormal);
                                if (newDotWallNormal > 0) {
                                    V2 currentToBound2 = v2sub(wallBound2, currentPos);
                                    f32 deltaOuterBound1 = v2outer(deltaPos, currentToBound1);
                                    f32 deltaOuterBound2 = v2outer(deltaPos, currentToBound2);
                                    if (deltaOuterBound1 * deltaOuterBound2 <= 0) {
                                        collided = true;
                                        f32 currentDotWallNormalMag = absval(currentDotWallNormal);
                                        f32 newDotWallNormalMag = absval(newDotWallNormal);
                                        f32 thisCollisionProp = currentDotWallNormalMag / (currentDotWallNormalMag + newDotWallNormalMag);
                                        if (thisCollisionProp < collisionProp) {
                                            collisionProp = thisCollisionProp;
                                            collisionWallUnitV = wallUnitV;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (collided) {
                        assert(collisionProp >= 0 && collisionProp <= 1);
                        V2 clippedDelta = v2scale(deltaPos, collisionProp);
                        currentPos = v2add(currentPos, clippedDelta);
                        V2 remainingDelta = v2scale(deltaPos, 1 - collisionProp);
                        f32 remainingDeltaAlongWall = v2dot(remainingDelta, collisionWallUnitV);
                        V2 remainingDeltaModded = v2scale(collisionWallUnitV, remainingDeltaAlongWall);
                        deltaPos = remainingDeltaModded;
                    } else {
                        currentPos = v2add(currentPos, deltaPos);
                        break;
                    }
                }

                currentPos.y += collision.dim.y;
                sprite->common.topleft = v2sub(currentPos, collision.topleft);

                // TODO(khvorov) Handle grounding
                // if (!sprite->isGrounded) {
                //     testPos.y -= 0.1f * msSinceLastUpdate;
                // }
            }

            if (input.keys[InputKeyID_Left].down) { sprite->common.mirrorX = true; }
            if (input.keys[InputKeyID_Right].down) { sprite->common.mirrorX = false; }

            if ((input.keys[InputKeyID_Left].down || input.keys[InputKeyID_Right].down)) {
                sprite->animationID = AnimationID_Commando_Walk; // TODO(khvorov) Only when grounded
            } else {
                sprite->animationID = AnimationID_Commando_Idle;
            }

            {
                Animation* animation = (Animation*)game->assets->animations.elements + sprite->animationID;
                if (sprite->currentAnimationCounterMS >= animation->frameDurationsInMS[sprite->animationFrame]) {
                    sprite->animationFrame = (sprite->animationFrame + 1) % animation->frameCount;
                    sprite->currentAnimationCounterMS = 0;
                } else {
                    sprite->currentAnimationCounterMS += msSinceLastUpdate;
                }
            }

            // TODO(khvorov) Special glyphs to draw as sprites
            {
                game->screenRects.len = 0;
                drawStr(game, STR("spritestr"), (V2) {200, 300}, (V4) {.g = 100, .a = 100}); // TODO(khvorov) Temp

                // TODO(khvorov) Temp code to draw collision lines
                V2arrarr stageCollisionPolygons = game->assets->stages.elements[0];
                for (i64 polyIndex = 0; polyIndex < stageCollisionPolygons.len; polyIndex++) {
                    V2arr collisionPolygon = stageCollisionPolygons.ptr[polyIndex];
                    for (u32 collisionLineIndex = 0; collisionLineIndex < collisionPolygon.len; collisionLineIndex++) {
                        CollisionLine collisionLine = {collisionPolygon.ptr[collisionLineIndex], collisionPolygon.ptr[(collisionLineIndex + 1) % collisionPolygon.len]};

                        Rect collisionLineRectWorld = {.topleft = (collisionLine.p1.x < collisionLine.p2.x) || (collisionLine.p1.y > collisionLine.p2.y) ? collisionLine.p1 : collisionLine.p2};
                        f32 thickness = 1;
                        collisionLineRectWorld.dim = (V2) {v2len(v2sub(collisionLine.p2, collisionLine.p1)), thickness};
                        bool vertical = collisionLine.p1.x == collisionLine.p2.x;
                        if (vertical) {
                            collisionLineRectWorld.dim.y = collisionLineRectWorld.dim.x;
                            collisionLineRectWorld.dim.x = thickness;
                        }

                        // TODO(khvorov) Reenable
                        // if (collisionLine.type == CollisionLineType_BlockFromRight) {
                        //     collisionLineRectWorld.topleft.x -= collisionLineRectWorld.dim.x;
                        // }

                        // if (collisionLine.type == CollisionLineType_BlockFromBottom) {
                        //     collisionLineRectWorld.topleft.y += collisionLineRectWorld.dim.y;
                        // }

                        drawRect(game, collisionLineRectWorld, (V4) {.r = 1, .g = 1, .b = 1, .a = 0.5});
                    }
                }
            }
        }

        {
            RECT rect = {};
            GetClientRect(window.hwnd, &rect);
            DWORD windowWidth = rect.right - rect.left;
            DWORD windowHeight = rect.bottom - rect.top;

            if (d3d11.rtView == NULL || (i32)windowWidth != game->window.w || (i32)windowHeight != game->window.h) {
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
                }

                game->window.w = windowWidth;
                game->window.h = windowHeight;
            }
        }

        if (d3d11.rtView) {
            // NOTE(khvorov) Copy sprites
            {
                D3D11_MAPPED_SUBRESOURCE mapped = {};
                d3d11.context->lpVtbl->Map(d3d11.context, (ID3D11Resource*)d3d11.rects.sprite.instanceBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
                SpriteRect* spriteRects = mapped.pData;
                for (i64 spriteIndex = 0; spriteIndex < game->sprites.len; spriteIndex++) {
                    Sprite* sprite = game->sprites.ptr + spriteIndex;
                    SpriteRect* spriteRect = spriteRects + spriteIndex;
                    spriteRect->common = sprite->common;
                    AtlasID id = getAtlasID(sprite->entity, sprite->animationID, sprite->animationFrame);
                    AtlasLocation atlasLoc = game->assets->atlas.locations[id];
                    spriteRect->texInAtlas = atlasLoc;
                    Rect collisionRect = game->assets->entities.collision[sprite->entity];
                    spriteRect->fliplines = v2add(collisionRect.topleft, v2scale(collisionRect.dim, 0.5f));

                    // TODO(khvorov) Temp collision shape drawing
                    Rect collision = game->assets->entities.collision[sprite->entity];
                    drawRect(game, (Rect) {v2add(sprite->common.topleft, collision.topleft), collision.dim}, (V4) {.r = 1, .b = 1, .a = 0.25});
                }

                // TODO(khvorov) Pull the correct stage
                // TODO(khvorov) Draw the stage sensibly
                {
                    SpriteRect* spriteRect = spriteRects + 0;
                    *spriteRect = (SpriteRect) {};
                    AtlasID id = AtlasID_Stage1_variant1;
                    AtlasLocation atlasLoc = game->assets->atlas.locations[id];
                    // NOTE(khvorov) Add 1 to account for the transparent
                    // border, assume the original art offset is (0, 0) because
                    // we expect all pixels in stages to be filled
                    atlasLoc.offset.y = atlasLoc.rect.dim.y - 1;
                    atlasLoc.offset.x += 1;
                    spriteRect->texInAtlas = atlasLoc;
                }

                d3d11.context->lpVtbl->Unmap(d3d11.context, (ID3D11Resource*)d3d11.rects.sprite.instanceBuf, 0);
            }

            d3d11CopyBuf(d3d11.context, d3d11.rects.screen.instanceBuf, game->screenRects.ptr, game->screenRects.len * sizeof(*game->screenRects.ptr));

            // NOTE(khvorov) Set cbuffer
            {
                D3D11_MAPPED_SUBRESOURCE mapped = {};
                d3d11.context->lpVtbl->Map(d3d11.context, (ID3D11Resource*)d3d11.cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
                CBuffer* cbuf = (CBuffer*)mapped.pData;
                cbuf->windowDim = (V2) {game->window.w, game->window.h};
                cbuf->atlasDim = (V2) {game->assets->atlas.w, game->assets->atlas.h};
                cbuf->cameraPos = game->cameraPos;
                cbuf->spriteScaleMultiplier = game->spriteScaleMultiplier;
                d3d11.context->lpVtbl->Unmap(d3d11.context, (ID3D11Resource*)d3d11.cbuffer, 0);
            }

            D3D11_VIEWPORT viewport = {
                .TopLeftX = 0,
                .TopLeftY = 0,
                .Width = (FLOAT)game->window.w,
                .Height = (FLOAT)game->window.h,
                .MinDepth = 0,
                .MaxDepth = 1,
            };

            FLOAT color[] = {0.01, 0.02, 0.03, 0};
            ID3D11DeviceContext_ClearRenderTargetView(d3d11.context, d3d11.rtView, color);

            ID3D11DeviceContext_IASetPrimitiveTopology(d3d11.context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            ID3D11DeviceContext_PSSetShaderResources(d3d11.context, 0, 1, &d3d11.textureView);

            d3d11.context->lpVtbl->VSSetConstantBuffers(d3d11.context, 0, 1, &d3d11.cbuffer);
            d3d11.context->lpVtbl->PSSetConstantBuffers(d3d11.context, 0, 1, &d3d11.cbuffer);

            ID3D11DeviceContext_RSSetViewports(d3d11.context, 1, &viewport);
            ID3D11DeviceContext_RSSetState(d3d11.context, d3d11.rasterizer);

            ID3D11DeviceContext_OMSetBlendState(d3d11.context, d3d11.blendState, NULL, ~0U);
            ID3D11DeviceContext_OMSetRenderTargets(d3d11.context, 1, &d3d11.rtView, 0);

            ID3D11DeviceContext_PSSetSamplers(d3d11.context, 0, 1, &d3d11.sampler);

            d3d11DrawRects(d3d11.context, d3d11.rects.sprite, sizeof(SpriteRect), game->sprites.len);
            d3d11DrawRects(d3d11.context, d3d11.rects.screen, sizeof(*game->screenRects.ptr), game->screenRects.len);
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

    D3D11Destroy(d3d11.device);
    D3D11Destroy(d3d11.context);
    D3D11Destroy(d3d11.swapChain);
    D3D11Destroy(d3d11.cbuffer);
    D3D11Destroy(d3d11.textureView);
    D3D11Destroy(d3d11.blendState);
    D3D11Destroy(d3d11.rasterizer);
    D3D11Destroy(d3d11.rtView);
    D3D11Destroy(d3d11.sampler);
    d3d11DestroyVSPS(d3d11.rects.sprite);
    d3d11DestroyVSPS(d3d11.rects.screen);

    return 0;
}
