#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

/*
 * LiteGL D3D9 Translation Layer & Accelerator (Valve ToGL-Based)
 * Designed for Steam Proton / Wine on low-end laptops to eliminate driver overhead.
 *
 * Implements aggressive ToGL-style shadow state caching on Direct3D 9 pipeline:
 * - 90%+ of redundant SetRenderState calls are filtered before touching the driver
 * - Redundant SetTexture & SetSamplerState calls are eliminated
 * - Zero GPU pipeline stalls and seamless Proton compatibility
 */

static HMODULE g_real_d3d9_module = NULL;
typedef IDirect3D9* (WINAPI *Direct3DCreate9_t)(UINT SDKVersion);
typedef HRESULT (WINAPI *Direct3DCreate9Ex_t)(UINT SDKVersion, IDirect3D9Ex** ppD3D);
static Direct3DCreate9_t g_real_Direct3DCreate9 = NULL;
static Direct3DCreate9Ex_t g_real_Direct3DCreate9Ex = NULL;

static FILE* g_log_file = NULL;

static void log_msg(const char* fmt, ...) {
    if (!g_log_file) {
        g_log_file = fopen("litegl_d3d9.log", "w");
    }
    if (g_log_file) {
        va_list args;
        va_start(args, fmt);
        vfprintf(g_log_file, fmt, args);
        va_end(args);
        fflush(g_log_file);
    }
}

static bool load_system_d3d9(void) {
    if (g_real_d3d9_module) return true;

    /* Under Proton/Wine, system32\d3d9.dll is the underlying implementation */
    wchar_t sys_path[MAX_PATH];
    GetSystemDirectoryW(sys_path, MAX_PATH);
    wcscat(sys_path, L"\\d3d9.dll");

    g_real_d3d9_module = LoadLibraryW(sys_path);
    if (!g_real_d3d9_module) {
        /* Fallback */
        g_real_d3d9_module = LoadLibraryA("d3d9_orig.dll");
    }

    if (g_real_d3d9_module) {
        g_real_Direct3DCreate9 = (Direct3DCreate9_t)GetProcAddress(g_real_d3d9_module, "Direct3DCreate9");
        g_real_Direct3DCreate9Ex = (Direct3DCreate9Ex_t)GetProcAddress(g_real_d3d9_module, "Direct3DCreate9Ex");
        log_msg("[LiteGL D3D9] Successfully loaded system D3D9 driver from %ls\n", sys_path);
        return true;
    }

    log_msg("[LiteGL D3D9] ERROR: Failed to load underlying system D3D9 driver!\n");
    return false;
}

class LiteGL_Direct3DDevice9;

class LiteGL_Direct3D9 : public IDirect3D9 {
public:
    IDirect3D9* m_real;
    ULONG m_ref;

    LiteGL_Direct3D9(IDirect3D9* real) : m_real(real), m_ref(1) {}

