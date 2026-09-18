# Leak YOLOv8n-Seg 1280 Model

- Source artifact: `leak_detection/models/leak_yolov8n_seg.onnx`
- Version: `leak-yolov8n-seg_1280_v1`
- Input: fixed `1280x1280`, NCHW, RGB tensor, values normalized to `[0, 1]`
- Outputs:
  - `output0` = `[1, 37, 33600]`：4 个框分量 + 1 个类别置信度 + 32 个掩码系数
  - `output1` = `[1, 32, 320, 320]`：32 张掩码原型
- Classes: `0=liquid_stain`
- SHA-256: `e5f5f1800e6cce2f6e9492716a2b344bfd94a0e4e64b604ab2ec63245ba4d8bf`
- Training/data licensing and evaluation details remain in the source project's model card and must be reviewed before commercial redistribution.

## Mask access

Instance masks are **not** part of the per-item C ABI struct; they are copied out separately:

```c
CVSDK_Status CVSDK_LeakProcessorCopyMask(CVSDK_LeakProcessor* processor, uint32_t index,
                                         uint8_t* buffer, uint32_t capacity,
                                         uint32_t* out_width, uint32_t* out_height,
                                         uint32_t* out_stride);
```

- Pass `buffer = NULL` to query `width`/`height`; the call returns `CVSDK_BUFFER_TOO_SMALL`.
- Allocate `width * height` bytes and call again with the real buffer; `out_stride` equals the
  width, so the buffer is tightly packed.
- Format is CV_8UC1 semantics: `0` is background, `255` is liquid stain, and the mask has the
  same size as the input image.
- Masks are valid only for the current frame: the next `CVSDK_LeakProcessorProcess` or
  `CVSDK_LeakProcessorReset` invalidates them.
- Copying a mask never advances the temporal sliding window, so it is safe to call for
  visualization after `Process`.

## Runtime false-positive suppression

The SDK applies candidate thresholding, target-level NMS, mask-area filtering and temporal
confirmation after model inference. Leak confirmation requires both window hits and a growing
mask area or a downward-moving centroid. These are runtime rules, not model behavior, and are
configured in `leak_rules.json`.
