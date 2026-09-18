# SDK 架构

```text
公开 C ABI
   │
   ▼
Detector task adapter
   ├── 结果阈值过滤
   └── FireSmokeProcessor（可选后处理）
   │
   ▼
InferBackend
   ├── mock
   └── onnxruntime（CPU / CUDA Execution Provider）
   │
   ▼
模型包 artifacts/onnxruntime/model.onnx
```

SDK 对外只暴露稳定的 C ABI。调用方传入同步生命周期内有效的原始图像，并获得检测框和火情告警状态；不会接触 ONNX Runtime、CUDA、OpenCV 或其他后端专有类型。

`InferBackend` 是后端扩展点。当前 `mock` 后端用于 ABI、内存所有权和规则测试；ONNX Runtime 后端负责模型加载、letterbox 预处理、YOLOv8 输出解码和 NMS。后续 TensorRT、RKNN 等后端应分别实现 `Load` 和 `Run`，并在其模型制品中声明目标平台和运行时兼容性。

火焰/烟雾任务后处理由 `FireSmokeProcessor` 组合完成：候选阈值过滤、火焰颜色门控、烟雾静态/光晕/软运动门控，以及按目标 IoU 关联的多帧告警。规则参数由模型包中的 `fire_rules.json` 提供。

> 注：除上述 Detector → FireSmokeProcessor 路径外，GaugeReader 和 LeakProcessor
> 走"自持模型"路径——绕过 InferBackend，直接持有 Ort::Session 并自行解码模型输出。
> GaugeReader 持有 detector.onnx + pose.onnx 两个会话；LeakProcessor 持有单个
> 分割模型的双输出（predictions + prototypes）。详见 src/algo/gauge/gauge_reader.h
> 和 src/algo/leak/leak_segmenter.h。
