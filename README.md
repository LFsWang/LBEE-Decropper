# LBEE-Decropper

Resize CGs in VisualArt's/Key visual novels so that they are not cropped from 4:3 to fill a 16:9 window. Instead they will be fit inside the window with black borders on the left and right.

將 VisualArt's/Key 視覺小說中的 CG 從裁切的 16:9 還原為完整的 4:3 顯示，左右加上黑邊。

## Supported Games / 適用遊戲

| Game / 遊戲 | Branch | Architecture |
|-------------|--------|-------------|
| Little Busters! English Edition | [`master`](https://github.com/Freakhollik/LBEE-Decropper) (original) | x86 |
| **Kanon** (Steam) | `kanon` | x64 |
| **AIR** (Steam) | `kanon` | x64 |

The x86 version for LBEE is maintained in the [original repository](https://github.com/Freakhollik/LBEE-Decropper). This branch (`kanon`) adds x64 support for Kanon and AIR.

LBEE 的 x86 版本請參見[原始 repo](https://github.com/Freakhollik/LBEE-Decropper)。本分支（`kanon`）新增 Kanon 與 AIR 的 x64 支援。

理論上適用於同引擎、同架構的其他 VisualArt's/Key 重製版遊戲。

## Build / 建置

Requirements / 需求：
- Visual Studio 2022 (with C++ and MASM support)
- Windows SDK (for `fxc.exe` shader compiler)
- CMake 3.20+

```bash
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

Output: `build/Release/D3D11.dll`

## Usage / 使用方法

Copy `build/Release/D3D11.dll` to the game directory (same folder as the `.exe`).

將 `build/Release/D3D11.dll` 複製到遊戲目錄（與 `.exe` 同層）即可。

## How It Works / 運作原理

### 1. DLL Proxy Hook / DLL 代理注入

The patch DLL acts as a proxy for the real `D3D11.dll`. On load, it finds the real DLL in System32, gets the addresses of all exported functions, and forwards them via MASM64 assembly trampolines (`proxy_x64.asm`). The `D3D11CreateDevice` export is intercepted to set up rendering hooks.

補丁 DLL 偽裝成 `D3D11.dll`，載入時找到 System32 中的真實 DLL 並透過 MASM64 assembly trampoline 轉發所有匯出函數。攔截 `D3D11CreateDevice` 來設置渲染 hook。

### 2. Deferred Context Interception / 延遲上下文攔截

The original LBEE version hooks `DrawIndexed()` on the **Immediate Context**. Kanon/AIR use a different rendering architecture — all draw commands are recorded on **Deferred Contexts** and replayed via `ExecuteCommandList()`, making immediate context hooks invisible to the actual rendering.

原版 LBEE 在即時上下文上 hook `DrawIndexed()`。但 Kanon/AIR 使用不同的渲染架構 — 所有繪製命令錄製在**延遲上下文（Deferred Context）**上，透過 `ExecuteCommandList()` 重播，導致即時上下文的 hook 完全看不到實際渲染。

The solution / 解決方案：
- **`CreateDeferredContext` hook** (on `ID3D11Device`): intercepts deferred context creation and installs a `DrawIndexed` hook on its VTable. / 攔截延遲上下文建立，在其 VTable 上安裝 DrawIndexed hook。
- **`ExecuteCommandList` hook** (on Immediate Context): resets CG detection state between command lists. / 在每個 command list 之間重置 CG 偵測狀態。
- **`DrawIndexed` hook** (on Deferred Context): the core decrop logic. / 核心 decrop 邏輯。

### 3. CG Detection & GS Injection / CG 偵測與 GS 注入

In the hooked `DrawIndexed()`, the patch checks if PS Shader Resource View slot 0 is a 1280x960 texture (the 4:3 CG). When detected:

在 hook 的 `DrawIndexed()` 中，檢查 PS SRV slot 0 是否為 1280x960 紋理（4:3 CG）。偵測到時：

1. A custom **Geometry Shader** (`CgGs`) is injected to resize the CG with letterboxing. / 注入自訂 Geometry Shader 進行 decrop。
2. PS samplers are swapped to use **bilinear filtering with black border color**. / 替換 sampler 為黑色邊框雙線性過濾。
3. **Stream Output** passes coordinate data to the Decal GS. / Stream Output 傳遞座標資訊給 Decal GS。

CG **Decals** (overlay variations) are detected via `BlendState` and transformed with `DecalGs`. / 透過 BlendState 偵測 CG Decal 並用 DecalGs 進行變換。

### 4. Geometry Shader Math / Geometry Shader 數學

The GS sits between the Vertex Shader and Pixel Shader, remapping vertex positions and texture coordinates so the full 1280x960 CG is visible within the 1280x720 window, scaled down with black borders on the sides. See `CgGs.hlsl` and `DecalGs.hlsl` for details.

GS 位於 VS 和 PS 之間，重新映射頂點位置和紋理座標，使完整的 1280x960 CG 在 1280x720 視窗中顯示，左右加上黑邊。詳見 `CgGs.hlsl` 和 `DecalGs.hlsl`。

## Porting Technical Notes / 移植技術筆記

### Key Discovery: Deferred Context Rendering / 核心發現：延遲上下文渲染

The most significant finding during the Kanon/AIR port. Investigation path:

移植過程中最重要的發現。調查路徑：

1. Immediate Context `Draw`/`DrawIndexed` hook — no CG draw calls / 無 CG 繪製呼叫
2. `PSSetShaderResources`, `Map`, `UpdateSubresource`, `CopyResource` hooks — no CG textures / 無 CG 紋理
3. GDI hooks (`BitBlt`, `StretchBlt`, `NtGdi*`) — not used / 未使用
4. `IDXGISurface1::GetDC`/`ReleaseDC` — not called / 未呼叫
5. Memory write tracing (VEH + `PAGE_GUARD`) — GPU writes, not CPU / GPU 寫入，非 CPU
6. **`ExecuteCommandList` hook — 5000+ calls/session** / 每次遊戲 5000+ 次呼叫
7. **Deferred Context `DrawIndexed` — 1280x960 CG textures found** / 找到 CG 紋理

### x64 Architecture Changes / x64 架構變更

- `__declspec(naked)` + inline asm trampolines → MASM64 assembly (`proxy_x64.asm`)
- Module definition file (`D3D11.def`) for DLL exports
- CMakeLists.txt with `fxc.exe` shader compilation + MinHook via FetchContent

## Acknowledgements / 致謝

- [MinHook](https://github.com/TsudaKageyu/minhook) — API hooking library
- [D3D11-Wallhack](https://github.com/DrNseven/D3D11-Wallhack) — VTable offset reference
- [LBEE-Decropper](https://github.com/Freakhollik/LBEE-Decropper) — Original project by Freakhollik
