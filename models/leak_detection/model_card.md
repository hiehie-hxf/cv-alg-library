# Leak Detection YOLOv8n-Seg Model

- Source artifact: `runs/segment/train/weights/best.onnx`
- Version: `leak-yolov8n-seg-best_1280_v1`
- Input: fixed 1280×1280, NCHW, RGB tensor, values normalized to `[0, 1]`
- Classes: `0=liquid_stain`
- SHA-256: `e5f5f1800e6cce2f6e9492716a2b344bfd94a0e4e64b604ab2ec63245ba4d8bf`
- Training/evaluation details remain in the source project's training log.

## Model summary

YOLOv8n-Seg based pixel-level segmentation for industrial pump leak detection.
Single class: liquid stain (soy sauce, oil, water).

## Performance

- Mask mAP50: 0.888
- Mask Recall: 0.909
- Inference speed: ~4.0 ms on RTX 3090
- Model size: 13.2 MB (ONNX)

## Runtime false-positive suppression

The SDK applies target-level IOU association and temporal confirmation after
model inference. Leak confirmation requires both window hits and strong-confidence
hits within a sliding window. Area growth and centroid displacement are used to
distinguish real flowing leaks from static stains and reflections. These are
runtime rules, not model behavior, and are configured in `leak_rules.json`.

## Limitations

- Transparent water leaks have low single-frame recall in RGB; frame differencing
  or polarization may be required.
- Static oil stains may be confused with real leaks; temporal filtering is required.
- Generalization to new pump sites requires site-specific data collection and
  fine-tuning.