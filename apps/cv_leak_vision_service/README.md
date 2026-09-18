# RTSP 漏液推理服务

`cv_leak_vision_service` 是一个独立进程，使用公开的 `include/cv_sdk/cv_sdk.h` 调用 SDK，
通过 OpenCV 拉取 RTSP/摄像头视频，并使用 libevent 提供平台层 HTTP/JSON 接口。

## 启动

```sh
./build/cv_leak_vision_service \
  --listen 0.0.0.0 \
  --port 8080 \
  --model models/leak_seg_1280 \
  --rules models/leak_seg_1280/leak_rules.json
```

生产部署时建议将模型和规则放在只读目录，通过绝对路径传入；服务账号只需要读取模型/规则权限，
日志目录和临时目录单独授予写权限。

## 平台接口

健康检查：

```sh
curl http://127.0.0.1:8080/health
```

创建一路流：

```sh
curl -X POST http://127.0.0.1:8080/v1/streams \
  -H 'Content-Type: application/json' \
  -d '{"id":"camera-01","url":"rtsp://user:password@host/live"}'
```

`model_package` 和 `rules_json` 可在请求中覆盖启动时的默认值。服务会为每个流创建独立的
漏液处理器，即独立的 ONNX 会话和独立的时序滑动窗口。

查询结果：

```sh
curl http://127.0.0.1:8080/v1/streams/camera-01
curl http://127.0.0.1:8080/v1/streams
```

停止流：

```sh
curl -X DELETE http://127.0.0.1:8080/v1/streams/camera-01
```

结果中的 `leaks` 为当前帧的漏液目标，`alert` 为多帧确认后的告警状态。当前接口返回最新结果，
不保存视频和历史事件；生产平台可在此基础上增加 WebSocket/SSE 推送、事件落库、鉴权、租户隔离、
限流和指标接口。

完整接口契约见 [openapi.yaml](openapi.yaml)。当前版本采用轮询获取最新结果；平台需要实时推送时，
建议在服务外增加事件转发器，或将服务结果接入消息队列，避免让 HTTP 请求线程承担长连接推送。

## 线程模型

每路视频使用两个线程：取流线程持续拉流并只保留最新一帧（旧帧直接覆盖，不排队），
推理线程按顺序消费最新帧并刷新结果缓存。HTTP 线程只读取缓存副本，不参与推理，
因此查询接口不会阻塞或干扰推理节奏。

`CVSDK_LeakProcessorProcess` 是有状态接口，每次调用都会推进时序滑动窗口。
服务固定预分配 16 个 `CVSDK_LeakItem` 并每帧只调用一次，避免"先查询容量、再取数据"
的双调用把窗口多推进一帧。

## 服务与 SDK 的边界

- SDK：同步、无状态输入图像、模型推理和漏液后处理；发布为动态库、头文件和模型规则制品。
- 服务：RTSP 拉流与重连、线程生命周期、每路流隔离、结果缓存、HTTP API、日志和健康检查。
- 平台：调用 HTTP API 管理流和消费结果，不直接依赖 OpenCV、ONNX Runtime 或 SDK 内部 C++ 类型。
