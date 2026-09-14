# 仪表读数 SDK 接口

`CVSDK_GaugeReader` 将模拟指针仪表的两阶段算法封装为同步 C API：仪表检测、ROI 姿态关键点定位和角度读数换算。

模型包需要包含：

```text
models/gauge_reader_640/
  artifacts/onnxruntime/detector.onnx
  artifacts/onnxruntime/pose.onnx
```

最小调用流程：

```c
CVSDK_GaugeReaderOptions options = {
    sizeof(options), "onnxruntime", 0.25f, 0.25f,
    0.0f, 100.0f, "MPa", 1, 0, {0}
};
CVSDK_GaugeReader* reader = NULL;
CVSDK_GaugeReaderCreate("models/gauge_reader_640", &options, &reader);

uint32_t count = 0;
CVSDK_GaugeReaderInfer(reader, &image, NULL, 0, &count);
CVSDK_GaugeReading items[8];
CVSDK_GaugeReaderInfer(reader, &image, items, 8, &count);
CVSDK_GaugeReaderDestroy(reader);
```

后端可选 `onnxruntime`、`onnxruntime-cuda` 或其 `onnx`/`onnx-cuda` 别名。CUDA 后端要求构建时启用 `CVSDK_ONNXRUNTIME_CUDA=ON`，并使用包含 CUDA Execution Provider 的 ONNX Runtime 制品。

当前 C++ 迁移不包含原 Python 项目依赖 PaddleOCR 的量程文字识别。调用方应在创建读数器时传入已知量程和单位；OCR 量程识别可作为独立上层模块接入。
