#include <stdint.h>
#include <stddef.h>

#define assert(cond) do { if (cond) {} else __debugbreak(); } while (0)
#define assertHR(hr) assert(SUCCEEDED(hr))
#define STR2(x) #x
#define STR(x) STR2(x)
#define unused(x) ((x) = (x))
#define arrayCount(x) (sizeof(x) / sizeof((x)[0]))
#define arrpush(arr, val) assert((arr).len < (arr).cap); (arr).ptr[(arr).len++] = (val)
#define arenaAllocArray(arena, type, count) ((type*)arenaAlloc((arena), sizeof(type) * (count)))

#define STB_SPRINTF_IMPLEMENTATION
#include "stb_sprintf.h"

#define STBI_ASSERT(x) assert(x)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

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

static void endTempMemory(TempMemory temp) {
    assert(temp.usedBefore <= temp.arena->used);
    assert(temp.tempBefore == temp.arena->tempCount - 1);
    temp.arena->used = temp.usedBefore;
    temp.arena->tempCount -= 1;
}

typedef struct V2 {
    f32 x, y;
} V2;

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

#include "rorclone.hlsl_vs.h"
#include "rorclone.hlsl_ps.h"

void benchAlignment(Arena* arena) {
    // NOTE(khvorov) Did not see any difference in speed with aligned vs unalligned array

    // NOTE(khvorov) Assume aligned
    assert(((u64)arenaFreeptr(arena) & 3) == 0);

    // NOTE(khvorov) Toggle misalignment
    // arena->used += 1;

    i64 floatsToAlloc[] = {100, 1000, 10000, 100000};
    for (i64 floatsToAllocIndex = 0; floatsToAllocIndex < (i64)arrayCount(floatsToAlloc); floatsToAllocIndex++) {
        i64 floatCount = floatsToAlloc[floatsToAllocIndex];
        u64 minDiff = UINT64_MAX;
        u64 maxDiff = 0;

        i64 timesToRun = 10000;
        f32 sumOfSums = 0;

        for (i64 runIndex = 0; runIndex < timesToRun; runIndex++) {
            TempMemory temp = beginTempMemory(arena);

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

            endTempMemory(temp);
        }

        {
            char* out = arenaFreeptr(arena);
            stbsp_snprintf(out, arenaFreesize(arena), "floatCount: %llu, sum: %f, min: %llu, max: %llu\n", floatCount, sumOfSums, minDiff, maxDiff);
            OutputDebugStringA(out);
        }
    }
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

    Arena arena_ = {};
    Arena* arena = &arena_;
    {
        arena->size = 1 * 1024 * 1024 * 1024;
        arena->base = VirtualAlloc(0, arena->size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        assert(arena->base);
    }

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
        V2 pos;
        V2 dim;
    } RectInstance;

    typedef struct CBuffer {
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
        d3d11.rects.storage.ptr = arenaAllocArray(arena, RectInstance, d3d11.rects.storage.cap);
    }

    {
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(RectInstance, pos), D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"DIM",      0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(RectInstance, dim), D3D11_INPUT_PER_INSTANCE_DATA, 1},
        };

        HRESULT ID3D11Device_CreateVertexShaderResult = ID3D11Device_CreateVertexShader(d3d11.device, globalShader_vs, sizeof(globalShader_vs), NULL, &d3d11.vshader);
        assertHR(ID3D11Device_CreateVertexShaderResult);
        HRESULT ID3D11Device_CreatePixelShaderResult = ID3D11Device_CreatePixelShader(d3d11.device, globalShader_ps, sizeof(globalShader_ps), NULL, &d3d11.pshader);
        assertHR(ID3D11Device_CreatePixelShaderResult);
        HRESULT ID3D11Device_CreateInputLayoutResult = ID3D11Device_CreateInputLayout(d3d11.device, desc, arrayCount(desc), globalShader_vs, sizeof(globalShader_vs), &d3d11.layout);
        assertHR(ID3D11Device_CreateInputLayoutResult);
    }

    {
        TempMemory temp = beginTempMemory(arena);

        u32* pixels = 0;
        i32 width = 0;
        i32 height = 0;
        {
            HANDLE hfile = CreateFileA(
                "data/Sprite-0001.png",
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                0,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                0
            );
            assert(hfile != INVALID_HANDLE_VALUE);

            LARGE_INTEGER fileSize = {};
            GetFileSizeEx(hfile, &fileSize);

            void* fileContent = arenaFreeptr(arena);
            DWORD bytesRead = 0;
            ReadFile(hfile, fileContent, fileSize.QuadPart, &bytesRead, 0);
            assert(bytesRead == fileSize.QuadPart);

            int channelsInFile = 0;
            pixels = (u32*)stbi_load_from_memory(fileContent, fileSize.QuadPart, &width, &height, &channelsInFile, 0);
            assert(pixels);
            assert(channelsInFile == 4);
        }

        D3D11_TEXTURE2D_DESC desc = {
            .Width = width,
            .Height = height,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .SampleDesc = { 1, 0 },
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
        };

        D3D11_SUBRESOURCE_DATA data = {
            .pSysMem = pixels,
            .SysMemPitch = width * sizeof(u32),
        };

        ID3D11Texture2D* texture;
        ID3D11Device_CreateTexture2D(d3d11.device, &desc, &data, &texture);
        ID3D11Device_CreateShaderResourceView(d3d11.device, (ID3D11Resource*)texture, NULL, &d3d11.textureView);
        ID3D11Texture2D_Release(texture);

        stbi_image_free(pixels);
        endTempMemory(temp);
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

    arrpush(d3d11.rects.storage, ((RectInstance) {{-10.0 + 2.5, 0}, {5, 5}}));
    arrpush(d3d11.rects.storage, ((RectInstance) {{4, 0}, {4, 4}}));

    for (;;) {
        assert(arena->tempCount == 0);

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

            f32 deltaX = 0.001f;
            if (keyboard[VK_LEFT] & downMask) {
                d3d11.rects.storage.ptr[0].pos.x -= deltaX;
            }
            if (keyboard[VK_RIGHT] & downMask) {
                d3d11.rects.storage.ptr[0].pos.x += deltaX;
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
                    ID3D11Device_CreateRenderTargetView(d3d11.device, (ID3D11Resource*)backbuffer, NULL, &d3d11.rtView);
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

            FLOAT color[] = {0.01, 0, 0.2, 0};
            ID3D11DeviceContext_ClearRenderTargetView(d3d11.context, d3d11.rtView, color);

            ID3D11DeviceContext_IASetInputLayout(d3d11.context, d3d11.layout);
            ID3D11DeviceContext_IASetPrimitiveTopology(d3d11.context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            {
                UINT stride = sizeof(RectInstance);
                UINT offset = 0;
                ID3D11DeviceContext_IASetVertexBuffers(d3d11.context, 0, 1, &d3d11.rects.buf, &stride, &offset);
            }

            d3d11.context->lpVtbl->VSSetConstantBuffers(d3d11.context, 0, 1, &d3d11.cbuffer.buf);

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
