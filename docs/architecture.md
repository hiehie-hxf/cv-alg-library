# MVP 架构

`C API -> Detector task adapter -> InferBackend -> hardware adapter`

当前 `mock` adapter 只用于验证 ABI、内存所有权和部署包接口。真实 ONNX Runtime/TensorRT/RKNN adapter 应分别实现 `Load` 与 `Run`，并在其 artifact manifest 中声明目标平台及版本兼容性。

预处理和后处理应随 task adapter 与 artifact metadata 维护；调用方只传递原始图像，绝不接触 ONNX Runtime/TensorRT/RKNN 类型。

## 服务层

服务层与 SDK 分进程/分职责：

```text
平台层
   │ HTTP/JSON
   ▼
cv_fire_vision_service
   ├── 每路流一个 StreamSession
   ├── OpenCV RTSP/摄像头取流与断线重连
   ├── 最新结果缓存、健康检查、生命周期管理
   └── C ABI 调用
         ▼
      libcv_sdk
         ├── ONNX Runtime 推理
         └── 火焰颜色/烟雾时空/多帧告警后处理
```

每个 `StreamSession` 独占检测器、火情处理器和工作线程，确保多路视频的时序窗口互不污染。
服务默认只保留每路视频的最新结果，不在内存中堆积帧；当推理速度低于拉流速度时，取流线程应采用
有界队列或最新帧策略。当前 MVP 使用单路线程和最新状态缓存，后续可将推理线程池、GPU 批处理、
事件消息队列和历史存储作为独立模块接入。

对外 API 契约保存在 `apps/cv_fire_vision_service/openapi.yaml`，当前包括 `/health`、流创建/删除、流列表和最新结果查询。
平台层不应直接链接 OpenCV、ONNX Runtime 或 SDK 内部头文件。
