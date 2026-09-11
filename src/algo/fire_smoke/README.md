# 火焰/烟雾任务组件

流水线：`YOLO 原始框(0=smoke, 1=fire) -> 火焰 HSV 颜色门控 -> 烟雾过曝/光晕/静态/软运动门控 -> 面积和多帧告警`。

`FireSmokeProcessor` 只对模型输出的候选框进行过滤和告警确认，不生成额外检测结果。火焰候选使用 OpenCV HSV 色彩门控过滤明显非火焰区域；烟雾候选使用过曝、光晕、静态和软运动规则降低误报。现场参数由 `models/fire_smoke_640/fire_rules.json` 统一管理。
