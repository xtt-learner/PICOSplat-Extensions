# PICOSplat 展示素材清单

此目录用于规划后续可加入[插件 README](../../README.md) 的截图、视频和拍摄参数。

## 候选素材

文件名和标识用于组织后续素材，可按最终内容调整。

| 标识 | 建议文件名 | 内容 | 建议形式 |
| --- | --- | --- | --- |
| `overview-video` | `overview.mp4` | 串联外观调整、过滤、裁剪、动态播放和原生捕获 | 30–60 秒总览视频 |
| `cubemap-image` | `cubemap-faces.png` | 六个面标注方向，附场景中采样 Cube RT 的效果 | 六面截图拼图 |
| `cubemap-video` | `cubemap-capture.mp4` | 改变捕获位置 → 点击捕获 → 展示输出更新 | 15–30 秒操作视频 |
| `appearance-image` | `appearance-presets.png` | 同视角默认、暖色、夜间效果，附参数 | 三栏对比截图 |
| `opacity-image` | `opacity-filter.png` | 同视角过滤前后，附阈值及四项点数统计 | 两栏对比截图 |
| `cullbox-video` | `cullbox-edit.mp4` 或 `cullbox-comparison.png` | RemoveInside、KeepInside、手动重建和恢复源资产 | 20–40 秒视频或对比图 |
| `sequence-video` | `gaussian-sequence.mp4` | 动态内容、暂停、单步、Seek、循环 | 20–40 秒视频 |

## 拍摄建议

### 总览

使用同一个小场景串联各项功能，开头直接展示效果。界面保留与操作有关的区域，让 GS 内容成为画面主体。模型、扫描和音频选用已获展示和分发授权的素材。

### 原生 Cubemap 捕获

- 选择在六个方向都有可辨认结构的 GS 内容，为各面标注 `+X/-X/+Y/-Y/+Z/-Z`。
- 展示 Capture Actor 的位置、目标 GS 和输出材质，说明输出来自哪个视点。
- 保留移动、触发捕获和输出更新的连续过程；自动刷新时标明捕获间隔。
- 如展示 SceneCapture 对比，使用相同数据、视点和设置，标明各自的捕获路径。

### 色调与曝光

固定相机、场景和后处理曝光，拍摄预设 `0/1/2` 的对比。可补充滑动 Exposure 的短视频，展示连续调整效果。

### 透明度过滤

选有低 alpha 杂点的视角，记录 `MinOpacityThreshold`、`OriginalSplatCount`、`RemovedByOpacityCount`、`RemovedByCullBoxCount` 和 `FilteredSplatCount`。拍摄透明度过滤时关闭裁剪框，让画面与统计直接对应。若展示性能，同时记录帧率与测量条件。

### 空间裁剪

依次展示源内容、盒体、`RebuildFilteredSplatData()` 后的结果和 `ClearSplatFilter()` 恢复。移动盒体后保留重建操作，准确反映当前交互方式。可补一个旋转盒体和一个组件 Tag 选择的例子。

### 帧序列播放

使用有明确运动的多帧数据，在视频里显示 `CurrentFrame`。分别展示暂停、前后单步和 Seek，标明播放 FPS、总帧数、设备与 UE 版本。展示渲染性能时，单独标注实测帧率和测量方法。

## 补入 README

图片补齐后，可在 README 对应的功能章节插入相对路径图片，例如：

```markdown
![固定视角下的默认、暖色和夜间效果](docs/media/appearance-presets.png)
```

视频可作为版本附件或上传附件提供，在 README 对应的功能章节加入最终演示链接。
