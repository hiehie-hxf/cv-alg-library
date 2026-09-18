# 漏液检测 SDK 接口

`CVSDK_LeakProcessor` 把 YOLOv8-Seg 实例分割封装为同步 C API：letterbox 前处理、双输出解码（掩码系数乘掩码原型）、实例掩码还原，以及基于**掩码面积与质心**的跨帧时序告警。该处理器**自持 ONNX 会话**，不经过 `CVSDK_Detector`。

模型包需要包含：

```text
models/leak_seg_1280/
  manifest.json                       # 模型契约：输入尺寸、类别、双输出定义
  leak_rules.json                     # 现场可调的阈值与时序规则
  model_card.md
  artifacts/onnxruntime/model.onnx
```

最小调用流程：

```c
CVSDK_LeakProcessor* processor = NULL;
/* options 传 NULL 时，规则文件默认为 <model_package>/leak_rules.json。 */
CVSDK_LeakProcessorCreate("models/leak_seg_1280", NULL, &processor);

/* 固定预分配容量，每帧只调用一次 Process。 */
CVSDK_LeakItem items[16];
CVSDK_LeakItemList list = {sizeof(list), items, 16, 0};
CVSDK_LeakAlertState state = {sizeof(state)};
CVSDK_LeakProcessorProcess(processor, &image, &list, &state);

/* 掩码单独取走：先查尺寸，再分配，再取数。 */
uint32_t width = 0, height = 0, stride = 0;
CVSDK_LeakProcessorCopyMask(processor, 0, NULL, 0, &width, &height, &stride);
uint8_t* mask = (uint8_t*)malloc((size_t)width * height);
CVSDK_LeakProcessorCopyMask(processor, 0, mask, width * height, &width, &height, &stride);

CVSDK_LeakProcessorDestroy(processor);
```

`Process` 是**有状态**接口，每次调用都会推进一次时序滑动窗口。因此**不能**使用「先传 `items = NULL` 查询容量、再分配、再调用一次」的常规双调用写法——那会把窗口多推进一帧，使告警判定提前触发。请固定预分配容量（上例为 16）并只调用一次；容量不足时接口返回 `CVSDK_BUFFER_TOO_SMALL` 且不写入明细，告警状态仍然有效。

时序判定使用三条滑动窗口（面积、质心、命中）。窗口未满时 `window_filled = 0`，此时只累计命中数 `hits`，不做趋势判定；默认配置 `window = 10` 下，前 9 帧始终处于预热期。窗口填满后，`hits >= min_hits` 且「面积增长超过 `area_growth_ratio`」或「质心下移超过 `centroid_down_px`」时，`level` 变为 `CVSDK_LEAK_ALERT_WARNING`。趋势是否成立可直接读 `area_growing` / `centroid_down`，`reason` 给出可读原因。

掩码只在**当前帧**有效：下一次 `CVSDK_LeakProcessorProcess` 或 `CVSDK_LeakProcessorReset` 之后即失效。`CopyMask` 本身不推进滑动窗口，可以安全地用于可视化。掩码为 CV_8UC1 语义（0 为背景、255 为漏液），尺寸与输入原图一致。

当前限制：模型为单类别 `liquid_stain`；掩码不在帧间缓存；处理器保存跨帧状态，因此**禁止多路视频共用同一实例**，也不保证并发调用安全——每路流应独立创建；透明水渍在单帧上的检出率较低，时序告警需要多帧累积才能确认。
