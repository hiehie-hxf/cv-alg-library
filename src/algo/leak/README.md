# 漏液检测任务组件

流水线：`YOLOv8-Seg 双输出(predictions 37x33600 + prototypes 32x320x320) -> letterbox 前处理 -> ONNX Runtime 推理 -> 掩码系数乘原型 -> 裁剪/放大/二值化 -> 面积与质心多帧告警`。

`LeakSegmenter` 自持 ONNX 会话，负责单帧分割；`LeakFilter` 只维护跨帧面积/质心滑动窗口；`LeakProcessor` 负责编排。

`LeakProcessor` 不生成额外的检测结果，只对模型输出做掩码还原和时序确认。现场参数由 `models/leak_seg_1280/leak_rules.json` 统一管理，模型输入尺寸与类别契约由同目录 `manifest.json` 描述。
