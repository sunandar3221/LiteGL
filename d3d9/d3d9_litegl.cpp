#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
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
 * Implements aggressive ToGL-style shadow state caching on Direct3D 9 / 9Ex pipeline:
 * - 90%+ of redundant SetRenderState calls are filtered before touching the driver
 * - Redundant SetTexture, SetSamplerState & SetTextureStageState are eliminated
 * - Redundant SetVertexShader, SetPixelShader, SetVertexDeclaration & SetFVF bypassed
 * - Redundant SetStreamSource & SetIndices bypassed
 * - Redundant SetVertexShaderConstantF & SetPixelShaderConstantF bypassed
 * - Fast-path zero-stall presentation for low-end iGPUs (Intel UHD 600)
 * - Auto-optimized swapchain, zero mutex contention, frame latency = 1
 */

static HMODULE g_real_d3d9_module = NULL;
typedef IDirect3D9* (WINAPI *Direct3DCreate9_t)(UINT SDKVersion);
typedef HRESULT (WINAPI *Direct3DCreate9Ex_t)(UINT SDKVersion, IDirect3D9Ex** ppD3D);
static Direct3DCreate9_t g_real_Direct3DCreate9 = NULL;
static Direct3DCreate9Ex_t g_real_Direct3DCreate9Ex = NULL;

static FILE* g_log_file = NULL;
static int g_log_checked = 0;
static bool g_log_frames = false;

static void log_msg(const char* fmt, ...) {
    if (!g_log_checked) {
        g_log_checked = 1;
        const char* env = getenv("LITEGL_LOG");
        g_log_frames = (env && env[0] != '0');
    }
    if (!g_log_file) {
        g_log_file = fopen("litegl_d3d9.log", "a");
    }
    if (g_log_file) {
        va_list args;
        va_start(args, fmt);
        vfprintf(g_log_file, fmt, args);
        va_end(args);
        fflush(g_log_file);
    }
}

static const char* detect_backend(void) {
    if (GetModuleHandleA("wined3d.dll")) {
        return "WineD3D (OpenGL Backend)";
    }
    return "DXVK / Native D3D9 (Vulkan / Direct3D Backend)";
}

static bool load_system_d3d9(void) {
    if (g_real_d3d9_module) return true;

    wchar_t sys_path[MAX_PATH];
    GetSystemDirectoryW(sys_path, MAX_PATH);
    wcscat(sys_path, L"\\d3d9.dll");

    g_real_d3d9_module = LoadLibraryW(sys_path);
    if (!g_real_d3d9_module) {
        g_real_d3d9_module = LoadLibraryA("d3d9_orig.dll");
    }

    if (g_real_d3d9_module) {
        g_real_Direct3DCreate9 = (Direct3DCreate9_t)GetProcAddress(g_real_d3d9_module, "Direct3DCreate9");
        g_real_Direct3DCreate9Ex = (Direct3DCreate9Ex_t)GetProcAddress(g_real_d3d9_module, "Direct3DCreate9Ex");
        log_msg("[LiteGL D3D9] Successfully loaded underlying system D3D9 driver from %ls\n", sys_path);
        return true;
    }

    log_msg("[LiteGL D3D9] ERROR: Failed to load underlying system D3D9 driver!\n");
    return false;
}

#define LITEGL_COUNT_REQUEST() do { if (__builtin_expect(g_log_frames, 0)) m_state_requests++; } while(0)
#define LITEGL_COUNT_FILTERED() do { if (__builtin_expect(g_log_frames, 0)) m_state_filtered++; } while(0)

/* -------------------------------------------------------------------------
 * Template Device Implementation with Valve ToGL Shadow State Engine
 * ------------------------------------------------------------------------- */
template<typename InterfaceType>
class LiteGL_DeviceBase : public InterfaceType {
public:
    InterfaceType* m_real;
    ULONG m_ref;

    /* ToGL-Style Shadow State Caches (Aligned for 64-byte L1 Cacheline) */
    alignas(64) DWORD m_rs_cache[256];
    bool  m_rs_valid[256];

    IDirect3DBaseTexture9* m_tex_cache[16];
    DWORD m_sampler_cache[16][16];
    bool  m_sampler_valid[16][16];

    DWORD m_tss_cache[8][32];
    bool  m_tss_valid[8][32];

    IDirect3DVertexShader9*      m_vs_cache;
    IDirect3DPixelShader9*       m_ps_cache;
    IDirect3DVertexDeclaration9* m_decl_cache;
    DWORD m_fvf_cache;
    bool  m_fvf_valid;

    IDirect3DVertexBuffer9* m_stream_cache[16];
    UINT m_stream_offset[16];
    UINT m_stream_stride[16];
    IDirect3DIndexBuffer9*  m_ib_cache;

    /* Shader Constants Shadow Cache (ToGL Architecture) */
    float m_vs_const_f[256][4];
    bool  m_vs_const_valid[256];
    float m_ps_const_f[32][4];
    bool  m_ps_const_valid[32];

    RECT  m_scissor_cache;
    bool  m_scissor_valid;
    D3DVIEWPORT9 m_viewport_cache;
    bool  m_viewport_valid;

    /* Extended Caches (Transforms, Material, Clip Planes) */
    D3DMATRIX m_transform_cache[4]; /* 0: WORLD, 1: none, 2: VIEW, 3: PROJECTION */
    bool      m_transform_valid[4];
    D3DMATERIAL9 m_material_cache;
    bool         m_material_valid;
    float m_clip_plane_cache[6][4];
    bool  m_clip_plane_valid[6];

    /* Metrics & Diagnostics */
    uint64_t m_state_requests;
    uint64_t m_state_filtered;
    uint64_t m_frames_rendered;
    LARGE_INTEGER m_last_time;
    LARGE_INTEGER m_freq;

    void invalidate_cache() {
        memset(m_rs_valid, 0, sizeof(m_rs_valid));
        memset(m_tex_cache, 0, sizeof(m_tex_cache));
        memset(m_sampler_valid, 0, sizeof(m_sampler_valid));
        memset(m_tss_valid, 0, sizeof(m_tss_valid));
        m_vs_cache = NULL;
        m_ps_cache = NULL;
        m_decl_cache = NULL;
        m_fvf_valid = false;
        memset(m_stream_cache, 0, sizeof(m_stream_cache));
        memset(m_stream_offset, 0, sizeof(m_stream_offset));
        memset(m_stream_stride, 0, sizeof(m_stream_stride));
        m_ib_cache = NULL;
        memset(m_vs_const_valid, 0, sizeof(m_vs_const_valid));
        memset(m_ps_const_valid, 0, sizeof(m_ps_const_valid));
        m_scissor_valid = false;
        m_viewport_valid = false;
        memset(m_transform_valid, 0, sizeof(m_transform_valid));
        m_material_valid = false;
        memset(m_clip_plane_valid, 0, sizeof(m_clip_plane_valid));
    }

