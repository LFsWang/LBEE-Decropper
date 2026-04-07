; proxy_x64.asm
; x64 trampoline functions for D3D11.dll proxy
; These replace the __declspec(naked) __asm { jmp } trampolines from the x86 version
; which are not supported in x64 MSVC.

.DATA

; Import the original function pointers (set in DllMain via GetProcAddress)
EXTERN _O_D3D11CoreCreateDevice:QWORD
EXTERN _O_D3D11CoreCreateLayeredDevice:QWORD
EXTERN _O_D3D11CoreGetLayeredDeviceSize:QWORD
EXTERN _O_D3D11CoreRegisterLayers:QWORD
EXTERN _O_D3D11CreateDeviceAndSwapChain:QWORD
EXTERN _O_D3DKMTCloseAdapter:QWORD
EXTERN _O_D3DKMTCreateAllocation:QWORD
EXTERN _O_D3DKMTCreateContext:QWORD
EXTERN _O_D3DKMTCreateDevice:QWORD
EXTERN _O_D3DKMTCreateSynchronizationObject:QWORD
EXTERN _O_D3DKMTDestroyAllocation:QWORD
EXTERN _O_D3DKMTDestroyContext:QWORD
EXTERN _O_D3DKMTDestroyDevice:QWORD
EXTERN _O_D3DKMTDestroySynchronizationObject:QWORD
EXTERN _O_D3DKMTEscape:QWORD
EXTERN _O_D3DKMTGetContextSchedulingPriority:QWORD
EXTERN _O_D3DKMTGetDeviceState:QWORD
EXTERN _O_D3DKMTGetDisplayModeList:QWORD
EXTERN _O_D3DKMTGetMultisampleMethodList:QWORD
EXTERN _O_D3DKMTGetRuntimeData:QWORD
EXTERN _O_D3DKMTGetSharedPrimaryHandle:QWORD
EXTERN _O_D3DKMTLock:QWORD
EXTERN _O_D3DKMTOpenAdapterFromHdc:QWORD
EXTERN _O_D3DKMTOpenResource:QWORD
EXTERN _O_D3DKMTPresent:QWORD
EXTERN _O_D3DKMTQueryAdapterInfo:QWORD
EXTERN _O_D3DKMTQueryAllocationResidency:QWORD
EXTERN _O_D3DKMTQueryResourceInfo:QWORD
EXTERN _O_D3DKMTRender:QWORD
EXTERN _O_D3DKMTSetAllocationPriority:QWORD
EXTERN _O_D3DKMTSetContextSchedulingPriority:QWORD
EXTERN _O_D3DKMTSetDisplayMode:QWORD
EXTERN _O_D3DKMTSetDisplayPrivateDriverFormat:QWORD
EXTERN _O_D3DKMTSetGammaRamp:QWORD
EXTERN _O_D3DKMTSetVidPnSourceOwner:QWORD
EXTERN _O_D3DKMTSignalSynchronizationObject:QWORD
EXTERN _O_D3DKMTUnlock:QWORD
EXTERN _O_D3DKMTWaitForSynchronizationObject:QWORD
EXTERN _O_D3DKMTWaitForVerticalBlankEvent:QWORD
EXTERN _O_D3DPerformance_BeginEvent:QWORD
EXTERN _O_D3DPerformance_EndEvent:QWORD
EXTERN _O_D3DPerformance_GetStatus:QWORD
EXTERN _O_D3DPerformance_SetMarker:QWORD
EXTERN _O_EnableFeatureLevelUpgrade:QWORD
EXTERN _O_OpenAdapter10:QWORD
EXTERN _O_OpenAdapter10_2:QWORD

.CODE

_I_D3D11CoreCreateDevice PROC
    jmp QWORD PTR [_O_D3D11CoreCreateDevice]
_I_D3D11CoreCreateDevice ENDP

_I_D3D11CoreCreateLayeredDevice PROC
    jmp QWORD PTR [_O_D3D11CoreCreateLayeredDevice]
_I_D3D11CoreCreateLayeredDevice ENDP

_I_D3D11CoreGetLayeredDeviceSize PROC
    jmp QWORD PTR [_O_D3D11CoreGetLayeredDeviceSize]
