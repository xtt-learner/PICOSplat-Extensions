# PICOSplat Extensions

**面向 Unreal Engine 5.5 的 Gaussian Splatting 场景编辑、动态展示与原生 Cubemap 捕获扩展。**

基于 [PICO PICOSplat](https://github.com/Pico-Developer/splat_unreal)，本项目将 GS 内容的外观调整、局部编辑、动态播放和离屏捕获接入 Unreal 编辑器与蓝图工作流。五组扩展支持从场景内容整理到交互展示的使用过程。

| 功能 | 提供的能力 | 主要入口 |
| --- | --- | --- |
| **GS 原生 Cubemap 捕获** | 从指定位置捕获单个 GS 组件，输出可供材质采样的 Cube Render Target | `AGSPortalCubeCaptureActor` |
| **色调与曝光控制** | 在编辑器和蓝图中调整 GS 外观，切换日间、暖色、夜间等预设 | `USplatComponent` |
| **透明度过滤** | 按高斯点自身的 alpha 阈值过滤，并查看过滤数量 | `RebuildFilteredSplatData()` |
| **空间裁剪框** | 保留或移除指定盒体区域中的高斯点，支持多个盒体和 Tag 选择 | `APICOSplatCullBoxActor` |
| **高斯帧序列播放** | 按指定帧率播放多帧 GS 资产，支持暂停、跳帧和循环 | `APICO4DGSActor` / `UPICO4DGSComponent` |

<!-- MEDIA: overview-video | 建议文件 docs/media/overview.mp4；补齐后将下方占位替换为可访问的视频链接。 -->
<table>
<tr><td align="center">
<br><br>
<strong>项目总览视频 · 待补</strong><br>
同一场景中展示外观调整、过滤裁剪、动态播放与 Cubemap 输出。
<br><br><br>
</td></tr>
</table>

## GS 原生 Cubemap 捕获

在交互场景中，将 GS 内容显示到另一处屏幕或全景材质，需要从指定位置生成离屏视图。本扩展在 PICOSplat 渲染路径中为六个方向分别计算视图、进行 GPU 排序并绘制，将结果写入 `UTextureRenderTargetCube`。

捕获直接使用 PICOSplat 的 GS 渲染数据。每个面使用独立的临时变换/索引缓冲和 RDG Graph，再将面纹理复制到对应的 Cube Slice。主视图与捕获视图共享目标组件的色调和曝光设置，方便在场景展示中保持一致的外观。

```text
指定 USplatComponent + 捕获位置
    → 六个面分别构建视图
    → 计算 / 排序 / 绘制高斯
    → 将各面写入 TextureRenderTargetCube
    → 供材质采样
```

<!-- MEDIA: cubemap-image | 建议文件 docs/media/cubemap-faces.png -->
<table>
<tr><td align="center">
<br><br>
<strong>截图位置 · 六面捕获结果</strong><br>
展示 +X、−X、+Y、−Y、+Z、−Z 六面，以及场景中采样 Cube RT 的效果。
<br><br><br>
</td></tr>
</table>

<!-- MEDIA: cubemap-video | 建议文件 docs/media/cubemap-capture.mp4 -->
<table>
<tr><td align="center">
<br><br>
<strong>视频位置 · 捕获与更新</strong><br>
改变捕获位置，手动重新捕获，展示输出随视点更新的过程。
<br><br><br>
</td></tr>
</table>

### 快速使用

1. 在关卡中放置已绑定 GS 资产的 Splat Actor。
2. 创建一个 `Texture Render Target Cube` 资产。
3. 放置 `AGSPortalCubeCaptureActor`，将其移动到需要捕获的位置。
4. 设置 `Target Splat Actor` 或直接指定 `Target Splat Component`，并设置 `Target Render Target Cube`。
5. 在工程的 `Config/DefaultEngine.ini` 中选择 GPU 同步排序，保存并重启编辑器：

   ```ini
   [/Script/PICOSplatRuntime.SplatSettings]
   SortingMethod=GPUSynchronous
   ```

6. 初次使用保持 `CubeFaceResolution = 512`，关闭 `Force Solid Color Test`，点击 `CapturePortalCube()`。
7. 在材质中采样指定的 Cube RT，检查输出；需要拆看某一面时配置 `Debug Face Render Target 2D`。

`CapturePortalCube()` 调用 `CaptureAllFacesSequential()`；`CaptureOnce()` 是转发到同一路径的兼容入口。`CaptureFace(0..5)` 用于单面调试。

### 自动捕获

| 设置 | 默认值 | 用途 |
| --- | --- | --- |
| `bCaptureOnBeginPlay` | `false` | 关卡开始运行后延迟捕获 |
| `bCaptureOnEditorMapOpened` | `false` | 编辑器加载包含该 Actor 的关卡后延迟捕获 |
| `AutoCaptureDelay` | `1.0` 秒 | 等待目标组件和渲染资源准备 |
| `bRecaptureOnPropertyChanged` | `false` | 相关属性变化或结束移动后延迟重新捕获 |
| `bCaptureEveryFrame` | `false` | 开启定时捕获；实际按 `CaptureInterval` 触发 |
| `CaptureInterval` | `0.25` 秒 | 定时捕获间隔 |

先通过手动捕获确认视点与输出，再按场景需要启用自动刷新。输出为运行时 Render Target，可在关卡加载或 BeginPlay 时自动重新生成。

捕获面向单个 GS 组件的离屏展示，适合为全景材质和场景预览提供视图。渲染范围、资源生命周期与调试方式见下方详细指南。

六面映射、调试 CVar 和资源准备说明见 [原生捕获详细指南](README_GSPortalCubeCapture.md)。

## 色调与曝光控制

`USplatComponent` 提供 `GlobalTint`、`Exposure` 和蓝图调用接口。参数经 SceneProxy 传入像素着色器，RGB 调节关系为：

```text
输出 RGB = 高斯原始 RGB × GlobalTint.rgb × Exposure
```

同一份 GS 资产可直接调整整体明暗和颜色氛围，方便制作日夜氛围切换、展项高亮和统一的场景视觉风格。

<!-- MEDIA: appearance-image | 建议文件 docs/media/appearance-presets.png -->
<table>
<tr><td align="center">
<br><br>
<strong>截图位置 · 同视角外观对比</strong><br>
默认 / 暖色 / 夜间三栏，标明 Tint 与 Exposure，保持相机和其他场景设置一致。
<br><br><br>
</td></tr>
</table>

| 蓝图接口 | 行为 |
| --- | --- |
| `SetGlobalTint(FLinearColor)` | 设置整体 RGB 色调并刷新渲染状态 |
| `SetSplatExposure(float)` | 设置非负的线性亮度倍率并刷新渲染状态 |
| `ApplyDayNightPreset(int32)` | 同时设置色调和曝光 |

`ApplyDayNightPreset()` 提供以下预设：

| 索引 | 效果 | Tint RGB | Exposure |
| --- | --- | --- | ---: |
| `0` / 其他值 | 默认 | `(1.0, 1.0, 1.0)` | `1.0` |
| `1` | 暖色 | `(1.0, 0.72, 0.45)` | `0.8` |
| `2` | 夜间氛围 | `(0.22, 0.28, 0.48)` | `0.45` |
| `3` | 红色调试 | `(1.0, 0.0, 0.0)` | `1.0` |
| `4` | 蓝色调试 | `(0.0, 0.0, 1.0)` | `1.0` |

运行时调用以上函数即可更新外观；`Exposure` 使用线性 RGB 倍率。

## 透明度过滤

按高斯点的 alpha 阈值清理低透明度点，并通过统计查看清理结果。组件保留源资产，以临时副本应用过滤，支持随时恢复和反复调整。

<!-- MEDIA: opacity-image | 建议文件 docs/media/opacity-filter.png -->
<table>
<tr><td align="center">
<br><br>
<strong>截图位置 · 透明度过滤前后</strong><br>
固定视角对比，标明阈值、原始点数、移除点数和剩余点数。
<br><br><br>
</td></tr>
</table>

1. 在目标 Splat 组件中启用 `bEnableSplatFiltering`。
2. 设置 `MinOpacityThreshold`，默认值为 `0.01`；alpha **小于**该值的点被移除。
3. 如果只测试透明度过滤，可关闭 `bEnableCullBoxes`。
4. 调用 `RebuildFilteredSplatData()`，或在开启 `bPreviewFilterInEditor` 时调整过滤开关、阈值等支持的属性。
5. 使用 `ClearSplatFilter()` 恢复原资产并关闭过滤。

| 统计字段 | 含义 |
| --- | --- |
| `OriginalSplatCount` | 源资产点数 |
| `RemovedByOpacityCount` | 由 alpha 阈值移除的点数 |
| `RemovedByCullBoxCount` | 通过 alpha 检查后，由空间规则移除的点数 |
| `FilteredSplatCount` | 最终保留点数 |

过滤使用高斯点存储的 alpha（8 位颜色通道归一化）。调用重建时遍历点并生成内存中的过滤资产，适合按需调整与预览。

## 空间裁剪框

通过 `APICOSplatCullBoxActor` 在场景中圈定保留或移除区域，可用于清理边缘杂点、裁出展示区域，或在同一份 GS 数据中选择局部内容。

<!-- MEDIA: cullbox-video | 建议文件 docs/media/cullbox-edit.mp4；也可用 docs/media/cullbox-comparison.png 替换。 -->
<table>
<tr><td align="center">
<br><br>
<strong>视频 / 截图位置 · 空间裁剪</strong><br>
调整盒体后点击 Rebuild，分别展示 Remove Inside、Keep Inside 和 Clear 恢复。
<br><br><br>
</td></tr>
</table>

### 使用步骤

1. 放置 `PICO Splat Cull Box` Actor，调整位置、旋转和缩放。
2. 设置 `Mode`：`RemoveInside` 移除盒内点，`KeepInside` 保留盒内点。
3. 在目标 Splat 组件上同时启用 `bEnableSplatFiltering` 和 `bEnableCullBoxes`。
4. 调用该组件的 `RebuildFilteredSplatData()`。
5. 移动盒体、改变模式或 Tag 后，再次调用 `RebuildFilteredSplatData()` 应用变化。

### 组合与选择规则

- 多个 `KeepInside` 盒体取并集：点位于任意一个匹配的保留盒内即可通过保留检查。
- 任意匹配的 `RemoveInside` 盒体都可以移除点；因此移除规则也作用于保留区域。
- 未设置 `TargetTag` 的盒体可作用于所有启用了裁剪的 Splat 组件。
- 设置 `TargetTag` 后，盒体作用于 **Component Tags** 中包含该 Tag 的组件。
- `bEnabled` 控制盒体是否参与过滤，`bDrawDebug` 控制盒体的游戏内可见设置。

区域选择以高斯**中心位置**为依据，边界处保留各高斯自身的覆盖范围。

## 高斯资产帧序列播放

`APICO4DGSActor` 将渲染组件和播放组件组合在一个可放置 Actor 中；`UPICO4DGSComponent` 按帧率播放预先导入的离散 `USplatAsset` 序列，将动态 GS 数据接入关卡与蓝图交互。

<!-- MEDIA: sequence-video | 建议文件 docs/media/gaussian-sequence.mp4 -->
<table>
<tr><td align="center">
<br><br>
<strong>视频位置 · 动态 GS 与播放控制</strong><br>
展示播放、暂停、前后单步和跳帧；保留当前帧编号，并标明帧率、帧数与设备。
<br><br><br>
</td></tr>
</table>

### 快速使用

1. 将各帧 `.ply` 导入为 `USplatAsset`。
2. 在关卡中放置 `PICO 4DGS Actor`。
3. 在其 `PICO4DGSComponent` 中，按时间顺序填入 `Frames` 数组，避免空帧。
4. 设置 `FPS`，默认 `10.0`；设置 `StartFrame` 和 `EndFrame`，`EndFrame = -1` 表示最后一帧。
5. 按需设置 `bLoop` 和 `bPlayOnBeginPlay`，运行关卡。

| 接口 | 用途 |
| --- | --- |
| `Play4DGS()` / `Pause4DGS()` / `Stop4DGS()` | 播放、暂停、停止并回到起始帧 |
| `SeekFrame(int32)` | 跳到指定帧；索引限制在配置的帧范围内 |
| `StepForward()` / `StepBackward()` | 手动前后单步，受帧范围和循环设置影响 |
| `SetPlaybackFPS(float)` / `SetLooping(bool)` | 修改播放速度和循环设置 |
| `SetFrameRange(int32, int32)` | 设置有效帧范围 |
| `SetSequenceFrames(...)` | 替换帧数组 |
| `GetCurrentFrame()` / `GetFrameCount()` / `IsPlaying()` | 读取播放状态 |

`APICO4DGSActor` 直接转发播放、暂停、停止和 Seek；其他接口从 `GetPICO4DGSComponent()` 返回的播放组件调用。

帧数组直接引用已导入资产，切帧通过 `SetSplatAsset()` 更新目标组件的渲染状态。部署时根据目标设备选择序列规模和播放帧率。

## 环境与安装

### 环境

- Unreal Engine **5.5**；插件描述文件声明 `EngineVersion = 5.5.0`。
- Windows C++ 构建环境，例如 Visual Studio 2022 及匹配的 Windows SDK。
- 编辑器模块平台为 Win64；运行时模块平台声明为 Win64 和 Android。
- 进行 PICO Android 构建时，额外配置项目所需的 Android 工具链与 XR 插件。

原生 Cubemap 捕获的已记录验证环境为 Win64 / PC Editor；扩展功能的 Android 真机验证待补充。

### 安装到工程

在 Unreal 工程根目录执行以下命令，即可将插件克隆到安装位置：

```powershell
git clone https://github.com/xtt-learner/PICOSplat-Extensions.git Plugins/PICOSplat
```

也可下载源码并按以下步骤安装：

1. 将**这份完整插件目录**放到 `<YourProject>/Plugins/PICOSplat/`，保留 `Source/ThirdParty` 和 `Shaders`。
2. 确认工程只加载一份同名 `PICOSplat`，避免与引擎安装目录中的同名版本冲突。
3. 在工程的 `.uproject` 插件列表中启用 `PICOSplat`，重新生成工程文件并编译对应 Editor Target。
4. 打开工程，等待 Shader 编译完成，再导入 `.ply` 并放入关卡。
5. 如需原生 Cubemap 捕获，再配置前述 `GPUSynchronous` 排序。

本项目适配 UE 5.5，并使用 Renderer Private 接口；升级引擎时需同步适配相关渲染接口。

## 代码入口

| 内容 | 文件 |
| --- | --- |
| 捕获 Actor 与自动捕获设置 | [GSPortalCubeCaptureActor.h](Source/PICOSplatRuntime/Public/GSPortalCubeCaptureActor.h) |
| 原生六面渲染 | [GSPortalCubeCaptureRenderer.cpp](Source/PICOSplatRuntime/Private/Rendering/GSPortalCubeCaptureRenderer.cpp) |
| 颜色控制、资产切换和过滤入口 | [SplatComponent.cpp](Source/PICOSplatRuntime/Private/SplatComponent.cpp) |
| 临时过滤资产构建 | [SplatAsset.cpp](Source/PICOSplatRuntime/Private/SplatAsset.cpp) |
| 盒体模式和目标 Tag | [PICOSplatCullBoxActor.h](Source/PICOSplatRuntime/Public/PICOSplatCullBoxActor.h) |
| 帧序列播放 | [PICO4DGSComponent.cpp](Source/PICOSplatRuntime/Private/PICO4DGSComponent.cpp) |
| 外观调节的像素 Shader | [render_splat.ps.ush](Source/ThirdParty/Shaders/Public/render_splat.ps.ush) |

展示素材的文件位置与拍摄建议见 [素材清单](docs/media/README.md)。

## 上游与许可

本项目基于 PICO 的 [splat_unreal](https://github.com/Pico-Developer/splat_unreal) 和通用 [splat](https://github.com/Pico-Developer/splat) 组件，在上游 PLY 导入、渲染与排序能力之上扩展场景编辑和交互展示功能。

- 上游 UE 插件的 [LICENSE.md](https://github.com/Pico-Developer/splat_unreal/blob/main/LICENSE.md) 指向 Unreal Engine EULA。
- 通用组件采用其自身的 [许可证](https://github.com/Pico-Developer/splat/blob/main/LICENSE.md)；本地第三方许可分别保留于 [splat/LICENSE.md](Source/ThirdParty/splat/LICENSE.md) 和 [Shaders/Public/LICENSE.md](Source/ThirdParty/Shaders/Public/LICENSE.md)。
- 插件许可声明见 [LICENSE.md](LICENSE.md)，来源与第三方组件说明见 [NOTICE.md](NOTICE.md)。