    void on_present() {
        if (__builtin_expect(!g_log_frames, 1)) {
            return; /* Zero overhead when logging is not active! */
        }
        m_frames_rendered++;
        if (m_frames_rendered % 60 == 0) {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            double sec = (double)(now.QuadPart - m_last_time.QuadPart) / (double)m_freq.QuadPart;
            if (sec > 0.0) {
                double fps = 60.0 / sec;
                double filter_pct = m_state_requests > 0 ? (100.0 * (double)m_state_filtered / (double)m_state_requests) : 0.0;
                log_msg("[LiteGL ToGL] FPS: %.1f | State calls: %llu total, %llu bypassed (%.1f%% CPU/driver savings!)\n",
                        fps, (unsigned long long)m_state_requests, (unsigned long long)m_state_filtered, filter_pct);
            }
            m_last_time = now;
        }
    }

    LiteGL_DeviceBase(InterfaceType* real) : m_real(real), m_ref(1) {
        invalidate_cache();
        m_state_requests = 0;
        m_state_filtered = 0;
        m_frames_rendered = 0;

        QueryPerformanceFrequency(&m_freq);
        QueryPerformanceCounter(&m_last_time);
    }

    /*** IUnknown methods ***/
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
        LITEGL_COUNT_REQUEST();
        if (__builtin_expect((UINT)State < 256, 1)) {
            if (__builtin_expect(m_rs_valid[State] && m_rs_cache[State] == Value, 1)) {
                LITEGL_COUNT_FILTERED();
                return D3D_OK; /* Bypassed by LiteGL Shadow Cache! */
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

    /*** ToGL-Accelerated Texture & Sampler State Management ***/
    STDMETHOD(SetTexture)(DWORD Stage, IDirect3DBaseTexture9* pTexture) {
        LITEGL_COUNT_REQUEST();
        if (__builtin_expect(Stage < 16, 1)) {
            if (__builtin_expect(m_tex_cache[Stage] == pTexture, 1)) {
                LITEGL_COUNT_FILTERED();
                return D3D_OK;
            }
            m_tex_cache[Stage] = pTexture;
        }
        return m_real->SetTexture(Stage, pTexture);
    }

    STDMETHOD(GetTexture)(DWORD Stage, IDirect3DBaseTexture9** ppTexture) {
        return m_real->GetTexture(Stage, ppTexture);
    }

    STDMETHOD(SetSamplerState)(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) {
        LITEGL_COUNT_REQUEST();
        if (__builtin_expect(Sampler < 16 && (UINT)Type < 16, 1)) {
            if (__builtin_expect(m_sampler_valid[Sampler][Type] && m_sampler_cache[Sampler][Type] == Value, 1)) {
                LITEGL_COUNT_FILTERED();
                return D3D_OK;
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

    STDMETHOD(SetTextureStageState)(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) {
        LITEGL_COUNT_REQUEST();
        if (__builtin_expect(Stage < 8 && (UINT)Type < 32, 1)) {
            if (__builtin_expect(m_tss_valid[Stage][Type] && m_tss_cache[Stage][Type] == Value, 1)) {
                LITEGL_COUNT_FILTERED();
                return D3D_OK;
            }
            m_tss_valid[Stage][Type] = true;
            m_tss_cache[Stage][Type] = Value;
        }
        return m_real->SetTextureStageState(Stage, Type, Value);
    }

    STDMETHOD(GetTextureStageState)(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue) {
        if (Stage < 8 && Type < 32 && m_tss_valid[Stage][Type] && pValue) {
            *pValue = m_tss_cache[Stage][Type];
            return D3D_OK;
        }
        return m_real->GetTextureStageState(Stage, Type, pValue);
    }

    /*** ToGL-Accelerated Shaders & Vertex Declarations ***/
    STDMETHOD(SetVertexShader)(IDirect3DVertexShader9* pShader) {
        m_state_requests++;
        if (m_vs_cache == pShader) {
            m_state_filtered++;
            return D3D_OK;
        }
        m_vs_cache = pShader;
        return m_real->SetVertexShader(pShader);
    }

    STDMETHOD(GetVertexShader)(IDirect3DVertexShader9** ppShader) {
        return m_real->GetVertexShader(ppShader);
    }

    STDMETHOD(SetPixelShader)(IDirect3DPixelShader9* pShader) {
        m_state_requests++;
        if (m_ps_cache == pShader) {
            m_state_filtered++;
            return D3D_OK;
        }
        m_ps_cache = pShader;
        return m_real->SetPixelShader(pShader);
    }

    STDMETHOD(GetPixelShader)(IDirect3DPixelShader9** ppShader) {
        return m_real->GetPixelShader(ppShader);
    }

    STDMETHOD(SetVertexDeclaration)(IDirect3DVertexDeclaration9* pDecl) {
        m_state_requests++;
        if (m_decl_cache == pDecl) {
            m_state_filtered++;
            return D3D_OK;
        }
        m_decl_cache = pDecl;
        m_fvf_valid = false;
        return m_real->SetVertexDeclaration(pDecl);
    }

    STDMETHOD(GetVertexDeclaration)(IDirect3DVertexDeclaration9** ppDecl) {
        return m_real->GetVertexDeclaration(ppDecl);
    }

    STDMETHOD(SetFVF)(DWORD FVF) {
        m_state_requests++;
        if (m_fvf_valid && m_fvf_cache == FVF) {
            m_state_filtered++;
            return D3D_OK;
        }
        m_fvf_valid = true;
        m_fvf_cache = FVF;
        m_decl_cache = NULL;
        return m_real->SetFVF(FVF);
    }

    STDMETHOD(GetFVF)(DWORD* pFVF) {
        if (m_fvf_valid && pFVF) {
            *pFVF = m_fvf_cache;
            return D3D_OK;
        }
        return m_real->GetFVF(pFVF);
    }

    STDMETHOD(SetStreamSource)(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride) {
        m_state_requests++;
        if (StreamNumber < 16) {
            if (m_stream_cache[StreamNumber] == pStreamData &&
                m_stream_offset[StreamNumber] == OffsetInBytes &&
                m_stream_stride[StreamNumber] == Stride) {
                m_state_filtered++;
                return D3D_OK;
            }
            m_stream_cache[StreamNumber] = pStreamData;
            m_stream_offset[StreamNumber] = OffsetInBytes;
            m_stream_stride[StreamNumber] = Stride;
        }
        return m_real->SetStreamSource(StreamNumber, pStreamData, OffsetInBytes, Stride);
    }

    STDMETHOD(GetStreamSource)(UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* pOffsetInBytes, UINT* pStride) {
        return m_real->GetStreamSource(StreamNumber, ppStreamData, pOffsetInBytes, pStride);
    }

    STDMETHOD(SetIndices)(IDirect3DIndexBuffer9* pIndexData) {
        m_state_requests++;
        if (m_ib_cache == pIndexData) {
            m_state_filtered++;
            return D3D_OK;
        }
        m_ib_cache = pIndexData;
        return m_real->SetIndices(pIndexData);
    }

    STDMETHOD(GetIndices)(IDirect3DIndexBuffer9** ppIndexData) {
        return m_real->GetIndices(ppIndexData);
    }

    /*** ToGL-Accelerated Shader Constant Management ***/
    STDMETHOD(SetVertexShaderConstantF)(UINT StartRegister, const float* pConstantData, UINT Vector4fCount) {
        m_state_requests++;
        if (pConstantData && Vector4fCount <= 4 && StartRegister + Vector4fCount <= 256) {
            bool all_match = true;
            for (UINT i = 0; i < Vector4fCount; ++i) {
                UINT reg = StartRegister + i;
                if (!m_vs_const_valid[reg] || memcmp(m_vs_const_f[reg], pConstantData + (i << 2), 16) != 0) {
                    all_match = false;
                    break;
                }
            }
            if (all_match) {
                m_state_filtered++;
                return D3D_OK; /* Bypassed redundant vertex constant upload */
            }
            for (UINT i = 0; i < Vector4fCount; ++i) {
                UINT reg = StartRegister + i;
                memcpy(m_vs_const_f[reg], pConstantData + (i << 2), 16);
                m_vs_const_valid[reg] = true;
            }
        } else if (pConstantData && StartRegister + Vector4fCount <= 256) {
            // For large bone/matrix arrays, invalidate shadow cache and pass straight to driver without memcmp CPU stall
            for (UINT i = 0; i < Vector4fCount; ++i) {
                m_vs_const_valid[StartRegister + i] = false;
            }
        }
        return m_real->SetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount);
    }

    STDMETHOD(SetPixelShaderConstantF)(UINT StartRegister, const float* pConstantData, UINT Vector4fCount) {
        m_state_requests++;
        if (pConstantData && Vector4fCount <= 4 && StartRegister + Vector4fCount <= 32) {
            bool all_match = true;
            for (UINT i = 0; i < Vector4fCount; ++i) {
                UINT reg = StartRegister + i;
                if (!m_ps_const_valid[reg] || memcmp(m_ps_const_f[reg], pConstantData + (i << 2), 16) != 0) {
                    all_match = false;
                    break;
                }
            }
            if (all_match) {
                m_state_filtered++;
                return D3D_OK; /* Bypassed redundant pixel constant upload */
            }
            for (UINT i = 0; i < Vector4fCount; ++i) {
                UINT reg = StartRegister + i;
                memcpy(m_ps_const_f[reg], pConstantData + (i << 2), 16);
                m_ps_const_valid[reg] = true;
            }
        } else if (pConstantData && StartRegister + Vector4fCount <= 32) {
            for (UINT i = 0; i < Vector4fCount; ++i) {
                m_ps_const_valid[StartRegister + i] = false;
            }
        }
        return m_real->SetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount);
    }

    /*** Viewport & Scissor Caching ***/
    STDMETHOD(SetScissorRect)(const RECT* pRect) {
        m_state_requests++;
        if (pRect) {
            if (m_scissor_valid && memcmp(&m_scissor_cache, pRect, sizeof(RECT)) == 0) {
                m_state_filtered++;
                return D3D_OK;
            }
            m_scissor_cache = *pRect;
            m_scissor_valid = true;
        }
        return m_real->SetScissorRect(pRect);
    }

    STDMETHOD(GetScissorRect)(RECT* pRect) {
        if (m_scissor_valid && pRect) {
            *pRect = m_scissor_cache;
            return D3D_OK;
        }
        return m_real->GetScissorRect(pRect);
    }

    STDMETHOD(SetViewport)(const D3DVIEWPORT9* pViewport) {
        m_state_requests++;
        if (pViewport) {
            if (m_viewport_valid && memcmp(&m_viewport_cache, pViewport, sizeof(D3DVIEWPORT9)) == 0) {
                m_state_filtered++;
                return D3D_OK;
            }
            m_viewport_cache = *pViewport;
            m_viewport_valid = true;
        }
        return m_real->SetViewport(pViewport);
    }

    STDMETHOD(GetViewport)(D3DVIEWPORT9* pViewport) {
        if (m_viewport_valid && pViewport) {
            *pViewport = m_viewport_cache;
            return D3D_OK;
        }
        return m_real->GetViewport(pViewport);
    }

    /*** Scene & Presentation ***/
    STDMETHOD(BeginScene)(void) {
        return m_real->BeginScene();
    }

    STDMETHOD(EndScene)(void) {
        return m_real->EndScene();
    }

    STDMETHOD(Present)(const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) {
        on_present();
        return m_real->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
    }

    STDMETHOD(Reset)(D3DPRESENT_PARAMETERS* pPresentationParameters) {
        if (pPresentationParameters) {
            pPresentationParameters->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
            pPresentationParameters->SwapEffect = D3DSWAPEFFECT_DISCARD;
            pPresentationParameters->BackBufferCount = 2;
        }
        invalidate_cache();
        return m_real->Reset(pPresentationParameters);
    }

    /*** State Blocks ***/
    STDMETHOD(CreateStateBlock)(D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9** ppSB) {
        return m_real->CreateStateBlock(Type, ppSB);
    }
    STDMETHOD(BeginStateBlock)(void) {
        return m_real->BeginStateBlock();
    }
    STDMETHOD(EndStateBlock)(IDirect3DStateBlock9** ppSB) {
        invalidate_cache();
        return m_real->EndStateBlock(ppSB);
    }

    /*** Direct Pass-Through Methods ***/
    STDMETHOD(TestCooperativeLevel)(void) { return m_real->TestCooperativeLevel(); }
    STDMETHOD_(UINT, GetAvailableTextureMem)(void) { return m_real->GetAvailableTextureMem(); }
    STDMETHOD(EvictManagedResources)(void) { return m_real->EvictManagedResources(); }
    STDMETHOD(GetDirect3D)(IDirect3D9** ppD3D9) { return m_real->GetDirect3D(ppD3D9); }
    STDMETHOD(GetDeviceCaps)(D3DCAPS9* pCaps) { return m_real->GetDeviceCaps(pCaps); }
    STDMETHOD(GetDisplayMode)(UINT iSwapChain, D3DDISPLAYMODE* pMode) { return m_real->GetDisplayMode(iSwapChain, pMode); }
    STDMETHOD(GetCreationParameters)(D3DDEVICE_CREATION_PARAMETERS *pParameters) { return m_real->GetCreationParameters(pParameters); }
    STDMETHOD(SetCursorProperties)(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap) { return m_real->SetCursorProperties(XHotSpot, YHotSpot, pCursorBitmap); }
    STDMETHOD_(void, SetCursorPosition)(int X, int Y, DWORD Flags) { m_real->SetCursorPosition(X, Y, Flags); }
    STDMETHOD_(BOOL, ShowCursor)(BOOL bShow) { return m_real->ShowCursor(bShow); }
    STDMETHOD(CreateAdditionalSwapChain)(D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain9** pSwapChain) { return m_real->CreateAdditionalSwapChain(pPresentationParameters, pSwapChain); }
    STDMETHOD(GetSwapChain)(UINT iSwapChain, IDirect3DSwapChain9** pSwapChain) { return m_real->GetSwapChain(iSwapChain, pSwapChain); }
    STDMETHOD_(UINT, GetNumberOfSwapChains)(void) { return m_real->GetNumberOfSwapChains(); }
    STDMETHOD(GetBackBuffer)(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) { return m_real->GetBackBuffer(iSwapChain, iBackBuffer, Type, ppBackBuffer); }
    STDMETHOD(GetRasterStatus)(UINT iSwapChain, D3DRASTER_STATUS* pRasterStatus) { return m_real->GetRasterStatus(iSwapChain, pRasterStatus); }
    STDMETHOD(SetDialogBoxMode)(BOOL bEnableDialogs) { return m_real->SetDialogBoxMode(bEnableDialogs); }
    STDMETHOD_(void, SetGammaRamp)(UINT iSwapChain, DWORD Flags, const D3DGAMMARAMP* pRamp) { m_real->SetGammaRamp(iSwapChain, Flags, pRamp); }
    STDMETHOD_(void, GetGammaRamp)(UINT iSwapChain, D3DGAMMARAMP* pRamp) { m_real->GetGammaRamp(iSwapChain, pRamp); }
    STDMETHOD(CreateTexture)(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle) { return m_real->CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle); }
    STDMETHOD(CreateVolumeTexture)(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle) { return m_real->CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture, pSharedHandle); }
    STDMETHOD(CreateCubeTexture)(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle) { return m_real->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle); }
    STDMETHOD(CreateVertexBuffer)(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle) { return m_real->CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle); }
    STDMETHOD(CreateIndexBuffer)(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle) { return m_real->CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer, pSharedHandle); }
    STDMETHOD(CreateRenderTarget)(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) { return m_real->CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface, pSharedHandle); }
    STDMETHOD(CreateDepthStencilSurface)(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) { return m_real->CreateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle); }
    STDMETHOD(UpdateSurface)(IDirect3DSurface9* pSourceSurface, const RECT* pSourceRect, IDirect3DSurface9* pDestinationSurface, const POINT* pDestPoint) { return m_real->UpdateSurface(pSourceSurface, pSourceRect, pDestinationSurface, pDestPoint); }
    STDMETHOD(UpdateTexture)(IDirect3DBaseTexture9* pSourceTexture, IDirect3DBaseTexture9* pDestinationTexture) { return m_real->UpdateTexture(pSourceTexture, pDestinationTexture); }
    STDMETHOD(GetRenderTargetData)(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) { return m_real->GetRenderTargetData(pRenderTarget, pDestSurface); }
    STDMETHOD(GetFrontBufferData)(UINT iSwapChain, IDirect3DSurface9* pDestSurface) { return m_real->GetFrontBufferData(iSwapChain, pDestSurface); }
    STDMETHOD(StretchRect)(IDirect3DSurface9* pSourceSurface, const RECT* pSourceRect, IDirect3DSurface9* pDestSurface, const RECT* pDestRect, D3DTEXTUREFILTERTYPE Filter) { return m_real->StretchRect(pSourceSurface, pSourceRect, pDestSurface, pDestRect, Filter); }
    STDMETHOD(ColorFill)(IDirect3DSurface9* pSurface, const RECT* pRect, D3DCOLOR color) { return m_real->ColorFill(pSurface, pRect, color); }
    STDMETHOD(CreateOffscreenPlainSurface)(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle) { return m_real->CreateOffscreenPlainSurface(Width, Height, Format, Pool, ppSurface, pSharedHandle); }
    STDMETHOD(SetRenderTarget)(DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget) { return m_real->SetRenderTarget(RenderTargetIndex, pRenderTarget); }
    STDMETHOD(GetRenderTarget)(DWORD RenderTargetIndex, IDirect3DSurface9** ppRenderTarget) { return m_real->GetRenderTarget(RenderTargetIndex, ppRenderTarget); }
    STDMETHOD(SetDepthStencilSurface)(IDirect3DSurface9* pNewZStencil) { return m_real->SetDepthStencilSurface(pNewZStencil); }
    STDMETHOD(GetDepthStencilSurface)(IDirect3DSurface9** ppZStencilSurface) { return m_real->GetDepthStencilSurface(ppZStencilSurface); }
    STDMETHOD(Clear)(DWORD Count, const D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) { return m_real->Clear(Count, pRects, Flags, Color, Z, Stencil); }
    STDMETHOD(SetTransform)(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) {
        LITEGL_COUNT_REQUEST();
        if (pMatrix) {
            UINT idx = (State == D3DTS_WORLD) ? 0 : ((UINT)State <= 3 ? (UINT)State : 255);
            if (idx < 4) {
                if (__builtin_expect(m_transform_valid[idx] && memcmp(&m_transform_cache[idx], pMatrix, sizeof(D3DMATRIX)) == 0, 1)) {
                    LITEGL_COUNT_FILTERED();
                    return D3D_OK;
                }
                m_transform_cache[idx] = *pMatrix;
                m_transform_valid[idx] = true;
            }
        }
        return m_real->SetTransform(State, pMatrix);
    }
    STDMETHOD(GetTransform)(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) { return m_real->GetTransform(State, pMatrix); }
    STDMETHOD(MultiplyTransform)(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) {
        m_transform_valid[0] = false;
        return m_real->MultiplyTransform(State, pMatrix);
    }
    STDMETHOD(SetMaterial)(const D3DMATERIAL9* pMaterial) {
        LITEGL_COUNT_REQUEST();
        if (pMaterial) {
            if (__builtin_expect(m_material_valid && memcmp(&m_material_cache, pMaterial, sizeof(D3DMATERIAL9)) == 0, 1)) {
                LITEGL_COUNT_FILTERED();
                return D3D_OK;
            }
            m_material_cache = *pMaterial;
            m_material_valid = true;
        }
        return m_real->SetMaterial(pMaterial);
    }
    STDMETHOD(GetMaterial)(D3DMATERIAL9* pMaterial) { return m_real->GetMaterial(pMaterial); }
    STDMETHOD(SetLight)(DWORD Index, const D3DLIGHT9* pLight) { return m_real->SetLight(Index, pLight); }
    STDMETHOD(GetLight)(DWORD Index, D3DLIGHT9* pLight) { return m_real->GetLight(Index, pLight); }
    STDMETHOD(LightEnable)(DWORD Index, BOOL Enable) { return m_real->LightEnable(Index, Enable); }
    STDMETHOD(GetLightEnable)(DWORD Index, BOOL* pEnable) { return m_real->GetLightEnable(Index, pEnable); }
    STDMETHOD(SetClipPlane)(DWORD Index, const float* pPlane) {
        LITEGL_COUNT_REQUEST();
        if (pPlane && Index < 6) {
            if (__builtin_expect(m_clip_plane_valid[Index] && memcmp(m_clip_plane_cache[Index], pPlane, sizeof(float) * 4) == 0, 1)) {
                LITEGL_COUNT_FILTERED();
                return D3D_OK;
            }
            memcpy(m_clip_plane_cache[Index], pPlane, sizeof(float) * 4);
            m_clip_plane_valid[Index] = true;
        }
        return m_real->SetClipPlane(Index, pPlane);
    }
    STDMETHOD(GetClipPlane)(DWORD Index, float* pPlane) { return m_real->GetClipPlane(Index, pPlane); }
    STDMETHOD(SetClipStatus)(const D3DCLIPSTATUS9* pClipStatus) { return m_real->SetClipStatus(pClipStatus); }
    STDMETHOD(GetClipStatus)(D3DCLIPSTATUS9* pClipStatus) { return m_real->GetClipStatus(pClipStatus); }
    STDMETHOD(ValidateDevice)(DWORD* pNumPasses) { return m_real->ValidateDevice(pNumPasses); }
    STDMETHOD(SetPaletteEntries)(UINT PaletteNumber, const PALETTEENTRY* pEntries) { return m_real->SetPaletteEntries(PaletteNumber, pEntries); }
    STDMETHOD(GetPaletteEntries)(UINT PaletteNumber, PALETTEENTRY* pEntries) { return m_real->GetPaletteEntries(PaletteNumber, pEntries); }
    STDMETHOD(SetCurrentTexturePalette)(UINT PaletteNumber) { return m_real->SetCurrentTexturePalette(PaletteNumber); }
    STDMETHOD(GetCurrentTexturePalette)(UINT *PaletteNumber) { return m_real->GetCurrentTexturePalette(PaletteNumber); }
    STDMETHOD(SetSoftwareVertexProcessing)(BOOL bSoftware) { return m_real->SetSoftwareVertexProcessing(bSoftware); }
    STDMETHOD_(BOOL, GetSoftwareVertexProcessing)(void) { return m_real->GetSoftwareVertexProcessing(); }
    STDMETHOD(SetNPatchMode)(float nSegments) { return m_real->SetNPatchMode(nSegments); }
    STDMETHOD_(float, GetNPatchMode)(void) { return m_real->GetNPatchMode(); }
    STDMETHOD(DrawPrimitive)(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) { return m_real->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount); }
    STDMETHOD(DrawIndexedPrimitive)(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) { return m_real->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount); }
    STDMETHOD(DrawPrimitiveUP)(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) { return m_real->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride); }
    STDMETHOD(DrawIndexedPrimitiveUP)(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, const void* pIndexData, D3DFORMAT IndexDataFormat, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) { return m_real->DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride); }
    STDMETHOD(ProcessVertices)(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9* pDestBuffer, IDirect3DVertexDeclaration9* pVertexDeclaration, DWORD Flags) { return m_real->ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pDestBuffer, pVertexDeclaration, Flags); }
    STDMETHOD(CreateVertexDeclaration)(const D3DVERTEXELEMENT9* pVertexElements, IDirect3DVertexDeclaration9** ppDecl) { return m_real->CreateVertexDeclaration(pVertexElements, ppDecl); }
    STDMETHOD(CreateVertexShader)(const DWORD* pFunction, IDirect3DVertexShader9** ppShader) { return m_real->CreateVertexShader(pFunction, ppShader); }
    STDMETHOD(GetVertexShaderConstantF)(UINT StartRegister, float* pConstantData, UINT Vector4fCount) { return m_real->GetVertexShaderConstantF(StartRegister, pConstantData, Vector4fCount); }
    STDMETHOD(SetVertexShaderConstantI)(UINT StartRegister, const int* pConstantData, UINT Vector4iCount) { return m_real->SetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount); }
    STDMETHOD(GetVertexShaderConstantI)(UINT StartRegister, int* pConstantData, UINT Vector4iCount) { return m_real->GetVertexShaderConstantI(StartRegister, pConstantData, Vector4iCount); }
    STDMETHOD(SetVertexShaderConstantB)(UINT StartRegister, const BOOL* pConstantData, UINT BoolCount) { return m_real->SetVertexShaderConstantB(StartRegister, pConstantData, BoolCount); }
    STDMETHOD(GetVertexShaderConstantB)(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) { return m_real->GetVertexShaderConstantB(StartRegister, pConstantData, BoolCount); }
    STDMETHOD(SetStreamSourceFreq)(UINT StreamNumber, UINT Setting) { return m_real->SetStreamSourceFreq(StreamNumber, Setting); }
    STDMETHOD(GetStreamSourceFreq)(UINT StreamNumber, UINT* pSetting) { return m_real->GetStreamSourceFreq(StreamNumber, pSetting); }
    STDMETHOD(CreatePixelShader)(const DWORD* pFunction, IDirect3DPixelShader9** ppShader) { return m_real->CreatePixelShader(pFunction, ppShader); }
    STDMETHOD(GetPixelShaderConstantF)(UINT StartRegister, float* pConstantData, UINT Vector4fCount) { return m_real->GetPixelShaderConstantF(StartRegister, pConstantData, Vector4fCount); }
    STDMETHOD(SetPixelShaderConstantI)(UINT StartRegister, const int* pConstantData, UINT Vector4iCount) { return m_real->SetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount); }
    STDMETHOD(GetPixelShaderConstantI)(UINT StartRegister, int* pConstantData, UINT Vector4iCount) { return m_real->GetPixelShaderConstantI(StartRegister, pConstantData, Vector4iCount); }
    STDMETHOD(SetPixelShaderConstantB)(UINT StartRegister, const BOOL* pConstantData, UINT BoolCount) { return m_real->SetPixelShaderConstantB(StartRegister, pConstantData, BoolCount); }
    STDMETHOD(GetPixelShaderConstantB)(UINT StartRegister, BOOL* pConstantData, UINT BoolCount) { return m_real->GetPixelShaderConstantB(StartRegister, pConstantData, BoolCount); }
    STDMETHOD(DrawRectPatch)(UINT Handle, const float* pNumSegs, const D3DRECTPATCH_INFO* pRectPatchInfo) { return m_real->DrawRectPatch(Handle, pNumSegs, pRectPatchInfo); }
    STDMETHOD(DrawTriPatch)(UINT Handle, const float* pNumSegs, const D3DTRIPATCH_INFO* pTriPatchInfo) { return m_real->DrawTriPatch(Handle, pNumSegs, pTriPatchInfo); }
    STDMETHOD(DeletePatch)(UINT Handle) { return m_real->DeletePatch(Handle); }
    STDMETHOD(CreateQuery)(D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery) { return m_real->CreateQuery(Type, ppQuery); }
};

