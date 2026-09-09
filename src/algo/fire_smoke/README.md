# 火焰/烟雾任务组件

流水线：`YOLO 原始框(0=smoke, 1=fire) -> 火焰颜色门控 -> 面积过滤 -> 多帧告警`。

当前 `FireSmokeProcessor` 实现不依赖 OpenCV：它用 RGB/BGR 像素的高亮暖色判定过滤明显非火焰的 `fire` 框。它只会移除候选，不会生成新检测。`SmokeStaticGate` 将作为同一流水线中的跨帧插件接入，需先确定 OpenCV/硬件图像算子依赖与现场视频回归基线。
