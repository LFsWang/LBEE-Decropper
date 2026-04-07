#if defined (_DEBUG)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include "MinHook.h"

#pragma comment(lib, "dxgi.lib")

#include "SharedInc.h"
#include "CgGs.h"
#include "DecalGs.h"


#define SAFE_RELEASE(x) if (x != NULL) { x->Release(); x = NULL; }

// Force enable debug logging for now
#define DEBUG_LOG_EN 1

#if DEBUG_LOG_EN
static FILE * debug;
#define DEBUG(x) fprintf x
#define DEBUGFLUSH fflush(debug)
#else // DEBUG_LOG_EN
#define DEBUG(x)
#define DEBUGFLUSH
#endif // DEBUG_LOG_EN

static HINSTANCE gs_hDLL = 0;

// Original function pointers for DLL proxy trampolines.
// These are referenced by proxy_x64.asm for the jmp targets.
// Must be extern "C" to avoid C++ name mangling so the ASM EXTERN directives can find them.
extern "C"
{
    FARPROC _O_D3D11CoreCreateDevice = NULL;
    FARPROC _O_D3D11CoreCreateLayeredDevice = NULL;
    FARPROC _O_D3D11CoreGetLayeredDeviceSize = NULL;
    FARPROC _O_D3D11CoreRegisterLayers = NULL;
    // _O_D3D11CreateDevice is declared below with its proper type
    FARPROC _O_D3D11CreateDeviceAndSwapChain = NULL;
    FARPROC _O_D3DKMTCloseAdapter = NULL;
    FARPROC _O_D3DKMTCreateAllocation = NULL;
    FARPROC _O_D3DKMTCreateContext = NULL;
    FARPROC _O_D3DKMTCreateDevice = NULL;
    FARPROC _O_D3DKMTCreateSynchronizationObject = NULL;
    FARPROC _O_D3DKMTDestroyAllocation = NULL;
    FARPROC _O_D3DKMTDestroyContext = NULL;
    FARPROC _O_D3DKMTDestroyDevice = NULL;
    FARPROC _O_D3DKMTDestroySynchronizationObject = NULL;
    FARPROC _O_D3DKMTEscape = NULL;
    FARPROC _O_D3DKMTGetContextSchedulingPriority = NULL;
    FARPROC _O_D3DKMTGetDeviceState = NULL;
    FARPROC _O_D3DKMTGetDisplayModeList = NULL;
    FARPROC _O_D3DKMTGetMultisampleMethodList = NULL;
    FARPROC _O_D3DKMTGetRuntimeData = NULL;
    FARPROC _O_D3DKMTGetSharedPrimaryHandle = NULL;
    FARPROC _O_D3DKMTLock = NULL;
    FARPROC _O_D3DKMTOpenAdapterFromHdc = NULL;
    FARPROC _O_D3DKMTOpenResource = NULL;
    FARPROC _O_D3DKMTPresent = NULL;
    FARPROC _O_D3DKMTQueryAdapterInfo = NULL;
    FARPROC _O_D3DKMTQueryAllocationResidency = NULL;
    FARPROC _O_D3DKMTQueryResourceInfo = NULL;
    FARPROC _O_D3DKMTRender = NULL;
    FARPROC _O_D3DKMTSetAllocationPriority = NULL;
    FARPROC _O_D3DKMTSetContextSchedulingPriority = NULL;
    FARPROC _O_D3DKMTSetDisplayMode = NULL;
    FARPROC _O_D3DKMTSetDisplayPrivateDriverFormat = NULL;
    FARPROC _O_D3DKMTSetGammaRamp = NULL;
    FARPROC _O_D3DKMTSetVidPnSourceOwner = NULL;
    FARPROC _O_D3DKMTSignalSynchronizationObject = NULL;
    FARPROC _O_D3DKMTUnlock = NULL;
    FARPROC _O_D3DKMTWaitForSynchronizationObject = NULL;
    FARPROC _O_D3DKMTWaitForVerticalBlankEvent = NULL;
    FARPROC _O_D3DPerformance_BeginEvent = NULL;
    FARPROC _O_D3DPerformance_EndEvent = NULL;
    FARPROC _O_D3DPerformance_GetStatus = NULL;
    FARPROC _O_D3DPerformance_SetMarker = NULL;
    FARPROC _O_EnableFeatureLevelUpgrade = NULL;
    FARPROC _O_OpenAdapter10 = NULL;
    FARPROC _O_OpenAdapter10_2 = NULL;
}


DWORD_PTR* pDeviceVTable  = NULL;
DWORD_PTR* pContextVTable = NULL;

typedef void(WINAPI *D3D11DrawIndexedHook) (ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation);
D3D11DrawIndexedHook phookD3D11DrawIndexed = NULL;

// Draw (VTable offset 13): void Draw(UINT VertexCount, UINT StartVertexLocation)
typedef void(WINAPI *D3D11DrawHook) (ID3D11DeviceContext* pContext, UINT VertexCount, UINT StartVertexLocation);
D3D11DrawHook phookD3D11Draw = NULL;

typedef void(WINAPI *D3D11SetPredicationHook) (ID3D11DeviceContext* pContext, ID3D11Predicate *pPredicate, BOOL PredicateValue);
D3D11SetPredicationHook phookD3D11SetPredication = NULL;

// PSSetShaderResources (VTable offset 8): catch all texture bindings
typedef void(WINAPI *D3D11PSSetShaderResourcesHook) (ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews);
D3D11PSSetShaderResourcesHook phookD3D11PSSetShaderResources = NULL;

// Map (VTable offset 14): catch CPU writes to GPU resources
typedef HRESULT(WINAPI *D3D11MapHook) (ID3D11DeviceContext* pContext, ID3D11Resource *pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE *pMappedResource);
D3D11MapHook phookD3D11Map = NULL;

// UpdateSubresource (VTable offset 48): catch CPU->GPU data uploads
typedef void(WINAPI *D3D11UpdateSubresourceHook) (ID3D11DeviceContext* pContext, ID3D11Resource *pDstResource, UINT DstSubresource, const D3D11_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch);
D3D11UpdateSubresourceHook phookD3D11UpdateSubresource = NULL;

// CopyResource (VTable offset 47)
typedef void(WINAPI *D3D11CopyResourceHook) (ID3D11DeviceContext* pContext, ID3D11Resource *pDstResource, ID3D11Resource *pSrcResource);
D3D11CopyResourceHook phookD3D11CopyResource = NULL;

// CopySubresourceRegion (VTable offset 46)
typedef void(WINAPI *D3D11CopySubresourceRegionHook) (ID3D11DeviceContext* pContext, ID3D11Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource *pSrcResource, UINT SrcSubresource, const D3D11_BOX *pSrcBox);
D3D11CopySubresourceRegionHook phookD3D11CopySubresourceRegion = NULL;

// Unmap (VTable offset 15)
typedef void(WINAPI *D3D11UnmapHook) (ID3D11DeviceContext* pContext, ID3D11Resource *pResource, UINT Subresource);
D3D11UnmapHook phookD3D11Unmap = NULL;

// Track the mapped 1280x721 texture
static ID3D11Resource* g_pMappedCgTexture = NULL;
static D3D11_MAPPED_SUBRESOURCE g_mappedData = {};

// DXGI Present hook
typedef HRESULT(WINAPI *DXGIPresentHook)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
DXGIPresentHook phookDXGIPresent = NULL;
static ID3D11Device* g_pDevice = NULL;
static int g_presentDumpCount = 0;

// GDI hooks to find the CG cropping point
typedef BOOL(WINAPI *tStretchBlt)(HDC hdcDest, int xDest, int yDest, int wDest, int hDest,
    HDC hdcSrc, int xSrc, int ySrc, int wSrc, int hSrc, DWORD rop);
tStretchBlt pOrigStretchBlt = NULL;

typedef BOOL(WINAPI *tBitBlt)(HDC hdc, int x, int y, int cx, int cy,
    HDC hdcSrc, int x1, int y1, DWORD rop);
tBitBlt pOrigBitBlt = NULL;

typedef int(WINAPI *tStretchDIBits)(HDC hdc, int xDest, int yDest, int DestWidth, int DestHeight,
    int xSrc, int ySrc, int SrcWidth, int SrcHeight, const VOID *lpBits,
    const BITMAPINFO *lpbmi, UINT iUsage, DWORD rop);
tStretchDIBits pOrigStretchDIBits = NULL;

// SetDIBitsToDevice - direct DIB to DC copy
typedef int(WINAPI *tSetDIBitsToDevice)(HDC hdc, int xDest, int yDest, DWORD w, DWORD h,
    int xSrc, int ySrc, UINT StartScan, UINT cLines, const VOID *lpvBits,
    const BITMAPINFO *lpbmi, UINT ColorUse);
tSetDIBitsToDevice pOrigSetDIBitsToDevice = NULL;

int WINAPI hookSetDIBitsToDevice(HDC hdc, int xDest, int yDest, DWORD w, DWORD h,
    int xSrc, int ySrc, UINT StartScan, UINT cLines, const VOID *lpvBits,
    const BITMAPINFO *lpbmi, UINT ColorUse)
{
    if (w >= 256 || h >= 256)
    {
        DEBUG((debug, "  SetDIBits: dst=(%d,%d) w=%u h=%u src=(%d,%d) startScan=%u cLines=%u",
            xDest, yDest, w, h, xSrc, ySrc, StartScan, cLines));
        if (lpbmi)
        {
            DEBUG((debug, " bmi=%dx%d bpp=%d",
                lpbmi->bmiHeader.biWidth, lpbmi->bmiHeader.biHeight, lpbmi->bmiHeader.biBitCount));
        }
        DEBUG((debug, "\n"));
        DEBUGFLUSH;
    }
    return pOrigSetDIBitsToDevice(hdc, xDest, yDest, w, h, xSrc, ySrc, StartScan, cLines, lpvBits, lpbmi, ColorUse);
}

BOOL WINAPI hookStretchBlt(HDC hdcDest, int xDest, int yDest, int wDest, int hDest,
    HDC hdcSrc, int xSrc, int ySrc, int wSrc, int hSrc, DWORD rop)
{
    if (wSrc >= 256 || hSrc >= 256 || wDest >= 256 || hDest >= 256)
    {
        DEBUG((debug, "  StretchBlt: src=(%d,%d,%d,%d) dst=(%d,%d,%d,%d) rop=0x%x\n",
            xSrc, ySrc, wSrc, hSrc, xDest, yDest, wDest, hDest, rop));
        DEBUGFLUSH;
    }
    return pOrigStretchBlt(hdcDest, xDest, yDest, wDest, hDest, hdcSrc, xSrc, ySrc, wSrc, hSrc, rop);
}

BOOL WINAPI hookBitBlt(HDC hdc, int x, int y, int cx, int cy,
    HDC hdcSrc, int x1, int y1, DWORD rop)
{
    if (cx >= 256 || cy >= 256)
    {
        DEBUG((debug, "  BitBlt: dst=(%d,%d,%d,%d) srcOff=(%d,%d) rop=0x%x\n",
            x, y, cx, cy, x1, y1, rop));
        DEBUGFLUSH;
    }
    return pOrigBitBlt(hdc, x, y, cx, cy, hdcSrc, x1, y1, rop);
}