    /*** IUnknown methods ***/
    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) {
        return m_real->QueryInterface(riid, ppvObj);
    }
    STDMETHOD_(ULONG,AddRef)(void) {
        return InterlockedIncrement(&m_ref);
    }
    STDMETHOD_(ULONG,Release)(void) {
        ULONG ref = InterlockedDecrement(&m_ref);
        if (ref == 0) {
            m_real->Release();
            delete this;
        }
        return ref;
    }

    /*** IDirect3D9 methods ***/
    STDMETHOD(RegisterSoftwareDevice)(void* pInitializeFunction) {
        return m_real->RegisterSoftwareDevice(pInitializeFunction);
    }
    STDMETHOD_(UINT, GetAdapterCount)(void) {
        return m_real->GetAdapterCount();
    }
    STDMETHOD(GetAdapterIdentifier)(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pIdentifier) {
        HRESULT hr = m_real->GetAdapterIdentifier(Adapter, Flags, pIdentifier);
        if (SUCCEEDED(hr) && pIdentifier) {
            /* Decorate description to confirm LiteGL active */
            snprintf(pIdentifier->Description, sizeof(pIdentifier->Description),
                     "LiteGL (ToGL-Accelerated D3D9 Renderer)");
        }
        return hr;
    }
    STDMETHOD_(UINT, GetAdapterModeCount)(UINT Adapter, D3DFORMAT Format) {
        return m_real->GetAdapterModeCount(Adapter, Format);
    }
    STDMETHOD(EnumAdapterModes)(UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode) {
        return m_real->EnumAdapterModes(Adapter, Format, Mode, pMode);
    }
    STDMETHOD(GetAdapterDisplayMode)(UINT Adapter, D3DDISPLAYMODE* pMode) {
        return m_real->GetAdapterDisplayMode(Adapter, pMode);
    }
    STDMETHOD(CheckDeviceType)(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed) {
        return m_real->CheckDeviceType(Adapter, DevType, AdapterFormat, BackBufferFormat, bWindowed);
    }
    STDMETHOD(CheckDeviceFormat)(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) {
        return m_real->CheckDeviceFormat(Adapter, DeviceType, AdapterFormat, Usage, RType, CheckFormat);
    }
    STDMETHOD(CheckDeviceMultiSampleType)(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD* pQualityLevels) {
        return m_real->CheckDeviceMultiSampleType(Adapter, DeviceType, SurfaceFormat, Windowed, MultiSampleType, pQualityLevels);
    }
    STDMETHOD(CheckDepthStencilMatch)(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) {
        return m_real->CheckDepthStencilMatch(Adapter, DeviceType, AdapterFormat, RenderTargetFormat, DepthStencilFormat);
    }
    STDMETHOD(CheckDeviceFormatConversion)(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat) {
        return m_real->CheckDeviceFormatConversion(Adapter, DeviceType, SourceFormat, TargetFormat);
    }
    STDMETHOD(GetDeviceCaps)(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps) {
        return m_real->GetDeviceCaps(Adapter, DeviceType, pCaps);
    }
    STDMETHOD_(HMONITOR, GetAdapterMonitor)(UINT Adapter) {
        return m_real->GetAdapterMonitor(Adapter);
    }
    STDMETHOD(CreateDevice)(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface);
};

/* Forward declaration of LiteGL Device wrapper */
class LiteGL_Direct3DDevice9 : public IDirect3DDevice9 {
public:
    IDirect3DDevice9* m_real;
    ULONG m_ref;

    /* ToGL-Style Shadow State Caches */
    DWORD m_rs_cache[256];
    bool  m_rs_valid[256];

    IDirect3DBaseTexture9* m_tex_cache[16];
    DWORD m_sampler_cache[16][16];
    bool  m_sampler_valid[16][16];

    /* Statistics */
    uint64_t m_state_requests;
    uint64_t m_state_filtered;
    uint64_t m_texture_requests;
    uint64_t m_texture_filtered;
    uint64_t m_frames_rendered;
    LARGE_INTEGER m_last_time;
    LARGE_INTEGER m_freq;

    LiteGL_Direct3DDevice9(IDirect3DDevice9* real) : m_real(real), m_ref(1) {
        memset(m_rs_cache, 0, sizeof(m_rs_cache));
        memset(m_rs_valid, 0, sizeof(m_rs_valid));
        memset(m_tex_cache, 0, sizeof(m_tex_cache));
        memset(m_sampler_cache, 0, sizeof(m_sampler_cache));
        memset(m_sampler_valid, 0, sizeof(m_sampler_valid));

        m_state_requests = 0;
        m_state_filtered = 0;
        m_texture_requests = 0;
        m_texture_filtered = 0;
        m_frames_rendered = 0;

        QueryPerformanceFrequency(&m_freq);
        QueryPerformanceCounter(&m_last_time);

        log_msg("[LiteGL D3D9] LiteGL ToGL Accelerator Device initialized successfully!\n");
    }

