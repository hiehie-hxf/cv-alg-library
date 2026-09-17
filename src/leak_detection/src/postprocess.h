#pragma once

#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>

#include "leak_detector.h"

namespace leak {

// YOLOv8-Seg 后处理（单类，nc = 1）。
//
//   output0 : [1, dims, num_preds] 原始张量，dims 在前、num_preds 在后
//             dims = 4 (cx,cy,w,h) + 1 (cls) + 32 (mask coef)
//   shape0  : output0 的 shape，长度 3
//   output1 : [1, 32, proto_h, proto_w] mask 原型
//   shape1  : output1 的 shape，长度 4
//   orig_size   : 原图尺寸，mask 会缩放到该尺寸
//   ratio       : letterbox 缩放比
//   pad_x/pad_y : letterbox padding 像素
//   conf_thres  : 候选框置信度阈值
//   iou_thres   : NMS 的 IoU 阈值
//   mask_thres  : 掩码二值化阈值
std::vector<LeakItem> PostprocessYoloSeg(const float* output0,
                                         const int64_t* shape0,
                                         const float* output1,
                                         const int64_t* shape1,
                                         const cv::Size& orig_size,
                                         float ratio,
                                         int pad_x,
                                         int pad_y,
                                         float conf_thres,
                                         float iou_thres,
                                         float mask_thres);

}  // namespace leak
