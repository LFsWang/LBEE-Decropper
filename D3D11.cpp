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

#if defined (_DEBUG)
#define DEBUG_LOG_EN 1
#endif

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
// Referenced by proxy_x64.asm. Must be extern "C" to avoid name mangling.
extern "C"
{
    FARPROC _O_D3D11CoreCreateDevice = NULL;
    FARPROC _O_D3D11CoreCreateLayeredDevice = NULL;
    FARPROC _O_D3D11CoreGetLayeredDeviceSize = NULL;
    FARPROC _O_D3D11CoreRegisterLayers = NULL;
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


DWORD_PTR* pContextVTable = NULL;

// Shader/sampler globals
ID3D11GeometryShader* pCgGs = NULL;
ID3D11GeometryShader* pDecalGs = NULL;
ID3D11SamplerState* pBlackBorderSampler = NULL;

ID3D11Buffer* pTexCoordsBuff = NULL;
#if USE_D3D11_1
ID3D11UnorderedAccessView* pTexCoordsUav = NULL;
#else
ID3D11ShaderResourceView* pTexCoordsSrv = NULL;
#endif

// Per-command-list CG detection state
static BOOL defFrameHasCg = FALSE;


//
// ExecuteCommandList hook: reset CG detection state between command lists
//
typedef void(WINAPI *D3D11ExecuteCommandListHook)(ID3D11DeviceContext* pContext, ID3D11CommandList *pCommandList, BOOL RestoreContextState);
D3D11ExecuteCommandListHook phookD3D11ExecuteCommandList = NULL;

void WINAPI hookD3D11ExecuteCommandList(ID3D11DeviceContext* pContext, ID3D11CommandList *pCommandList, BOOL RestoreContextState)
{
    defFrameHasCg = FALSE;
    phookD3D11ExecuteCommandList(pContext, pCommandList, RestoreContextState);
}


//
// Deferred context DrawIndexed hook: the core decrop logic
// Detects 1280x960 CG textures and injects a Geometry Shader to resize them
// to fit inside the 16:9 window with black letterbox borders.
//
typedef void(WINAPI *D3D11DeferredDrawIndexedHook)(ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation);
D3D11DeferredDrawIndexedHook phookDeferredDrawIndexed = NULL;

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


//
// CreateDeferredContext hook: install DrawIndexed hook on newly created deferred contexts
//
typedef HRESULT(WINAPI *D3D11CreateDeferredContextHook)(ID3D11Device* pDevice, UINT ContextFlags, ID3D11DeviceContext **ppDeferredContext);
D3D11CreateDeferredContextHook phookD3D11CreateDeferredContext = NULL;

HRESULT WINAPI hookD3D11CreateDeferredContext(ID3D11Device* pDevice, UINT ContextFlags, ID3D11DeviceContext **ppDeferredContext)
{
    HRESULT hr = phookD3D11CreateDeferredContext(pDevice, ContextFlags, ppDeferredContext);

    if (hr == S_OK && ppDeferredContext && *ppDeferredContext)
    {
        DWORD_PTR* pDefVT = (DWORD_PTR*)(*ppDeferredContext);
        pDefVT = (DWORD_PTR*)pDefVT[0];

        if (phookDeferredDrawIndexed == NULL)
        {
            MH_STATUS mh = MH_CreateHook((DWORD_PTR*)pDefVT[12], hookDeferredDrawIndexed, reinterpret_cast<void**>(&phookDeferredDrawIndexed));
            if (mh == MH_OK) MH_EnableHook((DWORD_PTR*)pDefVT[12]);
            else DEBUG((debug, "Error. Hook deferred DrawIndexed failed %d\n", mh));
        }
    }
    return hr;
}


//
// D3D11CreateDevice: intercept device creation, set up hooks and create shader resources
//
extern "C"
{
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

        DEBUG((debug, "D3D11CreateDevice\n"));
#if USE_D3D11_1
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;

        D3D_FEATURE_LEVEL retFl;
        D3D_FEATURE_LEVEL fl[2] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

        createDeviceHr = _O_D3D11CreateDevice(pAdapter, DriverType, Software, Flags, &fl[0],
            2, SDKVERSION, ppDevice, &retFl, ppImmediateContext);

        if (pFeatureLevel != NULL)
            *pFeatureLevel = retFl;

        if (retFl != D3D_FEATURE_LEVEL_11_1)
            DEBUG((debug, "Could not Create D3D11.1 Device\n"));
#else
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;

        createDeviceHr = _O_D3D11CreateDevice(pAdapter, DriverType, Software, Flags, pFeatureLevels,
            FeatureLevels, SDKVERSION, ppDevice, pFeatureLevel, ppImmediateContext);
#endif

        if (*ppImmediateContext != NULL
#if USE_D3D11_1
            && retFl == D3D_FEATURE_LEVEL_11_1
#endif
            )
        {
            pContextVTable = (DWORD_PTR*)(*ppImmediateContext);
            pContextVTable = (DWORD_PTR*)pContextVTable[0];

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

            // Create the Decrop GS
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
                DEBUG((debug, "Error. CgGs compile failed.\n"));

            hr = (*ppDevice)->CreateGeometryShader(&g_DecalGs[0], sizeof(g_DecalGs), NULL, &pDecalGs);
            if (hr != S_OK || pDecalGs == NULL)
                DEBUG((debug, "Error. DecalGs compile failed.\n"));

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
                DEBUG((debug, "Error. Black border sampler create failed.\n"));

            bufferDesc.ByteWidth = sizeof(float) * 256;
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
                DEBUG((debug, "Error. Tex Coords Buffer Create Failed.\n"));

#if USE_D3D11_1
            uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.FirstElement = 0;
            uavDesc.Buffer.NumElements = bufferDesc.ByteWidth / sizeof(float);
            uavDesc.Buffer.Flags = 0;
            hr = (*ppDevice)->CreateUnorderedAccessView(pTexCoordsBuff, &uavDesc, &pTexCoordsUav);
            if (hr != S_OK || pTexCoordsUav == NULL)
                DEBUG((debug, "Error. Tex Coords UAV Create Failed.\n"));
#else
            srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = bufferDesc.ByteWidth / sizeof(float);
            hr = (*ppDevice)->CreateShaderResourceView(pTexCoordsBuff, &srvDesc, &pTexCoordsSrv);
            if (hr != S_OK || pTexCoordsSrv == NULL)
                DEBUG((debug, "Error. Tex Coords SRV Create Failed.\n"));
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

        MH_STATUS mhRet = MH_Initialize();
        if (mhRet != MH_OK)
            DEBUG((debug, "Error. MH_Initialize() Failed.\n"));
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();

#if DEBUG_LOG_EN
        if(debug)
            fclose(debug);
#endif // DEBUG_LOG_EN

        FreeLibrary(gs_hDLL);
    }
    return TRUE;
}