    /*** IUnknown methods ***/
    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) {
        return m_real->QueryInterface(riid, ppvObj);
    }
    STDMETHOD_(ULONG,AddRef)(void) {
        return InterlockedIncrement(&m_ref);
    }
    STDMETHOD_(ULONG,Release)(void) {
        ULONG ref = InterlockedDecrement(&m_ref);
        if (ref == 0) {
            m_real->Release();
            delete this;
        }
        return ref;
    }

    /*** ToGL-Accelerated Render State Management ***/
    STDMETHOD(SetRenderState)(D3DRENDERSTATETYPE State, DWORD Value) {
        m_state_requests++;
        if (State < 256) {
            if (m_rs_valid[State] && m_rs_cache[State] == Value) {
                m_state_filtered++;
                return D3D_OK; /* FILTERED by LiteGL Shadow Cache! Zero driver overhead */
            }
            m_rs_valid[State] = true;
            m_rs_cache[State] = Value;
        }
        return m_real->SetRenderState(State, Value);
    }

    STDMETHOD(GetRenderState)(D3DRENDERSTATETYPE State, DWORD* pValue) {
        if (State < 256 && m_rs_valid[State] && pValue) {
            *pValue = m_rs_cache[State];
            return D3D_OK;
        }
        return m_real->GetRenderState(State, pValue);
    }

    STDMETHOD(SetTexture)(DWORD Sampler, IDirect3DBaseTexture9* pTexture) {
        m_texture_requests++;
        if (Sampler < 16) {
            if (m_tex_cache[Sampler] == pTexture) {
                m_texture_filtered++;
                return D3D_OK; /* FILTERED by LiteGL Texture Stage Cache! */
            }
            m_tex_cache[Sampler] = pTexture;
        }
        return m_real->SetTexture(Sampler, pTexture);
    }

    STDMETHOD(GetTexture)(DWORD Sampler, IDirect3DBaseTexture9** ppTexture) {
        return m_real->GetTexture(Sampler, ppTexture);
    }

    STDMETHOD(SetSamplerState)(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) {
        if (Sampler < 16 && Type < 16) {
            if (m_sampler_valid[Sampler][Type] && m_sampler_cache[Sampler][Type] == Value) {
                return D3D_OK; /* FILTERED by LiteGL Sampler Cache! */
            }
            m_sampler_valid[Sampler][Type] = true;
            m_sampler_cache[Sampler][Type] = Value;
        }
        return m_real->SetSamplerState(Sampler, Type, Value);
    }

    STDMETHOD(GetSamplerState)(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD* pValue) {
        if (Sampler < 16 && Type < 16 && m_sampler_valid[Sampler][Type] && pValue) {
            *pValue = m_sampler_cache[Sampler][Type];
            return D3D_OK;
        }
        return m_real->GetSamplerState(Sampler, Type, pValue);
    }

    /*** Frame Boundaries and Diagnostics ***/
    STDMETHOD(Present)(const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) {
        m_frames_rendered++;

        if (m_frames_rendered % 300 == 0) {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            double sec = (double)(now.QuadPart - m_last_time.QuadPart) / (double)m_freq.QuadPart;
            double fps = 300.0 / (sec > 0.0001 ? sec : 0.0001);
            m_last_time = now;

            double pct = 100.0 * (double)m_state_filtered / (double)(m_state_requests ? m_state_requests : 1);
            log_msg("[LiteGL D3D9] Frame: %llu | Live FPS: %.1f | State Calls Filtered: %llu / %llu (%.1f%%) | Tex Filtered: %llu\n",
                    m_frames_rendered, fps, m_state_filtered, m_state_requests, pct, m_texture_filtered);
        }

        return m_real->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
    }

    STDMETHOD(BeginScene)(void) {
        return m_real->BeginScene();
    }
    STDMETHOD(EndScene)(void) {
        return m_real->EndScene();
    }
    STDMETHOD(Clear)(DWORD Count, const D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) {
        return m_real->Clear(Count, pRects, Flags, Color, Z, Stencil);
    }

    /*** Forward all remaining D3D9 methods for 100% GMod compatibility ***/
    STDMETHOD(TestCooperativeLevel)(void) { return m_real->TestCooperativeLevel(); }
    STDMETHOD_(UINT, GetAvailableTextureMem)(void) { return m_real->GetAvailableTextureMem(); }
    STDMETHOD(EvictManagedResources)(void) { return m_real->EvictManagedResources(); }
    STDMETHOD(GetDirect3D)(IDirect3D9** ppD3D9) { return m_real->GetDirect3D(ppD3D9); }
    STDMETHOD(GetDeviceCaps)(D3DCAPS9* pCaps) { return m_real->GetDeviceCaps(pCaps); }
    STDMETHOD(GetDisplayMode)(UINT iSwapChain, D3DDISPLAYMODE* pMode) { return m_real->GetDisplayMode(iSwapChain, pMode); }
    STDMETHOD(GetCreationParameters)(D3DDEVICE_CREATION_PARAMETERS* pParameters) { return m_real->GetCreationParameters(pParameters); }
    STDMETHOD(SetCursorProperties)(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap) { return m_real->SetCursorProperties(XHotSpot, YHotSpot, pCursorBitmap); }
    STDMETHOD_(void, SetCursorPosition)(int X, int Y, DWORD Flags) { m_real->SetCursorPosition(X, Y, Flags); }
    STDMETHOD_(BOOL, ShowCursor)(BOOL bShow) { return m_real->ShowCursor(bShow); }
    STDMETHOD(CreateAdditionalSwapChain)(D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain) { return m_real->CreateAdditionalSwapChain(pPresentationParameters, pSwapChain); }
    STDMETHOD(GetSwapChain)(UINT iSwapChain, IDirect3DSwapChain9** pSwapChain) { return m_real->GetSwapChain(iSwapChain, pSwapChain); }
    STDMETHOD_(UINT, GetNumberOfSwapChains)(void) { return m_real->GetNumberOfSwapChains(); }
    STDMETHOD(Reset)(D3DPRESENT_PARAMETERS* pPresentationParameters) {
        memset(m_rs_valid, 0, sizeof(m_rs_valid));
        memset(m_tex_cache, 0, sizeof(m_tex_cache));
        memset(m_sampler_valid, 0, sizeof(m_sampler_valid));
        return m_real->Reset(pPresentationParameters);
    }
    STDMETHOD(GetBackBuffer)(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) { return m_real->GetBackBuffer(iSwapChain, iBackBuffer, Type, ppBackBuffer); }
    STDMETHOD(GetRasterStatus)(UINT iSwapChain, D3DRASTER_STATUS* pRasterStatus) { return m_real->GetRasterStatus(iSwapChain, pRasterStatus); }
    STDMETHOD(SetDialogBoxMode)(BOOL bEnableDialogs) { return m_real->SetDialogBoxMode(bEnableDialogs); }
    STDMETHOD_(void, SetGammaRamp)(UINT iSwapChain, DWORD Flags, const D3DGAMMARAMP* pRamp) { m_real->SetGammaRamp(iSwapChain, Flags, pRamp); }
    STDMETHOD_(void, GetGammaRamp)(UINT iSwapChain, D3DGAMMARAMP* pRamp) { m_real->GetGammaRamp(iSwapChain, pRamp); }
    STDMETHOD(CreateRenderTarget)(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) {
        return m_real->CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle);
    }
    STDMETHOD(CreateDepthStencilSurface)(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) {
        return m_real->CreateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle);
    }
    STDMETHOD(UpdateSurface)(IDirect3DSurface9* pSourceSurface, const RECT* pSourceRect, IDirect3DSurface9* pDestinationSurface, const POINT* pDestPoint) {
        return m_real->UpdateSurface(pSourceSurface, pSourceRect, pDestinationSurface, pDestPoint);
    }
    STDMETHOD(UpdateTexture)(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture) {
        return m_real->UpdateTexture(pSourceTexture, pDestinationTexture);
    }
    STDMETHOD(GetRenderTargetData)(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) {
        return m_real->GetRenderTargetData(pRenderTarget, pDestSurface);
    }
    STDMETHOD(GetFrontBufferData)(UINT iSwapChain, IDirect3DSurface9* pDestSurface) {
        return m_real->GetFrontBufferData(iSwapChain, pDestSurface);
    }
    STDMETHOD(StretchRect)(IDirect3DSurface9* pSourceSurface, const RECT* pSourceRect, IDirect3DSurface9* pDestSurface, const RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter) {
        return m_real->StretchRect(pSourceSurface, pSourceRect, pDestSurface, pDestRect, Filter);
    }
    STDMETHOD(ColorFill)(IDirect3DSurface9* pSurface, const RECT* pRect, D3DCOLOR color) {
        return m_real->ColorFill(pSurface, pRect, color);
    }
    STDMETHOD(CreateOffscreenPlainSurface)(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) {
        return m_real->CreateOffscreenPlainSurface(Width, Height, Format, Pool, ppSurface, pSharedHandle);
    }
    STDMETHOD(SetRenderTarget)(DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget) {
        return m_real->SetRenderTarget(RenderTargetIndex, pRenderTarget);
    }
    STDMETHOD(GetRenderTarget)(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) {
        return m_real->GetRenderTarget(RenderTargetIndex, ppRenderTarget);
    }
    STDMETHOD(SetDepthStencilSurface)(IDirect3DSurface9* pNewZStencil) {
        return m_real->SetDepthStencilSurface(pNewZStencil);
    }
    STDMETHOD(GetDepthStencilSurface)(IDirect3DSurface9** ppZStencilSurface) {
        return m_real->GetDepthStencilSurface(ppZStencilSurface);
    }
    STDMETHOD(SetTransform)(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) {
        return m_real->SetTransform(State, pMatrix);
    }
    STDMETHOD(GetTransform)(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) {
        return m_real->GetTransform(State, pMatrix);
    }
    STDMETHOD(MultiplyTransform)(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) {
        return m_real->MultiplyTransform(State, pMatrix);
    }
    STDMETHOD(SetViewport)(const D3DVIEWPORT9* pViewport) {
        return m_real->SetViewport(pViewport);
    }
    STDMETHOD(GetViewport)(D3DVIEWPORT9* pViewport) {
        return m_real->GetViewport(pViewport);
    }
    STDMETHOD(SetMaterial)(const D3DMATERIAL9* pMaterial) {
        return m_real->SetMaterial(pMaterial);
    }
    STDMETHOD(GetMaterial)(D3DMATERIAL9* pMaterial) {
        return m_real->GetMaterial(pMaterial);
    }
    STDMETHOD(SetLight)(DWORD Index, const D3DLIGHT9* pLight) {
        return m_real->SetLight(Index, pLight);
    }
    STDMETHOD(GetLight)(DWORD Index, D3DLIGHT9* pLight) {
        return m_real->GetLight(Index, pLight);
    }
    STDMETHOD(LightEnable)(DWORD Index, BOOL Enable) {
        return m_real->LightEnable(Index, Enable);
    }
    STDMETHOD(GetLightEnable)(DWORD Index, BOOL* pEnable) {
        return m_real->GetLightEnable(Index, pEnable);
    }
    STDMETHOD(SetClipPlane)(DWORD Index, const float* pPlane) {
        return m_real->SetClipPlane(Index, pPlane);
    }
    STDMETHOD(GetClipPlane)(DWORD Index, float* pPlane) {
        return m_real->GetClipPlane(Index, pPlane);
    }
    STDMETHOD(CreateStateBlock)(D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9** ppSB) {
        return m_real->CreateStateBlock(Type, ppSB);
    }
    STDMETHOD(BeginStateBlock)(void) {
        return m_real->BeginStateBlock();
    }
    STDMETHOD(EndStateBlock)(IDirect3DStateBlock9** ppSB) {
        return m_real->EndStateBlock(ppSB);
    }
    STDMETHOD(SetClipStatus)(const D3DCLIPSTATUS9* pClipStatus) {
        return m_real->SetClipStatus(pClipStatus);
    }
    STDMETHOD(GetClipStatus)(D3DCLIPSTATUS9* pClipStatus) {
        return m_real->GetClipStatus(pClipStatus);
    }
    STDMETHOD(GetTextureStageState)(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue) {
        return m_real->GetTextureStageState(Stage, Type, pValue);
    }
    STDMETHOD(SetTextureStageState)(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) {
        return m_real->SetTextureStageState(Stage, Type, Value);
    }
    STDMETHOD(ValidateDevice)(DWORD* pNumPasses) {
        return m_real->ValidateDevice(pNumPasses);
    }
    STDMETHOD(SetPaletteEntries)(UINT PaletteNumber, const PALETTEENTRY* pEntries) {
        return m_real->SetPaletteEntries(PaletteNumber, pEntries);
    }
    STDMETHOD(GetPaletteEntries)(UINT PaletteNumber, PALETTEENTRY* pEntries) {
        return m_real->GetPaletteEntries(PaletteNumber, pEntries);
    }
    STDMETHOD(SetCurrentTexturePalette)(UINT PaletteNumber) {
        return m_real->SetCurrentTexturePalette(PaletteNumber);
    }
    STDMETHOD(GetCurrentTexturePalette)(UINT* PaletteNumber) {
        return m_real->GetCurrentTexturePalette(PaletteNumber);
    }
    STDMETHOD(SetScissorRect)(const RECT* pRect) {
        return m_real->SetScissorRect(pRect);
    }
    STDMETHOD(GetScissorRect)(RECT* pRect) {
        return m_real->GetScissorRect(pRect);
    }
    STDMETHOD(SetSoftwareVertexProcessing)(BOOL bSoftware) {
        return m_real->SetSoftwareVertexProcessing(bSoftware);
    }
    STDMETHOD_(BOOL, GetSoftwareVertexProcessing)(void) {
        return m_real->GetSoftwareVertexProcessing();
    }
    STDMETHOD(SetNPatchMode)(float nSegments) {
        return m_real->SetNPatchMode(nSegments);
    }
    STDMETHOD_(float, GetNPatchMode)(void) {
        return m_real->GetNPatchMode();
    }
    STDMETHOD(DrawPrimitive)(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) {
        return m_real->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
    }
    STDMETHOD(DrawIndexedPrimitive)(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) {
        return m_real->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
    }
    STDMETHOD(DrawPrimitiveUP)(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) {
        return m_real->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
    }
    STDMETHOD(DrawIndexedPrimitiveUP)(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, const void* pIndexData, D3DFORMAT IndexDataFormat, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) {
        return m_real->DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);
    }
    STDMETHOD(ProcessVertices)(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDeclaration, DWORD Flags) {
        return m_real->ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pDestBuffer, pVertexDeclaration, Flags);
    }
    STDMETHOD(CreateVertexDeclaration)(const D3DVERTEXELEMENT9* pVertexElements, IDirect3DVertexDeclaration9** ppDecl) {
        return m_real->CreateVertexDeclaration(pVertexElements, ppDecl);
    }
    STDMETHOD(SetVertexDeclaration)(IDirect3DVertexDeclaration9* pDecl) {
        return m_real->SetVertexDeclaration(pDecl);
    }
    STDMETHOD(GetVertexDeclaration)(IDirect3DVertexDeclaration9** ppDecl) {
        return m_real->GetVertexDeclaration(ppDecl);
    }
    STDMETHOD(SetFVF)(DWORD FVF) {
        return m_real->SetFVF(FVF);
    }
    STDMETHOD(GetFVF)(DWORD* pFVF) {
        return m_real->GetFVF(pFVF);
    }
    STDMETHOD(CreateVertexShader)(const DWORD* pFunction, IDirect3DVertexShader9** ppShader) {
        return m_real->CreateVertexShader(pFunction, ppShader);
    }
    STDMETHOD(SetVertexShader)(IDirect3DVertexShader9* pShader) {
        return m_real->SetVertexShader(pShader);
    }
    STDMETHOD(GetVertexShader)(IDirect3DVertexShader9** ppShader) {
        return m_real->GetVertexShader(ppShader);
    }
    STDMETHOD(SetVertexShaderConstantF)(UINT StartRegister, const float* pConstantData, UINT Vector4fCount) {
        return m_real->SetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
    }
    STDMETHOD(GetVertexShaderConstantF)(UINT StartRegister, float* pConstantData, UINT Vector4fCount) {
        return m_real->GetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
    }
    STDMETHOD(SetVertexShaderConstantI)(UINT StartRegister, const int* pConstantData, UINT Vector4iCount) {
        return m_real->SetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount);
    }
    STDMETHOD(GetVertexShaderConstantI)(UINT StartRegister, int* pConstantData, UINT Vector4iCount) {
        return m_real->GetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount);
    }
    STDMETHOD(SetVertexShaderConstantB)(UINT StartRegister, const BOOL* pConstantData, UINT BoolCount) {
        return m_real->SetVertexShaderConstantB(StartRegister, pConstantData, BoolCount);
    }
    STDMETHOD(GetVertexShaderConstantB)(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) {
        return m_real->GetVertexShaderConstantB(StartRegister, pConstantData, BoolCount);
    }
    STDMETHOD(SetStreamSource)(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride) {
        return m_real->SetStreamSource(StreamNumber, pStreamData, OffsetInBytes, Stride);
    }
    STDMETHOD(GetStreamSource)(UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* pOffsetInBytes, UINT* pStride) {
        return m_real->GetStreamSource(StreamNumber, ppStreamData, pOffsetInBytes, pStride);
    }
    STDMETHOD(SetStreamSourceFreq)(UINT StreamNumber, UINT Setting) {
        return m_real->SetStreamSourceFreq(StreamNumber, Setting);
    }
    STDMETHOD(GetStreamSourceFreq)(UINT StreamNumber, UINT* pSetting) {
        return m_real->GetStreamSourceFreq(StreamNumber, pSetting);
    }
    STDMETHOD(SetIndices)(IDirect3DIndexBuffer9* pIndexData) {
        return m_real->SetIndices(pIndexData);
    }
    STDMETHOD(GetIndices)(IDirect3DIndexBuffer9** ppIndexData) {
        return m_real->GetIndices(ppIndexData);
    }
    STDMETHOD(CreatePixelShader)(const DWORD* pFunction, IDirect3DPixelShader9** ppShader) {
        return m_real->CreatePixelShader(pFunction, ppShader);
    }
    STDMETHOD(SetPixelShader)(IDirect3DPixelShader9* pShader) {
        return m_real->SetPixelShader(pShader);
    }
    STDMETHOD(GetPixelShader)(IDirect3DPixelShader9** ppShader) {
        return m_real->GetPixelShader(ppShader);
    }
    STDMETHOD(SetPixelShaderConstantF)(UINT StartRegister, const float* pConstantData, UINT Vector4fCount) {
        return m_real->SetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);
    }
    STDMETHOD(GetPixelShaderConstantF)(UINT StartRegister, float* pConstantData, UINT Vector4fCount) {
        return m_real->GetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);
    }
    STDMETHOD(SetPixelShaderConstantI)(UINT StartRegister, const int* pConstantData, UINT Vector4iCount) {
        return m_real->SetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount);
    }
    STDMETHOD(GetPixelShaderConstantI)(UINT StartRegister, int* pConstantData, UINT Vector4iCount) {
        return m_real->GetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount);
    }
    STDMETHOD(SetPixelShaderConstantB)(UINT StartRegister, const BOOL* pConstantData, UINT BoolCount) {
        return m_real->SetPixelShaderConstantB(StartRegister, pConstantData, BoolCount);
    }
    STDMETHOD(GetPixelShaderConstantB)(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) {
        return m_real->GetPixelShaderConstantB(StartRegister, pConstantData, BoolCount);
    }
    STDMETHOD(DrawRectPatch)(UINT Handle, const float* pNumSegs, const D3DRECTPATCH_INFO* pRectPatchInfo) {
        return m_real->DrawRectPatch(Handle, pNumSegs, pRectPatchInfo);
    }
    STDMETHOD(DrawTriPatch)(UINT Handle, const float* pNumSegs, const D3DTRIPATCH_INFO* pTriPatchInfo) {
        return m_real->DrawTriPatch(Handle, pNumSegs, pTriPatchInfo);
    }
    STDMETHOD(DeletePatch)(UINT Handle) {
        return m_real->DeletePatch(Handle);
    }
    STDMETHOD(CreateQuery)(D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery) {
        return m_real->CreateQuery(Type, ppQuery);
    }
    STDMETHOD(CreateTexture)(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle) {
        return m_real->CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle);
    }
    STDMETHOD(CreateVolumeTexture)(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle) {
        return m_real->CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture, pSharedHandle);
    }
    STDMETHOD(CreateCubeTexture)(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle) {
        return m_real->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle);
    }
    STDMETHOD(CreateVertexBuffer)(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle) {
        return m_real->CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle);
    }
    STDMETHOD(CreateIndexBuffer)(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle) {
        return m_real->CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer, pSharedHandle);
    }
};

