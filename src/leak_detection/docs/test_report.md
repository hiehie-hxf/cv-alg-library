# 漏液检测 — 测试报告

## 1. 测试环境

| 项 | 版本 / 值 |
|---|---|
| 主机 | kemove-Z790-UD (Linux x86_64) |
| 编译器 | GNU g++ 11.4.0 |
| CMake | 3.22.1 |
| OpenCV | 4.5.4 |
| ONNX Runtime | 1.20.1（CPU） |
| 模型 | models/leak_yolov8n_seg.onnx |
| 输入 | images [1, 3, 1280, 1280] |
| 输出 | output0 [1, 37, 33600]、output1 [1, 32, 320, 320] |
| 测试图像 | data/images/test/26.jpg … 34.jpg（9 张，4096x3072） |

## 2. 构建与单元测试

```
$ cd build && ctest --output-on-failure

    Start 1: test_leak_filter
1/2 Test #1: test_leak_filter .................   Passed    0.00 sec
    Start 2: test_postprocess
2/2 Test #2: test_postprocess .................   Passed    0.02 sec

100% tests passed, 0 tests failed out of 2
Total Test time (real) =   0.02 sec
```

**结论：2/2 通过。**

### 2.1 test_postprocess

用合成张量构造已知答案，覆盖 5 项行为：

| 序号 | 覆盖点 | 断言 |
|---|---|---|
| 1 | 空指针防护 | 传入 nullptr 返回空 vector，不崩溃 |
| 2 | 置信度过滤 | 8 个 conf=0.10 的候选（阈值 0.25）被丢弃 |
| 3 | NMS | 两个完全重叠的候选（IoU=1.0 > 0.5）只保留 conf 高的 |
| 4 | letterbox 反变换 | 框精确还原为 (540, 590, 200, 100) |
| 5 | 掩码裁剪到框内 | area == 框面积，且框外像素数 == 0 |

实际输出：

```
detections = 1
  conf=0.9 box=[540,590,200,100] area=20000 mask=1280x1280 type=0
area=20000 expected=20000 leaked_outside_box=0
test_postprocess passed
```

第 5 项的 `leaked_outside_box` 是专门加的断言：把框区域清零后统计剩余非零像素，
必须为 **0**。这直接证明掩码没有渗出框外——而不是只看总数变小。

（作为对照：改为「整张 320x320 直接缩放到原图」的旧方案时，
同样的输入会得到 `area=414736`，即框外被点亮了约 39 万像素。）

### 2.2 test_leak_filter

```
hit_count = 0
test_leak_filter passed
```

单帧空输入时窗口未满，`alert=false`、`hit_count=0`，行为符合预期。

## 3. 端到端逐帧结果（9 帧连续序列）

命令（多图模式，共用同一个 detector 实例，时序状态连续累积）：

```bash
./build/leak_detect models/leak_yolov8n_seg.onnx <config> \
  data/images/test/26.jpg data/images/test/27.jpg data/images/test/28.jpg \
  data/images/test/29.jpg data/images/test/30.jpg data/images/test/31.jpg \
  data/images/test/32.jpg data/images/test/33.jpg data/images/test/34.jpg
```

> **配置说明**：正式配置 `config/leak_rules.json` 的 `window=10`，而测试集只有 9 张图，
> 窗口填不满，判定分支不会执行。因此本节使用临时配置 `window=3, min_hits=2`
> （其余参数与正式配置一致），以便观察到判定行为。

| frame | 图片 | Detected | Alert | hits | growing | down |
|---|---|---|---|---|---|---|
| 0 | 26.jpg | 2 | NO | 1 | 0 | 0 |
| 1 | 27.jpg | 0 | NO | 1 | 0 | 0 |
| 2 | 28.jpg | 1 | **YES** | 2 | 0 | 1 |
| 3 | 29.jpg | 1 | **YES** | 2 | 1 | 1 |
| 4 | 30.jpg | 1 | NO | 3 | 0 | 0 |
| 5 | 31.jpg | 1 | **YES** | 3 | 0 | 1 |
| 6 | 32.jpg | 1 | **YES** | 3 | 1 | 1 |
| 7 | 33.jpg | 0 | NO | 2 | 0 | 0 |
| 8 | 34.jpg | 0 | NO | 1 | 0 | 0 |

### 3.1 检测明细

| frame | conf | box (x, y, w, h) | area |
|---|---|---|---|
| 0 | 0.478857 | 778.5, 316.3, 1973.1, 2605.6 | 836510 |
| 0 | 0.393378 | 1488.9, 1028.1, 1151.9, 1876.9 | 631348 |
| 2 | 0.280376 | 1909.1, 1860.2, 1360.0, 312.0 | 150153 |
| 3 | 0.569563 | 974.4, 563.7, 1664.7, 1478.7 | 1587633 |
| 4 | 0.795170 | 1752.7, 1655.3, 440.2, 202.8 | 51517 |
| 5 | 0.543409 | 897.3, 722.5, 1718.6, 2253.8 | 493378 |
| 6 | 0.761749 | 1658.4, 1435.3, 1613.9, 1309.0 | 1074887 |