_I_D3D11CoreGetLayeredDeviceSize ENDP

_I_D3D11CoreRegisterLayers PROC
    jmp QWORD PTR [_O_D3D11CoreRegisterLayers]
_I_D3D11CoreRegisterLayers ENDP

_I_D3D11CreateDeviceAndSwapChain PROC
    jmp QWORD PTR [_O_D3D11CreateDeviceAndSwapChain]
_I_D3D11CreateDeviceAndSwapChain ENDP

_I_D3DKMTCloseAdapter PROC
    jmp QWORD PTR [_O_D3DKMTCloseAdapter]
_I_D3DKMTCloseAdapter ENDP

_I_D3DKMTCreateAllocation PROC
    jmp QWORD PTR [_O_D3DKMTCreateAllocation]
_I_D3DKMTCreateAllocation ENDP

_I_D3DKMTCreateContext PROC
    jmp QWORD PTR [_O_D3DKMTCreateContext]
_I_D3DKMTCreateContext ENDP

_I_D3DKMTCreateDevice PROC
    jmp QWORD PTR [_O_D3DKMTCreateDevice]
_I_D3DKMTCreateDevice ENDP

_I_D3DKMTCreateSynchronizationObject PROC
    jmp QWORD PTR [_O_D3DKMTCreateSynchronizationObject]
_I_D3DKMTCreateSynchronizationObject ENDP

_I_D3DKMTDestroyAllocation PROC
    jmp QWORD PTR [_O_D3DKMTDestroyAllocation]
_I_D3DKMTDestroyAllocation ENDP

_I_D3DKMTDestroyContext PROC
    jmp QWORD PTR [_O_D3DKMTDestroyContext]
_I_D3DKMTDestroyContext ENDP

_I_D3DKMTDestroyDevice PROC
    jmp QWORD PTR [_O_D3DKMTDestroyDevice]
_I_D3DKMTDestroyDevice ENDP

_I_D3DKMTDestroySynchronizationObject PROC
    jmp QWORD PTR [_O_D3DKMTDestroySynchronizationObject]
_I_D3DKMTDestroySynchronizationObject ENDP

_I_D3DKMTEscape PROC
    jmp QWORD PTR [_O_D3DKMTEscape]
_I_D3DKMTEscape ENDP

_I_D3DKMTGetContextSchedulingPriority PROC
    jmp QWORD PTR [_O_D3DKMTGetContextSchedulingPriority]
_I_D3DKMTGetContextSchedulingPriority ENDP

_I_D3DKMTGetDeviceState PROC
    jmp QWORD PTR [_O_D3DKMTGetDeviceState]
_I_D3DKMTGetDeviceState ENDP

_I_D3DKMTGetDisplayModeList PROC
    jmp QWORD PTR [_O_D3DKMTGetDisplayModeList]
_I_D3DKMTGetDisplayModeList ENDP

_I_D3DKMTGetMultisampleMethodList PROC
    jmp QWORD PTR [_O_D3DKMTGetMultisampleMethodList]
_I_D3DKMTGetMultisampleMethodList ENDP

_I_D3DKMTGetRuntimeData PROC
    jmp QWORD PTR [_O_D3DKMTGetRuntimeData]
_I_D3DKMTGetRuntimeData ENDP

_I_D3DKMTGetSharedPrimaryHandle PROC
    jmp QWORD PTR [_O_D3DKMTGetSharedPrimaryHandle]
_I_D3DKMTGetSharedPrimaryHandle ENDP

_I_D3DKMTLock PROC
    jmp QWORD PTR [_O_D3DKMTLock]
_I_D3DKMTLock ENDP

_I_D3DKMTOpenAdapterFromHdc PROC
    jmp QWORD PTR [_O_D3DKMTOpenAdapterFromHdc]
_I_D3DKMTOpenAdapterFromHdc ENDP

_I_D3DKMTOpenResource PROC
    jmp QWORD PTR [_O_D3DKMTOpenResource]
_I_D3DKMTOpenResource ENDP