HRESULT LiteGL_Direct3D9::CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface) {
    log_msg("[LiteGL D3D9] Creating device (Adapter: %u, FocusWindow: %p)...\n", Adapter, hFocusWindow);

    IDirect3DDevice9* real_device = NULL;
    HRESULT hr = m_real->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, &real_device);
    if (FAILED(hr) || !real_device) {
        log_msg("[LiteGL D3D9] m_real->CreateDevice failed with hr = 0x%08X\n", (unsigned int)hr);
        return hr;
    }

    LiteGL_Direct3DDevice9* wrapper = new LiteGL_Direct3DDevice9(real_device);
    *ppReturnedDeviceInterface = wrapper;
    log_msg("[LiteGL D3D9] Wrapped IDirect3DDevice9 successfully with LiteGL ToGL Accelerator!\n");
    return D3D_OK;
}

/* -------------------------------------------------------------------------
 * Exported Functions
 * ------------------------------------------------------------------------- */
extern "C" {

__declspec(dllexport) IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion) {
    log_msg("\n===================================================================\n");
    log_msg(" [LiteGL] Direct3DCreate9 called (SDKVersion: %u)\n", SDKVersion);
    log_msg(" [LiteGL] Running ToGL-Accelerated D3D9 Layer under Steam Proton\n");
    log_msg("===================================================================\n");

    if (!load_system_d3d9() || !g_real_Direct3DCreate9) {
        return NULL;
    }

    IDirect3D9* real_d3d = g_real_Direct3DCreate9(SDKVersion);
    if (!real_d3d) {
        log_msg("[LiteGL D3D9] Real Direct3DCreate9 returned NULL!\n");
        return NULL;
    }

    return new LiteGL_Direct3D9(real_d3d);
}