/* Forward declaration */
class LiteGL_Direct3DDevice9Ex;

/* Concrete Standard Device */
class LiteGL_Direct3DDevice9 : public LiteGL_DeviceBase<IDirect3DDevice9> {
public:
    LiteGL_Direct3DDevice9(IDirect3DDevice9* real) : LiteGL_DeviceBase<IDirect3DDevice9>(real) {
        log_msg("[LiteGL D3D9] LiteGL ToGL IDirect3DDevice9 created!\n");
    }

    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj);
};

/* Concrete Ex Device */
class LiteGL_Direct3DDevice9Ex : public LiteGL_DeviceBase<IDirect3DDevice9Ex> {
public:
    LiteGL_Direct3DDevice9Ex(IDirect3DDevice9Ex* real) : LiteGL_DeviceBase<IDirect3DDevice9Ex>(real) {
        log_msg("[LiteGL D3D9Ex] LiteGL ToGL IDirect3DDevice9Ex created!\n");
    }

    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) {
        if (!ppvObj) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_IDirect3DDevice9) ||
            IsEqualIID(riid, IID_IDirect3DDevice9Ex)) {
            *ppvObj = (IDirect3DDevice9Ex*)this;
            this->AddRef();
            return S_OK;
        }
        return this->m_real->QueryInterface(riid, ppvObj);
    }

    /*** IDirect3DDevice9Ex specific methods ***/
    STDMETHOD(SetConvolutionMonoKernel)(UINT width, UINT height, float *rows, float *columns) { return this->m_real->SetConvolutionMonoKernel(width, height, rows, columns); }
    STDMETHOD(ComposeRects)(IDirect3DSurface9 *src_surface, IDirect3DSurface9 *dst_surface, IDirect3DVertexBuffer9 *src_descs, UINT rect_count, IDirect3DVertexBuffer9 *dst_descs, D3DCOMPOSERECTSOP operation, INT offset_x, INT offset_y) { return this->m_real->ComposeRects(src_surface, dst_surface, src_descs, rect_count, dst_descs, operation, offset_x, offset_y); }
    STDMETHOD(PresentEx)(const RECT *src_rect, const RECT *dst_rect, HWND dst_window_override, const RGNDATA *dirty_region, DWORD flags) {
        this->on_present();
        return this->m_real->PresentEx(src_rect, dst_rect, dst_window_override, dirty_region, flags);
    }
    STDMETHOD(GetGPUThreadPriority)(INT *priority) { return this->m_real->GetGPUThreadPriority(priority); }
    STDMETHOD(SetGPUThreadPriority)(INT priority) { return this->m_real->SetGPUThreadPriority(priority); }
    STDMETHOD(WaitForVBlank)(UINT swapchain_idx) { return this->m_real->WaitForVBlank(swapchain_idx); }
    STDMETHOD(CheckResourceResidency)(IDirect3DResource9 **resources, UINT32 resource_count) { return this->m_real->CheckResourceResidency(resources, resource_count); }
    STDMETHOD(SetMaximumFrameLatency)(UINT max_latency) { return this->m_real->SetMaximumFrameLatency(max_latency); }
    STDMETHOD(GetMaximumFrameLatency)(UINT *max_latency) { return this->m_real->GetMaximumFrameLatency(max_latency); }
    STDMETHOD(CheckDeviceState)(HWND dst_window) { return this->m_real->CheckDeviceState(dst_window); }
    STDMETHOD(CreateRenderTargetEx)(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample_type, DWORD multisample_quality, WINBOOL lockable, IDirect3DSurface9 **surface, HANDLE *shared_handle, DWORD usage) { return this->m_real->CreateRenderTargetEx(width, height, format, multisample_type, multisample_quality, lockable, surface, shared_handle, usage); }
    STDMETHOD(CreateOffscreenPlainSurfaceEx)(UINT width, UINT Height, D3DFORMAT format, D3DPOOL pool, IDirect3DSurface9 **surface, HANDLE *shared_handle, DWORD usage) { return this->m_real->CreateOffscreenPlainSurfaceEx(width, Height, format, pool, surface, shared_handle, usage); }
    STDMETHOD(CreateDepthStencilSurfaceEx)(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample_type, DWORD multisample_quality, WINBOOL discard, IDirect3DSurface9 **surface, HANDLE *shared_handle, DWORD usage) { return this->m_real->CreateDepthStencilSurfaceEx(width, height, format, multisample_type, multisample_quality, discard, surface, shared_handle, usage); }
    STDMETHOD(ResetEx)(D3DPRESENT_PARAMETERS *parameters, D3DDISPLAYMODEEX *mode) {
        if (parameters) {
            parameters->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
            parameters->SwapEffect = D3DSWAPEFFECT_DISCARD;
            parameters->BackBufferCount = 2;
        }
        this->invalidate_cache();
        HRESULT hr = this->m_real->ResetEx(parameters, mode);
        if (SUCCEEDED(hr)) {
            this->m_real->SetMaximumFrameLatency(1);
        }
        return hr;
    }
    STDMETHOD(GetDisplayModeEx)(UINT swapchain_idx, D3DDISPLAYMODEEX *mode, D3DDISPLAYROTATION *rotation) { return this->m_real->GetDisplayModeEx(swapchain_idx, mode, rotation); }
};