_I_D3DKMTPresent PROC
    jmp QWORD PTR [_O_D3DKMTPresent]
_I_D3DKMTPresent ENDP

_I_D3DKMTQueryAdapterInfo PROC
    jmp QWORD PTR [_O_D3DKMTQueryAdapterInfo]
_I_D3DKMTQueryAdapterInfo ENDP

_I_D3DKMTQueryAllocationResidency PROC
    jmp QWORD PTR [_O_D3DKMTQueryAllocationResidency]
_I_D3DKMTQueryAllocationResidency ENDP

_I_D3DKMTQueryResourceInfo PROC
    jmp QWORD PTR [_O_D3DKMTQueryResourceInfo]
_I_D3DKMTQueryResourceInfo ENDP

_I_D3DKMTRender PROC
    jmp QWORD PTR [_O_D3DKMTRender]
_I_D3DKMTRender ENDP

_I_D3DKMTSetAllocationPriority PROC
    jmp QWORD PTR [_O_D3DKMTSetAllocationPriority]
_I_D3DKMTSetAllocationPriority ENDP

_I_D3DKMTSetContextSchedulingPriority PROC
    jmp QWORD PTR [_O_D3DKMTSetContextSchedulingPriority]
_I_D3DKMTSetContextSchedulingPriority ENDP

_I_D3DKMTSetDisplayMode PROC
    jmp QWORD PTR [_O_D3DKMTSetDisplayMode]
_I_D3DKMTSetDisplayMode ENDP

_I_D3DKMTSetDisplayPrivateDriverFormat PROC
    jmp QWORD PTR [_O_D3DKMTSetDisplayPrivateDriverFormat]
_I_D3DKMTSetDisplayPrivateDriverFormat ENDP

_I_D3DKMTSetGammaRamp PROC
    jmp QWORD PTR [_O_D3DKMTSetGammaRamp]
_I_D3DKMTSetGammaRamp ENDP

_I_D3DKMTSetVidPnSourceOwner PROC
    jmp QWORD PTR [_O_D3DKMTSetVidPnSourceOwner]
_I_D3DKMTSetVidPnSourceOwner ENDP

_I_D3DKMTSignalSynchronizationObject PROC
    jmp QWORD PTR [_O_D3DKMTSignalSynchronizationObject]
_I_D3DKMTSignalSynchronizationObject ENDP

_I_D3DKMTUnlock PROC
    jmp QWORD PTR [_O_D3DKMTUnlock]
_I_D3DKMTUnlock ENDP

_I_D3DKMTWaitForSynchronizationObject PROC
    jmp QWORD PTR [_O_D3DKMTWaitForSynchronizationObject]
_I_D3DKMTWaitForSynchronizationObject ENDP

_I_D3DKMTWaitForVerticalBlankEvent PROC
    jmp QWORD PTR [_O_D3DKMTWaitForVerticalBlankEvent]
_I_D3DKMTWaitForVerticalBlankEvent ENDP

_I_D3DPerformance_BeginEvent PROC
    jmp QWORD PTR [_O_D3DPerformance_BeginEvent]
_I_D3DPerformance_BeginEvent ENDP

_I_D3DPerformance_EndEvent PROC
    jmp QWORD PTR [_O_D3DPerformance_EndEvent]
_I_D3DPerformance_EndEvent ENDP

_I_D3DPerformance_GetStatus PROC
    jmp QWORD PTR [_O_D3DPerformance_GetStatus]
_I_D3DPerformance_GetStatus ENDP

_I_D3DPerformance_SetMarker PROC
    jmp QWORD PTR [_O_D3DPerformance_SetMarker]
_I_D3DPerformance_SetMarker ENDP

_I_EnableFeatureLevelUpgrade PROC
    jmp QWORD PTR [_O_EnableFeatureLevelUpgrade]
_I_EnableFeatureLevelUpgrade ENDP

_I_OpenAdapter10 PROC
    jmp QWORD PTR [_O_OpenAdapter10]
_I_OpenAdapter10 ENDP

_I_OpenAdapter10_2 PROC
    jmp QWORD PTR [_O_OpenAdapter10_2]
_I_OpenAdapter10_2 ENDP

END