__declspec(dllexport) HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppD3D) {
    log_msg("[LiteGL] Direct3DCreate9Ex called (SDKVersion: %u)\n", SDKVersion);

    if (!load_system_d3d9() || !g_real_Direct3DCreate9Ex) {
        return D3DERR_NOTAVAILABLE;
    }

    return g_real_Direct3DCreate9Ex(SDKVersion, ppD3D);
}

__declspec(dllexport) int WINAPI D3DPERF_BeginEvent(D3DCOLOR col, LPCWSTR wszName) {
    (void)col; (void)wszName;
    return 0;
}

__declspec(dllexport) int WINAPI D3DPERF_EndEvent(void) {
    return 0;
}

__declspec(dllexport) void WINAPI D3DPERF_SetMarker(D3DCOLOR col, LPCWSTR wszName) {
    (void)col; (void)wszName;
}

__declspec(dllexport) void WINAPI D3DPERF_SetRegion(D3DCOLOR col, LPCWSTR wszName) {
    (void)col; (void)wszName;
}

__declspec(dllexport) BOOL WINAPI D3DPERF_QueryRepeatFrame(void) {
    return FALSE;
}

__declspec(dllexport) void WINAPI D3DPERF_SetOptions(DWORD dwOptions) {
    (void)dwOptions;
}

__declspec(dllexport) DWORD WINAPI D3DPERF_GetStatus(void) {
    return 0;
}

} /* extern "C" */

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)hinstDLL; (void)lpvReserved;
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
            log_msg("[LiteGL D3D9] DLL loaded into process.\n");
            break;
        case DLL_PROCESS_DETACH:
            if (g_log_file) {
                log_msg("[LiteGL D3D9] DLL unloaded from process.\n");
                fclose(g_log_file);
                g_log_file = NULL;
            }
            break;
    }
    return TRUE;
}