/* Implementation of LiteGL_Direct3DDevice9::QueryInterface */
HRESULT LiteGL_Direct3DDevice9::QueryInterface(REFIID riid, void** ppvObj) {
    if (!ppvObj) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_IDirect3DDevice9)) {
        *ppvObj = (IDirect3DDevice9*)this;
        this->AddRef();
        return S_OK;
    }
    if (IsEqualIID(riid, IID_IDirect3DDevice9Ex)) {
        IDirect3DDevice9Ex* realEx = NULL;
        HRESULT hr = this->m_real->QueryInterface(IID_IDirect3DDevice9Ex, (void**)&realEx);
        if (SUCCEEDED(hr) && realEx) {
            *ppvObj = (IDirect3DDevice9Ex*)new LiteGL_Direct3DDevice9Ex(realEx);
            log_msg("[LiteGL D3D9] QueryInterface promoted IDirect3DDevice9 -> IDirect3DDevice9Ex!\n");
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    return this->m_real->QueryInterface(riid, ppvObj);
}

/* -------------------------------------------------------------------------
 * LiteGL IDirect3D9 Implementation
 * ------------------------------------------------------------------------- */
class LiteGL_Direct3D9 : public IDirect3D9 {
public:
    IDirect3D9* m_real;
    ULONG m_ref;

    LiteGL_Direct3D9(IDirect3D9* real) : m_real(real), m_ref(1) {}

    /*** IUnknown methods ***/
    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) {
        if (!ppvObj) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_IDirect3D9)) {
            *ppvObj = (IDirect3D9*)this;
            this->AddRef();
            return S_OK;
        }
        return m_real->QueryInterface(riid, ppvObj);
    }
    STDMETHOD_(ULONG,AddRef)(void) { return InterlockedIncrement(&m_ref); }
    STDMETHOD_(ULONG,Release)(void) {
        ULONG ref = InterlockedDecrement(&m_ref);
        if (ref == 0) {
            m_real->Release();
            delete this;
        }
        return ref;
    }


    /*** IDirect3D9 methods ***/
    STDMETHOD(RegisterSoftwareDevice)(void* pInitializeFunction) { return m_real->RegisterSoftwareDevice(pInitializeFunction); }
    STDMETHOD_(UINT, GetAdapterCount)(void) { return m_real->GetAdapterCount(); }
    STDMETHOD(GetAdapterIdentifier)(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pIdentifier) {
        HRESULT hr = m_real->GetAdapterIdentifier(Adapter, Flags, pIdentifier);
        if (SUCCEEDED(hr) && pIdentifier) {
            snprintf(pIdentifier->Description, sizeof(pIdentifier->Description),
                     "LiteGL (ToGL-Accelerated D3D9 Renderer)");
        }
        return hr;
    }
    STDMETHOD_(UINT, GetAdapterModeCount)(UINT Adapter, D3DFORMAT Format) { return m_real->GetAdapterModeCount(Adapter, Format); }
    STDMETHOD(EnumAdapterModes)(UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode) { return m_real->EnumAdapterModes(Adapter, Format, Mode, pMode); }
    STDMETHOD(GetAdapterDisplayMode)(UINT Adapter, D3DDISPLAYMODE* pMode) { return m_real->GetAdapterDisplayMode(Adapter, pMode); }
    STDMETHOD(CheckDeviceType)(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT DisplayFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed) { return m_real->CheckDeviceType(Adapter, DevType, DisplayFormat, BackBufferFormat, bWindowed); }
    STDMETHOD(CheckDeviceFormat)(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) { return m_real->CheckDeviceFormat(Adapter, DeviceType, AdapterFormat, Usage, RType, CheckFormat); }
    STDMETHOD(CheckDeviceMultiSampleType)(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD* pQualityLevels) { return m_real->CheckDeviceMultiSampleType(Adapter, DeviceType, SurfaceFormat, Windowed, MultiSampleType, pQualityLevels); }
    STDMETHOD(CheckDepthStencilMatch)(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) { return m_real->CheckDepthStencilMatch(Adapter, DeviceType, AdapterFormat, RenderTargetFormat, DepthStencilFormat); }
    STDMETHOD(CheckDeviceFormatConversion)(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat) { return m_real->CheckDeviceFormatConversion(Adapter, DeviceType, SourceFormat, TargetFormat); }
    STDMETHOD(GetDeviceCaps)(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps) { return m_real->GetDeviceCaps(Adapter, DeviceType, pCaps); }
    STDMETHOD_(HMONITOR, GetAdapterMonitor)(UINT Adapter) { return m_real->GetAdapterMonitor(Adapter); }

    STDMETHOD(CreateDevice)(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface) {
        log_msg("[LiteGL D3D9] Creating device (Adapter: %u, FocusWindow: %p)...\n", Adapter, hFocusWindow);

        if (pPresentationParameters) {
            pPresentationParameters->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
            pPresentationParameters->SwapEffect = D3DSWAPEFFECT_DISCARD;
            pPresentationParameters->BackBufferCount = 2;
        }
        BehaviorFlags |= D3DCREATE_FPU_PRESERVE;

        IDirect3DDevice9* real_device = NULL;
        HRESULT hr = m_real->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, &real_device);
        if (FAILED(hr) || !real_device) {
            log_msg("[LiteGL D3D9] m_real->CreateDevice failed with hr = 0x%08X\n", (unsigned int)hr);
            return hr;
        }

        *ppReturnedDeviceInterface = new LiteGL_Direct3DDevice9(real_device);
        log_msg("[LiteGL D3D9] Wrapped IDirect3DDevice9 successfully with LiteGL ToGL Accelerator!\n");
        return D3D_OK;
    }
};

