// TODO(khvorov) Link this somewhere https://gist.github.com/mmozeiko/5e727f845db182d468a34d524508ad5f

#pragma comment (lib, "gdi32")
#pragma comment (lib, "user32")
#pragma comment (lib, "dxguid")
#pragma comment (lib, "d3d11")

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <dxgidebug.h>
#include <intrin.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "rorclone.hlsl_vs.h"
#include "rorclone.hlsl_ps.h"

#define assert(cond) do { if (cond) {} else __debugbreak(); } while (0)
#define assertHR(hr) assert(SUCCEEDED(hr))
#define STR2(x) #x
#define STR(x) STR2(x)
#define unused(x) ((x) = (x))
#define arrayCount(x) (sizeof(x) / sizeof((x)[0]))

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

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previnstance, LPSTR cmdline, int cmdshow) {
    unused(previnstance);
    unused(cmdline);
    unused(cmdshow);

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
    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP,
        windowClass.lpszClassName, L"rorclone",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, windowClass.hInstance, NULL
    );
    assert(hwnd && "Failed to create window");

    ID3D11Device* device = 0;
    ID3D11DeviceContext* context = 0;
    {
        UINT flags = 0;
        #ifndef NDEBUG
            flags |= D3D11_CREATE_DEVICE_DEBUG;
        #endif
        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
        HRESULT D3D11CreateDeviceResult = D3D11CreateDevice(
            NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, levels, arrayCount(levels),
            D3D11_SDK_VERSION, &device, NULL, &context
        );
        assertHR(D3D11CreateDeviceResult);
    }

    #ifndef NDEBUG
    {
        ID3D11InfoQueue* info;
        ID3D11Device_QueryInterface(device, &IID_ID3D11InfoQueue, (void**)&info);
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

    IDXGISwapChain1* swapChain = 0;
    {
        IDXGIDevice* dxgiDevice = 0;
        HRESULT ID3D11Device_QueryInterfaceResult = ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void**)&dxgiDevice);
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

        HRESULT IDXGIFactory2_CreateSwapChainForHwndResult = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown*)device, hwnd, &desc, NULL, NULL, &swapChain);
        assertHR(IDXGIFactory2_CreateSwapChainForHwndResult);

        IDXGIFactory_MakeWindowAssociation(factory, hwnd, DXGI_MWA_NO_ALT_ENTER);

        IDXGIFactory2_Release(factory);
        IDXGIAdapter_Release(dxgiAdapter);
        IDXGIDevice_Release(dxgiDevice);
    }

    struct Vertex {
        V2 pos;
        V2 scale;
    };

    ID3D11Buffer* vbuffer = 0;
    {
        struct Vertex data[] = {
            {{0.0f, 0.0f}, .scale = {0.1, 0.1}},
            {{0.5f, 0.5f}, .scale = {0.1, 0.1}},
        };

        D3D11_BUFFER_DESC desc = {
            .ByteWidth = sizeof(data),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };

        D3D11_SUBRESOURCE_DATA initial = { .pSysMem = data };
        HRESULT ID3D11Device_CreateBufferResult = ID3D11Device_CreateBuffer(device, &desc, &initial, &vbuffer);
        assertHR(ID3D11Device_CreateBufferResult);
    }

    ID3D11InputLayout* layout = 0;
    ID3D11VertexShader* vshader = 0;
    ID3D11PixelShader* pshader = 0;
    {
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(struct Vertex, pos),   D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            {"SCALE",    0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(struct Vertex, scale), D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        };

        HRESULT ID3D11Device_CreateVertexShaderResult = ID3D11Device_CreateVertexShader(device, globalShader_vs, sizeof(globalShader_vs), NULL, &vshader);
        assertHR(ID3D11Device_CreateVertexShaderResult);
        HRESULT ID3D11Device_CreatePixelShaderResult = ID3D11Device_CreatePixelShader(device, globalShader_ps, sizeof(globalShader_ps), NULL, &pshader);
        assertHR(ID3D11Device_CreatePixelShaderResult);
        HRESULT ID3D11Device_CreateInputLayoutResult = ID3D11Device_CreateInputLayout(device, desc, arrayCount(desc), globalShader_vs, sizeof(globalShader_vs), &layout);
        assertHR(ID3D11Device_CreateInputLayoutResult);
    }

    ID3D11ShaderResourceView* textureView = 0;
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
        ID3D11Device_CreateTexture2D(device, &desc, &data, &texture);
        ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource*)texture, NULL, &textureView);
        ID3D11Texture2D_Release(texture);
    }

    ID3D11SamplerState* sampler = 0;
    {
        D3D11_SAMPLER_DESC desc = {
            .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
            .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
        };
        ID3D11Device_CreateSamplerState(device, &desc, &sampler);
    }

    ID3D11BlendState* blendState = 0;
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
        ID3D11Device_CreateBlendState(device, &desc, &blendState);
    }

    ID3D11RasterizerState* rasterizerState = 0;
    {
        D3D11_RASTERIZER_DESC desc = {
            .FillMode = D3D11_FILL_SOLID,
            .CullMode = D3D11_CULL_NONE,
        };
        ID3D11Device_CreateRasterizerState(device, &desc, &rasterizerState);
    }

    ID3D11RenderTargetView* rtView = NULL;

    ShowWindow(hwnd, SW_SHOWDEFAULT);

    DWORD currentWidth = 0;
    DWORD currentHeight = 0;

    for (;;) {
        MSG msg = {};
        if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }

        // get current size for window client area
        RECT rect = {};
        GetClientRect(hwnd, &rect);
        DWORD windowWidth = rect.right - rect.left;
        DWORD windowHeight = rect.bottom - rect.top;

        if (rtView == NULL || windowWidth != currentWidth || windowHeight != currentHeight) {
            if (rtView) {
                ID3D11DeviceContext_ClearState(context);
                ID3D11RenderTargetView_Release(rtView);
                rtView = NULL;
            }

            if (windowWidth != 0 && windowHeight != 0) {
                HRESULT IDXGISwapChain1_ResizeBuffersResult = IDXGISwapChain1_ResizeBuffers(swapChain, 0, windowWidth, windowHeight, DXGI_FORMAT_UNKNOWN, 0);
                if (FAILED(IDXGISwapChain1_ResizeBuffersResult)) {
                    fatalError("Failed to resize swap chain!");
                }

                ID3D11Texture2D* backbuffer = 0;
                IDXGISwapChain1_GetBuffer(swapChain, 0, &IID_ID3D11Texture2D, (void**)&backbuffer);
                ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource*)backbuffer, NULL, &rtView);
                ID3D11Texture2D_Release(backbuffer);

                D3D11_TEXTURE2D_DESC depthDesc = {
                    .Width = windowWidth,
                    .Height = windowHeight,
                    .MipLevels = 1,
                    .ArraySize = 1,
                    .Format = DXGI_FORMAT_D32_FLOAT, // or use DXGI_FORMAT_D32_FLOAT_S8X24_UINT if you need stencil
                    .SampleDesc = { 1, 0 },
                    .Usage = D3D11_USAGE_DEFAULT,
                    .BindFlags = D3D11_BIND_DEPTH_STENCIL,
                };

                ID3D11Texture2D* depth;
                ID3D11Device_CreateTexture2D(device, &depthDesc, NULL, &depth);
                ID3D11Texture2D_Release(depth);
            }

            currentWidth = windowWidth;
            currentHeight = windowHeight;
        }

        if (rtView) {
            D3D11_VIEWPORT viewport = {
                .TopLeftX = 0,
                .TopLeftY = 0,
                .Width = (FLOAT)windowWidth,
                .Height = (FLOAT)windowHeight,
                .MinDepth = 0,
                .MaxDepth = 1,
            };

            FLOAT color[] = {0, 0, 0, 0};
            ID3D11DeviceContext_ClearRenderTargetView(context, rtView, color);

            ID3D11DeviceContext_IASetInputLayout(context, layout);
            ID3D11DeviceContext_IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            {
                UINT stride = sizeof(struct Vertex);
                UINT offset = 0;
                ID3D11DeviceContext_IASetVertexBuffers(context, 0, 1, &vbuffer, &stride, &offset);
            }

            ID3D11DeviceContext_VSSetShader(context, vshader, NULL, 0);

            ID3D11DeviceContext_RSSetViewports(context, 1, &viewport);
            ID3D11DeviceContext_RSSetState(context, rasterizerState);

            ID3D11DeviceContext_PSSetSamplers(context, 0, 1, &sampler);
            ID3D11DeviceContext_PSSetShaderResources(context, 0, 1, &textureView);
            ID3D11DeviceContext_PSSetShader(context, pshader, NULL, 0);

            ID3D11DeviceContext_OMSetBlendState(context, blendState, NULL, ~0U);
            ID3D11DeviceContext_OMSetRenderTargets(context, 1, &rtView, 0);

            ID3D11DeviceContext_DrawInstanced(context, 4, 2, 0, 0);
        }

        BOOL vsync = TRUE;
        HRESULT IDXGISwapChain1_PresentResult = IDXGISwapChain1_Present(swapChain, vsync ? 1 : 0, 0);
        if (IDXGISwapChain1_PresentResult == DXGI_STATUS_OCCLUDED) {
            if (vsync) {
                Sleep(10);
            }
        } else if (FAILED(IDXGISwapChain1_PresentResult)) {
            fatalError("Failed to present swap chain! Device lost?");
        }
    }
}
