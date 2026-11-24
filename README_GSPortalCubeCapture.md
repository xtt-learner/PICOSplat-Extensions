# PICOSplat GS Native Cubemap Capture 使用说明

五组扩展功能、安装步骤和效果展示位置见 [插件总览](README.md)。本文保留原生 Cubemap 捕获的详细操作和调试说明。

`GSPortalCubeCapture` 是 PICOSplat Runtime 中新增的 GS 原生 Cubemap Capture 功能。它不使用 UE `SceneCaptureCube` / `SceneCapture2D`，而是在 PICOSplat 内部按 6 个 cubemap face 分别渲染指定 `USplatComponent`，并写入一个 `UTextureRenderTargetCube`。

注意：`TextureRenderTargetCube` 是运行时 GPU 渲染结果，不等同于烘焙后的 `TextureCube` 资产。重新打开 UE、重新加载 Map 或重新运行 PIE 后，RT 内容不保证保留，需要重新 Capture。

## 捕获入口

完整六面捕获使用：

- `CapturePortalCube()`
- `CaptureAllFacesSequential()`

`CapturePortalCube()` 内部调用 `CaptureAllFacesSequential()`。`CaptureOnce()` 仅作为旧蓝图/旧按钮的兼容入口保留，现在也会转发到 `CapturePortalCube()`。

捕获按 face 顺序提交：每个 face 独立创建临时 `PortalTransforms`、`PortalIndices`、RDG graph 和 FaceTexture，再将结果 copy 到对应 cube slice。

`CapturePortalCube()` 返回及日志中的 `DONE` 表示捕获请求已提交。`IsCaptureValid()` 检查输入与资源状态；读取 GPU 输出时应使用对应的渲染同步机制。

## 创建资源

1. 创建一个 `Texture Render Target Cube`，例如 `RT_GSPortal_Cube`。
2. 建议初始 face 分辨率使用 `512`。
3. 如需调试单 face，再创建一个普通 `Texture Render Target 2D`，例如 `RT_GSPortal_DebugFace2D`。

## 放置 Actor

1. 将 `AGSPortalCubeCaptureActor` 放入关卡。
2. 设置 `Target Splat Actor`，或设置 `Target Splat Actor Name`，或直接设置 `Target Splat Component`。
3. 设置 `Target Render Target Cube` 为 `RT_GSPortal_Cube`。
4. `CubeFaceResolution` 建议先用 `512`。
5. 确认 PICOSplat sorting method 使用 GPU synchronous sorting。

## 捕获

推荐点击：

- `ResolveTargetSplat`
- `CapturePortalCube`

或者直接点击：

- `CaptureAllFacesSequential`

如果 `bCaptureOnBeginPlay` 开启，BeginPlay 后会延迟 `AutoCaptureDelay` 秒调用 `CapturePortalCube()`。

## 自动 Capture

`AGSPortalCubeCaptureActor` 提供以下自动刷新属性：

- `bCaptureOnBeginPlay`：运行时 BeginPlay 后自动刷新 cubemap。
- `bCaptureOnEditorMapOpened`：编辑器加载包含该 Actor 的 Map 后自动刷新 cubemap。
- `AutoCaptureDelay`：自动刷新延迟，默认 `1.0` 秒，用来等待 GS Actor、RenderTarget 和 shader 资源准备完成。
- `bRecaptureOnPropertyChanged`：属性变化后延迟刷新，默认关闭，避免编辑器里频繁触发 Capture。

正式运行推荐：

1. 在目标 GS Map 中放置并配置 `AGSPortalCubeCaptureActor`。
2. 设置 `Target Splat Actor` / `Target Splat Component`。
3. 设置 `Target Render Target Cube`。
4. 开启 `bCaptureOnBeginPlay`。
5. 保持 `AutoCaptureDelay = 1.0`，如 GS 加载较慢可适当增大。

编辑器预览推荐：

1. 打开目标 GS Map。
2. 点击 `CapturePortalCube()`。
3. 或开启 `bCaptureOnEditorMapOpened`，让编辑器加载 Map 后自动延迟 Capture。

如果入口 Map 不加载目标 GS Map，则目标 GS Map 中 Actor 的 BeginPlay 不会执行。若想入口 Map 运行时刷新目标 GS 画面，需要把目标 GS Map 作为子关卡/流式关卡加载，或改用烘焙静态 Cubemap。

## 默认 CVar

捕获使用以下默认值：

```text
pico.GSPortalCube.DebugSingleFaceIndex -1
pico.GSPortalCube.DebugSolidColor 0
pico.GSPortalCube.DebugDisablePortalDraw 0
pico.GSPortalCube.DebugCentersOnly 0
pico.GSPortalCube.DebugFixedSplatRadius 0
pico.GSPortalCube.DebugBranchColor 0
pico.GSPortalCube.DebugOpaqueBlend 0
pico.GSPortalCube.DebugDisableSorting 0
pico.GSPortalCube.DebugSplatOffsetScale 1
pico.GSPortalCube.DebugFocalScale 1
```

调试输出默认关闭：

```text
pico.GSPortalCube.DebugFace2DIndex -1
pico.GSPortalCube.DebugVisibilityStats 0
pico.GSPortalCube.DebugTransformStats 0
pico.GSPortalCube.DebugProjectionPoints 0
```

## 调试工具

以下工具可按需启用，完成排查后恢复默认设置：

- `DebugFaceRenderTarget2D`
- `pico.GSPortalCube.DebugFace2DIndex`
- `pico.GSPortalCube.DebugSingleFaceIndex`
- `pico.GSPortalCube.DebugSolidColor`
- `pico.GSPortalCube.DebugDisablePortalDraw`
- `pico.GSPortalCube.DebugCentersOnly`
- `pico.GSPortalCube.DebugFixedSplatRadius`
- `pico.GSPortalCube.DebugBranchColor`
- `pico.GSPortalCube.DebugOpaqueBlend`
- `pico.GSPortalCube.DebugDisableSorting`
- `pico.GSPortalCube.DebugVisibilityStats`
- `pico.GSPortalCube.DebugTransformStats`

如果要查看某个 face 的 2D 结果：

1. 给 Actor 设置 `Debug Face Render Target 2D`。
2. 设置 `pico.GSPortalCube.DebugFace2DIndex 0..5`。
3. 调用 `CaptureAllFacesSequential()`。

## Cube Face 映射

当前 face 到 cube slice 的默认映射集中在：

```text
Plugins/PICOSplat/Source/PICOSplatRuntime/Private/Rendering/GSPortalCubeCaptureRenderer.cpp
GetCubeSliceIndexForFace()
```

当前默认：

| FaceIndex | FaceName | Slice |
| --- | --- | --- |
| 0 | +X | 0 |
| 1 | -X | 1 |
| 2 | +Y | 2 |
| 3 | -Y | 3 |
| 4 | +Z | 4 |
| 5 | -Z | 5 |

face 方向在同一文件的 `GetCubeFaceBasis()` 中集中定义，后续如果出现上下颠倒、左右反向或 cubemap seam 不一致，优先调整 `GetCubeFaceBasis()` 和 `GetCubeSliceIndexForFace()`。

## 捕获范围与运行环境

- 已记录验证环境：Win64 / PC Editor。
- 捕获目标：单个 `USplatComponent`，使用 GPU synchronous sorting。
- 输出内容：目标 GS 的六面视图；普通 Mesh、天空、灯光、后处理及普通场景深度遮挡不参与捕获。
- 自动刷新：`bCaptureEveryFrame` 开启后按 `CaptureInterval` 调度，可根据场景负载选择更新频率。
