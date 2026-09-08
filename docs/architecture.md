# MVP 架构

`C API -> Detector task adapter -> InferBackend -> hardware adapter`

当前 `mock` adapter 只用于验证 ABI、内存所有权和部署包接口。真实 ONNX Runtime/TensorRT/RKNN adapter 应分别实现 `Load` 与 `Run`，并在其 artifact manifest 中声明目标平台及版本兼容性。

预处理和后处理应随 task adapter 与 artifact metadata 维护；调用方只传递原始图像，绝不接触 ONNX Runtime/TensorRT/RKNN 类型。
