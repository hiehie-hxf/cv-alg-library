#include "postprocess.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <opencv2/imgproc.hpp>

namespace leak {
namespace {

constexpr int kNumClasses = 1;  // 单类版本：liquid_stain

float Sigmoid(float v) {
  return 1.f / (1.f + std::exp(-v));
}

float IntersectionOverUnion(const cv::Rect2f& a, const cv::Rect2f& b) {
  const float left = std::max(a.x, b.x);
  const float top = std::max(a.y, b.y);
  const float right = std::min(a.x + a.width, b.x + b.width);
  const float bottom = std::min(a.y + a.height, b.y + b.height);

  const float inter = std::max(0.f, right - left) * std::max(0.f, bottom - top);
  const float uni = a.width * a.height + b.width * b.height - inter;
  return uni > 0.f ? inter / uni : 0.f;
}

int ClampInt(int v, int lo, int hi) {
  return std::max(lo, std::min(v, hi));
}

struct Detection {
  float conf = 0.f;
  cv::Rect2f box;
  int index = 0;  // num_preds 方向的列索引
};

}  // namespace

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
                                         float mask_thres) {
  std::vector<LeakItem> results;

  if (output0 == nullptr || output1 == nullptr || shape0 == nullptr || shape1 == nullptr) {
    return results;
  }
  if (ratio <= 0.f || orig_size.width <= 0 || orig_size.height <= 0) {
    return results;
  }

  // output0: [1, dims, num_preds]
  const int num_dims = static_cast<int>(shape0[1]);
  const int num_preds = static_cast<int>(shape0[2]);
  // output1: [1, 32, proto_h, proto_w]
  const int proto_c = static_cast<int>(shape1[1]);
  const int proto_h = static_cast<int>(shape1[2]);
  const int proto_w = static_cast<int>(shape1[3]);

  const int num_masks = num_dims - 4 - kNumClasses;
  if (num_preds <= 0 || num_masks <= 0) {
    return results;
  }
  if (proto_c < num_masks || proto_h <= 0 || proto_w <= 0) {
    return results;
  }

  const float img_w = static_cast<float>(orig_size.width);
  const float img_h = static_cast<float>(orig_size.height);
  const float fpad_x = static_cast<float>(pad_x);
  const float fpad_y = static_cast<float>(pad_y);

  // ---- 1~3. 置信度过滤 + xywh -> xyxy + 映射回原图 ----
  // 布局是 [batch, dims, num_preds]，所以第 d 维第 i 个候选是 d * num_preds + i。
  std::vector<Detection> dets;
  for (int i = 0; i < num_preds; ++i) {
    const float conf = output0[4 * num_preds + i];
    if (conf < conf_thres) {
      continue;
    }

    const float cx = output0[0 * num_preds + i];
    const float cy = output0[1 * num_preds + i];
    const float bw = output0[2 * num_preds + i];
    const float bh = output0[3 * num_preds + i];

    float x1 = (cx - bw * 0.5f - fpad_x) / ratio;
    float y1 = (cy - bh * 0.5f - fpad_y) / ratio;
    float x2 = (cx + bw * 0.5f - fpad_x) / ratio;
    float y2 = (cy + bh * 0.5f - fpad_y) / ratio;

    x1 = std::max(0.f, std::min(x1, img_w));
    y1 = std::max(0.f, std::min(y1, img_h));
    x2 = std::max(0.f, std::min(x2, img_w));
    y2 = std::max(0.f, std::min(y2, img_h));

    if (x2 <= x1 || y2 <= y1) {
      continue;
    }

    Detection det;
    det.conf = conf;
    det.box = cv::Rect2f(x1, y1, x2 - x1, y2 - y1);
    det.index = i;
    dets.push_back(det);
  }

  if (dets.empty()) {
    return results;
  }

  // ---- 4. NMS：按 conf 降序贪心抑制 ----
  std::sort(dets.begin(), dets.end(),
            [](const Detection& a, const Detection& b) { return a.conf > b.conf; });

  std::vector<int> keep;
  std::vector<char> removed(dets.size(), 0);
  for (std::size_t a = 0; a < dets.size(); ++a) {
    if (removed[a]) {
      continue;
    }
    keep.push_back(static_cast<int>(a));
    for (std::size_t b = a + 1; b < dets.size(); ++b) {
      if (removed[b]) {
        continue;
      }
      if (IntersectionOverUnion(dets[a].box, dets[b].box) > iou_thres) {
        removed[b] = 1;
      }
    }
  }

  // ---- 5. 逐个检测生成掩码（裁剪到框内，参考 ultralytics process_mask）----
  // 网络输入画布尺寸由原图尺寸、缩放比和 padding 反推，签名里无需额外传 input_w/h。
  const float input_w = static_cast<float>(std::lround(static_cast<double>(orig_size.width) * ratio)) +
                        2.f * fpad_x;
  const float input_h = static_cast<float>(std::lround(static_cast<double>(orig_size.height) * ratio)) +
                        2.f * fpad_y;
  if (input_w <= 0.f || input_h <= 0.f) {
    return results;
  }

  // proto 分辨率相对网络输入的缩放系数
  const float sx = static_cast<float>(proto_w) / input_w;
  const float sy = static_cast<float>(proto_h) / input_h;

  const std::size_t proto_plane = static_cast<std::size_t>(proto_h) * proto_w;
  results.reserve(keep.size());

  for (const int k : keep) {
    const Detection& det = dets[static_cast<std::size_t>(k)];

    std::vector<float> coefs(static_cast<std::size_t>(num_masks));
    for (int m = 0; m < num_masks; ++m) {
      coefs[static_cast<std::size_t>(m)] =
          output0[(4 + kNumClasses + m) * num_preds + det.index];
    }

    // mask_raw[y, x] = sigmoid(sum_m coefs[m] * proto[m, y, x])
    cv::Mat mask_raw(proto_h, proto_w, CV_32F);
    for (int y = 0; y < proto_h; ++y) {
      float* row = mask_raw.ptr<float>(y);
      for (int x = 0; x < proto_w; ++x) {
        const std::size_t offset = static_cast<std::size_t>(y) * proto_w + x;
        float sum = 0.f;
        for (int m = 0; m < num_masks; ++m) {
          sum += coefs[static_cast<std::size_t>(m)] *
                 output1[static_cast<std::size_t>(m) * proto_plane + offset];
        }
        row[x] = Sigmoid(sum);
      }
    }

    // 目标在原图中的像素范围
    const int bx1 = ClampInt(static_cast<int>(std::floor(det.box.x)), 0, orig_size.width - 1);
    const int by1 = ClampInt(static_cast<int>(std::floor(det.box.y)), 0, orig_size.height - 1);
    const int bx2 = ClampInt(static_cast<int>(std::ceil(det.box.x + det.box.width)),
                             bx1 + 1, orig_size.width);
    const int by2 = ClampInt(static_cast<int>(std::ceil(det.box.y + det.box.height)),
                             by1 + 1, orig_size.height);

    // 同一块区域在网络输入坐标下的位置，再换算到 proto 分辨率
    const int px1 = ClampInt(static_cast<int>(std::floor((bx1 * ratio + fpad_x) * sx)),
                             0, proto_w - 1);
    const int py1 = ClampInt(static_cast<int>(std::floor((by1 * ratio + fpad_y) * sy)),
                             0, proto_h - 1);
    const int px2 = ClampInt(
        static_cast<int>(std::ceil((bx2 * ratio + fpad_x) * sx)), px1 + 1, proto_w);
    const int py2 = ClampInt(
        static_cast<int>(std::ceil((by2 * ratio + fpad_y) * sy)), py1 + 1, proto_h);

    // 只取框内区域放大，避免框外残余响应污染 area 和质心
    const cv::Mat crop = mask_raw(cv::Rect(px1, py1, px2 - px1, py2 - py1));
    cv::Mat crop_scaled;
    cv::resize(crop, crop_scaled, cv::Size(bx2 - bx1, by2 - by1), 0, 0, cv::INTER_LINEAR);

    cv::Mat crop_bin;
    cv::threshold(crop_scaled, crop_bin, mask_thres, 255, cv::THRESH_BINARY);
    crop_bin.convertTo(crop_bin, CV_8UC1);

    // 贴进原图尺寸的空白掩码，框外一律为 0
    cv::Mat full_mask = cv::Mat::zeros(orig_size.height, orig_size.width, CV_8UC1);
    crop_bin.copyTo(full_mask(cv::Rect(bx1, by1, bx2 - bx1, by2 - by1)));

    LeakItem item;
    item.confidence = det.conf;
    item.box = det.box;
    item.mask = full_mask;
    item.area = cv::countNonZero(item.mask);
    results.push_back(item);
  }

  return results;
}

}  // namespace leak