目视核对（29.jpg）见 `docs/result_preview.jpg`：掩码紧贴液渍实际边界，
框外无渗出；已确认原图 4096x3072 经 `ratio=0.3125`、`pad_y=160` 的 letterbox 反变换正确。

> 注：9 张测试图是**独立的现场照片**，并非真实连续视频帧。
> 因此本节的 `growing` / `down` 变化只用于验证**判定逻辑被正确触发**，
> 不代表真实的漏液发展趋势。

## 4. 时序判定逻辑验证

单独构造已知面积与质心的掩码，精确验证判定链路
（配置：`window=3, min_hits=2, area_growth_ratio=1.15, centroid_down_px=5.0`）：

```
[A] 预热期：窗口未满时 hit_count 应如实累加
  frame1 (无检出): alert=0 hits=0
  frame2 (area=4000 cy=104.5): alert=0 hits=1
[B] 窗口满：面积增长 + 质心下移 -> 告警
  frame3 (area=8000 cy=209.5): alert=1 hits=2 growing=1 down=1
[C] Reset：应清空三个队列
  after reset: alert=0 hits=0
[D] 负向：命中数够但面积不涨、质心不动 -> 不得告警
  frame3 (三帧完全相同): alert=0 hits=3 growing=0 down=0
[E] 负向：连续空帧 -> 不得告警
  4 帧空输入: any_alert=0
leak_filter verify PASSED
```

### 4.1 [A] 预热期行为

窗口未满时 `alert` 恒为 `false`，但 `hit_count` **如实反映当前命中数**
（frame2 为 1），而不是恒为 0。这样操作员在预热期就能看到「已经在检出目标，
只是还没到判定窗口」。

### 4.2 [B] 三要素与关系

frame3 同时满足三个条件：

- `hits = 2 >= min_hits(2)`
- `area_growing`：8000 > 4000 x 1.15 = 4600 → `true`
- `centroid_down`：209.5 > 104.5 + 5.0 → `true`

`alert = (hits >= min_hits) && (area_growing || centroid_down)` → `true`

### 4.3 [C] Reset 生效

`Reset()` 后三个队列清空，下一帧回到预热期状态（`alert=0, hits=0`）。
注意 `Reset()` 只清空时序状态，**不卸载已加载的模型**。

### 4.4 [D] 负向测试：命中数够但无趋势 → 不告警

这是最关键的边界条件。连续三帧输入**完全相同**的掩码（area=4000、cy=104.5）：

- `hits = 3`，已经**超过** `min_hits(2)`
- 但 `area_growing = 0`（4000 不大于 4000 x 1.15）、`centroid_down = 0`

结果 `alert = false`。**证明告警并非只看命中数**——静止不动的液渍不会反复误报，
这是由 `&&` 与 `||` 的组合保证的。

### 4.5 [E] 负向测试：连续空帧

4 帧均无检测时 `any_alert = 0`。

## 5. 边界条件汇总

| 场景 | 期望 | 实际 | 结果 |
|---|---|---|---|
| 空指针 / 空张量 | 返回空结果，不崩溃 | 返回空 vector | 通过 |
| 置信度全部低于阈值 | 无检测 | 0 个检测 | 通过 |
| 两个框完全重叠 | NMS 只留 1 个 | 1 个检测 | 通过 |
| 框外区域 | 掩码必须为 0 | `leaked_outside_box=0` | 通过 |
| 窗口未满 | `alert=false`，`hits` 累加 | hits 从 0 → 1 | 通过 |
| 命中够但无趋势 | 不告警 | `alert=false, hits=3` | 通过 |
| 连续空帧 | 不告警 | `any_alert=0` | 通过 |
| Reset 后 | 回到预热期 | `alert=0, hits=0` | 通过 |
| 面积增长 + 质心下移 | 告警 | `alert=true` | 通过 |

## 6. 尚未覆盖 / 已知限制

1. **真实视频流未测试**。当前测试使用的是独立现场照片，非同源连续帧，
   真实趋势判定的准确性需要在视频序列上另行验证。
2. **`window=10` 的正式配置未走完判定分支**。测试集仅 9 帧，不足 10 帧；
   如需覆盖请补足帧数或临时调小 `window`。
3. **多类别未覆盖**。后处理为单类（`nc = 1`）实现。
4. **非对称 padding 未覆盖**。`postprocess` 由 `round(orig * ratio) + 2*pad`
   反推网络输入尺寸，仅对对称 letterbox 成立。
5. **JSON 配置解析的健壮性未做模糊测试**，当前为按 key 定位的简易实现。