int WINAPI hookStretchDIBits(HDC hdc, int xDest, int yDest, int DestWidth, int DestHeight,
    int xSrc, int ySrc, int SrcWidth, int SrcHeight, const VOID *lpBits,
    const BITMAPINFO *lpbmi, UINT iUsage, DWORD rop)
{
    if (SrcWidth >= 256 || SrcHeight >= 256 || DestWidth >= 256 || DestHeight >= 256)
    {
        DEBUG((debug, "  StretchDIBits: src=(%d,%d,%d,%d) dst=(%d,%d,%d,%d)",
            xSrc, ySrc, SrcWidth, SrcHeight, xDest, yDest, DestWidth, DestHeight));
        if (lpbmi)
        {
            DEBUG((debug, " bmi=%dx%d bpp=%d",
                lpbmi->bmiHeader.biWidth, lpbmi->bmiHeader.biHeight, lpbmi->bmiHeader.biBitCount));
        }
        DEBUG((debug, "\n"));
        DEBUGFLUSH;
    }
    return pOrigStretchDIBits(hdc, xDest, yDest, DestWidth, DestHeight,
        xSrc, ySrc, SrcWidth, SrcHeight, lpBits, lpbmi, iUsage, rop);
}

// NtGdi hooks (win32u.dll - kernel-mode GDI wrappers)
typedef BOOL(WINAPI *tNtGdiBitBlt)(HDC hdcDst, int x, int y, int cx, int cy,
    HDC hdcSrc, int x1, int y1, DWORD rop, DWORD crBackColor, FLONG fl);
tNtGdiBitBlt pOrigNtGdiBitBlt = NULL;

BOOL WINAPI hookNtGdiBitBlt(HDC hdcDst, int x, int y, int cx, int cy,
    HDC hdcSrc, int x1, int y1, DWORD rop, DWORD crBackColor, FLONG fl)
{
    if (cx >= 256 || cy >= 256)
    {
        DEBUG((debug, "  NtGdiBitBlt: dst=(%d,%d,%d,%d) src=(%d,%d) rop=0x%x fl=0x%x\n",
            x, y, cx, cy, x1, y1, rop, fl));
        DEBUGFLUSH;
    }
    return pOrigNtGdiBitBlt(hdcDst, x, y, cx, cy, hdcSrc, x1, y1, rop, crBackColor, fl);
}

typedef BOOL(WINAPI *tNtGdiStretchBlt)(HDC hdcDst, int xDst, int yDst, int cxDst, int cyDst,
    HDC hdcSrc, int xSrc, int ySrc, int cxSrc, int cySrc, DWORD rop, DWORD crBackColor);
tNtGdiStretchBlt pOrigNtGdiStretchBlt = NULL;

BOOL WINAPI hookNtGdiStretchBlt(HDC hdcDst, int xDst, int yDst, int cxDst, int cyDst,
    HDC hdcSrc, int xSrc, int ySrc, int cxSrc, int cySrc, DWORD rop, DWORD crBackColor)
{
    if (cxSrc >= 256 || cySrc >= 256 || cxDst >= 256 || cyDst >= 256)
    {
        DEBUG((debug, "  NtGdiStretchBlt: src=(%d,%d,%d,%d) dst=(%d,%d,%d,%d) rop=0x%x\n",
            xSrc, ySrc, cxSrc, cySrc, xDst, yDst, cxDst, cyDst, rop));
        DEBUGFLUSH;
    }
    return pOrigNtGdiStretchBlt(hdcDst, xDst, yDst, cxDst, cyDst, hdcSrc, xSrc, ySrc, cxSrc, cySrc, rop, crBackColor);
}

typedef BOOL(WINAPI *tNtGdiAlphaBlend)(HDC hdcDst, LONG xDst, LONG yDst, LONG cxDst, LONG cyDst,
    HDC hdcSrc, LONG xSrc, LONG ySrc, LONG cxSrc, LONG cySrc, DWORD dwBlendFunction, HANDLE hcmXform);
tNtGdiAlphaBlend pOrigNtGdiAlphaBlend = NULL;

BOOL WINAPI hookNtGdiAlphaBlend(HDC hdcDst, LONG xDst, LONG yDst, LONG cxDst, LONG cyDst,
    HDC hdcSrc, LONG xSrc, LONG ySrc, LONG cxSrc, LONG cySrc, DWORD dwBlendFunction, HANDLE hcmXform)
{
    if (cxSrc >= 256 || cySrc >= 256 || cxDst >= 256 || cyDst >= 256)
    {
        DEBUG((debug, "  NtGdiAlphaBlend: src=(%d,%d,%d,%d) dst=(%d,%d,%d,%d) blend=0x%x\n",
            xSrc, ySrc, cxSrc, cySrc, xDst, yDst, cxDst, cyDst, dwBlendFunction));
        DEBUGFLUSH;
    }
    return pOrigNtGdiAlphaBlend(hdcDst, xDst, yDst, cxDst, cyDst, hdcSrc, xSrc, ySrc, cxSrc, cySrc, dwBlendFunction, hcmXform);
}

// IDXGISurface1::GetDC / ReleaseDC hooks
typedef HRESULT(WINAPI *tDXGISurfaceGetDC)(IDXGISurface1* pSurface, BOOL Discard, HDC* phdc);
tDXGISurfaceGetDC pOrigDXGISurfaceGetDC = NULL;

HRESULT WINAPI hookDXGISurfaceGetDC(IDXGISurface1* pSurface, BOOL Discard, HDC* phdc)
{
    HRESULT hr = pOrigDXGISurfaceGetDC(pSurface, Discard, phdc);
    DXGI_SURFACE_DESC desc;
    if (pSurface->GetDesc(&desc) == S_OK)
    {
        DEBUG((debug, "  IDXGISurface1::GetDC: %ux%u discard=%d hdc=%p hr=0x%x\n",
            desc.Width, desc.Height, Discard, phdc ? *phdc : NULL, hr));
        DEBUGFLUSH;
    }
    return hr;
}

typedef HRESULT(WINAPI *tDXGISurfaceReleaseDC)(IDXGISurface1* pSurface, RECT* pDirtyRect);
tDXGISurfaceReleaseDC pOrigDXGISurfaceReleaseDC = NULL;

HRESULT WINAPI hookDXGISurfaceReleaseDC(IDXGISurface1* pSurface, RECT* pDirtyRect)
{
    DEBUG((debug, "  IDXGISurface1::ReleaseDC: dirtyRect=%p", pDirtyRect));
    if (pDirtyRect) DEBUG((debug, " (%d,%d,%d,%d)", pDirtyRect->left, pDirtyRect->top, pDirtyRect->right, pDirtyRect->bottom));
    DEBUG((debug, "\n"));
    DEBUGFLUSH;
    return pOrigDXGISurfaceReleaseDC(pSurface, pDirtyRect);
}

// Search process memory for a byte pattern
static void* ScanMemoryForPattern(const BYTE* pattern, size_t patternLen, size_t minRegionSize)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    BYTE* addr = (BYTE*)si.lpMinimumApplicationAddress;
    BYTE* maxAddr = (BYTE*)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi;

    while (addr < maxAddr)
    {
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0)
            break;

        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_READONLY ||
             mbi.Protect == PAGE_EXECUTE_READWRITE || mbi.Protect == PAGE_EXECUTE_READ) &&
            mbi.RegionSize >= minRegionSize)
        {
            BYTE* regionEnd = (BYTE*)mbi.BaseAddress + mbi.RegionSize - patternLen;
            // Search with stride of 4 (pixel aligned)
            for (BYTE* p = (BYTE*)mbi.BaseAddress; p <= regionEnd; p += 4)
            {
                if (memcmp(p, pattern, patternLen) == 0)
                {
                    return p;
                }
            }
        }
        addr = (BYTE*)mbi.BaseAddress + mbi.RegionSize;
    }
    return NULL;
}

static BOOL g_cgSearchDone = FALSE;
static BYTE* g_cgFramebuffer = NULL;  // Cached address of game's 1280x960 framebuffer
static ID3D11Texture2D* g_pStagingWrite = NULL;  // Staging texture for writing back to backbuffer

static BOOL defFrameHasCg = FALSE;

// Hook ExecuteCommandList (vtable 58) on immediate context
typedef void(WINAPI *D3D11ExecuteCommandListHook)(ID3D11DeviceContext* pContext, ID3D11CommandList *pCommandList, BOOL RestoreContextState);
D3D11ExecuteCommandListHook phookD3D11ExecuteCommandList = NULL;

void WINAPI hookD3D11ExecuteCommandList(ID3D11DeviceContext* pContext, ID3D11CommandList *pCommandList, BOOL RestoreContextState)
{
    // Reset CG detection state for next command list
    defFrameHasCg = FALSE;
    phookD3D11ExecuteCommandList(pContext, pCommandList, RestoreContextState);
}

// Shader/sampler globals (needed by both immediate and deferred context hooks)
ID3D11GeometryShader* pCgGs = NULL;
ID3D11GeometryShader* pDecalGs = NULL;
ID3D11SamplerState* pBlackBorderSampler = NULL;

ID3D11Buffer* pTexCoordsBuff = NULL;
#if USE_D3D11_1
ID3D11UnorderedAccessView* pTexCoordsUav = NULL;
#else
ID3D11ShaderResourceView* pTexCoordsSrv = NULL;
#endif

// Deferred context Draw/DrawIndexed hooks
typedef void(WINAPI *D3D11DeferredDrawHook)(ID3D11DeviceContext* pContext, UINT VertexCount, UINT StartVertexLocation);
D3D11DeferredDrawHook phookDeferredDraw = NULL;

typedef void(WINAPI *D3D11DeferredDrawIndexedHook)(ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation);
D3D11DeferredDrawIndexedHook phookDeferredDrawIndexed = NULL;

