# LBEE-Decropper

Resize CGs in VisualArt's/Key visual novels so that they are not cropped from 4:3 to fill a 16:9 window. Instead they will be fit inside the window with black borders on the left and right.

## Supported Games

| Game | Branch | Architecture |
|------|--------|-------------|
| Little Busters! English Edition | `master` | x86 |
| **Kanon** (Steam) | `kanon` | x64 |
| **AIR** (Steam) | `kanon` | x64 |

Theoretically applicable to other VisualArt's/Key remasters using the same engine.

## Usage

### Kanon / AIR (x64)

Build with Visual Studio 2022 + Windows SDK:

```bash
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

Copy `build/Release/D3D11.dll` to the game directory (same folder as the `.exe`).

## How It Works

### 1. DLL Proxy Hook

The patch DLL acts as a proxy for the real `D3D11.dll`. On load, it finds the real DLL in System32, gets the addresses of all exported functions, and forwards them via trampolines (MASM64 assembly for x64). The `D3D11CreateDevice` export is intercepted to set up rendering hooks.

### 2. Deferred Context Interception (Kanon/AIR)

The original LBEE version hooks `DrawIndexed()` on the **Immediate Context**. Kanon/AIR use a different rendering architecture — all draw commands are recorded on **Deferred Contexts** and replayed via `ExecuteCommandList()`, making immediate context hooks invisible to the actual rendering.

The solution:
- **`CreateDeferredContext` hook** (on `ID3D11Device`): intercepts deferred context creation and installs a `DrawIndexed` hook on its VTable.
- **`ExecuteCommandList` hook** (on Immediate Context): resets CG detection state between command lists.
- **`DrawIndexed` hook** (on Deferred Context): the core decrop logic — detects 1280x960 CG textures and injects a Geometry Shader.

### 3. CG Detection

In the hooked `DrawIndexed()`, the patch checks if PS Shader Resource View slot 0 is a 1280x960 texture (the 4:3 CG). When detected:

1. A custom **Geometry Shader** (`CgGs`) is injected to resize the CG to fit the 16:9 window with letterboxing.
2. PS samplers are swapped to use **bilinear filtering with black border color**.
3. **Stream Output** is configured to pass coordinate data to the Decal GS.

CG **Decals** (overlay variations) are detected via `BlendState` examination and transformed with a separate Geometry Shader (`DecalGs`).

### 4. Geometry Shader Math

The GS sits between the Vertex Shader and Pixel Shader, allowing modification of vertex positions and texture coordinates without changing the game's shaders.

The game renders 1280x960 CGs into 1280x720 space, displaying only a 720-pixel portion of the 960 vertical range. The GS remaps the coordinates so the full 960 pixels are visible, scaled down with black borders on the sides.

See `CgGs.hlsl` and `DecalGs.hlsl` for the full transformation math.

## Kanon/AIR Porting Technical Notes

### Key Discovery: Deferred Context Rendering

The most significant finding during the port was that Kanon/AIR render entirely through D3D11 Deferred Contexts. Investigation involved:

1. Hooking Immediate Context `Draw`/`DrawIndexed` — **no CG draw calls detected**
2. Hooking `PSSetShaderResources`, `Map`, `UpdateSubresource`, `CopyResource` — **no CG textures found**
3. Hooking GDI functions (`BitBlt`, `StretchBlt`, `NtGdi*`) — **not used for CG rendering**
4. Hooking `IDXGISurface1::GetDC`/`ReleaseDC` — **not called**
5. Memory write tracing via VEH + `PAGE_GUARD` — **GPU writes, not CPU** (confirming D3D11 rendering)
6. Finally: hooking `ExecuteCommandList` — **5000+ calls per session**, confirming deferred context usage
7. Hooking `CreateDeferredContext` + deferred `DrawIndexed` — **1280x960 CG textures found**

### x64 Architecture Changes

- `__declspec(naked)` + inline `__asm { jmp }` trampolines replaced with MASM64 assembly (`proxy_x64.asm`)
- Module definition file (`D3D11.def`) for DLL export mapping
- CMakeLists.txt build system with automated HLSL shader compilation via `fxc.exe` and MinHook via FetchContent

## Acknowledgements

- [MinHook](https://github.com/TsudaKageyu/minhook) — API hooking library
- [D3D11-Wallhack](https://github.com/DrNseven/D3D11-Wallhack) — VTable offset reference
- [hulver.com](http://www.hulver.com/scoop/story/2006/2/18/125521/185) — D3D11 function definition generation
