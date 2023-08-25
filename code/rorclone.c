#include <stdint.h>
#include <stddef.h>

#define assert(cond) do { if (cond) {} else __debugbreak(); } while (0)
#define assertHR(hr) assert(SUCCEEDED(hr))
#define STR2(x) #x
#define STR(x) STR2(x)
#define unused(x) ((x) = (x))
#define arrayCount(x) (sizeof(x) / sizeof((x)[0]))
#define arrpush(arr, val) assert((arr).len < (arr).cap); (arr).ptr[(arr).len++] = (val)

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

// TODO(khvorov) Remove
typedef struct RectInstance {
    V2 pos;
    V2 dim;
} RectInstance;
static RectInstance tempStorage[1024];

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previnstance, LPSTR cmdline, int cmdshow) {
    unused(previnstance);
    unused(cmdline);
    unused(cmdshow);

    struct {
        HWND hwnd;
        DWORD w, h;
    } window = {};

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
    }
    
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
        d3d11.rects.storage.ptr = tempStorage;
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
        u32 pixels[] = {
            0xff0000ff, 0xff00ff00,
            0xffff0000, 0xffff00ff,
        };
        UINT width = 2;
        UINT height = 2;

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
    }

    {
        D3D11_SAMPLER_DESC desc = {
            .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
            .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
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

    arrpush(d3d11.rects.storage, ((RectInstance) {{0, 0}, {1, 5}}));
    arrpush(d3d11.rects.storage, ((RectInstance) {{4, 0}, {1, 4}}));
    d3d11.cbuffer.storage.cameraHalfSpanX = 10;

    for (;;) {

        for (MSG msg = {}; PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);) {
            switch (msg.message) {
                case WM_MOUSEMOVE:
                case WM_KEYDOWN:
                    break;

                case WM_QUIT: goto breakmainloop; break;
                default: TranslateMessage(&msg); DispatchMessageW(&msg); break;
            }
        }

        {
            u8 keyboard[256];
            GetKeyboardState(keyboard);
            u8 downMask = (1 << 7);
            if (keyboard[VK_LEFT] & downMask) {
                d3d11.rects.storage.ptr[0].pos.x -= 0.01f;
            }
            if (keyboard[VK_RIGHT] & downMask) {
                d3d11.rects.storage.ptr[0].pos.x += 0.01f;
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

            FLOAT color[] = {0, 0, 0, 0};
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
