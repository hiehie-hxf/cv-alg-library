# Fire/Smoke YOLOv8n 640 Model

- Source artifact: `robot_fire_vision/models/fire-smoke-yolov8n-best.onnx`
- Version: `fire-smoke-yolov8n-best_640_v1`
- Input: fixed `640x640`, NCHW, RGB tensor, values normalized to `[0, 1]`
- Classes: `0=smoke`, `1=fire`
- SHA-256: `696fda05ea3b584a949bd32eb189723fdf50096c7069d1118bd9d2196612abab`
- Training/data licensing and evaluation details remain in the source project's model card and must be reviewed before commercial redistribution.

## Runtime false-positive suppression

The SDK applies target-level IoU association and temporal confirmation after model inference.
Fire confirmation requires both window hits and strong-confidence hits; critical fire requires
consecutive high-confidence frames. Smoke evidence is suppressed during large global frame motion.
These are runtime rules, not model behavior, and are configured in `fire_rules.json`.
