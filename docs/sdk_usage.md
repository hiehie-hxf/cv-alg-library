# cv-alg-library SDK 使用文档

面向接入本 SDK 的 C/C++ 与平台侧开发者。文档只描述**已实现并可通过测试验证**的行为；
未实现的能力集中在[第 1.2 节 当前边界](#12-当前边界重要)与[第 13 节 版本与兼容性](#13-版本与兼容性)中声明。

| 项目 | 值 |
|---|---|
| SDK 版本 | 0.1.0 |
| C ABI 版本 | 2（`CVSDK_API_VERSION`） |
| 语言标准 | C++17 实现；公开头文件兼容 C99 与 C++ |
| 支持平台 | Linux (x86_64 / aarch64)、macOS (arm64 / x86_64) |
| 公开头文件 | `include/cv_sdk/cv_sdk.h`（**唯一**对外头文件） |
| 相关文档 | [架构说明](architecture.md)、[测试说明](../tests/README.md)、[服务接口](../apps/cv_fire_vision_service/README.md)、[第三方依赖](../third_party/README.md) |

---

## 目录

1. [概述](#1-概述)
2. [快速开始](#2-快速开始)
3. [集成到你的工程](#3-集成到你的工程)
4. [核心概念](#4-核心概念)
5. [编程约定](#5-编程约定)
6. [编程指南](#6-编程指南)
7. [API 参考](#7-api-参考)
8. [配置参考](#8-配置参考)
9. [告警语义](#9-告警语义)
10. [性能与最佳实践](#10-性能与最佳实践)
11. [示例程序](#11-示例程序)
12. [常见问题排查](#12-常见问题排查)
13. [版本与兼容性](#13-版本与兼容性)
14. [附录](#14-附录)

---

## 1. 概述

### 1.1 能力范围

本 SDK 提供**同步**的目标检测与火焰/烟雾后处理能力，运行时不依赖 Python，对外只暴露稳定的 C ABI：

- **目标检测**：加载模型包，对单帧图像执行推理，输出检测框。
- **火情后处理**：对检测框执行颜色门控、烟雾时空门控和多帧时序确认，输出告警等级。

SDK 自身**不负责**图像采集、解码、显示、视频流管理、任务调度与结果存储。
这些属于调用方（业务进程）的职责——参见[第 4.1 节](#41-分层与调用边界)。

调用方只需要提供一个指向像素数据的指针和图像描述；SDK 不会持有该指针，
也不会向调用方暴露 ONNX Runtime、OpenCV、TensorRT 等任何后端类型。

### 1.2 当前边界（重要）

以下限制是刻意收敛的结果，接入前请确认与你的场景匹配：

| 边界 | 说明 |
|---|---|
| 同步 API | 没有异步/回调式推理接口。`Infer` 在调用线程完成全部计算后返回。 |
| 单帧输入 | 每次调用处理一张图像，不支持 batch 与多流复用。 |
| CPU 推理 | 当前 ONNX Runtime 后端只配置 CPU Execution Provider，**没有 GPU 接口**。 |
| 后端选择 | 仅支持 `mock` 与 `onnxruntime` 两个后端，通过字符串选择，非插件式注册。 |
| 序列化 | 不提供跨进程/网络传输的序列化格式，结果为进程内结构体。 |
| 模型包 | 只支持「目录 + manifest.json」形式，不支持加密、签名校验与在线下载。 |

> **关于 GPU**：`CVSDK_DetectorOptions` 中没有设备或 provider 字段，
> `OnnxRuntimeBackend` 也没有调用任何 `AppendExecutionProvider`。
> 需要 GPU 时须改造后端实现并更换带 CUDA/TensorRT provider 的 ONNX Runtime 制品，
> 详见[第 13 节](#13-版本与兼容性)。

---

## 2. 快速开始

### 2.1 环境要求

| 依赖 | 要求 | 用途 |
|---|---|---|
| CMake | ≥ 3.20 | 构建 |
| C++ 编译器 | 支持 C++17（GCC 9+ / Clang 12+ / MSVC 2019+） | 构建 |
| OpenCV | 4.x（`core` `imgproc` `videoio` `highgui` `imgcodecs`） | 火情算法必需，无开关可关闭 |
| ONNX Runtime | 1.20.1（默认版本，可按平台覆盖） | 推理后端 |
| libevent | 2.x（仅构建服务时需要） | `cv_fire_vision_service` 的 HTTP 服务 |
| Threads | 系统 pthread | 日志线程 |

> OpenCV 是**强制**依赖：`CVSDK_WITH_OPENCV=OFF` 会直接以 `FATAL_ERROR` 终止配置，
> 因为火焰颜色门控与烟雾静态门控的实现依赖 OpenCV。

### 2.2 构建 SDK

```sh
cmake -S . -B build -DCVSDK_BUILD_TESTS=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

若 ONNX Runtime 不在仓库默认位置（`third_party/onnxruntime/prebuilt/<平台>/<版本>`），
用 `CVSDK_ONNXRUNTIME_ROOT` 指定：

```sh
cmake -S . -B build -DCVSDK_ONNXRUNTIME_ROOT=/opt/onnxruntime-1.20.1
```

主要构建选项：

| 选项 | 默认 | 说明 |
|---|---|---|
| `CVSDK_BUILD_TESTS` | `ON` | 构建单元/回归/压力测试 |
| `CVSDK_BUILD_EXAMPLES` | `ON` | 构建示例程序 |
| `CVSDK_BUILD_SERVICE` | `OFF` | 构建 RTSP HTTP 推理服务 |
| `CVSDK_WITH_ONNXRUNTIME` | `ON` | 启用 ONNX Runtime 后端 |
| `CVSDK_WITH_OPENCV` | `ON` | 启用 OpenCV 算子（关闭会报错） |
| `CVSDK_INSTALL_MODEL_ARTIFACTS` | `OFF` | 把 `.onnx` 纳入安装包 |
| `CVSDK_INSTALL_RUNTIME_DEPS` | `OFF` | 把已定位的 ONNX Runtime 动态库纳入安装包 |
| `CVSDK_ONNXRUNTIME_VERSION` | `1.20.1` | 锁定的 ONNX Runtime 版本 |
| `CVSDK_ONNXRUNTIME_ROOT` | 空 | 覆盖 ONNX Runtime SDK 根目录 |

### 2.3 最小可运行示例

以下程序是完整的、可编译的 C 示例，覆盖「加载模型 → 推理 → 火情后处理」全链路：

```c
#include "cv_sdk/cv_sdk.h"
#include <stdio.h>

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s <model-package> <fire_rules.json>\n", argv[0]);
    return 2;
  }

  /* 1. 可选：配置日志（默认仅 WARN 及以上输出到 stderr） */
  CVSDK_LogOptions log = {sizeof(log), CVSDK_LOG_INFO, NULL, 0, 0, 0, NULL, NULL};
  CVSDK_ConfigureLogging(&log);

  /* 2. 创建检测器 */
  CVSDK_DetectorOptions options = {sizeof(options), "onnxruntime", 0.10f, {0}};
  CVSDK_Detector* detector = NULL;
  if (CVSDK_DetectorCreate(argv[1], &options, &detector) != CVSDK_OK) {
    fprintf(stderr, "detector create failed: %s\n", CVSDK_GetLastError());
    return 1;
  }

  /* 3. 创建火情后处理器（每路视频流必须独立创建） */
  CVSDK_FireSmokeProcessor* processor = NULL;
  if (CVSDK_FireSmokeProcessorCreate(argv[2], &processor) != CVSDK_OK) {
    fprintf(stderr, "processor create failed: %s\n", CVSDK_GetLastError());
    CVSDK_DetectorDestroy(detector);
    return 1;
  }

  /* 4. 描述一帧图像；实际使用时这里替换为你的解码结果 */
  unsigned char pixels[640 * 640 * 3] = {0};
  CVSDK_Image image = {sizeof(image), pixels, 640, 640, 640 * 3, CVSDK_PIXEL_FORMAT_BGR8};

  /* 5. 同步推理 */
  CVSDK_Detection raw[256];
  CVSDK_DetectionList raw_list = {sizeof(raw_list), raw, 256, 0};
  CVSDK_Status status = CVSDK_DetectorInfer(detector, &image, &raw_list);
  if (status != CVSDK_OK) {
    fprintf(stderr, "infer failed: %s\n", CVSDK_GetLastError());
    CVSDK_FireSmokeProcessorDestroy(processor);
    CVSDK_DetectorDestroy(detector);
    return 1;
  }

  /* 6. 火情后处理：门控过滤 + 多帧告警 */
  CVSDK_Detection filtered[256];
  CVSDK_DetectionList filtered_list = {sizeof(filtered_list), filtered, 256, 0};
  CVSDK_FireAlertState state = {sizeof(state), CVSDK_FIRE_ALERT_NONE, 0.F, 0.F, 0, 0, {0}};
  status = CVSDK_FireSmokeProcessorProcess(processor, &image, raw, raw_list.count, &filtered_list,
                                           &state);
  if (status != CVSDK_OK) {
    fprintf(stderr, "postprocess failed: %s\n", CVSDK_GetLastError());
    CVSDK_FireSmokeProcessorDestroy(processor);
    CVSDK_DetectorDestroy(detector);
    return 1;
  }

  printf("raw=%u filtered=%u level=%d fire_hits=%u smoke_hits=%u reason=%s\n", raw_list.count,
         filtered_list.count, (int)state.level, state.fire_hits, state.smoke_hits, state.reason);

  CVSDK_FireSmokeProcessorDestroy(processor);
  CVSDK_DetectorDestroy(detector);
  return 0;
}
```

关键点：

- 所有结构体初始化时，**第一个字段必须是 `struct_size`**，用 `sizeof(结构体)` 赋值。
- 句柄先初始化为 `NULL`，创建失败时不需要（也不能）销毁。
- `raw` / `filtered` 数组由**调用方**分配，SDK 不负责释放。

### 2.4 运行随仓库提供的示例

```sh
# 检测链路（使用 mock 后端，不需要真实模型）
./build/cv_sdk_detect_example models/demo_detector

# 火情完整链路（需要 models/fire_smoke_640/artifacts/onnxruntime/model.onnx）
./build/cv_sdk_fire_vision_example models/fire_smoke_640 models/fire_smoke_640/fire_rules.json 0
./build/cv_sdk_fire_vision_example models/fire_smoke_640 models/fire_smoke_640/fire_rules.json \
    rtsp://user:password@host/live --headless
./build/cv_sdk_fire_vision_example models/fire_smoke_640 models/fire_smoke_640/fire_rules.json test.jpg
```

第三个参数同时支持摄像头序号（`0`）、文件路径、RTSP URL 和**单张图片**（自动识别，
处理完一帧后退出）。更多参数见[第 11 节](#11-示例程序)。

---

## 3. 集成到你的工程

### 3.1 交付包内容

```sh
cmake -S . -B build -DCVSDK_BUILD_SERVICE=ON -DCVSDK_WITH_ONNXRUNTIME=ON
cmake --build build -j4
cmake --install build --prefix dist/cv-alg-library
```

安装后的目录结构：

```text
dist/cv-alg-library/
├── include/cv_sdk/cv_sdk.h               # 唯一需要包含的头文件
├── lib/
│   ├── libcv_sdk.so -> libcv_sdk.so.0    # 动态库（SOVERSION 0）
│   └── cmake/cv_alg_library/cv_sdkTargets.cmake
├── bin/cv_fire_vision_service            # 可选，需 CVSDK_BUILD_SERVICE=ON
└── share/
    ├── doc/cv_alg_library/               # README、服务文档、openapi.yaml
    └── cv_alg_library/models/fire_smoke_640/   # manifest.json / model_card / fire_rules.json
```

`.onnx` 模型二进制**默认不进入交付包**（保持模型与代码分离发布）。
需要打包时加 `-DCVSDK_INSTALL_MODEL_ARTIFACTS=ON`；需要连带 ONNX Runtime 动态库时加
`-DCVSDK_INSTALL_RUNTIME_DEPS=ON`。

### 3.2 手动集成（推荐）

由于头文件只有一个、且没有第三方类型泄漏，最简单的集成方式是把交付包当作普通 C 库使用：

```cmake
add_executable(my_app main.c)
target_include_directories(my_app PRIVATE ${CVSDK_PREFIX}/include)
target_link_directories(my_app PRIVATE ${CVSDK_PREFIX}/lib)
target_link_libraries(my_app PRIVATE cv_sdk)
```

或者直接编译：

```sh
cc -std=c99 main.c -I"$PREFIX/include" -L"$PREFIX/lib" -lcv_sdk -o my_app
```

### 3.3 CMake 集成

安装目标导出的 imported target 名称为 **`cv_sdk::cv_sdk`**。但请注意：
仓库当前**只安装了 `cv_sdkTargets.cmake`，没有生成 `cv_alg_libraryConfig.cmake`**，
因此 `find_package(cv_alg_library)` 无法直接工作。现阶段请显式包含 targets 文件：

```cmake
include(${CVSDK_PREFIX}/lib/cmake/cv_alg_library/cv_sdkTargets.cmake)
target_link_libraries(my_app PRIVATE cv_sdk::cv_sdk)
```

> 若你希望使用标准 `find_package(cv_alg_library CONFIG REQUIRED)`，
> 需要先在构建脚本中补一个 package config 文件（属于待补齐项）。

### 3.4 运行时依赖

`libcv_sdk` 在运行时需要：

- ONNX Runtime 动态库（仅当 `CVSDK_WITH_ONNXRUNTIME=ON` 编译时）
- OpenCV `core` / `imgproc` 动态库

用 `ldd`（Linux）或 `otool -L`（macOS）确认实际依赖，并确保交付环境中可见。

> **部署注意**：当前构建系统只设置了 `BUILD_RPATH`，**没有设置安装后的 RPATH**。
> 因此安装到自定义前缀后，`libcv_sdk` 可能找不到 `libonnxruntime` / OpenCV。
> 可选做法：
> 1. 构建时加 `-DCVSDK_INSTALL_RUNTIME_DEPS=ON` 把 ONNX Runtime 一并安装，再设置
>    `LD_LIBRARY_PATH`（Linux）或 `DYLD_LIBRARY_PATH`（macOS）；
> 2. 或在你的构建脚本里为 `cv_sdk` 补 `CMAKE_INSTALL_RPATH`（Linux 可用 `$ORIGIN`）。

### 3.5 部署检查清单

- [ ] 交付环境已安装 OpenCV 4.x 运行时库，版本与编译期一致。
- [ ] ONNX Runtime 动态库版本与编译期一致（默认 1.20.1）。
- [ ] 模型包目录完整：`manifest.json` + `artifacts/onnxruntime/model.onnx`。
- [ ] 规则文件 `fire_rules.json` 已随包发布，路径对运行账号可读。
- [ ] 应用捕获了 `SIGINT` / `SIGTERM`，退出前销毁句柄（见[第 6.5 节](#65-多路视频)）。
- [ ] 每路视频流拥有**独立的** `CVSDK_FireSmokeProcessor` 实例。

---

## 4. 核心概念

### 4.1 分层与调用边界

```text
你的应用进程
  │  只 include <cv_sdk/cv_sdk.h>，只用 C 类型
  ▼
libcv_sdk (动态库)
  ├── C ABI 边界：参数校验、异常 → 错误码
  ├── 任务适配层：检测器、火情后处理编排
  ├── 后端抽象：mock / onnxruntime
  └── 基础层：异步日志、JSON 解析、线程局部错误
```

**你的代码永远不会看到** `Ort::Session`、`cv::Mat`、`InferBackend` 等内部类型。
图像解码、缩放、显示、RTSP 拉流都由你自己（或使用 OpenCV/FFmpeg）完成。

### 4.2 句柄生命周期

SDK 提供四类不透明句柄：

| 句柄 | 创建 | 销毁 | 能否跨线程共享 |
|---|---|---|---|
| `CVSDK_Detector` | `CVSDK_DetectorCreate` | `CVSDK_DetectorDestroy` | 建议每路流独立 |
| `CVSDK_FireFilter` | `CVSDK_FireFilterCreate` | `CVSDK_FireFilterDestroy` | **禁止**，含跨帧状态 |
| `CVSDK_FireSmokeProcessor` | `CVSDK_FireSmokeProcessorCreate` | `CVSDK_FireSmokeProcessorDestroy` | **禁止**，含跨帧状态 |

规则：

- 创建成功后**必须**销毁，否则泄漏模型会话与内存。
- `*Destroy(NULL)` 是安全的空操作。
- 销毁后句柄立即失效，不得再次使用。
- 创建失败时句柄不会被写出（见[第 5.2 节](#52-返回值与错误处理)），
  所以**务必先把句柄初始化为 `NULL`**。

### 4.3 模型包

模型包是一个目录，至少包含：

```text
models/fire_smoke_640/
├── manifest.json                       # 模型契约元数据（必填）
├── model_card.md                       # 人类可读的模型说明（建议）
└── artifacts/
    └── onnxruntime/
        └── model.onnx                  # 后端制品（onnxruntime 后端必需）
```

不同后端对内容的要求不同：

| 后端 | 必需内容 | 是否校验 manifest 内容 |
|---|---|---|
| `mock` | `manifest.json` 存在 | 否，仅检查文件存在 |
| `onnxruntime` | `artifacts/onnxruntime/model.onnx` 存在 | **否**，完全不读取 manifest |

> **当前实现的重要事实**：`manifest.json` 被设计为「模型契约的唯一来源」，
> 但当前加载路径**既不读取也不校验**它——输入尺寸从模型张量形状推导，
> letterbox 填充色、归一化系数、类别顺序、`sha256` 全部为硬编码或忽略。
> 因此 manifest 目前是**记录性文档**而非强制契约。详见[第 13 节](#13-版本与兼容性)。

### 4.4 有状态与无状态对象

这是本 SDK 最容易误用的地方：

| 对象 | 状态 | 约束 |
|---|---|---|
| `CVSDK_Detector` | 无跨帧状态 | 单帧输入单帧输出 |
| `CVSDK_FireFilter` | 滑动窗口 + 目标轨迹 | **每路视频流独立实例** |
| `CVSDK_FireSmokeProcessor` | 内含颜色门控、烟雾历史帧、轨迹窗口 | **每路视频流独立实例** |

时序确认依赖「同一路视频的连续帧」。如果两路视频共用一个 processor 实例，
A 相机的帧会污染 B 相机的滑动窗口，产生无法解释的误报与漏报。

### 4.5 内存所有权

| 数据 | 所有者 | 约定 |
|---|---|---|
| `CVSDK_Image::data` | 调用方 | 同步 API 返回前保持有效即可；SDK 不会异步持有 |
| `CVSDK_DetectionList::items` | 调用方 | SDK 只写入，从不分配或释放 |
| `CVSDK_FireAlertState` | 调用方 | 传入前必须设置 `struct_size` |
| `CVSDK_StatusMessage()` 返回值 | SDK | 静态字符串，**不得** `free` |
| `CVSDK_GetLastError()` 返回值 | SDK（线程局部） | **不得** `free`；下一次调用可能覆盖 |

---

## 5. 编程约定

### 5.1 结构体版本化（`struct_size`）

所有跨 ABI 的结构体都以 `uint32_t struct_size` 开头，用于向后兼容扩展：

```c
CVSDK_Image image = {sizeof(image), pixels, 640, 640, 640 * 3, CVSDK_PIXEL_FORMAT_BGR8};
/*                    ^^^^^^^^^^^ 必须是 sizeof(结构体) */
```

规则：

1. **总是**把 `struct_size` 设为 `sizeof(你的结构体)`。
2. SDK 的校验方式是 `struct_size >= 自己所需的大小`，因此旧版本调用方传入较小的结构体时，
   新版本 SDK 会拒绝（返回 `CVSDK_INVALID_ARGUMENT`）而不是读越界。
3. 版本升级只会在**结构体末尾追加**字段，已发布字段的顺序和含义不会改变。
4. `CVSDK_DetectorOptions::reserved[8]` 是预留扩展槽，**调用方必须置 0**。
5. 建议对结构体做**全字段显式初始化**。C 允许省略尾部字段（会被置 0），
   但在 `-Wall -Wextra` 下会触发 `-Wmissing-field-initializers`；
   常见的 `= {0}` 简写同样会触发该警告。按声明顺序写全字段即可消除：

   ```c
   CVSDK_FireAlertState state = {sizeof(state), CVSDK_FIRE_ALERT_NONE, 0.F, 0.F, 0, 0, {0}};
   ```

### 5.2 返回值与错误处理

所有可能失败的函数返回 `CVSDK_Status`，失败详情通过线程局部的 `CVSDK_GetLastError()` 获取：

```c
CVSDK_Status status = CVSDK_DetectorCreate(package, &options, &detector);
if (status != CVSDK_OK) {
  fprintf(stderr, "create failed: %s (%s)\n", CVSDK_StatusMessage(status), CVSDK_GetLastError());
  return 1;
}
```

- `CVSDK_StatusMessage(status)` 给出稳定的英文枚举描述（如 `"invalid argument"`）。
- `CVSDK_GetLastError()` 给出该次失败的**具体上下文**（如文件路径、JSON 字段路径），
  更适合写日志。
- 错误信息是**线程局部**的：在哪个线程失败，就在哪个线程读取。跨线程读取拿到的是别的线程的值。
- 错误信息在下一次 SDK 调用时可能被覆盖，需要保留请立即拷贝。
- SDK 内部**不会让 C++ 异常越过 C ABI**：所有异常都被转换成错误码。

**句柄出参的写入时机**（易踩坑）：`CVSDK_*Create` 系列函数只在参数校验通过后
才把 `*out_handle` 置为 `NULL`。若参数校验本身失败，出参**不会被写入**。因此：

```c
CVSDK_Detector* detector = NULL;   /* 必须显式初始化 */
```

### 5.3 缓冲区查询协议

检测结果与过滤后结果的输出都采用**两段式容量协商**。`CVSDK_BUFFER_TOO_SMALL`
在首次查询时是**正常流程，不是错误**：

```c
/* 第一段：查询需要多少容量 */
CVSDK_DetectionList query = {sizeof(query), NULL, 0, 0};
CVSDK_Status s = CVSDK_DetectorInfer(detector, &image, &query);
/* s == CVSDK_OK              → 没有目标，count == 0
   s == CVSDK_BUFFER_TOO_SMALL → 有目标，count == 所需数量
   其他                        → 真实错误 */

/* 第二段：按所需容量提供缓冲区 */
CVSDK_Detection* buffer = malloc(query.count * sizeof(CVSDK_Detection));
CVSDK_DetectionList out = {sizeof(out), buffer, query.count, 0};
s = CVSDK_DetectorInfer(detector, &image, &out);
```

精确语义：

| 条件 | 返回 | `count` |
|---|---|---|
| `items == NULL`，结果为空 | `CVSDK_OK` | `0` |
| `items == NULL`，结果非空 | `CVSDK_BUFFER_TOO_SMALL` | 所需数量 |
| `items != NULL`，`capacity >= count` | `CVSDK_OK` | 实际数量 |
| `items != NULL`，`capacity < count` | `CVSDK_BUFFER_TOO_SMALL` | 所需数量 |

要点：

- `count` **总是**被写入（包括 `items == NULL` 的查询调用），可直接用于分配。
- 容量不足时**不会**写入部分结果，缓冲区内容不可用。
- 固定容量缓冲区（如示例中的 `CVSDK_Detection raw[256]`）在高密度场景可能不足，
  生产代码应处理 `CVSDK_BUFFER_TOO_SMALL` 并扩容重试。

### 5.4 图像输入契约

`CVSDK_Image` 各字段的校验规则（`CVSDK_DetectorInfer` 会完整校验）：

| 字段 | 要求 |
|---|---|
| `struct_size` | `>= sizeof(CVSDK_Image)` |
| `data` | 非 `NULL` |
| `width` / `height` | 均 `> 0` |
| `stride_bytes` | `>= width`，且 `>= width * 通道数`（GRAY8 为 1，其余为 3） |
| `pixel_format` | `BGR8` / `RGB8` / `GRAY8` 之一 |

通道数与行跨度的关系：

```c
/* BGR8 / RGB8：3 字节/像素 */
CVSDK_Image a = {sizeof(a), data, w, h, w * 3, CVSDK_PIXEL_FORMAT_BGR8};
/* GRAY8：1 字节/像素 */
CVSDK_Image b = {sizeof(b), data, w, h, w,     CVSDK_PIXEL_FORMAT_GRAY8};
```

`stride_bytes` 允许行尾对齐填充（常见于硬件解码与 OpenCV 的 `cv::Mat::step`），
但必须不小于实际像素行宽度。

> **`GRAY8` 的适用范围**：灰度输入只对检测器有效。火情后处理内部按 3 通道解释图像，
> 传 `GRAY8` 会导致颜色门控判定为 0（丢弃火焰框）并可能越界读取。
> **调用 `CVSDK_FireSmokeProcessorProcess` 时应始终使用 `BGR8` 或 `RGB8`。**
>
> 另注意：`CVSDK_FireSmokeProcessorProcess` 与 `CVSDK_FireFilterProcess` 的入参校验
> 比 `CVSDK_DetectorInfer` **宽松**——前者只检查 `data`、`width`、`height` 非空非零，
> 不校验 `stride_bytes` 与 `pixel_format`。请自行保证图像描述正确。

### 5.5 线程安全模型

| 组件 | 线程安全性 |
|---|---|
| `CVSDK_ConfigureLogging` / `CVSDK_Log` / `CVSDK_GetLogStats` | 可从任意线程调用，内部有互斥保护 |
| `CVSDK_GetLastError` | 线程局部，每个线程看到自己的值 |
| `CVSDK_StatusMessage` / `CVSDK_GetApiVersion` | 只读，无状态 |
| `CVSDK_Detector` 及其 `Infer` | **SDK 未声明并发安全**。请勿多线程并发调用同一句柄 |
| `CVSDK_FireFilter` / `CVSDK_FireSmokeProcessor` | **禁止**并发调用，且禁止多路流共享 |

推荐的并发模型是「一路视频一个工作线程，线程内独占 detector 与 processor」，
即仓库中 `CVSDK_FireSmokeProcessor` 与 `cv_fire_vision_service` 采用的模型。

> 底层 ONNX Runtime 的 `Session::Run` 本身支持多线程并发，但 SDK 没有对此作出承诺，
> 请不要依赖这一实现细节。

---

## 6. 编程指南

### 6.1 目标检测

```c
CVSDK_DetectorOptions options = {sizeof(options), "onnxruntime", 0.10f, {0}};
/*                                backend 字符串 ↑      ↑ score_threshold */
```

`backend` 取值：

| 值 | 含义 |
|---|---|
| `NULL` | 等价于 `"mock"` |
| `"mock"` | 桩后端，只验证 `manifest.json` 存在，返回固定框。用于验证集成链路 |
| `"onnxruntime"` / `"onnx"` | ONNX Runtime 后端（推荐值） |
| 其他 | 返回 `CVSDK_UNSUPPORTED`，错误信息 `requested backend is not built` |

`score_threshold` 仅在 `[0, 1]` 范围内才被采纳，否则静默保留默认值 `0.25`。
该阈值在**后端输出之后**统一应用于所有类别。

> **不要把这个阈值设得过高。** 它是进入火情后处理第一道门槛，
> 后处理还会按类别再做一次候选阈值过滤（见[第 6.2 节](#62-火情后处理推荐路径)）。
> 实践中应满足：`score_threshold <= min(fire_candidate_conf, smoke_candidate_conf)`，
> 否则后处理层永远收不到候选框。

### 6.2 火情后处理（推荐路径）

`CVSDK_FireSmokeProcessor` 是完整火情后处理入口，内部固定按以下顺序执行：

```text
原始检测框
  │  ① 类别候选阈值过滤（fire / smoke 分别判定）
  │  ② 火焰 HSV 颜色门控（仅火焰框）
  ▼
SmokeStaticGate
  │  ③ 过曝 / 光晕 / 长基线静态 / 软运动门控（仅烟雾框）
  ▼
FireFilter
  │  ④ 面积过滤 → IoU 目标关联 → 滑动窗口时序确认
  ▼
过滤后检测框 + 告警状态
```

```c
CVSDK_Detection filtered[256];
CVSDK_DetectionList filtered_list = {sizeof(filtered_list), filtered, 256, 0};
CVSDK_FireAlertState state = {sizeof(state), CVSDK_FIRE_ALERT_NONE, 0.F, 0.F, 0, 0, {0}};
/*                             ^^^^^^^^^^^ struct_size 必须设置 */

CVSDK_Status status = CVSDK_FireSmokeProcessorProcess(processor, &image, raw, raw_count,
                                                      &filtered_list, &state);
```

**输入契约**：

- `image` 必须是 `BGR8` 或 `RGB8`（见[第 5.4 节](#54-图像输入契约)）。
- `raw[].class_id` 必须遵循固定约定：**`0 = smoke`，`1 = fire`**。
  这一约定是硬编码的，不来自 manifest。
- `class_id` 非 0/1 的框**不会被此函数丢弃**，会原样出现在输出列表中，
  但不会参与任何告警判定（时序层会忽略它们）。

**输出语义**（重要）：

- `filtered` 是**通过门控的展示框**，坐标为原图像素。
- 告警判定在时序层内部**再次**按 `min_area_ratio` 与分数范围筛选。
- 因此两者不是一一对应：**输出框非空不代表会告警**（框太小），
  告警状态也不保证输出列表中有对应条目。

`Reset()` 清空所有跨帧状态（颜色统计、烟雾历史帧、轨迹窗口），
适用于视频流重连或场景切换后需要重新积累证据的场景。

### 6.3 单独使用时序告警器（进阶）

如果你只想要多帧确认逻辑、自己实现门控，可以直接使用低阶的 `CVSDK_FireFilter`：

```c
CVSDK_FireFilter* filter = NULL;
if (CVSDK_FireFilterCreate("fire_rules.json", &filter) != CVSDK_OK) { /* ... */ }

/* width/height 为**原图**尺寸，用于计算面积比 */
CVSDK_FireAlertState state = {sizeof(state), CVSDK_FIRE_ALERT_NONE, 0.F, 0.F, 0, 0, {0}};
CVSDK_FireFilterProcess(filter, image_width, image_height, detections, count, &state);
```

该函数**不做任何图像分析**，只消费检测框，因此是纯 CPU 的规则计算，
可以脱离 OpenCV 与真实图像独立测试（仓库中的
`tests/unit/fire_filter_test.cpp` 与 `tests/regression/fire_rules_regression.cpp` 正是如此）。
它仍然读取同一份 `fire_rules.json`，与 `FireSmokeProcessor` 共享阈值语义。

### 6.4 日志接入

默认行为是「仅 WARN 及以上、输出到 stderr」。接入业务日志系统有两种方式，
可同时使用：

```c
/* 方式一：滚动文件 */
CVSDK_LogOptions opts = {sizeof(opts),
                         CVSDK_LOG_INFO,      /* min_level */
                         "/var/log/cv_sdk.log",
                         20u * 1024 * 1024,   /* max_file_bytes，推荐 20 MiB */
                         3,                   /* max_rotated_files */
                         4096,                /* queue_capacity */
                         NULL, NULL};
CVSDK_ConfigureLogging(&opts);

/* 方式二：业务回调（运行在 SDK 日志线程） */
static void OnLog(CVSDK_LogLevel level, const char* message_json, void* user_data) {
  /* 必须快速返回；不得阻塞、不得销毁 SDK */
  my_logger_write(message_json);
}
```

日志为**单行 JSON**：

```json
{"ts":"2026-09-11T15:38:04.123","level":"WARN","module":"api.detector","message":"invalid inference image","sdk_version":"0.1.0"}
```

运行特性：

| 特性 | 行为 |
|---|---|
| 异步写出 | 推理线程只做等级判断与有界入队，写盘/回调在后台线程 |
| 队列容量 | 默认 4096；满时优先驱逐一条等级更低的旧记录，否则丢弃新记录 |
| 丢弃统计 | 通过 `CVSDK_GetLogStats` 读 `dropped_count`，队列长度见 `queued_count` |
| 重复抑制 | `WARN` **及以下**等级的同内容日志每秒最多输出一条，并附带抑制计数 |
| 错误等级 | `ERROR` / `FATAL` **不受**重复抑制，也不会被驱逐 |

注意事项：

- **`CVSDK_Log` 的 `level` 传 `CVSDK_LOG_OFF` 会被直接忽略**，因为它不在有效区间内。
- **回调运行在 SDK 日志线程**：必须快速返回。阻塞回调会拖慢整个日志队列；
  在回调里销毁 SDK（如 `CVSDK_DetectorDestroy`）会造成死锁或崩溃。
- **不要记录敏感信息**：日志内容会被写入文件或转发，不应包含图像数据、OCR 文本、车牌等。
- **文件写入失败是静默的**：路径不可写时不会返回错误，也不会降级到 stderr。
  部署时请确认日志目录权限。
- **滚动需要同时设置两个参数**：仅设 `max_file_bytes > 0` 而 `max_rotated_files == 0`
  时不会发生任何轮转，文件会无限增长。
- `CVSDK_ConfigureLogging(NULL)` 恢复全部默认值（会**清除**已配置的文件与回调）。

### 6.5 多路视频

标准模式是「一路流 = 一个线程 = 一套句柄」：

```c
typedef struct {
  CVSDK_Detector* detector;
  CVSDK_FireSmokeProcessor* processor;
  pthread_t worker;
} StreamContext;
```

要点：

- 每路流独立创建 `CVSDK_FireSmokeProcessor`（强制），
  建议 `CVSDK_Detector` 也独立创建（简单、无争议）。
- 取流与推理解耦：推荐「最新帧」策略——丢弃积压旧帧，只处理最新一帧，
  避免推理慢于取流时队列无限增长。仓库示例程序与服务均采用此策略。
- 退出顺序：先置停止标志 → join 工作线程 → 销毁 processor → 销毁 detector。
- 视频重连或场景切换后，如果跨帧证据已失效，调用 `Reset()` 而不是重建句柄。

---

## 7. API 参考

### 7.1 版本与错误

#### `CVSDK_GetApiVersion`

```c
uint32_t CVSDK_GetApiVersion(void);
```

返回编译期 C ABI 版本（当前为 `2`）。建议在初始化时校验：

```c
if (CVSDK_GetApiVersion() != 2) { /* 头文件版本不匹配 */ }
```

#### `CVSDK_StatusMessage`

```c
const char* CVSDK_StatusMessage(CVSDK_Status status);
```

返回静态的英文状态描述。未知值返回 `"unknown status"`。返回值**不得释放**。

#### `CVSDK_GetLastError`

```c
const char* CVSDK_GetLastError(void);
```

返回**当前线程**最近一次错误详情，如
`"ONNX artifact not found: models/x/artifacts/onnxruntime/model.onnx"`。
返回值归 SDK 所有，不得释放，并在下次调用时可能被覆盖。

### 7.2 日志

#### `CVSDK_ConfigureLogging`

```c
CVSDK_Status CVSDK_ConfigureLogging(const CVSDK_LogOptions* options);
```

| 参数 | 说明 |
|---|---|
| `options` | 日志配置；传 `NULL` 恢复默认（仅 WARN、输出到 stderr） |

返回：`CVSDK_OK`；`struct_size` 不足或 `min_level` 越界时返回 `CVSDK_INVALID_ARGUMENT`。

`CVSDK_LogOptions` 字段：

| 字段 | 类型 | 说明 |
|---|---|---|
| `struct_size` | `uint32_t` | 必须为 `sizeof(CVSDK_LogOptions)` |
| `min_level` | `CVSDK_LogLevel` | 最低输出等级，默认 `CVSDK_LOG_WARN` |
| `file_path` | `const char*` | `NULL` 关闭文件输出 |
| `max_file_bytes` | `uint64_t` | `0` 关闭滚动；推荐 20 MiB |
| `max_rotated_files` | `uint32_t` | 仅当 `max_file_bytes > 0` 时生效 |
| `queue_capacity` | `uint32_t` | `0` 表示使用默认值 4096 |
| `callback` | `CVSDK_LogCallback` | 可选，运行在 SDK 日志线程 |
| `user_data` | `void*` | 透传给回调 |

配置是**同步替换**输出目标；建议在创建工作线程之前完成配置。

#### `CVSDK_GetLogStats`

```c
CVSDK_Status CVSDK_GetLogStats(CVSDK_LogStats* out_stats);
```

| 字段 | 含义 |
|---|---|
| `accepted_count` | 累计接受（入队）条数 |
| `dropped_count` | 累计丢弃条数（含被驱逐的低等级记录） |
| `queued_count` | 当前队列长度 |

`struct_size` 不足时返回 `CVSDK_INVALID_ARGUMENT`。

#### `CVSDK_Log`

```c
void CVSDK_Log(CVSDK_LogLevel level, const char* module, const char* message);
```

写入一条 SDK 日志。以下情况**静默忽略**，不返回错误：`level` 超出 `[TRACE, OFF)`、
`module` 或 `message` 为 `NULL`、`level < min_level`、队列已满且无法驱逐。

### 7.3 检测器

#### `CVSDK_DetectorCreate`

```c
CVSDK_Status CVSDK_DetectorCreate(const char* model_package,
                                  const CVSDK_DetectorOptions* options,
                                  CVSDK_Detector** out_detector);
```

| 参数 | 说明 |
|---|---|
| `model_package` | 模型包目录路径，必须包含 `manifest.json` 与后端制品 |
| `options` | 运行参数，可传 `NULL`（等价于 `backend = "mock"`、`score_threshold = 0.25`） |
| `out_detector` | 输出句柄；成功时必须由 `CVSDK_DetectorDestroy` 释放 |

可能的返回码：

| 返回码 | 场景 |
|---|---|
| `CVSDK_OK` | 加载成功 |
| `CVSDK_INVALID_ARGUMENT` | 参数为空、`struct_size` 不足 |
| `CVSDK_NOT_FOUND` | 模型包目录或制品文件不存在 |
| `CVSDK_UNSUPPORTED` | `backend` 字符串不受支持 |
| `CVSDK_OUT_OF_MEMORY` | 分配失败 |
| `CVSDK_INTERNAL_ERROR` | ONNX Runtime 加载失败等，详情见 `CVSDK_GetLastError()` |

#### `CVSDK_DetectorInfer`

```c
CVSDK_Status CVSDK_DetectorInfer(CVSDK_Detector* detector, const CVSDK_Image* image,
                                 CVSDK_DetectionList* in_out_detections);
```

同步执行一次推理。完成全部计算后才返回；不会异步持有 `image->data`。

返回：`CVSDK_OK`、`CVSDK_BUFFER_TOO_SMALL`（见[第 5.3 节](#53-缓冲区查询协议)）、
`CVSDK_INVALID_ARGUMENT`（图像描述非法）。

#### `CVSDK_DetectorDestroy`

```c
void CVSDK_DetectorDestroy(CVSDK_Detector* detector);
```

释放检测器。传 `NULL` 安全。

### 7.4 火情后处理

#### `CVSDK_FireSmokeProcessorCreate`

```c
CVSDK_Status CVSDK_FireSmokeProcessorCreate(const char* config_json_path,
                                            CVSDK_FireSmokeProcessor** out_processor);
```

加载并**完整校验**规则文件（见[第 8.3 节](#83-校验规则)）。
校验失败返回 `CVSDK_INVALID_ARGUMENT`，`CVSDK_GetLastError()` 会指出具体字段路径。

#### `CVSDK_FireSmokeProcessorProcess`

```c
CVSDK_Status CVSDK_FireSmokeProcessorProcess(CVSDK_FireSmokeProcessor* processor,
                                             const CVSDK_Image* image,
                                             const CVSDK_Detection* raw_detections,
                                             uint32_t raw_detection_count,
                                             CVSDK_DetectionList* filtered_detections,
                                             CVSDK_FireAlertState* out_state);
```

| 参数 | 说明 |
|---|---|
| `image` | 当前帧，必须为 `BGR8` / `RGB8` |
| `raw_detections` | 模型原始输出；`class_id` 必须是 `0=smoke` / `1=fire` |
| `raw_detection_count` | 原始框数量；为 `0` 时 `raw_detections` 可为 `NULL` |
| `filtered_detections` | 输出过滤后框，遵循容量查询协议 |
| `out_state` | 输出告警状态；`struct_size` 必须预先设置 |

返回：`CVSDK_OK`、`CVSDK_BUFFER_TOO_SMALL`、
`CVSDK_INVALID_ARGUMENT`（参数为空或 `out_state->struct_size` 不足）。

#### `CVSDK_FireSmokeProcessorReset` / `CVSDK_FireSmokeProcessorDestroy`

```c
CVSDK_Status CVSDK_FireSmokeProcessorReset(CVSDK_FireSmokeProcessor* processor);
void CVSDK_FireSmokeProcessorDestroy(CVSDK_FireSmokeProcessor* processor);
```

`Reset` 清空颜色统计、烟雾历史帧与全部轨迹窗口，但保留配置。
`Destroy` 释放资源，传 `NULL` 安全。

#### `CVSDK_FireFilterCreate` / `CVSDK_FireFilterProcess` / `CVSDK_FireFilterReset` / `CVSDK_FireFilterDestroy`

低阶时序告警器，加载同一份规则文件但不做图像分析，只消费检测框。
签名与 `Processor` 版本对应，`Process` 接收 `(width, height, detections, count, out_state)`
其中 `width` / `height` 是**原图**尺寸（用于面积比计算）。

---

## 8. 配置参考

配置被刻意切成两份，职责不重叠：

| 文件 | 管什么 | 谁可以改 |
|---|---|---|
| `manifest.json` | 模型契约：输入尺寸、类别顺序、归一化、制品哈希 | 模型发布方 |
| `fire_rules.json` | 可现场调节的阈值与规则开关 | 站点运维 |

**不要**把模型输入尺寸、类别顺序、归一化系数写进 `fire_rules.json`——
这些属于模型契约，换模型时必须一起换。

### 8.1 模型包 manifest.json

当前 `models/fire_smoke_640/manifest.json` 的结构：

```json
{
  "format_version": 1,
  "task": "fire_smoke_detection",
  "model_version": "fire-smoke-yolov8n-best_640_v1",
  "input": {
    "layout": "NCHW",
    "pixel_format": "BGR8",
    "shape": [1, 3, 640, 640],
    "letterbox_color": [114, 114, 114],
    "normalize_scale": 0.0039215686
  },
  "classes": ["smoke", "fire"],
  "artifacts": { "onnxruntime": "artifacts/onnxruntime/model.onnx" },
  "sha256": { "artifacts/onnxruntime/model.onnx": "696fda..." }
}
```

> **再次强调**：以上字段当前**不参与**加载校验。
> 实际生效的是「模型包目录 + `artifacts/onnxruntime/model.onnx` 这个固定路径」，
> 输入尺寸来自模型张量形状，letterbox 填充色固定 114，归一化为固定 `/255`，
> 类别顺序固定为 `0=smoke, 1=fire`。manifest 中的 `sha256` 也不会被校验。
> 换模型时请人工确认这些契约一致。

### 8.2 fire_rules.json 字段参考

`postprocess` — 通用后处理：

| 字段 | 类型 | 范围 | 默认 | 说明 |
|---|---|---|---|---|
| `min_area_ratio` | number | `[0,1]` | 0.0005 | 框面积占全图比例下限，小于此值的框不参与告警 |
| `candidate_thresholds.fire` | number | `[0,1]` | 0.25 | 火焰候选置信度阈值 |
| `candidate_thresholds.smoke` | number | `[0,1]` | 0.10 | 烟雾候选置信度阈值 |

`fire_rules.color_gate` — 火焰颜色门控：

| 字段 | 类型 | 范围 | 默认 | 说明 |
|---|---|---|---|---|
| `enabled` | bool | — | true | 是否启用 HSV 暖色门控 |
| `min_fraction` | number | `[0,1]` | 0.08 | 框内暖色高亮像素比例下限 |

`fire_rules.temporal` — 火焰/烟雾时序规则：

| 字段 | 类型 | 范围 | 默认 | 必填 |
|---|---|---|---|---|
| `fire_window` | int | `[1,10000]` | 5 | 是 |
| `fire_min_hits` | int | `[1,10000]` | 3 | 是 |
| `fire_confirm_conf` | number | `[0,1]` | 0.40 | 是 |
| `critical_fire_conf` | number | `[0,1]` | 0.75 | 是 |
| `fire_strong_min_hits` | int | `[1,10000]` | 2 | 否 |
| `critical_consecutive` | int | `[1,10000]` | 2 | 否 |
| `track_iou_threshold` | number | `(0,1]` | 0.30 | 否 |
| `track_max_missed` | int | `[1,10000]` | 1 | 否 |
| `smoke_window` | int | `[1,10000]` | 10 | 是 |
| `smoke_min_hits` | int | `[1,10000]` | 3 | 是 |
| `smoke_confirm_conf` | number | `[0,1]` | 0.22 | 是 |

`smoke_rules.static_gate` — 烟雾时空门控：

| 字段 | 类型 | 范围 | 默认 | 必填 | 说明 |
|---|---|---|---|---|---|
| `enabled` | bool | — | true | 是 | 是否启用烟雾门控 |
| `static_diff` | number | `>= 0` | 2.5 | 是 | 长基线差分阈值（灰度级） |
| `static_ratio` | number | `>= 0` | 2.0 | 是 | 相对全图运动基准的阈值倍数 |
| `bypass_conf` | number | `[0,1]` | 0.80 | 是 | 高于此置信度的烟雾框跳过门控 |
| `overexposed_max` | number | `[0,1]` | 0.50 | 是 | 框内过曝（≥245）像素比例上限 |
| `halo_mean` | number | `>= 0` | 190 | 是 | 区域均值上限（光晕判据） |
| `halo_core_frac` | number | `[0,1]` | 0.04 | 是 | 核心高亮（≥250）比例判据 |
| `halo_core_mean` | number | `>= 0` | 150 | 是 | 与 `halo_core_frac` 联合判据 |
| `soft_active_min` | int | `[1,10000]` | 25 | 是 | 软运动活跃像素数下限 |
| `global_motion_max_ratio` | number | `(0,1]` | 0.35 | 否 | 全图运动占比上限，超过则本帧丢弃全部烟雾框 |

> 以上「默认」列是 `FireConfig` 结构体的内置初值。
> **必填字段缺失时加载直接失败**，内置默认值只对标记为「否」的可选字段生效。

### 8.3 校验规则

`CVSDK_FireSmokeProcessorCreate` / `CVSDK_FireFilterCreate` 会执行完整校验，
失败时返回 `CVSDK_INVALID_ARGUMENT`，`CVSDK_GetLastError()` 给出具体原因：

**结构校验**：

- `schema_version` 必须等于 `1`，否则报 `schema_version must equal 1`。
- 数值字段必须在声明范围内，例如

  ```text
  expected number [0,1] at postprocess.candidate_thresholds.fire
  expected positive integer at fire_rules.temporal.fire_window
  expected non-negative number at smoke_rules.static_gate.static_diff
  ```

- 整数字段必须是整数值（`5.5` 会被拒绝）。
- 布尔字段必须是 JSON 布尔值，不能是 `0` / `1`。

**交叉校验**（全部须满足，否则报 `invalid temporal rule relationship`）：

```text
fire_min_hits         <= fire_window
smoke_min_hits        <= smoke_window
critical_fire_conf    >= fire_confirm_conf
fire_strong_min_hits  <= fire_window
critical_consecutive  <= fire_window
0 < track_iou_threshold        <= 1
0 < global_motion_max_ratio    <= 1
```

**修改配置后必须重新创建处理器**，已创建的实例不会感知文件变化。

### 8.4 调参指南

**火焰阈值**：

- 提高 `candidate_thresholds.fire` 可减少低置信度框与误报，但会降低召回。
- 建议先调它，再考虑 `min_fraction`。
- `min_fraction` 提高会过滤掉更多偏色区域（如暖色灯光、皮肤），过高会漏掉远距离小火。

**烟雾阈值**：

- 烟雾通常保留较低阈值（误报靠时空门控抑制，而不是靠提高置信度阈值）。
- 若现场有固定光源闪烁、蒸汽、水汽，优先调整 `static_diff` / `halo_*` 系列，
  而不是提高 `smoke_min_hits`。

**时序确认**：

- `*_min_hits` 与 `*_window` 共同决定确认速度与稳定性。
  例如 `fire_window=5, fire_min_hits=3` 表示「最近 5 帧内至少 3 帧命中」。
- 增大 `window` 会延长确认时间但更抗噪；增大 `min_hits` 会降低误报但增加漏报。
- `track_iou_threshold` 降低会让不同位置的目标更容易被关联为同一事件；
  提高则更倾向于把目标拆成多个轨迹。
- `track_max_missed` 允许目标短暂丢失若干帧而不重置轨迹。
  现场目标被遮挡频繁时可适当提高。

**修改后的验证流程**：先跑
`./build/cv_sdk_fire_regression_test` 确认规则逻辑未破坏，
再用真实视频回归确认误报/漏报变化。

---

## 9. 告警语义

### 9.1 等级定义

| 枚举 | 值 | 含义 |
|---|---|---|
| `CVSDK_FIRE_ALERT_NONE` | 0 | 无告警 |
| `CVSDK_FIRE_ALERT_INFO` | 1 | 发现候选，尚未确认 |
| `CVSDK_FIRE_ALERT_WARNING_SMOKE` | 2 | 烟雾已确认 |
| `CVSDK_FIRE_ALERT_WARNING_FIRE` | 3 | 火焰已确认 |
| `CVSDK_FIRE_ALERT_CRITICAL` | 4 | 严重告警 |

等级是**单调递增**的，可以直接用 `>=` 比较（示例程序用
`level >= CVSDK_FIRE_ALERT_WARNING_FIRE` 决定高亮颜色）。

### 9.2 判定逻辑

时序层对火焰与烟雾分别维护轨迹集合，逐帧执行：

```text
① 过滤输入框：score ∉ [0,1] 或 width/height < 0 的框丢弃
② 面积过滤：width × height / (图像宽 × 高) < min_area_ratio 的框丢弃
③ 按 class_id 分流：1 → 火焰轨迹集，0 → 烟雾轨迹集
④ 目标关联：与已有轨迹 IoU >= track_iou_threshold 则关联（贪心，一对一）
⑤ 本帧未匹配的轨迹计一次 missed 并补 0 分；missed > track_max_missed 的轨迹删除
⑥ 每个轨迹维护长度等于 window 的分数滑窗
⑦ 取「命中数最多、其次置信度最高」的轨迹作为代表轨迹，计算：
     Hits      = 窗口内 score > 0 的帧数
     Max       = 窗口内最大 score
     StrongHits        = 窗口内 score >= threshold 的帧数
     ConsecutiveStrong = 从最近一帧往前连续 score >= threshold 的帧数
```

> 滑窗中记录的是**有效置信度或 0**：关联上目标但置信度低于该类别
> `candidate_thresholds` 的帧、以及未关联上的帧，都记 0 分。
> 因此 `Hits` 的实际含义是「达到类别候选阈值的帧数」，
> 而 `Max` 是窗口内的真实峰值置信度。

判定条件：

```text
fire_confirmed  = Hits >= fire_min_hits
                  且 Max >= fire_confirm_conf
                  且 StrongHits(fire_confirm_conf) >= fire_strong_min_hits

smoke_confirmed = Hits >= smoke_min_hits
                  且 Max >= smoke_confirm_conf

critical        = ConsecutiveStrong(critical_fire_conf) >= critical_consecutive
```

等级判定（**按顺序短路**）：

| 条件 | 结果等级 | `reason` |
|---|---|---|
| `critical` 或（`fire_confirmed` 且 `smoke_confirmed`） | `CRITICAL` | `fire_high_confidence_sustained` 或 `fire_and_smoke_confirmed` |
| `fire_confirmed` | `WARNING_FIRE` | `fire_confirmed` |
| `smoke_confirmed` | `WARNING_SMOKE` | `smoke_confirmed` |
| `fire_hits > 0` 或 `smoke_hits > 0` | `INFO` | `candidate_detected` |
| 其他 | `NONE` | `no_detection` |

> **注意**：`CRITICAL` 中的「连续强命中」条件**只由火焰驱动**。
> 烟雾无论多么确信都不会单独升级到 `CRITICAL`，
> 只有「火焰已确认且烟雾也确认」这一组合会让烟雾参与 `CRITICAL` 判定。

### 9.3 `reason` 取值

`reason` 是固定枚举文本（不是自由描述），最大 96 字节（含结尾 `\0`）：

```text
no_detection
candidate_detected
smoke_confirmed
fire_confirmed
fire_and_smoke_confirmed
fire_high_confidence_sustained
```

适合直接作为事件分类字段落库或做告警去重键。

### 9.4 状态字段含义

| 字段 | 含义 |
|---|---|
| `level` | 当前告警等级 |
| `max_fire_confidence` | **代表火焰轨迹**在窗口内的最大置信度（不是当前帧最大值） |
| `max_smoke_confidence` | **代表烟雾轨迹**在窗口内的最大置信度 |
| `fire_hits` | 代表火焰轨迹在窗口内的命中帧数 |
| `smoke_hits` | 代表烟雾轨迹在窗口内的命中帧数 |
| `reason` | 判定原因，见上 |

「代表轨迹」的选取规则是：命中数最多者优先；命中数相同时取窗口最大置信度更高者。
因此即使当前帧没有火焰框，`max_fire_confidence` 仍可能保持非零——
这正是「多帧确认」的语义：告警会在一段时间内保持，而不是逐帧闪烁。

---

## 10. 性能与最佳实践

### 10.1 实测数据

README 记录的当前基线：**macOS ARM64 CPU、640 输入、`fire01.jpg` 单帧完整链路约 1.2 秒**。
这个数量级用于判断「是否满足实时要求」，不能作为目标平台指标。

### 10.2 当前实现的性能特征

- 预处理（letterbox + 归一化 + NCHW 排布）在 CPU 上以逐像素循环实现，是主要热点之一。
- ONNX Runtime 会话固定使用 4 个 intra-op 线程。
- 后处理中的烟雾门控在 160×90 灰度图上做差分，开销受控。
- 颜色门控会把框裁剪后缩放到最长边 96 像素再统计，开销与框大小弱相关。

### 10.3 建议

**取流与推理解耦**：使用「最新帧」策略——取流线程只保留最新一帧，
推理线程消费并丢弃积压帧。这样预览/进度不受推理速度影响，
延迟保持为「一帧推理时间」而不是「队列积压时间」。仓库示例程序与此模型一致。

**缓冲区按需扩容**：不要假设检测框数量上限。处理
`CVSDK_BUFFER_TOO_SMALL` 并扩容重试，或为每路流预分配合理上限。

**避免在推理线程做重活**：绘制、编码、网络上报应放到独立线程。

**控制日志量**：`CVSDK_LOG_INFO` 在逐帧路径上会产生大量日志。
生产环境建议保持 `WARN` 或以上，或使用回调转发到采样后的日志系统。

**选择合适的后端**：实时边缘场景应使用带硬件加速的 ONNX Runtime 制品或专用后端，
当前 CPU 路径不适合高帧率场景。具体改造方式见[第 13 节](#13-版本与兼容性)。

---

## 11. 示例程序

| 程序 | 命令 | 说明 |
|---|---|---|
| `cv_sdk_detect_example` | `<model-package>` | 最小 C 调用示例（mock 后端） |
| `cv_sdk_fire_vision_example` | `<model_package> <fire_rules.json> <source> [--headless]` | 完整火情链路，带预览 |
| `cv_fire_vision_service` | `--listen --port --model --rules` | RTSP HTTP 服务（需 `CVSDK_BUILD_SERVICE=ON`） |

`cv_sdk_fire_vision_example` 的 `<source>` 支持：

| 形式 | 示例 | 行为 |
|---|---|---|
| 摄像头序号 | `0` | 本地摄像头；打开失败会提示权限问题 |
| RTSP/RTSPs | `rtsp://user:pass@host/live` | 断流后每 2 秒自动重连 |
| 视频文件 | `demo.mp4` | 播放到结尾退出 |
| 单张图片 | `fire01.jpg` | 处理一帧后输出结果并退出 |

交互：`ESC` / `q` 退出；`--headless` 关闭窗口（服务器/无桌面环境必需）。

控制台逐帧输出：

```text
inference frame=132 raw=3 filtered=1 level=warning_fire infer_ms=1187.4
```

`raw` 是模型原始框数，`filtered` 是门控后框数，`level` 是告警等级，
`infer_ms` 是本帧「推理 + 后处理」耗时。

Python 侧通过 `ctypes` 直接调用 C ABI 的示例见
[`tests/sample/python/ctypes_fire_filter.py`](../tests/sample/python/ctypes_fire_filter.py)。

RTSP 服务的 HTTP 接口（`/health`、`/v1/streams` 等）见
[服务文档](../apps/cv_fire_vision_service/README.md)与
[openapi.yaml](../apps/cv_fire_vision_service/openapi.yaml)。

---

## 12. 常见问题排查

### `CVSDK_BUFFER_TOO_SMALL` 是不是出错了？

不是。首次以 `items = NULL` 调用时，只要有检测结果就会返回该状态码，
这是容量查询协议的**正常流程**。详见[第 5.3 节](#53-缓冲区查询协议)。

### 创建检测器返回 `CVSDK_NOT_FOUND`

`CVSDK_GetLastError()` 会给出缺失的确切路径：

```text
ONNX artifact not found: models/fire_smoke_640/artifacts/onnxruntime/model.onnx
```

确认模型包目录结构完整。注意 `.onnx` 默认不进入 Git，
需要从模型制品库获取后放入 `artifacts/onnxruntime/`。

### 创建检测器返回 `CVSDK_UNSUPPORTED`

`backend` 字符串不受支持（错误信息 `requested backend is not built`）。
有效值为 `"mock"`、`"onnxruntime"`、`"onnx"`，或传 `NULL`（等价 `"mock"`）。

### 有检测框但 `level` 一直是 `NONE`

按顺序检查：

1. **类别标识**：`class_id` 是否为 `0`/`1`？其他值不参与告警。
2. **面积过滤**：框是否太小？`min_area_ratio` 默认 `0.0005`，
   在 1920×1080 上约等于 1040 像素面积。
3. **置信度**：`max_fire_confidence` / `max_smoke_confidence` 是否达到
   `fire_confirm_conf` / `smoke_confirm_conf`？
4. **命中数**：`fire_hits` / `smoke_hits` 是否达到对应的 `*_min_hits`？
   告警需要**连续多帧**，单帧命中只会给出 `INFO`。
5. **分数来源**：注意 `max_*_confidence` 是**滑窗内的最大值**，
   即使当前帧没有框，只要窗口内还有命中就可能保持非零。

### 烟雾框在启动初期被大量丢弃

这是预期行为。烟雾门控需要一个「至少 1.6 秒之前的历史帧」作为长基线参考帧。
在服务启动后的最初约 1.6 秒内，所有未达到 `bypass_conf` 的烟雾候选框都会被丢弃
（内部计为 `warmup`）。视频重连后同理，需要重新积累。

如果这一延迟不可接受，可以：

- 提高 `bypass_conf`（高置信烟雾框会跳过门控）；
- 或临时关闭 `smoke_rules.static_gate.enabled`（会显著增加误报）。

### 所有烟雾框在同一帧被丢弃

检查该帧的全图运动是否超过 `global_motion_max_ratio`（默认 `0.35`）。
摄像机被风吹动、云台转动、画面大幅变化时，该帧的全部烟雾证据会被丢弃，
以避免把整体位移误判为烟雾扩散。

### 规则文件加载失败

`CVSDK_GetLastError()` 会指出具体字段与原因，例如：

| 错误文本 | 处理 |
|---|---|
| `schema_version must equal 1` | 补上或修正 `schema_version` |
| `expected number [0,1] at <路径>` | 字段缺失、超范围或类型不符 |
| `expected positive integer at <路径>` | 整数字段为 0、负数或小数 |
| `invalid temporal rule relationship` | 违反[第 8.3 节](#83-校验规则)的交叉约束 |
| `invalid fire_rules.color_gate configuration` | `enabled` 非布尔，或 `min_fraction` 非法 |

注意：**必填字段不能省略**，省略不会回退到内置默认值，而是直接失败。

### 多路视频告警互相串扰

几乎可以确定是多个视频流共享了同一个 `CVSDK_FireSmokeProcessor` 实例。
请为每路视频流创建独立实例，见[第 4.4 节](#44-有状态与无状态对象)。

### 运行时报找不到动态库

安装到自定义前缀后，运行时可能找不到 `libonnxruntime` 或 OpenCV。
参见[第 3.4 节](#34-运行时依赖)的处理方式。

### 日志没有任何输出

按顺序检查：

1. `min_level` 是否高于你期望看到的等级（默认 `WARN`）。
2. `file_path` 目录是否可写——**写盘失败是静默的**，可以先用回调验证。
3. 回调是否被真实触发（回调运行在日志线程，不在推理线程）。
4. `max_file_bytes > 0` 但 `max_rotated_files == 0`：不会轮转，
   但也不应导致「没有输出」，可作为排除项。
5. 是否在回调里做了阻塞操作，导致日志线程卡住。

### 结构体相关崩溃或 `CVSDK_INVALID_ARGUMENT`

- 确认每个结构体的 `struct_size` 都设为 `sizeof(结构体)`。
- 确认 `reserved` 字段已置 0。
- 确认头文件版本与动态库版本一致（用 `CVSDK_GetApiVersion()` 校验）。
- 若在 C++ 中 `delete` 了 SDK 返回的字符串指针，会导致崩溃——
  这些指针归 SDK 所有，不得释放。

---

## 13. 版本与兼容性

### 13.1 兼容性承诺

| 维度 | 承诺 |
|---|---|
| C ABI 结构体 | 只追加字段，不改变已有字段的顺序与含义 |
| 枚举值 | 已有取值不变；新值追加在末尾 |
| 函数签名 | 已发布函数不改变参数与返回类型 |
| 二进制兼容 | 同一 `CVSDK_API_VERSION` 内向下兼容 |
| 动态库 SO 版本 | `SOVERSION 0`（当前处于 MVP，未承诺稳定 soname） |

接入方应在初始化时校验：

```c
if (CVSDK_GetApiVersion() != 2) {
  fprintf(stderr, "unexpected SDK ABI version %u\n", CVSDK_GetApiVersion());
}
```

### 13.2 当前已知限制汇总

以下限制是**已确认的实现现状**，不是使用方式问题：

| 限制 | 影响 | 当前应对 |
|---|---|---|
| 无 GPU 接口 | 只有 CPU 推理 | 更换带 provider 的 ONNX Runtime 制品并改造后端实现 |
| manifest 不参与校验 | `sha256`、类别顺序、输入尺寸契约不被强制 | 人工核对模型包与代码约定 |
| 后端选择为硬编码字符串 | 新增后端需修改 `Detector::Init` | 按需扩展，暂无插件机制 |
| 无 `cv_alg_libraryConfig.cmake` | `find_package` 不可用 | 显式 `include` targets 文件 |
| 未设置安装 RPATH | 自定义前缀部署需配 `LD_LIBRARY_PATH` | 构建时加 `CVSDK_INSTALL_RUNTIME_DEPS` 并设置环境变量 |
| 日志 `sdk_version` 为硬编码字符串 | 日志中的版本号不随构建自动更新 | 以 `CVSDK_GetApiVersion()` 与包名版本为准 |
| 后端内部阈值固定 | ONNX 后端预筛选阈值 0.10、NMS IoU 0.45 不可配置 | 通过 `DetectorOptions.score_threshold` 与后处理阈值间接调节 |
| 目录名为占位 | `src/algo/{ocr,measure,tracking,...}` 仅含说明文档 | 等待后续版本实现 |

### 13.3 升级注意事项

升级 SDK 时：

1. 用 `CVSDK_GetApiVersion()` 校验 ABI 版本是否变化。
2. 重新编译调用方（结构体大小变化会影响 `struct_size` 校验）。
3. 模型包与规则文件通常无需改动，但请核对 `schema_version`
   （当前必须为 `1`，若未来提升会在 `CVSDK_GetLastError()` 中明确报错）。
4. 若替换了 ONNX Runtime 版本，请重新跑一遍火情回归测试
   （`cv_sdk_fire_regression_test`）。

---

## 14. 附录

### 附录 A 状态码

| 状态码 | 值 | `CVSDK_StatusMessage()` | 典型场景 |
|---|---|---|---|
| `CVSDK_OK` | 0 | `success` | 成功 |
| `CVSDK_INVALID_ARGUMENT` | 1 | `invalid argument` | 参数为空、`struct_size` 不足、配置校验失败 |
| `CVSDK_NOT_FOUND` | 2 | `not found` | 模型包/制品/规则文件不存在 |
| `CVSDK_UNSUPPORTED` | 3 | `unsupported` | 后端字符串不支持、模型输出格式不支持 |
| `CVSDK_OUT_OF_MEMORY` | 4 | `out of memory` | 内存分配失败 |
| `CVSDK_INTERNAL_ERROR` | 5 | `internal error` | 后端异常，详情见 `CVSDK_GetLastError()` |
| `CVSDK_BUFFER_TOO_SMALL` | 6 | `output buffer too small` | 容量不足或首次查询（正常流程） |

### 附录 B 枚举值

**`CVSDK_PixelFormat`**

| 值 | 名称 |
|---|---|
| 1 | `CVSDK_PIXEL_FORMAT_BGR8` |
| 2 | `CVSDK_PIXEL_FORMAT_RGB8` |
| 3 | `CVSDK_PIXEL_FORMAT_GRAY8`（检测器可用，后处理不可用） |

**`CVSDK_LogLevel`**

| 值 | 名称 | 说明 |
|---|---|---|
| 0 | `CVSDK_LOG_TRACE` | 受重复抑制 |
| 1 | `CVSDK_LOG_DEBUG` | 受重复抑制 |
| 2 | `CVSDK_LOG_INFO` | 受重复抑制 |
| 3 | `CVSDK_LOG_WARN` | 受重复抑制；默认最低输出等级 |
| 4 | `CVSDK_LOG_ERROR` | 不受抑制、不被驱逐 |
| 5 | `CVSDK_LOG_FATAL` | 不受抑制、不被驱逐 |
| 6 | `CVSDK_LOG_OFF` | 作为 `CVSDK_Log` 的等级参数时会被忽略 |

**`CVSDK_FireAlertLevel`** — 见[第 9.1 节](#91-等级定义)。

### 附录 C 完整调用序列

```text
进程启动
  ├── (可选) CVSDK_ConfigureLogging
  ├── (可选) CVSDK_GetApiVersion 校验
  │
  ├── 每路视频流：
  │     ├── CVSDK_DetectorCreate(model_package, options)
  │     ├── CVSDK_FireSmokeProcessorCreate(fire_rules.json)
  │     └── 启动工作线程
  │           └── 逐帧循环：
  │                 ├── 取流 → 解码为 BGR8 缓冲
  │                 ├── CVSDK_DetectorInfer
  │                 ├── CVSDK_FireSmokeProcessorProcess
  │                 └── 消费 filtered 框与告警状态
  │
  └── 进程退出（收到信号或正常结束）
        ├── 置停止标志 → join 工作线程
        ├── CVSDK_FireSmokeProcessorDestroy(processor)
        └── CVSDK_DetectorDestroy(detector)
```

### 附录 D 术语表

| 术语 | 含义 |
|---|---|
| 模型包（model package） | 包含 `manifest.json` 与后端制品的目录 |
| 制品（artifact） | 后端可加载的模型文件，如 `artifacts/onnxruntime/model.onnx` |
| 句柄（handle） | 不透明指针类型，如 `CVSDK_Detector*` |
| 门控（gate） | 基于图像内容的后处理过滤规则，如火焰颜色门控 |
| 轨迹（track） | 时序层中跨帧关联的同一目标 |
| 滑窗（window） | 轨迹保存的最近 N 帧置信度序列 |
| 代表轨迹 | 命中数最多（其次置信度最高）的轨迹，其统计量决定告警等级 |

### 附录 E 修订记录

| 日期 | 版本 | 说明 |
|---|---|---|
| 2026-09-11 | 0.1.0 | 初版，覆盖检测与火情后处理全部已实现 API |