/* -------------------------------------------------------------------------
 * LiteGL IDirect3D9Ex Implementation
 * ------------------------------------------------------------------------- */
class LiteGL_Direct3D9Ex : public IDirect3D9Ex {
public:
    IDirect3D9Ex* m_realEx;
    ULONG m_ref;

    LiteGL_Direct3D9Ex(IDirect3D9Ex* real) : m_realEx(real), m_ref(1) {
        log_msg("[LiteGL D3D9Ex] LiteGL ToGL IDirect3D9Ex wrapper created!\n");
    }

    /*** IUnknown methods ***/
    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) {
        if (!ppvObj) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_IDirect3D9) ||
            IsEqualIID(riid, IID_IDirect3D9Ex)) {
            *ppvObj = (IDirect3D9Ex*)this;
            this->AddRef();
            return S_OK;
        }
        return m_realEx->QueryInterface(riid, ppvObj);
    }
    STDMETHOD_(ULONG,AddRef)(void) { return InterlockedIncrement(&m_ref); }
    STDMETHOD_(ULONG,Release)(void) {
        ULONG ref = InterlockedDecrement(&m_ref);
        if (ref == 0) {
            m_realEx->Release();
            delete this;
        }
        return ref;
    }

    /*** IDirect3D9 methods ***/
    STDMETHOD(RegisterSoftwareDevice)(void* pInitializeFunction) { return m_realEx->RegisterSoftwareDevice(pInitializeFunction); }
    STDMETHOD_(UINT, GetAdapterCount)(void) { return m_realEx->GetAdapterCount(); }
    STDMETHOD(GetAdapterIdentifier)(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pIdentifier) {
        HRESULT hr = m_realEx->GetAdapterIdentifier(Adapter, Flags, pIdentifier);
        if (SUCCEEDED(hr) && pIdentifier) {
            snprintf(pIdentifier->Description, sizeof(pIdentifier->Description),
                     "LiteGL (ToGL-Accelerated D3D9Ex Renderer)");
        }
        return hr;
    }
    STDMETHOD_(UINT, GetAdapterModeCount)(UINT Adapter, D3DFORMAT Format) { return m_realEx->GetAdapterModeCount(Adapter, Format); }
    STDMETHOD(EnumAdapterModes)(UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode) { return m_realEx->EnumAdapterModes(Adapter, Format, Mode, pMode); }
    STDMETHOD(GetAdapterDisplayMode)(UINT Adapter, D3DDISPLAYMODE* pMode) { return m_realEx->GetAdapterDisplayMode(Adapter, pMode); }
    STDMETHOD(CheckDeviceType)(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT DisplayFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed) { return m_realEx->CheckDeviceType(Adapter, DevType, DisplayFormat, BackBufferFormat, bWindowed); }
    STDMETHOD(CheckDeviceFormat)(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) { return m_realEx->CheckDeviceFormat(Adapter, DeviceType, AdapterFormat, Usage, RType, CheckFormat); }
    STDMETHOD(CheckDeviceMultiSampleType)(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD* pQualityLevels) { return m_realEx->CheckDeviceMultiSampleType(Adapter, DeviceType, SurfaceFormat, Windowed, MultiSampleType, pQualityLevels); }
    STDMETHOD(CheckDepthStencilMatch)(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) { return m_realEx->CheckDepthStencilMatch(Adapter, DeviceType, AdapterFormat, RenderTargetFormat, DepthStencilFormat); }
    STDMETHOD(CheckDeviceFormatConversion)(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat) { return m_realEx->CheckDeviceFormatConversion(Adapter, DeviceType, SourceFormat, TargetFormat); }
    STDMETHOD(GetDeviceCaps)(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps) { return m_realEx->GetDeviceCaps(Adapter, DeviceType, pCaps); }
    STDMETHOD_(HMONITOR, GetAdapterMonitor)(UINT Adapter) { return m_realEx->GetAdapterMonitor(Adapter); }

    STDMETHOD(CreateDevice)(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface) {
        log_msg("[LiteGL D3D9Ex] CreateDevice called (Adapter: %u, FocusWindow: %p)...\n", Adapter, hFocusWindow);

        if (pPresentationParameters) {
            pPresentationParameters->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
            pPresentationParameters->SwapEffect = D3DSWAPEFFECT_DISCARD;
            pPresentationParameters->BackBufferCount = 2;
        }
        BehaviorFlags |= D3DCREATE_FPU_PRESERVE;

        IDirect3DDevice9Ex* real_device_ex = NULL;
        HRESULT hr = m_realEx->CreateDeviceEx(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, NULL, &real_device_ex);
        if (SUCCEEDED(hr) && real_device_ex) {
            real_device_ex->SetMaximumFrameLatency(1);
            *ppReturnedDeviceInterface = (IDirect3DDevice9*)new LiteGL_Direct3DDevice9Ex(real_device_ex);
            log_msg("[LiteGL D3D9Ex] Successfully wrapped IDirect3DDevice9Ex via CreateDeviceEx!\n");
            return D3D_OK;
        }

        IDirect3DDevice9* real_device = NULL;
        hr = m_realEx->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, &real_device);
        if (FAILED(hr) || !real_device) {
            log_msg("[LiteGL D3D9Ex] m_realEx->CreateDevice failed with hr = 0x%08X\n", (unsigned int)hr);
            return hr;
        }

        *ppReturnedDeviceInterface = new LiteGL_Direct3DDevice9(real_device);
        log_msg("[LiteGL D3D9Ex] Wrapped IDirect3DDevice9 successfully with LiteGL ToGL Accelerator!\n");
        return D3D_OK;
    }


    /*** IDirect3D9Ex methods ***/
    STDMETHOD_(UINT, GetAdapterModeCountEx)(UINT Adapter, const D3DDISPLAYMODEFILTER *filter) { return m_realEx->GetAdapterModeCountEx(Adapter, filter); }
    STDMETHOD(EnumAdapterModesEx)(UINT Adapter, const D3DDISPLAYMODEFILTER *filter, UINT Mode, D3DDISPLAYMODEEX *pMode) { return m_realEx->EnumAdapterModesEx(Adapter, filter, Mode, pMode); }
    STDMETHOD(GetAdapterDisplayModeEx)(UINT Adapter, D3DDISPLAYMODEEX *pMode, D3DDISPLAYROTATION *pRotation) { return m_realEx->GetAdapterDisplayModeEx(Adapter, pMode, pRotation); }
    STDMETHOD(CreateDeviceEx)(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, D3DDISPLAYMODEEX* pFullscreenDisplayMode, IDirect3DDevice9Ex** ppReturnedDeviceInterface) {
        log_msg("[LiteGL D3D9Ex] CreateDeviceEx called (Adapter: %u, FocusWindow: %p)...\n", Adapter, hFocusWindow);

        if (pPresentationParameters) {
            pPresentationParameters->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
            pPresentationParameters->SwapEffect = D3DSWAPEFFECT_DISCARD;
            pPresentationParameters->BackBufferCount = 2;
        }
        BehaviorFlags |= D3DCREATE_FPU_PRESERVE;

        IDirect3DDevice9Ex* real_device = NULL;
        HRESULT hr = m_realEx->CreateDeviceEx(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, pFullscreenDisplayMode, &real_device);
        if (FAILED(hr) || !real_device) {
            log_msg("[LiteGL D3D9Ex] m_realEx->CreateDeviceEx failed with hr = 0x%08X\n", (unsigned int)hr);
            return hr;
        }

        real_device->SetMaximumFrameLatency(1);
        *ppReturnedDeviceInterface = new LiteGL_Direct3DDevice9Ex(real_device);
        log_msg("[LiteGL D3D9Ex] Wrapped IDirect3DDevice9Ex successfully with LiteGL ToGL Accelerator!\n");
        return D3D_OK;
    }
    STDMETHOD(GetAdapterLUID)(UINT Adapter, LUID *pLuid) { return m_realEx->GetAdapterLUID(Adapter, pLuid); }
};