void WINAPI hookDeferredDraw(ID3D11DeviceContext* pContext, UINT VertexCount, UINT StartVertexLocation)
{
    // Check PS SRV 0 for texture dimensions
    ID3D11ShaderResourceView* pSrv = NULL;
    pContext->PSGetShaderResources(0, 1, &pSrv);
    if (pSrv != NULL)
    {
        ID3D11Resource* pResource = NULL;
        pSrv->GetResource(&pResource);
        if (pResource != NULL)
        {
            D3D11_RESOURCE_DIMENSION resDim;
            pResource->GetType(&resDim);
            if (resDim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
            {
                D3D11_TEXTURE2D_DESC desc;
                static_cast<ID3D11Texture2D*>(pResource)->GetDesc(&desc);
                if (desc.Width >= 256 || desc.Height >= 256)
                {
                    DEBUG((debug, "  DEF Draw: %ux%u vtx=%u\n", desc.Width, desc.Height, VertexCount));
                }
            }
            pResource->Release();
        }
        pSrv->Release();
    }
    phookDeferredDraw(pContext, VertexCount, StartVertexLocation);
}

// (defFrameHasCg declared earlier)

void WINAPI hookDeferredDrawIndexed(ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{
    ID3D11GeometryShader* pGs = NULL;
    ID3D11ShaderResourceView* pSrv = NULL;
    ID3D11Resource* pResource = NULL;
    D3D11_RESOURCE_DIMENSION resDim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    D3D11_TEXTURE2D_DESC texDesc;
    texDesc.Width = 0;
    static const int NumSamplers = 4;
    BOOL restoreGs = FALSE;
    BOOL swapSamplers = FALSE;
    ID3D11SamplerState* pOrigSampler[NumSamplers] = { NULL, NULL, NULL, NULL };
    ID3D11BlendState* pBlendState = NULL;
    D3D11_BLEND_DESC blendDesc;
    UINT soOffset = 0;
    blendDesc.RenderTarget[0].BlendEnable = 0;

#if USE_D3D11_1 == 0
    ID3D11ShaderResourceView* pNoSrv = NULL;
    ID3D11Buffer* pNoBuffer = NULL;
    BOOL restoreSo = FALSE;
    BOOL restoreGsSrv = FALSE;
#endif

    pContext->GSGetShader(&pGs, NULL, NULL);
    pContext->PSGetShaderResources(0, 1, &pSrv);

    // Detect CG decal
    if (defFrameHasCg == TRUE)
    {
        pContext->OMGetBlendState(&pBlendState, NULL, NULL);
        if (pBlendState != NULL)
        {
            pBlendState->GetDesc(&blendDesc);

            if (blendDesc.RenderTarget[0].BlendEnable == 1 &&
                blendDesc.RenderTarget[0].SrcBlendAlpha == D3D10_BLEND_ZERO &&
                blendDesc.RenderTarget[0].DestBlendAlpha == D3D10_BLEND_ONE)
            {
                restoreGs = TRUE;
                pContext->GSSetShader(pDecalGs, NULL, NULL);
                swapSamplers = TRUE;

#if USE_D3D11_1 == 0
                restoreGsSrv = TRUE;
                pContext->GSSetShaderResources(0, 1, &pTexCoordsSrv);
#endif
            }
        }
        SAFE_RELEASE(pBlendState)
    }
    // Detect CG: 1280x960 texture, no GS active
    else if (pSrv != NULL && pGs == NULL && pCgGs != NULL && pDecalGs != NULL && pBlackBorderSampler
#if USE_D3D11_1 == 0
        && pTexCoordsBuff
#endif
        )
    {
        pSrv->GetResource(&pResource);
        if (pResource != NULL)
        {
            pResource->GetType(&resDim);
            if (resDim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
            {
                static_cast<ID3D11Texture2D*>(pResource)->GetDesc(&texDesc);

                if (texDesc.Width == 1280 && texDesc.Height == 960)
                {
                    defFrameHasCg = TRUE;

                    restoreGs = TRUE;
                    pContext->GSSetShader(pCgGs, NULL, NULL);
                    swapSamplers = TRUE;

#if USE_D3D11_1 == 0
                    restoreSo = TRUE;
                    pContext->SOSetTargets(1, &pTexCoordsBuff, &soOffset);
#endif
                }
            }
            SAFE_RELEASE(pResource)
        }
        SAFE_RELEASE(pSrv)
    }
    SAFE_RELEASE(pGs);

    if (swapSamplers)
    {
        pContext->PSGetSamplers(0, NumSamplers, &pOrigSampler[0]);
        for (UINT i = 0; i < NumSamplers; i++)
            pContext->PSSetSamplers(i, 1, &pBlackBorderSampler);
    }

    /////
    phookDeferredDrawIndexed(pContext, IndexCount, StartIndexLocation, BaseVertexLocation);
    /////

    if (restoreGs)
        pContext->GSSetShader(NULL, NULL, NULL);

    if (swapSamplers)
    {
        pContext->PSSetSamplers(0, NumSamplers, &pOrigSampler[0]);
        for (int i = 0; i < NumSamplers; i++)
            SAFE_RELEASE(pOrigSampler[i])
    }

#if USE_D3D11_1 == 0
    if (restoreSo)
        pContext->SOSetTargets(1, &pNoBuffer, NULL);
    if (restoreGsSrv)
        pContext->GSSetShaderResources(0, 1, &pNoSrv);
#endif
}

// Hook CreateDeferredContext on device (vtable 27)
typedef HRESULT(WINAPI *D3D11CreateDeferredContextHook)(ID3D11Device* pDevice, UINT ContextFlags, ID3D11DeviceContext **ppDeferredContext);
D3D11CreateDeferredContextHook phookD3D11CreateDeferredContext = NULL;

HRESULT WINAPI hookD3D11CreateDeferredContext(ID3D11Device* pDevice, UINT ContextFlags, ID3D11DeviceContext **ppDeferredContext)
{
    HRESULT hr = phookD3D11CreateDeferredContext(pDevice, ContextFlags, ppDeferredContext);
    DEBUG((debug, "  CreateDeferredContext! flags=%u ctx=%p hr=0x%x\n",
        ContextFlags, ppDeferredContext ? *ppDeferredContext : NULL, hr));

    if (hr == S_OK && ppDeferredContext && *ppDeferredContext)
    {
        // Hook Draw/DrawIndexed on the deferred context
        DWORD_PTR* pDefVT = (DWORD_PTR*)(*ppDeferredContext);
        pDefVT = (DWORD_PTR*)pDefVT[0];
        DEBUG((debug, "  Deferred VTable=%p DrawIndexed@%p Draw@%p\n",
            pDefVT, (void*)pDefVT[12], (void*)pDefVT[13]));

        if (phookDeferredDrawIndexed == NULL)
        {
            MH_STATUS mh = MH_CreateHook((DWORD_PTR*)pDefVT[12], hookDeferredDrawIndexed, reinterpret_cast<void**>(&phookDeferredDrawIndexed));
            if (mh == MH_OK) MH_EnableHook((DWORD_PTR*)pDefVT[12]);
            else DEBUG((debug, "  Hook deferred DrawIndexed failed %d\n", mh));
        }

        if (phookDeferredDraw == NULL)
        {
            MH_STATUS mh = MH_CreateHook((DWORD_PTR*)pDefVT[13], hookDeferredDraw, reinterpret_cast<void**>(&phookDeferredDraw));
            if (mh == MH_OK) MH_EnableHook((DWORD_PTR*)pDefVT[13]);
            else DEBUG((debug, "  Hook deferred Draw failed %d\n", mh));
        }
    }
    DEBUGFLUSH;
    return hr;
}

// Memory write tracing: find who writes to the framebuffer
static BYTE* g_traceAddr = NULL;       // Address we're monitoring
static DWORD g_traceOldProtect = 0;    // Original page protection
static BOOL g_traceActive = FALSE;
static int g_traceHitCount = 0;

LONG WINAPI TraceVEH(PEXCEPTION_POINTERS pExInfo)
{
    if (pExInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        pExInfo->ExceptionRecord->ExceptionInformation[0] == 1 &&  // write access
        g_traceActive)
    {
        BYTE* faultAddr = (BYTE*)pExInfo->ExceptionRecord->ExceptionInformation[1];

        // Check if the fault is within our monitored region
        if (faultAddr >= g_traceAddr && faultAddr < g_traceAddr + 4096)
        {
            void* rip = (void*)pExInfo->ContextRecord->Rip;
            void* rsp = (void*)pExInfo->ContextRecord->Rsp;

            if (g_traceHitCount < 20)
            {
                DEBUG((debug, "  WRITE TRACE: RIP=%p wrote to %p (RSP=%p RCX=%p RDX=%p R8=%p)\n",
                    rip, faultAddr, rsp,
                    (void*)pExInfo->ContextRecord->Rcx,
                    (void*)pExInfo->ContextRecord->Rdx,
                    (void*)pExInfo->ContextRecord->R8));
                DEBUGFLUSH;
                g_traceHitCount++;
            }

            // Temporarily restore write access so the write can proceed
            DWORD oldProt;
            VirtualProtect(g_traceAddr, 4096, PAGE_READWRITE, &oldProt);
            // Set single-step (trap flag) to re-enable protection after the write instruction
            pExInfo->ContextRecord->EFlags |= 0x100;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }

    // Single-step trap: re-protect the page after the write completed
    if (pExInfo->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP && g_traceActive)
    {
        DWORD oldProt;
        VirtualProtect(g_traceAddr, 4096, PAGE_READONLY, &oldProt);
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

// Bilinear scale 1280x960 RGBA -> 960x720 RGBA, then place centered in 1280x720 with black bars
static void ScaleAndLetterbox(const BYTE* src, int srcW, int srcH, int srcPitch,
                               BYTE* dst, int dstW, int dstH, int dstPitch)
{
    // Output image: 960x720 centered in 1280x720
    int scaledW = 960;   // 1280 * 720/960
    int scaledH = 720;
    int offsetX = (dstW - scaledW) / 2;  // 160

    // Clear to black
    for (int y = 0; y < dstH; y++)
        memset(dst + y * dstPitch, 0, dstW * 4);

    // Scale using bilinear interpolation
    // For each output pixel (ox, oy) in the scaled region:
    // source (sx, sy) = (ox * srcW / scaledW, oy * srcH / scaledH)
    for (int oy = 0; oy < scaledH; oy++)
    {
        BYTE* dstRow = dst + oy * dstPitch + offsetX * 4;

        // Fixed point 16.16 for source Y
        int sy_fp = oy * ((srcH << 16) / scaledH);
        int sy = sy_fp >> 16;
        int fy = sy_fp & 0xFFFF;  // fractional part
        if (sy >= srcH - 1) { sy = srcH - 2; fy = 0xFFFF; }

        const BYTE* srcRow0 = src + sy * srcPitch;
        const BYTE* srcRow1 = src + (sy + 1) * srcPitch;

        for (int ox = 0; ox < scaledW; ox++)
        {
            int sx_fp = ox * ((srcW << 16) / scaledW);
            int sx = sx_fp >> 16;
            int fx = sx_fp & 0xFFFF;
            if (sx >= srcW - 1) { sx = srcW - 2; fx = 0xFFFF; }

            int sx4 = sx * 4;

            // 4 source pixels
            for (int c = 0; c < 4; c++)
            {
                int p00 = srcRow0[sx4 + c];
                int p10 = srcRow0[sx4 + 4 + c];
                int p01 = srcRow1[sx4 + c];
                int p11 = srcRow1[sx4 + 4 + c];

                // Bilinear
                int top = p00 + ((p10 - p00) * fx >> 16);
                int bot = p01 + ((p11 - p01) * fx >> 16);
                int val = top + ((bot - top) * fy >> 16);
                dstRow[ox * 4 + c] = (BYTE)(val < 0 ? 0 : (val > 255 ? 255 : val));
            }
            // Force alpha to 255
            dstRow[ox * 4 + 3] = 255;
        }
    }
}

HRESULT WINAPI hookDXGIPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    static int g_presentFrameNum = 0;
    g_presentFrameNum++;

    // Phase 1: Find the framebuffer address (once)
    if (g_cgFramebuffer == NULL && g_pDevice != NULL && g_presentFrameNum > 3600 && (g_presentFrameNum % 60 == 0))
    {
        // search attempt
        ID3D11Texture2D* pBackBuffer = NULL;
        HRESULT hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
        if (hr == S_OK && pBackBuffer != NULL)
        {
            D3D11_TEXTURE2D_DESC bbDesc;
            pBackBuffer->GetDesc(&bbDesc);

            // Read back the back buffer
            D3D11_TEXTURE2D_DESC stagingDesc = bbDesc;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            stagingDesc.MiscFlags = 0;

            ID3D11Texture2D* pStaging = NULL;
            hr = g_pDevice->CreateTexture2D(&stagingDesc, NULL, &pStaging);
            if (hr == S_OK && pStaging != NULL)
            {
                ID3D11DeviceContext* pCtx = NULL;
                g_pDevice->GetImmediateContext(&pCtx);
                if (pCtx)
                {
                    pCtx->CopyResource(pStaging, pBackBuffer);
                    D3D11_MAPPED_SUBRESOURCE mapped;
                    hr = pCtx->Map(pStaging, 0, D3D11_MAP_READ, 0, &mapped);
                    if (hr == S_OK)
                    {
                        // Take a signature from row 360 (middle of backbuffer), 64 pixels (256 bytes)
                        // These pixels should be distinctive CG content
                        BYTE* bbRow = (BYTE*)mapped.pData + 360 * mapped.RowPitch + 400 * 4;  // row 360, col 400
                        size_t sigLen = 64 * 4;  // 64 pixels = 256 bytes

                        DEBUG((debug, "  CG Search: sig from bb row360 col400: (%u,%u,%u,%u)...\n",
                            bbRow[0], bbRow[1], bbRow[2], bbRow[3]));

                        // Check if this looks like real CG content by sampling color diversity
                        // across multiple rows of the backbuffer
                        int uniqueColors = 0;
                        {
                            DWORD seen[256] = {};
                            for (int sampleY = 100; sampleY < 620; sampleY += 40)
                            {
                                for (int sampleX = 100; sampleX < 1180; sampleX += 80)
                                {
                                    BYTE* px = (BYTE*)mapped.pData + sampleY * mapped.RowPitch + sampleX * 4;
                                    // Hash the RGB into a bucket
                                    BYTE hash = (BYTE)(px[0] ^ px[1] ^ px[2]);
                                    if (!seen[hash]) { seen[hash] = 1; uniqueColors++; }
                                }
                            }
                        }
                        BOOL isContent = (uniqueColors > 30);  // Real CG should have many distinct color hashes
                        DEBUG((debug, "  Color diversity: %d unique hashes (need >30)\n", uniqueColors));

                        if (isContent)
                        {
                            // Take TWO rows from backbuffer for cross-validation
                            BYTE* bbRow200 = (BYTE*)mapped.pData + 200 * mapped.RowPitch;  // full row 200
                            BYTE* bbRow201 = (BYTE*)mapped.pData + 201 * mapped.RowPitch;  // full row 201
                            size_t rowBytes = 1280 * 4;  // 5120 bytes per row

                            DEBUG((debug, "  Searching for full row (5120 bytes) from bb row 200...\n"));
                            DEBUGFLUSH;

                            // Search for full row 200 in memory
                            void* found = ScanMemoryForPattern(bbRow200, rowBytes, 1024 * 1024);
                            int matchCount = 0;

                            while (found != NULL && matchCount < 5)
                            {
                                BYTE* foundAddr = (BYTE*)found;
                                DEBUG((debug, "  MATCH at %p\n", foundAddr));

                                // Check various pitches: does row 201 appear at foundAddr+pitch?
                                int pitches[] = { 5120, 5124, 5128, 5132, 5136, 5140, 5144, 5148, 5152, 5160, 5168, 5176, 5184, 5248, 5376, 5504, 5632, 6144, 8192 };
                                for (int pi = 0; pi < sizeof(pitches)/sizeof(pitches[0]); pi++)
                                {
                                    int pitch = pitches[pi];
                                    BYTE* nextRowAddr = foundAddr + pitch;
                                    MEMORY_BASIC_INFORMATION mbi;
                                    if (VirtualQuery(nextRowAddr, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
                                        (BYTE*)nextRowAddr + rowBytes <= (BYTE*)mbi.BaseAddress + mbi.RegionSize)
                                    {
                                        if (memcmp(nextRowAddr, bbRow201, rowBytes) == 0)
                                        {
                                            DEBUG((debug, "  CONFIRMED pitch=%d! Row 201 matches at %p\n", pitch, nextRowAddr));

                                            // Found the correct pitch. Now find the image start.
                                            // We matched row 200, so the image starts at foundAddr - 200*pitch
                                            BYTE* imgBase = foundAddr - 200 * pitch;
                                            DEBUG((debug, "  Image base (row0) = %p\n", imgBase));

                                            // Check if we can read 960 rows
                                            BYTE* imgEnd = imgBase + 960 * pitch;
                                            MEMORY_BASIC_INFORMATION mbi2;
                                            if (VirtualQuery(imgBase, &mbi2, sizeof(mbi2)) && mbi2.State == MEM_COMMIT &&
                                                (BYTE*)imgBase >= (BYTE*)mbi2.BaseAddress &&
                                                imgEnd <= (BYTE*)mbi2.BaseAddress + mbi2.RegionSize)
                                            {
                                                // Cache the framebuffer address
                                                g_cgFramebuffer = imgBase;
                                                DEBUG((debug, "  CACHED framebuffer at %p (pitch=%d)\n", imgBase, pitch));

                                                // Activate memory write trace on a middle row of the framebuffer
                                                if (!g_traceActive)
                                                {
                                                    // Monitor row 360 (middle of visible area)
                                                    g_traceAddr = imgBase + 360 * 5120;
                                                    // Align to page boundary
                                                    SYSTEM_INFO si;
                                                    GetSystemInfo(&si);
                                                    g_traceAddr = (BYTE*)((ULONG_PTR)g_traceAddr & ~((ULONG_PTR)si.dwPageSize - 1));
                                                    DEBUG((debug, "  Setting write trace on page %p\n", g_traceAddr));
                                                    DEBUGFLUSH;
                                                    if (VirtualProtect(g_traceAddr, 4096, PAGE_READONLY, &g_traceOldProtect))
                                                    {
                                                        g_traceActive = TRUE;
                                                        DEBUG((debug, "  Write trace ACTIVE (old protect=0x%x)\n", g_traceOldProtect));
                                                    }
                                                    else
                                                    {
                                                        DEBUG((debug, "  VirtualProtect failed: %d\n", GetLastError()));
                                                    }
                                                    DEBUGFLUSH;
                                                }
                                            }
                                            else
                                            {
                                                DEBUG((debug, "  Cannot read full 960 rows from base\n"));
                                            }
                                        }
                                    }
                                }

                                matchCount++;
                                // Continue search
                                BYTE* nextSearch = foundAddr + rowBytes;
                                found = NULL;
                                MEMORY_BASIC_INFORMATION mbi3;
                                if (VirtualQuery(nextSearch, &mbi3, sizeof(mbi3)) && mbi3.State == MEM_COMMIT)
                                {
                                    BYTE* regionEnd = (BYTE*)mbi3.BaseAddress + mbi3.RegionSize - rowBytes;
                                    for (BYTE* p = nextSearch; p <= regionEnd; p += 4)
                                    {
                                        if (memcmp(p, bbRow200, rowBytes) == 0)
                                        {
                                            found = p;
                                            break;
                                        }
                                    }
                                }
                            }
                            DEBUG((debug, "  Search done. %d matches.\n", matchCount));
                        }
                        else
                        {
                            DEBUG((debug, "  Backbuffer appears to be solid color, skipping search\n"));
                            // will retry next time since g_cgFramebuffer is still NULL
                        }

                        pCtx->Unmap(pStaging, 0);
                    }
                    pCtx->Release();
                }
                pStaging->Release();
            }
            pBackBuffer->Release();
        }
    }

    // Phase 2: Letterbox (DISABLED during trace)
    if (false && g_cgFramebuffer != NULL && g_pDevice != NULL)
    {
        // Check if the framebuffer bottom rows have content (Event CG detection)
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(g_cgFramebuffer + 800 * 5120, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT)
        {
            BYTE* bottomRow = g_cgFramebuffer + 800 * 5120 + 640 * 4;  // row 800, col 640
            BOOL hasBottomContent = (bottomRow[0] + bottomRow[1] + bottomRow[2] > 30);

            if (hasBottomContent)
            {
                ID3D11Texture2D* pBackBuffer = NULL;
                HRESULT hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
                if (hr == S_OK && pBackBuffer != NULL)
                {
                    // Create staging texture once
                    if (g_pStagingWrite == NULL)
                    {
                        D3D11_TEXTURE2D_DESC desc;
                        pBackBuffer->GetDesc(&desc);
                        desc.Usage = D3D11_USAGE_STAGING;
                        desc.BindFlags = 0;
                        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                        desc.MiscFlags = 0;
                        g_pDevice->CreateTexture2D(&desc, NULL, &g_pStagingWrite);
                    }

                    if (g_pStagingWrite != NULL)
                    {
                        ID3D11DeviceContext* pCtx = NULL;
                        g_pDevice->GetImmediateContext(&pCtx);
                        if (pCtx)
                        {
                            D3D11_MAPPED_SUBRESOURCE mapped;
                            hr = pCtx->Map(g_pStagingWrite, 0, D3D11_MAP_WRITE, 0, &mapped);
                            if (hr == S_OK)
                            {
                                ScaleAndLetterbox(g_cgFramebuffer, 1280, 960, 5120,
                                                  (BYTE*)mapped.pData, 1280, 720, mapped.RowPitch);
                                pCtx->Unmap(g_pStagingWrite, 0);
                                pCtx->CopyResource(pBackBuffer, g_pStagingWrite);
                            }
                            pCtx->Release();
                        }
                    }
                    pBackBuffer->Release();
                }
            }
        }
    }

    return phookDXGIPresent(pSwapChain, SyncInterval, Flags);
}

// (moved to earlier in file)
BOOL frameHasCg = FALSE;

// Diagnostic: catch Map calls to detect CPU->GPU texture writes
HRESULT WINAPI hookD3D11Map(ID3D11DeviceContext* pContext, ID3D11Resource *pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE *pMappedResource)
{
    HRESULT hr = phookD3D11Map(pContext, pResource, Subresource, MapType, MapFlags, pMappedResource);

    if (hr == S_OK && pResource != NULL)
    {
        D3D11_RESOURCE_DIMENSION resDim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
        pResource->GetType(&resDim);
        if (resDim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
        {
            D3D11_TEXTURE2D_DESC desc;
            static_cast<ID3D11Texture2D*>(pResource)->GetDesc(&desc);
            // Log ALL texture Maps, no size filter
            DEBUG((debug, "  Map TEX: %ux%u fmt=%u bind=0x%x usage=%u pitch=%u\n",
                desc.Width, desc.Height, desc.Format, desc.BindFlags, desc.Usage,
                pMappedResource ? pMappedResource->RowPitch : 0));

            // Track for Unmap inspection
            if (desc.Width == 1280 && (desc.Height == 721 || desc.Height == 720))
            {
                g_pMappedCgTexture = pResource;
                if (pMappedResource) g_mappedData = *pMappedResource;
            }
        }
        else if (resDim == D3D11_RESOURCE_DIMENSION_BUFFER)
        {
            D3D11_BUFFER_DESC bdesc;
            static_cast<ID3D11Buffer*>(pResource)->GetDesc(&bdesc);
            if (bdesc.ByteWidth >= 1024)
            {
                DEBUG((debug, "  Map BUF: %u bytes bind=0x%x\n", bdesc.ByteWidth, bdesc.BindFlags));
            }
        }
    }
    return hr;
}

// Diagnostic: catch Unmap to inspect written data
void WINAPI hookD3D11Unmap(ID3D11DeviceContext* pContext, ID3D11Resource *pResource, UINT Subresource)
{
    if (pResource == g_pMappedCgTexture && g_mappedData.pData != NULL)
    {
        unsigned char* data = (unsigned char*)g_mappedData.pData;
        UINT pitch = g_mappedData.RowPitch;

        // Sample pixels across the image: (0,0), (640,0), (0,360), (640,360), (1279,719)
        DEBUG((debug, "  Unmap CG: pitch=%u\n", pitch));
        for (int sy = 0; sy < 720; sy += 180)
        {
            for (int sx = 0; sx < 1280; sx += 320)
            {
                unsigned char* p = data + sy * pitch + sx * 4;
                DEBUG((debug, "    (%d,%d)=(%u,%u,%u,%u)\n", sx, sy, p[0], p[1], p[2], p[3]));
            }
        }

        // Dump first frame to file for analysis
        static int dumpCount = 0;
        if (dumpCount < 3)
        {
            char path[MAX_PATH];
            char tempPath[MAX_PATH];
            GetTempPath(sizeof(tempPath), tempPath);
            sprintf_s(path, "%sCG_DUMP_%d.raw", tempPath, dumpCount);
            FILE* f = fopen(path, "wb");
            if (f)
            {
                for (int y = 0; y < 720; y++)
                    fwrite(data + y * pitch, 1280 * 4, 1, f);
                fclose(f);
                DEBUG((debug, "  Dumped frame to %s\n", path));
            }
            dumpCount++;
        }

        g_pMappedCgTexture = NULL;
        g_mappedData = {};
    }
    phookD3D11Unmap(pContext, pResource, Subresource);
}

// Diagnostic: catch UpdateSubresource to detect CPU->GPU uploads
void WINAPI hookD3D11UpdateSubresource(ID3D11DeviceContext* pContext, ID3D11Resource *pDstResource, UINT DstSubresource, const D3D11_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch)
{
    if (pDstResource != NULL)
    {
        D3D11_RESOURCE_DIMENSION resDim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
        pDstResource->GetType(&resDim);
        if (resDim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
        {
            D3D11_TEXTURE2D_DESC desc;
            static_cast<ID3D11Texture2D*>(pDstResource)->GetDesc(&desc);
            if (desc.Width >= 128 && desc.Height >= 128)
            {
                DEBUG((debug, "  Update TEX: %ux%u pitch=%u box=%p\n", desc.Width, desc.Height, SrcRowPitch, pDstBox));
                if (pDstBox)
                {
                    DEBUG((debug, "    box: (%u,%u)-(%u,%u)\n", pDstBox->left, pDstBox->top, pDstBox->right, pDstBox->bottom));
                }
            }
        }
    }
    phookD3D11UpdateSubresource(pContext, pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
}

// Diagnostic: catch CopyResource
void WINAPI hookD3D11CopyResource(ID3D11DeviceContext* pContext, ID3D11Resource *pDstResource, ID3D11Resource *pSrcResource)
{
    if (pDstResource != NULL)
    {
        D3D11_RESOURCE_DIMENSION resDim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
        pDstResource->GetType(&resDim);
        if (resDim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
        {
            D3D11_TEXTURE2D_DESC dstDesc;
            static_cast<ID3D11Texture2D*>(pDstResource)->GetDesc(&dstDesc);
            D3D11_TEXTURE2D_DESC srcDesc = {};
            if (pSrcResource) {
                D3D11_RESOURCE_DIMENSION srcDim;
                pSrcResource->GetType(&srcDim);
                if (srcDim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
                    static_cast<ID3D11Texture2D*>(pSrcResource)->GetDesc(&srcDesc);
            }
            DEBUG((debug, "  CopyRes: src=%ux%u -> dst=%ux%u\n", srcDesc.Width, srcDesc.Height, dstDesc.Width, dstDesc.Height));
        }
    }
    phookD3D11CopyResource(pContext, pDstResource, pSrcResource);
}

// Diagnostic: catch CopySubresourceRegion
void WINAPI hookD3D11CopySubresourceRegion(ID3D11DeviceContext* pContext, ID3D11Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource *pSrcResource, UINT SrcSubresource, const D3D11_BOX *pSrcBox)
{
    if (pDstResource != NULL)
    {
        D3D11_RESOURCE_DIMENSION resDim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
        pDstResource->GetType(&resDim);
        if (resDim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
        {
            D3D11_TEXTURE2D_DESC dstDesc;
            static_cast<ID3D11Texture2D*>(pDstResource)->GetDesc(&dstDesc);
            if (dstDesc.Width >= 128 && dstDesc.Height >= 128)
            {
                DEBUG((debug, "  CopySub: dst=%ux%u @(%u,%u)", dstDesc.Width, dstDesc.Height, DstX, DstY));
                if (pSrcBox)
                    DEBUG((debug, " srcBox=(%u,%u)-(%u,%u)", pSrcBox->left, pSrcBox->top, pSrcBox->right, pSrcBox->bottom));
                if (pSrcResource) {
                    D3D11_RESOURCE_DIMENSION srcDim;
                    pSrcResource->GetType(&srcDim);
                    if (srcDim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
                        D3D11_TEXTURE2D_DESC srcDesc;
                        static_cast<ID3D11Texture2D*>(pSrcResource)->GetDesc(&srcDesc);
                        DEBUG((debug, " src=%ux%u", srcDesc.Width, srcDesc.Height));
                    }
                }
                DEBUG((debug, "\n"));
            }
        }
    }
    phookD3D11CopySubresourceRegion(pContext, pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox);
}

// Diagnostic: catch ALL texture bindings
void WINAPI hookD3D11PSSetShaderResources(ID3D11DeviceContext* pContext, UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
    if (ppShaderResourceViews != NULL)
    {
        for (UINT i = 0; i < NumViews; i++)
        {
            if (ppShaderResourceViews[i] != NULL)
            {
                ID3D11Resource* pResource = NULL;
                ppShaderResourceViews[i]->GetResource(&pResource);
                if (pResource != NULL)
                {
                    D3D11_RESOURCE_DIMENSION resDim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
                    pResource->GetType(&resDim);
                    if (resDim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
                    {
                        D3D11_TEXTURE2D_DESC desc;
                        static_cast<ID3D11Texture2D*>(pResource)->GetDesc(&desc);
                        if (desc.Width >= 128 && desc.Height >= 128)
                        {
                            DEBUG((debug, "  PSSRV s%u: %ux%u\n", StartSlot + i, desc.Width, desc.Height));
                        }
                    }
                    pResource->Release();
                }
            }
        }
    }
    phookD3D11PSSetShaderResources(pContext, StartSlot, NumViews, ppShaderResourceViews);
}

// Diagnostic: log ALL Draw calls with texture info from multiple SRV slots
void WINAPI hookD3D11Draw(ID3D11DeviceContext* pContext, UINT VertexCount, UINT StartVertexLocation)
{
    // Check PS SRV slots 0-3
    ID3D11ShaderResourceView* srvs[4] = {};
    pContext->PSGetShaderResources(0, 4, srvs);
    for (int slot = 0; slot < 4; slot++)
    {
        if (srvs[slot] != NULL)
        {
            ID3D11Resource* pResource = NULL;
            srvs[slot]->GetResource(&pResource);
            if (pResource != NULL)
            {
                D3D11_RESOURCE_DIMENSION resDim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
                pResource->GetType(&resDim);
                if (resDim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
                {
                    D3D11_TEXTURE2D_DESC desc;
                    static_cast<ID3D11Texture2D*>(pResource)->GetDesc(&desc);
                    DEBUG((debug, "  Draw s%d: %ux%u vtx=%u\n", slot, desc.Width, desc.Height, VertexCount));
                }
                pResource->Release();
            }
            srvs[slot]->Release();
        }
    }

    // Also check VS SRV slots
    ID3D11ShaderResourceView* vsSrvs[4] = {};
    pContext->VSGetShaderResources(0, 4, vsSrvs);
    for (int slot = 0; slot < 4; slot++)
    {
        if (vsSrvs[slot] != NULL)
        {
            ID3D11Resource* pResource = NULL;
            vsSrvs[slot]->GetResource(&pResource);
            if (pResource != NULL)
            {
                D3D11_RESOURCE_DIMENSION resDim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
                pResource->GetType(&resDim);
                if (resDim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
                {
                    D3D11_TEXTURE2D_DESC desc;
                    static_cast<ID3D11Texture2D*>(pResource)->GetDesc(&desc);
                    DEBUG((debug, "  Draw VS s%d: %ux%u vtx=%u\n", slot, desc.Width, desc.Height, VertexCount));
                }
                pResource->Release();
            }
            vsSrvs[slot]->Release();
        }
    }

    phookD3D11Draw(pContext, VertexCount, StartVertexLocation);
}

void WINAPI hookD3D11SetPredication(ID3D11DeviceContext* pContext, ID3D11Predicate *pPredicate, BOOL PredicateValue)
{
    // The app calls this at the start and end of every frame. So we'll use this to detect when a frame ends.
    // I don't think they would use it any other time. The app has no use for predication.
    DEBUG((debug, "hookD3D11SetPredication\n"));
    frameHasCg = FALSE;
    phookD3D11SetPredication(pContext, pPredicate, PredicateValue);
    DEBUGFLUSH;
}

void WINAPI hookD3D11DrawIndexed(ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{
    //return;
    DEBUG((debug, "hookD3D11DrawIndexed\n"));

    ID3D11GeometryShader* pGs = NULL;
    ID3D11ShaderResourceView* pSrv = NULL;
    ID3D11Resource* pResource = NULL;
    D3D11_RESOURCE_DIMENSION resDim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    D3D11_TEXTURE2D_DESC texDesc;
    texDesc.Width = 0;
    static const int NumSamplers = 4;
    BOOL restoreGs = FALSE;
    BOOL swapSamplers = FALSE;
    ID3D11SamplerState* pOrigSampler[NumSamplers] = { NULL, NULL, NULL, NULL }; // The sampler slot used seems to change afer recalling the text
    ID3D11ShaderResourceView* pNoSrv = NULL;
    ID3D11Buffer* pNoBuffer = NULL;
    ID3D11BlendState* pBlendState = NULL;
    D3D11_BLEND_DESC blendDesc;
    UINT soOffset = 0;
    blendDesc.RenderTarget[0].BlendEnable = 0;

#if USE_D3D11_1
    ID3D11RenderTargetView* pRtv = NULL;
    ID3D11DepthStencilView* pDsv = NULL;
#else
    BOOL restoreSo = FALSE;
    BOOL restoreGsSrv = FALSE;
#endif

    pContext->GSGetShader(&pGs, NULL, NULL);
    pContext->PSGetShaderResources(0, 1, &pSrv);

    // Diagnostic: log texture dimensions for every draw call with a texture
    if (pSrv != NULL)
    {
        ID3D11Resource* pDiagResource = NULL;
        pSrv->GetResource(&pDiagResource);
        if (pDiagResource != NULL)
        {
            D3D11_RESOURCE_DIMENSION diagDim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
            pDiagResource->GetType(&diagDim);
            if (diagDim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
            {
                D3D11_TEXTURE2D_DESC diagDesc;
                static_cast<ID3D11Texture2D*>(pDiagResource)->GetDesc(&diagDesc);
                // Only log "interesting" textures (not tiny UI elements)
                if (diagDesc.Width >= 256 || diagDesc.Height >= 256)
                {
                    DEBUG((debug, "  TEX: %ux%u gs=%p\n", diagDesc.Width, diagDesc.Height, pGs));
                }
            }
            pDiagResource->Release();
        }
    }

    // Detect CG decal
    if (frameHasCg == TRUE)
    {
        pContext->OMGetBlendState(&pBlendState, NULL, NULL);
        if (pBlendState != NULL)
        {
            pBlendState->GetDesc(&blendDesc);

            // Detect the CG decal based on the slightly unusual blend state.
            // Other draw calls appear to keep the Src alpha rather than the dest
            if (blendDesc.RenderTarget[0].BlendEnable == 1 &&
                blendDesc.RenderTarget[0].SrcBlendAlpha == D3D10_BLEND_ZERO &&
                blendDesc.RenderTarget[0].DestBlendAlpha == D3D10_BLEND_ONE)
            {
                DEBUG((debug, "hookD3D11DrawIndexed Decal detected\n"));
                DEBUGFLUSH;

                // Swap in the replacement GS to adjust the vertex positions
                restoreGs = TRUE;
                pContext->GSSetShader(pDecalGs, NULL, NULL);

                // Swap in our replacement sampler.
                // After hiding and recalling text, the app will switch to a point sampler instead of a linear sampler
                // for some reason. This becomes a problem now that the CG is being shrunk down to the window size.
                // To fix this, swap in our linear sampler.
                swapSamplers = TRUE;

#if USE_D3D11_1
                // Add the texture coordinates UAV to the output merger stage.
                // I'm not going to bother unbinding the UAV after the draw call.
                pContext->OMGetRenderTargets(1, &pRtv, &pDsv);
                pContext->OMSetRenderTargetsAndUnorderedAccessViews(1, &pRtv, pDsv, 1, 1, &pTexCoordsUav, NULL);
                SAFE_RELEASE(pRtv);
                SAFE_RELEASE(pDsv);
#else
                restoreGsSrv = TRUE;
                pContext->GSSetShaderResources(0, 1, &pTexCoordsSrv);
#endif
            }
        }

        SAFE_RELEASE(pBlendState)
    }
    // Detect CG
    else if (pSrv != NULL && pGs == NULL && pCgGs != NULL && pDecalGs != NULL && pBlackBorderSampler
#if USE_D3D11_1
        && pTexCoordsUav
#else
        && pTexCoordsBuff
#endif
        )
    {
        pSrv->GetResource(&pResource);
        if (pResource != NULL)
        {
            pResource->GetType(&resDim);
            if (resDim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
            {
                static_cast<ID3D11Texture2D*>(pResource)->GetDesc(&texDesc);

                // Detect the 4:3 CG based on the W/H
                if (texDesc.Width == 1280 && texDesc.Height == 960)
                {
                    DEBUG((debug, "hookD3D11DrawIndexed CG detected\n"));
                    DEBUGFLUSH;

                    frameHasCg = TRUE;

                    // Swap in the replacement GS to adjust the texture coordinates
                    restoreGs = TRUE;
                    pContext->GSSetShader(pCgGs, NULL, NULL);

                    // Swap in a sampler with a black border color.
                    swapSamplers = TRUE;
#if USE_D3D11_1
                    // Add the texture coordinates UAV to the output merger stage.
                    // I'm not going to bother unbinding the UAV after the draw call.
                    pContext->OMGetRenderTargets(1, &pRtv, &pDsv);
                    pContext->OMSetRenderTargetsAndUnorderedAccessViews(1, &pRtv, pDsv, 1, 1, &pTexCoordsUav, NULL);
                    SAFE_RELEASE(pRtv);
                    SAFE_RELEASE(pDsv);
#else
                    restoreSo = TRUE;
                    pContext->SOSetTargets(1, &pTexCoordsBuff, &soOffset);
#endif
                }
            }
            SAFE_RELEASE(pResource)
        }
        SAFE_RELEASE(pSrv)
    }
    SAFE_RELEASE(pGs);

    if (swapSamplers)
    {
        pContext->PSGetSamplers(0, NumSamplers, &pOrigSampler[0]);
        for (UINT i = 0; i < NumSamplers; i++)
        {
            pContext->PSSetSamplers(i, 1, &pBlackBorderSampler);
        }

    }

    /////
    phookD3D11DrawIndexed(pContext, IndexCount, StartIndexLocation, BaseVertexLocation);
    /////

    if (restoreGs)
    {
        pContext->GSSetShader(NULL, NULL, NULL);
    }

    if (swapSamplers)
    {
        pContext->PSSetSamplers(0, NumSamplers, &pOrigSampler[0]);
        for (int i = 0; i < NumSamplers; i++)
        {
            SAFE_RELEASE(pOrigSampler[i])
        }
    }

#if USE_D3D11_1 == 0
    if (restoreSo)
    {
        pContext->SOSetTargets(1, &pNoBuffer, NULL);
    }

    if (restoreGsSrv)
    {
        pContext->GSSetShaderResources(0, 1, &pNoSrv);
    }
#endif

    DEBUGFLUSH;
}


extern "C"
{
    // D3D11CreateDevice has custom logic so it remains a full C++ function.
    // Exported via D3D11.def as D3D11CreateDevice=_I_D3D11CreateDevice
    typedef HRESULT(WINAPI *tD3D11CreateDevice)(_In_opt_ VOID* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
        UINT Flags, _In_opt_ const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT SDKVERSION,
        _Out_opt_ ID3D11Device** ppDevice, _Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel, _Out_opt_ VOID** ppImmediateContext);
    tD3D11CreateDevice _O_D3D11CreateDevice;

    HRESULT WINAPI _I_D3D11CreateDevice(
        _In_opt_ VOID* pAdapter,
        D3D_DRIVER_TYPE DriverType,
        HMODULE Software,
        UINT Flags,
        _In_opt_ const D3D_FEATURE_LEVEL* pFeatureLevels,
        UINT FeatureLevels,
        UINT SDKVERSION,
        _Out_opt_ ID3D11Device** ppDevice,
        _Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel,
        _Out_opt_ VOID** ppImmediateContext)
    {
        MH_STATUS mhRet;
        HRESULT createDeviceHr;
        HRESULT hr;
        D3D11_SAMPLER_DESC samplerDesc;
        D3D11_BUFFER_DESC bufferDesc;

        // wait for debugger
#if 0
        static BOOL wait = TRUE;
        while (wait);
#endif

#if DEBUG_LOG_EN
        // The debug runtime gets angry about mismatched VS/GS/PS in/out semantics and removes the GS.
        // So we can't use the debug runtime. I hate Microsoft.
        //Flags &= ~(D3D11_CREATE_DEVICE_DEBUG);
        //Flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        static int createDeviceCount = 0;
        createDeviceCount++;
        DEBUG((debug, "D3D11CreateDevice call #%d DriverType=%d\n", createDeviceCount, DriverType));
#if USE_D3D11_1
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;

        // The D3D version needs to upgraded to 11.1 so that we can access UAVs from GS.
        D3D_FEATURE_LEVEL retFl;
        D3D_FEATURE_LEVEL fl[2] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

        createDeviceHr = _O_D3D11CreateDevice(pAdapter, DriverType, Software, Flags, &fl[0],
            2, SDKVERSION, ppDevice, &retFl, ppImmediateContext);

        if (pFeatureLevel != NULL)
        {
            *pFeatureLevel = retFl;
        }

        if (retFl != D3D_FEATURE_LEVEL_11_1)
        {
            DEBUG((debug, "Could not Create D3D11.1 Device\n"));
        }
#else
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;

        createDeviceHr = _O_D3D11CreateDevice(pAdapter, DriverType, Software, Flags, pFeatureLevels,
            FeatureLevels, SDKVERSION, ppDevice, pFeatureLevel, ppImmediateContext);
#endif

        DEBUG((debug, "  ppImmediateContext=%p phookDrawIndexed=%p\n", ppImmediateContext ? *ppImmediateContext : NULL, phookD3D11DrawIndexed));

        if (*ppImmediateContext != NULL
#if USE_D3D11_1
            && retFl == D3D_FEATURE_LEVEL_11_1
#endif
            )
        {
            // Disable previous hooks if re-hooking a new device
            if (phookD3D11DrawIndexed != NULL)
            {
                DEBUG((debug, "  Re-hooking new device context (previous hooks will be replaced)\n"));
                MH_DisableHook(MH_ALL_HOOKS);
                MH_RemoveHook(MH_ALL_HOOKS);
                phookD3D11DrawIndexed = NULL;
                phookD3D11Draw = NULL;
                phookD3D11SetPredication = NULL;
                phookD3D11PSSetShaderResources = NULL;
            }

            // Get the VTable and Hook DrawIndexed
            pContextVTable = (DWORD_PTR*)(*ppImmediateContext);
            pContextVTable = (DWORD_PTR*)pContextVTable[0];
            DEBUG((debug, "  ctx0 VTable=%p DrawIndexed@%p Draw@%p Map@%p\n",
                pContextVTable, (void*)pContextVTable[12], (void*)pContextVTable[13], (void*)pContextVTable[14]));

            // Check if ID3D11DeviceContext1 has different vtable entries
            {
                ID3D11DeviceContext1* pCtx1 = NULL;
                HRESULT qiHr = ((ID3D11DeviceContext*)*ppImmediateContext)->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&pCtx1);
                if (qiHr == S_OK && pCtx1 != NULL)
                {
                    DWORD_PTR* vt1 = (DWORD_PTR*)pCtx1;
                    vt1 = (DWORD_PTR*)vt1[0];
                    DEBUG((debug, "  ctx1 VTable=%p DrawIndexed@%p Draw@%p Map@%p\n",
                        vt1, (void*)vt1[12], (void*)vt1[13], (void*)vt1[14]));
                    // Dump base copy methods vs extended
                    DEBUG((debug, "  vt[46] CopySub=%p vt[47] CopyRes=%p vt[48] UpdateSub=%p\n",
                        (void*)vt1[46], (void*)vt1[47], (void*)vt1[48]));
                    // Dump all from 105 to 120 to find Context1 methods
                    for (int vi = 105; vi <= 120; vi++)
                        DEBUG((debug, "  vt[%d]=%p\n", vi, (void*)vt1[vi]));
                    if (vt1[12] != pContextVTable[12])
                        DEBUG((debug, "  WARNING: ctx1 DrawIndexed differs from ctx0!\n"));
                    if (vt1[13] != pContextVTable[13])
                        DEBUG((debug, "  WARNING: ctx1 Draw differs from ctx0!\n"));
                    if (vt1[14] != pContextVTable[14])
                        DEBUG((debug, "  WARNING: ctx1 Map differs from ctx0!\n"));
                    pCtx1->Release();
                }
                else
                {
                    DEBUG((debug, "  QueryInterface for ID3D11DeviceContext1 failed hr=0x%x\n", qiHr));
                }
            }

            mhRet = MH_CreateHook((DWORD_PTR*)pContextVTable[12], hookD3D11DrawIndexed, reinterpret_cast<void**>(&phookD3D11DrawIndexed));
            if (mhRet != MH_OK)
            {
                DEBUG((debug, "Error. MH_CreateHook DrawIndexed Failed.\n"));
            }

            mhRet = MH_EnableHook((DWORD_PTR*)pContextVTable[12]);
            if (mhRet != MH_OK)
            {
                DEBUG((debug, "Error. MH_EnableHook DrawIndexed Failed.\n"));
            }

            // Hook PSSetShaderResources (VTable offset 8) - catch all texture bindings
            mhRet = MH_CreateHook((DWORD_PTR*)pContextVTable[8], hookD3D11PSSetShaderResources, reinterpret_cast<void**>(&phookD3D11PSSetShaderResources));
            if (mhRet != MH_OK)
            {
                DEBUG((debug, "Error. MH_CreateHook PSSetShaderResources Failed.\n"));
            }
            mhRet = MH_EnableHook((DWORD_PTR*)pContextVTable[8]);
            if (mhRet != MH_OK)
            {
                DEBUG((debug, "Error. MH_EnableHook PSSetShaderResources Failed.\n"));
            }

            // Hook Draw (VTable offset 13) - Kanon may use Draw instead of DrawIndexed
            mhRet = MH_CreateHook((DWORD_PTR*)pContextVTable[13], hookD3D11Draw, reinterpret_cast<void**>(&phookD3D11Draw));
            if (mhRet != MH_OK)
            {
                DEBUG((debug, "Error. MH_CreateHook Draw Failed.\n"));
            }

            mhRet = MH_EnableHook((DWORD_PTR*)pContextVTable[13]);
            if (mhRet != MH_OK)
            {
                DEBUG((debug, "Error. MH_EnableHook Draw Failed.\n"));
            }

            mhRet = MH_CreateHook((DWORD_PTR*)pContextVTable[30], hookD3D11SetPredication, reinterpret_cast<void**>(&phookD3D11SetPredication));
            if (mhRet != MH_OK)
            {
                DEBUG((debug, "Error. MH_CreateHook SetPredication Failed.\n"));
            }

            mhRet = MH_EnableHook((DWORD_PTR*)pContextVTable[30]);
            if (mhRet != MH_OK)
            {
                DEBUG((debug, "Error. MH_EnableHook SetPredication Failed.\n"));
            }

            // Hook ExecuteCommandList (VTable offset 58)
            mhRet = MH_CreateHook((DWORD_PTR*)pContextVTable[58], hookD3D11ExecuteCommandList, reinterpret_cast<void**>(&phookD3D11ExecuteCommandList));
            if (mhRet == MH_OK) MH_EnableHook((DWORD_PTR*)pContextVTable[58]);
            else DEBUG((debug, "Error. Hook ExecuteCommandList failed %d\n", mhRet));

            // Hook CreateDeferredContext on device (VTable offset 27)
            {
                DWORD_PTR* pDevVT = (DWORD_PTR*)(*ppDevice);
                pDevVT = (DWORD_PTR*)pDevVT[0];
                mhRet = MH_CreateHook((DWORD_PTR*)pDevVT[27], hookD3D11CreateDeferredContext, reinterpret_cast<void**>(&phookD3D11CreateDeferredContext));
                if (mhRet == MH_OK) MH_EnableHook((DWORD_PTR*)pDevVT[27]);
                else DEBUG((debug, "Error. Hook CreateDeferredContext failed %d\n", mhRet));
            }

            // Hook Map (VTable offset 14)
            mhRet = MH_CreateHook((DWORD_PTR*)pContextVTable[14], hookD3D11Map, reinterpret_cast<void**>(&phookD3D11Map));
            if (mhRet != MH_OK) { DEBUG((debug, "Error. MH_CreateHook Map Failed. %d\n", mhRet)); }
            mhRet = MH_EnableHook((DWORD_PTR*)pContextVTable[14]);
            if (mhRet != MH_OK) { DEBUG((debug, "Error. MH_EnableHook Map Failed.\n")); }

            // Hook UpdateSubresource (VTable offset 48)
            mhRet = MH_CreateHook((DWORD_PTR*)pContextVTable[48], hookD3D11UpdateSubresource, reinterpret_cast<void**>(&phookD3D11UpdateSubresource));
            if (mhRet != MH_OK) { DEBUG((debug, "Error. MH_CreateHook UpdateSubresource Failed. %d\n", mhRet)); }
            mhRet = MH_EnableHook((DWORD_PTR*)pContextVTable[48]);
            if (mhRet != MH_OK) { DEBUG((debug, "Error. MH_EnableHook UpdateSubresource Failed.\n")); }

            // Hook Unmap (VTable offset 15)
            mhRet = MH_CreateHook((DWORD_PTR*)pContextVTable[15], hookD3D11Unmap, reinterpret_cast<void**>(&phookD3D11Unmap));
            if (mhRet != MH_OK) { DEBUG((debug, "Error. MH_CreateHook Unmap Failed. %d\n", mhRet)); }
            mhRet = MH_EnableHook((DWORD_PTR*)pContextVTable[15]);
            if (mhRet != MH_OK) { DEBUG((debug, "Error. MH_EnableHook Unmap Failed.\n")); }

            // Hook CopySubresourceRegion (VTable offset 46)
            mhRet = MH_CreateHook((DWORD_PTR*)pContextVTable[46], hookD3D11CopySubresourceRegion, reinterpret_cast<void**>(&phookD3D11CopySubresourceRegion));
            if (mhRet != MH_OK) { DEBUG((debug, "Error. MH_CreateHook CopySubresourceRegion Failed. %d\n", mhRet)); }
            mhRet = MH_EnableHook((DWORD_PTR*)pContextVTable[46]);
            if (mhRet != MH_OK) { DEBUG((debug, "Error. MH_EnableHook CopySubresourceRegion Failed.\n")); }

            // Hook CopyResource (VTable offset 47)
            mhRet = MH_CreateHook((DWORD_PTR*)pContextVTable[47], hookD3D11CopyResource, reinterpret_cast<void**>(&phookD3D11CopyResource));
            if (mhRet != MH_OK) { DEBUG((debug, "Error. MH_CreateHook CopyResource Failed. %d\n", mhRet)); }
            mhRet = MH_EnableHook((DWORD_PTR*)pContextVTable[47]);
            if (mhRet != MH_OK) { DEBUG((debug, "Error. MH_EnableHook CopyResource Failed.\n")); }

            // Hook ID3D11DeviceContext1 extended methods
            {
                ID3D11DeviceContext1* pCtx1 = NULL;
                HRESULT qihr = ((ID3D11DeviceContext*)*ppImmediateContext)->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&pCtx1);
                if (qihr == S_OK && pCtx1)
                {
                    DWORD_PTR* vt1 = (DWORD_PTR*)pCtx1;
                    vt1 = (DWORD_PTR*)vt1[0];
                    // Log all vtable entries from offset 110 to 130 to find Context1 methods
                    DEBUG((debug, "  Context1 VTable extended methods:\n"));
                    for (int vi = 110; vi <= 135; vi++)
                    {
                        DEBUG((debug, "    vt[%d] = %p\n", vi, (void*)vt1[vi]));
                    }
                    pCtx1->Release();
                }
            }

            // Hook IDXGISwapChain::Present via device -> DXGI chain
            g_pDevice = *ppDevice;
            {
                IDXGIDevice* pDXGIDevice = NULL;
                hr = (*ppDevice)->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDXGIDevice);
                if (hr == S_OK && pDXGIDevice)
                {
                    IDXGIAdapter* pAdapter = NULL;
                    hr = pDXGIDevice->GetAdapter(&pAdapter);
                    if (hr == S_OK && pAdapter)
                    {
                        IDXGIFactory* pFactory = NULL;
                        hr = pAdapter->GetParent(__uuidof(IDXGIFactory), (void**)&pFactory);
                        if (hr == S_OK && pFactory)
                        {
                            // Create a temporary swap chain to get the Present vtable address
                            DXGI_SWAP_CHAIN_DESC scDesc = {};
                            scDesc.BufferCount = 1;
                            scDesc.BufferDesc.Width = 4;
                            scDesc.BufferDesc.Height = 4;
                            scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                            scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                            scDesc.OutputWindow = GetDesktopWindow();
                            scDesc.SampleDesc.Count = 1;
                            scDesc.Windowed = TRUE;
                            scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

                            IDXGISwapChain* pTempSC = NULL;
                            hr = pFactory->CreateSwapChain(*ppDevice, &scDesc, &pTempSC);
                            if (hr == S_OK && pTempSC)
                            {
                                DWORD_PTR* pSCVTable = (DWORD_PTR*)pTempSC;
                                pSCVTable = (DWORD_PTR*)pSCVTable[0];
                                // IDXGISwapChain::Present is vtable offset 8
                                DEBUG((debug, "  SwapChain VTable=%p Present@%p\n", pSCVTable, (void*)pSCVTable[8]));

                                mhRet = MH_CreateHook((DWORD_PTR*)pSCVTable[8], hookDXGIPresent, reinterpret_cast<void**>(&phookDXGIPresent));
                                if (mhRet != MH_OK) { DEBUG((debug, "Error. MH_CreateHook Present Failed. %d\n", mhRet)); }
                                mhRet = MH_EnableHook((DWORD_PTR*)pSCVTable[8]);
                                if (mhRet != MH_OK) { DEBUG((debug, "Error. MH_EnableHook Present Failed.\n")); }

                                // Hook IDXGISurface1::GetDC/ReleaseDC via the temp swap chain's back buffer
                                ID3D11Texture2D* pTempBB = NULL;
                                if (pTempSC->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pTempBB) == S_OK && pTempBB)
                                {
                                    IDXGISurface1* pSurf = NULL;
                                    if (pTempBB->QueryInterface(__uuidof(IDXGISurface1), (void**)&pSurf) == S_OK && pSurf)
                                    {
                                        DWORD_PTR* pSurfVT = (DWORD_PTR*)pSurf;
                                        pSurfVT = (DWORD_PTR*)pSurfVT[0];
                                        // IDXGISurface1::GetDC is vtable offset 11, ReleaseDC is 12
                                        // (IUnknown:3 + IDXGIObject:3 + IDXGIDeviceSubObject:1 + IDXGISurface:2 + GetDC:1 + ReleaseDC:1)
                                        DEBUG((debug, "  Surface1 VTable: GetDC@%p ReleaseDC@%p\n", (void*)pSurfVT[11], (void*)pSurfVT[12]));
                                        mhRet = MH_CreateHook((DWORD_PTR*)pSurfVT[11], hookDXGISurfaceGetDC, (void**)&pOrigDXGISurfaceGetDC);
                                        if (mhRet == MH_OK) MH_EnableHook((DWORD_PTR*)pSurfVT[11]);
                                        else DEBUG((debug, "  Hook GetDC failed %d\n", mhRet));
                                        mhRet = MH_CreateHook((DWORD_PTR*)pSurfVT[12], hookDXGISurfaceReleaseDC, (void**)&pOrigDXGISurfaceReleaseDC);
                                        if (mhRet == MH_OK) MH_EnableHook((DWORD_PTR*)pSurfVT[12]);
                                        else DEBUG((debug, "  Hook ReleaseDC failed %d\n", mhRet));
                                        pSurf->Release();
                                    }
                                    pTempBB->Release();
                                }

                                pTempSC->Release();
                            }
                            else { DEBUG((debug, "Error. Temp SwapChain creation failed. hr=0x%x\n", hr)); }
                            pFactory->Release();
                        }
                        pAdapter->Release();
                    }
                    pDXGIDevice->Release();
                }
            }

            // Create the Decrop GS now since we have the device pointer
#if USE_D3D11_1
            hr = (*ppDevice)->CreateGeometryShader(&g_CgGs[0], sizeof(g_CgGs), NULL, &pCgGs);
#else
            D3D11_SO_DECLARATION_ENTRY pSoDecl[7] =
            {
                { 0, "SV_POSITION", 0, 0, 4, 0 },
                { 0, "a", 0, 0, 4, 0 },
                { 0, "b", 0, 0, 4, 0 },
                { 0, "c", 0, 0, 4, 0 },
                { 0, "d", 0, 0, 4, 0 },
                { 0, "e", 0, 0, 4, 0 },
                { 0, "f", 0, 0, 4, 0 },
            };


            hr = (*ppDevice)->CreateGeometryShaderWithStreamOutput(&g_CgGs[0], sizeof(g_CgGs), &pSoDecl[0], 7, NULL, 0, 0, NULL, &pCgGs);
#endif
            if (hr != S_OK || pCgGs == NULL)
            {
                DEBUG((debug, "Error. CgGs compile failed.\n"));
            }

            hr = (*ppDevice)->CreateGeometryShader(&g_DecalGs[0], sizeof(g_DecalGs), NULL, &pDecalGs);
            if (hr != S_OK || pDecalGs == NULL)
            {
                DEBUG((debug, "Error. DecalGs compile failed.\n"));
            }

            samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
            samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
            samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
            samplerDesc.MipLODBias = 0.0f;
            samplerDesc.MaxAnisotropy = 1;
            samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
            samplerDesc.BorderColor[0] = 0.0f;
            samplerDesc.BorderColor[1] = 0.0f;
            samplerDesc.BorderColor[2] = 0.0f;
            samplerDesc.BorderColor[3] = 0.0f;
            samplerDesc.MinLOD = 0.0f;
            samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
            hr = (*ppDevice)->CreateSamplerState(&samplerDesc, &pBlackBorderSampler);
            if (hr != S_OK || pBlackBorderSampler == NULL)
            {
                DEBUG((debug, "Error. Black border color sampler create failed.\n"));
            }

            bufferDesc.ByteWidth =
#if USE_D3D11_1
                sizeof(float)* 256;
#else
                sizeof(float)* 256;
#endif
            bufferDesc.Usage = D3D11_USAGE_DEFAULT;
            bufferDesc.BindFlags =
#if USE_D3D11_1
                D3D11_BIND_UNORDERED_ACCESS;
#else
                D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_STREAM_OUTPUT;
#endif
            bufferDesc.CPUAccessFlags = 0;
            bufferDesc.MiscFlags = 0;
            bufferDesc.StructureByteStride = sizeof(float);
            hr = (*ppDevice)->CreateBuffer(&bufferDesc, NULL, &pTexCoordsBuff);
            if (hr != S_OK || pTexCoordsBuff == NULL)
            {
                DEBUG((debug, "Error. Tex Coords Buffer Create Failed.\n"));
            }

#if USE_D3D11_1
            uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.FirstElement = 0;
            uavDesc.Buffer.NumElements = bufferDesc.ByteWidth / sizeof(float);;
            uavDesc.Buffer.Flags = 0;
            hr = (*ppDevice)->CreateUnorderedAccessView(pTexCoordsBuff, &uavDesc, &pTexCoordsUav);
            if (hr != S_OK || pTexCoordsUav == NULL)
            {
                DEBUG((debug, "Error. Tex Coords UAV Create Failed.\n"));
            }
#else
            srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = bufferDesc.ByteWidth / sizeof(float);
            hr = (*ppDevice)->CreateShaderResourceView(pTexCoordsBuff, &srvDesc, &pTexCoordsSrv);
            if (hr != S_OK || pTexCoordsSrv == NULL)
            {
                DEBUG((debug, "Error. Tex Coords SRV Create Failed.\n"));
            }
#endif
        }

        DEBUGFLUSH;
        return createDeviceHr;
    }
}


BOOL WINAPI DllMain(HINSTANCE hI, DWORD reason, LPVOID notUsed)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
#if DEBUG_LOG_EN
        if(!debug)
        {
            char tempPath[MAX_PATH];
            GetTempPath(sizeof(tempPath), tempPath);
            strcat_s(tempPath, "D3D11_DLL_SHIM_DEBUG.txt");
            debug = fopen(tempPath, "w");
        }
#endif // DEBUG_LOG_EN
        DEBUG((debug, "DllMain\n"));

        char realDLL[MAX_PATH];
        (void)GetSystemDirectory(realDLL, sizeof(realDLL) - 1);
        strcat_s(realDLL, MAX_PATH, "\\D3D11.dll");
        gs_hDLL = LoadLibrary(realDLL);
        if (!gs_hDLL)
            return FALSE;

        _O_D3D11CoreCreateDevice = GetProcAddress(gs_hDLL, "D3D11CoreCreateDevice");
        _O_D3D11CoreCreateLayeredDevice = GetProcAddress(gs_hDLL, "D3D11CoreCreateLayeredDevice");
        _O_D3D11CoreGetLayeredDeviceSize = GetProcAddress(gs_hDLL, "D3D11CoreGetLayeredDeviceSize");
        _O_D3D11CoreRegisterLayers = GetProcAddress(gs_hDLL, "D3D11CoreRegisterLayers");
        _O_D3D11CreateDevice = (tD3D11CreateDevice)GetProcAddress(gs_hDLL, "D3D11CreateDevice");
        _O_D3D11CreateDeviceAndSwapChain = GetProcAddress(gs_hDLL, "D3D11CreateDeviceAndSwapChain");
        _O_D3DKMTCloseAdapter = GetProcAddress(gs_hDLL, "D3DKMTCloseAdapter");
        _O_D3DKMTCreateAllocation = GetProcAddress(gs_hDLL, "D3DKMTCreateAllocation");
        _O_D3DKMTCreateContext = GetProcAddress(gs_hDLL, "D3DKMTCreateContext");
        _O_D3DKMTCreateDevice = GetProcAddress(gs_hDLL, "D3DKMTCreateDevice");
        _O_D3DKMTCreateSynchronizationObject = GetProcAddress(gs_hDLL, "D3DKMTCreateSynchronizationObject");
        _O_D3DKMTDestroyAllocation = GetProcAddress(gs_hDLL, "D3DKMTDestroyAllocation");
        _O_D3DKMTDestroyContext = GetProcAddress(gs_hDLL, "D3DKMTDestroyContext");
        _O_D3DKMTDestroyDevice = GetProcAddress(gs_hDLL, "D3DKMTDestroyDevice");
        _O_D3DKMTDestroySynchronizationObject = GetProcAddress(gs_hDLL, "D3DKMTDestroySynchronizationObject");
        _O_D3DKMTEscape = GetProcAddress(gs_hDLL, "D3DKMTEscape");
        _O_D3DKMTGetContextSchedulingPriority = GetProcAddress(gs_hDLL, "D3DKMTGetContextSchedulingPriority");
        _O_D3DKMTGetDeviceState = GetProcAddress(gs_hDLL, "D3DKMTGetDeviceState");
        _O_D3DKMTGetDisplayModeList = GetProcAddress(gs_hDLL, "D3DKMTGetDisplayModeList");
        _O_D3DKMTGetMultisampleMethodList = GetProcAddress(gs_hDLL, "D3DKMTGetMultisampleMethodList");
        _O_D3DKMTGetRuntimeData = GetProcAddress(gs_hDLL, "D3DKMTGetRuntimeData");
        _O_D3DKMTGetSharedPrimaryHandle = GetProcAddress(gs_hDLL, "D3DKMTGetSharedPrimaryHandle");
        _O_D3DKMTLock = GetProcAddress(gs_hDLL, "D3DKMTLock");
        _O_D3DKMTOpenAdapterFromHdc = GetProcAddress(gs_hDLL, "D3DKMTOpenAdapterFromHdc");
        _O_D3DKMTOpenResource = GetProcAddress(gs_hDLL, "D3DKMTOpenResource");
        _O_D3DKMTPresent = GetProcAddress(gs_hDLL, "D3DKMTPresent");
        _O_D3DKMTQueryAdapterInfo = GetProcAddress(gs_hDLL, "D3DKMTQueryAdapterInfo");
        _O_D3DKMTQueryAllocationResidency = GetProcAddress(gs_hDLL, "D3DKMTQueryAllocationResidency");
        _O_D3DKMTQueryResourceInfo = GetProcAddress(gs_hDLL, "D3DKMTQueryResourceInfo");
        _O_D3DKMTRender = GetProcAddress(gs_hDLL, "D3DKMTRender");
        _O_D3DKMTSetAllocationPriority = GetProcAddress(gs_hDLL, "D3DKMTSetAllocationPriority");
        _O_D3DKMTSetContextSchedulingPriority = GetProcAddress(gs_hDLL, "D3DKMTSetContextSchedulingPriority");
        _O_D3DKMTSetDisplayMode = GetProcAddress(gs_hDLL, "D3DKMTSetDisplayMode");
        _O_D3DKMTSetDisplayPrivateDriverFormat = GetProcAddress(gs_hDLL, "D3DKMTSetDisplayPrivateDriverFormat");
        _O_D3DKMTSetGammaRamp = GetProcAddress(gs_hDLL, "D3DKMTSetGammaRamp");
        _O_D3DKMTSetVidPnSourceOwner = GetProcAddress(gs_hDLL, "D3DKMTSetVidPnSourceOwner");
        _O_D3DKMTSignalSynchronizationObject = GetProcAddress(gs_hDLL, "D3DKMTSignalSynchronizationObject");
        _O_D3DKMTUnlock = GetProcAddress(gs_hDLL, "D3DKMTUnlock");
        _O_D3DKMTWaitForSynchronizationObject = GetProcAddress(gs_hDLL, "D3DKMTWaitForSynchronizationObject");
        _O_D3DKMTWaitForVerticalBlankEvent = GetProcAddress(gs_hDLL, "D3DKMTWaitForVerticalBlankEvent");
        _O_D3DPerformance_BeginEvent = GetProcAddress(gs_hDLL, "D3DPerformance_BeginEvent");
        _O_D3DPerformance_EndEvent = GetProcAddress(gs_hDLL, "D3DPerformance_EndEvent");
        _O_D3DPerformance_GetStatus = GetProcAddress(gs_hDLL, "D3DPerformance_GetStatus");
        _O_D3DPerformance_SetMarker = GetProcAddress(gs_hDLL, "D3DPerformance_SetMarker");
        _O_EnableFeatureLevelUpgrade = GetProcAddress(gs_hDLL, "EnableFeatureLevelUpgrade");
        _O_OpenAdapter10 = GetProcAddress(gs_hDLL, "OpenAdapter10");
        _O_OpenAdapter10_2 = GetProcAddress(gs_hDLL, "OpenAdapter10_2");

        // Register Vectored Exception Handler for memory write tracing
        AddVectoredExceptionHandler(1, TraceVEH);

        MH_STATUS mhRet = MH_Initialize();
        if (mhRet != MH_OK)
        {
            DEBUG((debug, "Error. MH_Initialize() Failed.\n"));
        }

        // Hook GDI functions to find CG cropping
        HMODULE hGdi32 = GetModuleHandle("gdi32.dll");
        if (hGdi32)
        {
            mhRet = MH_CreateHook(GetProcAddress(hGdi32, "StretchBlt"), hookStretchBlt, reinterpret_cast<void**>(&pOrigStretchBlt));
            if (mhRet == MH_OK) MH_EnableHook(GetProcAddress(hGdi32, "StretchBlt"));
            else DEBUG((debug, "Error. Hook StretchBlt failed %d\n", mhRet));

            mhRet = MH_CreateHook(GetProcAddress(hGdi32, "BitBlt"), hookBitBlt, reinterpret_cast<void**>(&pOrigBitBlt));
            if (mhRet == MH_OK) MH_EnableHook(GetProcAddress(hGdi32, "BitBlt"));
            else DEBUG((debug, "Error. Hook BitBlt failed %d\n", mhRet));

            mhRet = MH_CreateHook(GetProcAddress(hGdi32, "StretchDIBits"), hookStretchDIBits, reinterpret_cast<void**>(&pOrigStretchDIBits));
            if (mhRet == MH_OK) MH_EnableHook(GetProcAddress(hGdi32, "StretchDIBits"));
            else DEBUG((debug, "Error. Hook StretchDIBits failed %d\n", mhRet));

            mhRet = MH_CreateHook(GetProcAddress(hGdi32, "SetDIBitsToDevice"), hookSetDIBitsToDevice, reinterpret_cast<void**>(&pOrigSetDIBitsToDevice));
            if (mhRet == MH_OK) MH_EnableHook(GetProcAddress(hGdi32, "SetDIBitsToDevice"));
            else DEBUG((debug, "Error. Hook SetDIBitsToDevice failed %d\n", mhRet));

            DEBUG((debug, "GDI gdi32 hooks installed\n"));
        }

        // Hook NtGdi* functions in win32u.dll (kernel-mode GDI)
        HMODULE hWin32u = GetModuleHandle("win32u.dll");
        if (hWin32u)
        {
            FARPROC fn;
            fn = GetProcAddress(hWin32u, "NtGdiBitBlt");
            if (fn) { mhRet = MH_CreateHook(fn, hookNtGdiBitBlt, (void**)&pOrigNtGdiBitBlt); if (mhRet == MH_OK) MH_EnableHook(fn); else DEBUG((debug, "Hook NtGdiBitBlt failed %d\n", mhRet)); }

            fn = GetProcAddress(hWin32u, "NtGdiStretchBlt");
            if (fn) { mhRet = MH_CreateHook(fn, hookNtGdiStretchBlt, (void**)&pOrigNtGdiStretchBlt); if (mhRet == MH_OK) MH_EnableHook(fn); else DEBUG((debug, "Hook NtGdiStretchBlt failed %d\n", mhRet)); }

            fn = GetProcAddress(hWin32u, "NtGdiAlphaBlend");
            if (fn) { mhRet = MH_CreateHook(fn, hookNtGdiAlphaBlend, (void**)&pOrigNtGdiAlphaBlend); if (mhRet == MH_OK) MH_EnableHook(fn); else DEBUG((debug, "Hook NtGdiAlphaBlend failed %d\n", mhRet)); }

            DEBUG((debug, "NtGdi hooks installed\n"));
        }
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        MH_STATUS mhRet = MH_Uninitialize();
        if (mhRet != MH_OK)
        {
            DEBUG((debug, "Error. MH_Uninitialize() Failed.\n"));
        }

        mhRet = MH_DisableHook((DWORD_PTR*)pContextVTable[12]);
        if (mhRet != MH_OK)
        {
            DEBUG((debug, "Error. MH_DisableHook DrawIndexed Failed.\n"));
        }

        mhRet = MH_DisableHook((DWORD_PTR*)pContextVTable[30]);
        if (mhRet != MH_OK)
        {
            DEBUG((debug, "Error. MH_DisableHook SetPredication Failed.\n"));
        }


#if DEBUG_LOG_EN
        if(debug)
            fclose(debug);
#endif // DEBUG_LOG_EN

        FreeLibrary(gs_hDLL);

    }
    return TRUE;
}