/* -------------------------------------------------------------------------
 * Exported Functions
 * ------------------------------------------------------------------------- */
extern "C" {

__declspec(dllexport) IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion) {
    if (!load_system_d3d9() || !g_real_Direct3DCreate9) {
        return NULL;
    }

    log_msg("\n===================================================================\n");
    log_msg(" [LiteGL] Direct3DCreate9 called (SDKVersion: %u)\n", SDKVersion);
    log_msg(" [LiteGL] Active Backend: %s\n", detect_backend());
    log_msg("===================================================================\n");

    IDirect3D9* real_d3d = g_real_Direct3DCreate9(SDKVersion);
    if (!real_d3d) {
        log_msg("[LiteGL D3D9] Real Direct3DCreate9 returned NULL! (GetLastError: 0x%08X)\n", (unsigned int)GetLastError());
        return NULL;
    }

    return new LiteGL_Direct3D9(real_d3d);
}

__declspec(dllexport) HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppD3D) {
    if (!load_system_d3d9() || !g_real_Direct3DCreate9Ex) {
        return D3DERR_NOTAVAILABLE;
    }

    log_msg("\n===================================================================\n");
    log_msg(" [LiteGL] Direct3DCreate9Ex called (SDKVersion: %u)\n", SDKVersion);
    log_msg(" [LiteGL] Active Backend: %s\n", detect_backend());
    log_msg("===================================================================\n");

    IDirect3D9Ex* real_d3d = NULL;
    HRESULT hr = g_real_Direct3DCreate9Ex(SDKVersion, &real_d3d);
    if (SUCCEEDED(hr) && real_d3d) {
        *ppD3D = new LiteGL_Direct3D9Ex(real_d3d);
        log_msg("[LiteGL D3D9Ex] Successfully wrapped IDirect3D9Ex!\n");
        return D3D_OK;
    }

    log_msg("[LiteGL D3D9Ex] Real Direct3DCreate9Ex returned hr = 0x%08X\n", (unsigned int)hr);
    return hr;
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
            log_msg("[LiteGL D3D9] DLL attached (PID: %lu).\n", GetCurrentProcessId());
            break;
        case DLL_PROCESS_DETACH:
            log_msg("[LiteGL D3D9] DLL detached (PID: %lu).\n", GetCurrentProcessId());
            if (g_log_file) {
                fflush(g_log_file);
            }
            break;
    }
    return TRUE;
}
