#include "algo/digital_gauge/digital_gauge_reader.h"

#include "base/status.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <numeric>
#include <opencv2/imgproc.hpp>
#include <set>
#include <string>

namespace cvsdk {
namespace {
constexpr int kShearPad = 24, kPadV = 16, kPadX = 26;
constexpr double kSegmentDilation = 1.75, kDpDiff = .20, kDpMeanRatio = .55;
constexpr double kDpCoreMin = .32;
constexpr int kCellUsedSegments = 2;

// 所有段位窗口均使用单个数字 cell 的归一化坐标，避免绑定绝对分辨率。
struct NormalizedRect {
  double x0, x1, y0, y1;
};
const std::map<char, NormalizedRect> kSegments = {
    {'a', {.220, .820, .055, .165}}, {'f', {.170, .315, .215, .390}},
    {'b', {.695, .840, .215, .390}}, {'g', {.220, .820, .435, .545}},
    {'e', {.170, .315, .620, .800}}, {'c', {.695, .840, .620, .800}},
    {'d', {.220, .820, .855, .950}}, {'p', {.855, 1.00, .790, .990}}};
const NormalizedRect kDpCore{.855, .945, .825, .965};
const std::map<std::string, char> kCodes = {
    {"abcdef", '0'}, {"bc", '1'},     {"abdeg", '2'}, {"abcdg", '3'},   {"bcfg", '4'},
    {"acdfg", '5'},  {"acdefg", '6'}, {"abc", '7'},   {"abcdefg", '8'}, {"abcdfg", '9'}};
const std::array<double, 7> kPitchFactors{.90, .94, .97, 1.0, 1.03, 1.06, 1.10};

struct Grid {
  double pitch = 0, x0 = 0, cy0 = 0, ch = 0, score = -1, lx0 = 0, lx1 = 0;
  int cells = 0;
};
struct Cell {
  int index = 0, hamming = 0, on = 0;
  bool used = false, dot = false;
  char digit = '?';
  double decision = 0, confidence = 0;
  std::string pattern;
};
struct Decoded {
  std::vector<Cell> cells;
  double specificity = 1;
};
struct RowResult {
  cv::Rect bounds;
  Grid grid;
  std::vector<Cell> cells;
  std::string text;
  double value = 0;
  bool has_value = false;
  double confidence = 0, specificity = 0, shear = 0;
  uint32_t flags = 0;
};

cv::Mat CleanBand(const cv::Mat& mask) {
  // 状态灯和小数点高度明显小于数字笔画；粗拟合阶段只保留高连通域。
  cv::Mat labels, stats, centers;
  int count = cv::connectedComponentsWithStats(mask, labels, stats, centers, 8);
  std::vector<cv::Point> points;
  cv::findNonZero(mask, points);
  cv::Mat clean = cv::Mat::zeros(mask.size(), CV_8U);
  if (points.empty())
    return clean;
  auto mm =
      std::minmax_element(points.begin(), points.end(), [](auto a, auto b) { return a.y < b.y; });
  int lit_height = std::max(1, mm.second->y - mm.first->y + 1);
  for (int i = 1; i < count; ++i)
    if (stats.at<int>(i, cv::CC_STAT_HEIGHT) >= .60 * lit_height)
      clean.setTo(1, labels == i);
  return clean;
}

double EstimateShear(const cv::Mat& mask, const DigitalGaugeConfig& config) {
  // 搜索使列投影能量最大的仿射斜切量，将倾斜竖段校正为垂直方向。
  double best = 0, best_score = -1;
  double yc = mask.rows / 2.0;
  for (double shear = config.shear_min; shear <= config.shear_max + 1e-9;
       shear += config.shear_step) {
    cv::Mat warped;
    cv::Matx23d matrix(1, shear, -shear * yc, 0, 1, 0);
    cv::warpAffine(mask, warped, matrix, {mask.cols * 3, mask.rows}, cv::INTER_NEAREST,
                   cv::BORDER_CONSTANT, 0);
    cv::Mat profile;
    cv::reduce(warped, profile, 0, cv::REDUCE_SUM, CV_64F);
    double score = profile.dot(profile);
    if (score > best_score) {
      best_score = score;
      best = shear;
    }
  }
  return best;
}

cv::Mat Shear(const cv::Mat& image, double shear, int interpolation) {
  double yc = image.rows / 2.0;
  cv::Mat output;
  cv::Matx23d matrix(1, shear, -shear * yc + kShearPad, 0, 1, 0);
  cv::warpAffine(image, output, matrix, {image.cols + 2 * kShearPad, image.rows}, interpolation,
                 cv::BORDER_CONSTANT, 0);
  return output;
}

double RectSum(const cv::Mat& integral, double x0, double y0, double x1, double y1,
               int* area = nullptr) {
  int a = std::clamp((int)std::lround(x0), 0, integral.cols - 1);
  int b = std::clamp((int)std::lround(x1), 0, integral.cols - 1);
  int c = std::clamp((int)std::lround(y0), 0, integral.rows - 1);
  int d = std::clamp((int)std::lround(y1), 0, integral.rows - 1);
  if (area)
    *area = std::max(0, b - a) * std::max(0, d - c);
  if (b <= a || d <= c)
    return 0;
  return integral.at<double>(d, b) - integral.at<double>(c, b) - integral.at<double>(d, a) +
         integral.at<double>(c, a);
}

double ScoreGrid(const cv::Mat& ii, double total, int lx0, int lx1, double cy0, double ch,
                 double pitch, double x0, int cells) {
  double purity = 0, coverage = 0;
  int count = 0;
  for (int ci = 0; ci < cells; ++ci) {
    double cx = x0 + ci * pitch;
    if (cx + pitch < lx0 - 2 || cx > lx1 + 2)
      continue;
    for (const auto& entry : kSegments) {
      auto r = entry.second;
      int area = 0;
      double sum = RectSum(ii, cx + r.x0 * pitch, cy0 + r.y0 * ch, cx + r.x1 * pitch,
                           cy0 + r.y1 * ch, &area);
      if (!area)
        continue;
      double fraction = sum / area;
      purity += std::max(fraction, 1 - fraction);
      coverage += sum;
      ++count;
    }
  }
  if (!count || total <= 0)
    return -1;
  return purity / count * std::min(1.0, coverage / total);
}

bool FitGrid(const cv::Mat& band, int min_digits, int max_digits, Grid* result) {
  // 先粗搜 pitch/相位/行高/位数，再围绕最优解以更细步长局部搜索。
  cv::Mat ii;
  cv::integral(band, ii, CV_64F);
  double total = cv::sum(band)[0];
  std::vector<cv::Point> points;
  cv::findNonZero(band, points);
  if (points.empty())
    return false;
  int lx0 = points[0].x, lx1 = points[0].x, ly0 = points[0].y, ly1 = points[0].y;
  for (auto p : points) {
    lx0 = std::min(lx0, p.x);
    lx1 = std::max(lx1, p.x);
    ly0 = std::min(ly0, p.y);
    ly1 = std::max(ly1, p.y);
  }
  int h0 = ly1 - ly0;
  if (h0 < 10)
    return false;
  Grid best;
  auto search = [&](double p0, double p1, double pstep, int off_step,
                    const std::vector<int>& margins) {
    for (int margin : margins) {
      double cy = ly0 - margin, ch = h0 + 2 * margin;
      if (ch < 12)
        continue;
      for (double pitch = p0; pitch < p1 + 1e-9; pitch += pstep) {
        int pw = std::lround(pitch);
        if (pw < 8)
          continue;
        for (int nc = min_digits; nc <= max_digits; ++nc)
          for (int off = 0; off < pw; off += off_step) {
            double x0 = lx0 - off, score = ScoreGrid(ii, total, lx0, lx1, cy, ch, pitch, x0, nc);
            if (score > best.score)
              best = {pitch, x0, cy, ch, score, (double)lx0, (double)lx1, nc};
          }
      }
    }
  };
  double lo = .60 * h0, hi = 1.10 * h0, step = std::max(1.0, (hi - lo) / 60.0);
  search(lo, hi, step, 2, {0, 4, 8, 12, 16, 20, 24});
  if (best.score < 0)
    return false;
  Grid coarse = best;
  std::vector<int> margins;
  int inferred_margin = std::max(0, (int)std::lround((coarse.ch - h0) / 2));
  for (int d : {-4, -2, 0, 2, 4})
    margins.push_back(std::max(0, inferred_margin + d));
  search(std::max(8.0, coarse.pitch - 3), coarse.pitch + 3, .25, 1, margins);
  best.lx0 = lx0;
  best.lx1 = lx1;
  *result = best;
  return true;
}

std::pair<char, int> Nearest(const std::string& pattern) {
  char best = '?';
  int distance = 99;
  for (const auto& entry : kCodes) {
    int d = 0;
    for (char c : std::string("abcdefg"))
      d += (entry.first.find(c) == std::string::npos) != (pattern.find(c) == std::string::npos);
    if (d < distance) {
      distance = d;
      best = entry.second;
    }
  }
  return {best, distance};
}

Decoded Decode(const cv::Mat& mask_ii, const cv::Mat& intensity_ii, const Grid& grid,
               double on_fraction) {
  // 积分图让每个候选网格的七段占比和邻域亮度都能以 O(1) 计算。
  Decoded out;
  std::vector<double> ratios;
  for (int ci = 0; ci < grid.cells; ++ci) {
    double cx = grid.x0 + ci * grid.pitch;
    if (cx > grid.lx1 + .15 * grid.pitch || cx + grid.pitch < grid.lx0 - .15 * grid.pitch)
      continue;
    std::map<char, double> fractions, means;
    int on = 0;
    for (const auto& entry : kSegments) {
      char key = entry.first;
      auto r = entry.second;
      int area = 0;
      double sum = RectSum(mask_ii, cx + r.x0 * grid.pitch, grid.cy0 + r.y0 * grid.ch,
                           cx + r.x1 * grid.pitch, grid.cy0 + r.y1 * grid.ch, &area);
      double intensity = RectSum(intensity_ii, cx + r.x0 * grid.pitch, grid.cy0 + r.y0 * grid.ch,
                                 cx + r.x1 * grid.pitch, grid.cy0 + r.y1 * grid.ch);
      fractions[key] = area ? sum / area : 0;
      means[key] = area ? intensity / area : 0;
      if (key != 'p' && fractions[key] > on_fraction) {
        ++on;
        double mx = (r.x0 + r.x1) / 2, my = (r.y0 + r.y1) / 2;
        int oa = 0;
        double outer =
            RectSum(intensity_ii, cx + (mx + (r.x0 - mx) * kSegmentDilation) * grid.pitch,
                    grid.cy0 + (my + (r.y0 - my) * kSegmentDilation) * grid.ch,
                    cx + (mx + (r.x1 - mx) * kSegmentDilation) * grid.pitch,
                    grid.cy0 + (my + (r.y1 - my) * kSegmentDilation) * grid.ch, &oa);
        if (oa > area && means[key] > 1)
          ratios.push_back(((outer - intensity) / (oa - area)) / (means[key] + 1e-6));
      }
    }
    Cell cell;
    cell.index = ci;
    cell.on = on;
    cell.used = on >= kCellUsedSegments;
    if (cell.used)
      for (char key : std::string("abcdefg"))
        if (fractions[key] > on_fraction)
          cell.pattern += key;
    auto exact = kCodes.find(cell.pattern);
    if (cell.used && exact != kCodes.end())
      cell.digit = exact->second;
    else if (cell.used) {
      auto near = Nearest(cell.pattern);
      cell.digit = near.first;
      cell.hamming = near.second;
    }
    // 小数点必须同时满足核心亮、核心比环带亮、且相对数字笔画足够亮。
    int ca = 0, wa = 0;
    double core = RectSum(mask_ii, cx + kDpCore.x0 * grid.pitch, grid.cy0 + kDpCore.y0 * grid.ch,
                          cx + kDpCore.x1 * grid.pitch, grid.cy0 + kDpCore.y1 * grid.ch, &ca);
    auto p = kSegments.at('p');
    double win = RectSum(mask_ii, cx + p.x0 * grid.pitch, grid.cy0 + p.y0 * grid.ch,
                         cx + p.x1 * grid.pitch, grid.cy0 + p.y1 * grid.ch, &wa);
    double cf = ca ? core / ca : 0, wf = wa ? win / wa : 0,
           rf = wa > ca ? (wf * wa - cf * ca) / (wa - ca) : 0;
    double core_intensity =
        RectSum(intensity_ii, cx + kDpCore.x0 * grid.pitch, grid.cy0 + kDpCore.y0 * grid.ch,
                cx + kDpCore.x1 * grid.pitch, grid.cy0 + kDpCore.y1 * grid.ch);
    double ref = 0;
    for (char key : std::string("abcdefg"))
      ref = std::max(ref, means[key]);
    cell.dot = cf > kDpCoreMin && (cf - rf) > kDpDiff && ref > 0 &&
               (ca ? core_intensity / ca : 0) / ref > kDpMeanRatio;
    double decis = 0, min_margin = 1;
    for (char key : std::string("abcdefg")) {
      decis += std::abs(fractions[key] - .5) * 2;
      min_margin = std::min(min_margin, std::abs(fractions[key] - on_fraction));
    }
    cell.decision = cell.used ? decis / 7 : 1;
    cell.confidence =
        cell.used ? std::min(1.0, min_margin / on_fraction) * (1 - .4 * cell.hamming) : 1;
    out.cells.push_back(cell);
  }
  out.specificity =
      ratios.empty()
          ? 1
          : std::clamp(1 - std::accumulate(ratios.begin(), ratios.end(), 0.0) / ratios.size(), 0.0,
                       1.0);
  return out;
}

double Hypothesis(const Decoded& decoded, int base_count) {
  // 合法段码、判定余量、数字召回率和段间特异性共同抑制过曝造成的“888”。
  double total = 0;
  int count = 0, dots = 0;
  for (const auto& c : decoded.cells)
    if (c.used) {
      double valid = kCodes.count(c.pattern) ? 1 : std::max(0., 1 - .35 * c.hamming);
      total += valid * (.35 + .65 * std::min(1., c.decision));
      ++count;
      dots += c.dot;
    }
  if (!count)
    return -1;
  double sensitivity = count >= base_count ? 1 : std::pow((double)count / base_count, 2);
  return total / count * sensitivity * (dots <= 1 ? 1 : .7) * (.15 + .85 * decoded.specificity);
}

bool ReadRow(const cv::Mat& mask, const cv::Mat& intensity, const cv::Rect& bounds,
             const DigitalGaugeConfig& config, RowResult* output) {
  cv::Mat raw_mask = mask(bounds), raw_intensity = intensity(bounds);
  double shear = EstimateShear(CleanBand(raw_mask), config);
  cv::Mat warped_mask = Shear(raw_mask, shear, cv::INTER_NEAREST),
          warped_intensity = Shear(raw_intensity, shear, cv::INTER_LINEAR);
  // 阶段一使用较低阈值拟合完整几何，防止淡笔画在网格拟合前丢失。
  cv::Mat base_float, base;
  cv::threshold(warped_intensity, base_float, config.base_threshold, 1, cv::THRESH_BINARY);
  base_float.convertTo(base, CV_8U);
  base.setTo(0, warped_mask == 0);
  base = CleanBand(base);
  if (cv::sum(base)[0] < 50)
    return false;
  Grid coarse;
  if (!FitGrid(base, config.minimum_digits, config.maximum_digits, &coarse))
    return false;
  std::vector<cv::Point> points;
  cv::findNonZero(base, points);
  int ymin = base.rows, ymax = 0;
  for (auto p : points) {
    ymin = std::min(ymin, p.y);
    ymax = std::max(ymax, p.y);
  }
  int lit_height = ymax - ymin + 1;
  // 阶段二按字高选择亮灭阈值，并对 pitch、相位和位数做解码级联合搜索。
  int threshold = lit_height >= config.large_digit_height_split ? config.large_digit_threshold
                                                                : config.small_digit_threshold;
  cv::Mat chosen_float, chosen;
  cv::threshold(warped_intensity, chosen_float, threshold, 1, cv::THRESH_BINARY);
  chosen_float.convertTo(chosen, CV_8U);
  chosen.setTo(0, warped_mask == 0);
  chosen = CleanBand(chosen);
  cv::Mat base_ii, chosen_ii, intensity_ii;
  cv::integral(base, base_ii, CV_64F);
  cv::integral(chosen, chosen_ii, CV_64F);
  cv::integral(warped_intensity, intensity_ii, CV_64F);
  int base_count = 0;
  for (const auto& c : Decode(base_ii, intensity_ii, coarse, config.on_fraction).cells)
    base_count += c.used;
  if (!base_count)
    return false;
  double best_score = -1;
  Grid best_grid;
  Decoded best_decoded;
  std::set<int> candidates = {std::max(config.minimum_digits, coarse.cells - 1), coarse.cells,
                              std::min(config.maximum_digits, coarse.cells + 1)};
  for (double factor : kPitchFactors) {
    double pitch = coarse.pitch * factor;
    for (int nc : candidates)
      for (int off = 0; off < (int)std::lround(pitch); ++off) {
        Grid grid = coarse;
        grid.pitch = pitch;
        grid.x0 = coarse.lx0 - off;
        grid.cells = nc;
        auto decoded = Decode(chosen_ii, intensity_ii, grid, config.on_fraction);
        double score = Hypothesis(decoded, base_count);
        if (score > best_score) {
          best_score = score;
          best_grid = grid;
          best_decoded = std::move(decoded);
        }
      }
  }
  if (best_score < 0)
    return false;
  RowResult row;
  row.bounds = bounds;
  row.grid = best_grid;
  row.cells = best_decoded.cells;
  row.specificity = best_decoded.specificity;
  row.shear = shear;
  double min_conf = 1;
  int used = 0, dots = 0, corrected = 0;
  for (const auto& c : row.cells)
    if (c.used) {
      row.text += c.digit;
      if (c.dot)
        row.text += '.';
      min_conf = std::min(min_conf, c.confidence);
      ++used;
      dots += c.dot;
      corrected += c.hamming > 0;
    }
  char* end = nullptr;
  row.value = std::strtod(row.text.c_str(), &end);
  row.has_value = end && *end == '\0' && !row.text.empty();
  int detected = used;
  double sensitivity = (double)detected / std::max(1, base_count);
  row.confidence = min_conf * sensitivity * (.45 + .55 * row.specificity);
  if (detected < base_count)
    row.flags |= CVSDK_DIGITAL_GAUGE_FLAG_MISSING_DIGIT;
  if (dots > 1)
    row.flags |= CVSDK_DIGITAL_GAUGE_FLAG_MULTIPLE_DOTS;
  if (min_conf < .30)
    row.flags |= CVSDK_DIGITAL_GAUGE_FLAG_LOW_CONFIDENCE;
  if (corrected)
    row.flags |= CVSDK_DIGITAL_GAUGE_FLAG_CODE_CORRECTED;
  if (row.specificity < config.minimum_specificity)
    row.flags |= CVSDK_DIGITAL_GAUGE_FLAG_LOW_SPECIFICITY;
  *output = std::move(row);
  return true;
}
} // namespace

CVSDK_Status DigitalGaugeReader::Init(const char* package_dir,
                                      const CVSDK_DigitalGaugeReaderOptions* options) {
  if (!package_dir || !options || options->minimum_confidence < 0 ||
      options->minimum_confidence > 1) {
    SetLastError("invalid digital gauge reader options");
    return CVSDK_INVALID_ARGUMENT;
  }
  CVSDK_Status status = LoadDigitalGaugeConfig(package_dir, &config_);
  if (status != CVSDK_OK)
    return status;
  minimum_confidence_ = options->minimum_confidence;
  return_ambiguous_ = options->return_ambiguous != 0;
  initialized_ = true;
  return CVSDK_OK;
}

CVSDK_Status DigitalGaugeReader::Infer(const CVSDK_Image& image,
                                       std::vector<CVSDK_DigitalGaugeReading>* output) const {
  if (!initialized_ || !output || !image.data) {
    SetLastError("digital gauge reader is not initialized");
    return CVSDK_INVALID_ARGUMENT;
  }
  cv::Mat source((int)image.height, (int)image.width,
                 image.pixel_format == CVSDK_PIXEL_FORMAT_GRAY8 ? CV_8UC1 : CV_8UC3,
                 const_cast<uint8_t*>(image.data), image.stride_bytes),
      bgr;
  if (image.pixel_format == CVSDK_PIXEL_FORMAT_RGB8)
    cv::cvtColor(source, bgr, cv::COLOR_RGB2BGR);
  else if (image.pixel_format == CVSDK_PIXEL_FORMAT_GRAY8)
    cv::cvtColor(source, bgr, cv::COLOR_GRAY2BGR);
  else
    bgr = source;
  cv::Mat hsv;
  cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
  std::vector<cv::Mat> channels;
  cv::split(hsv, channels);
  // 红/绿发光像素用于行带定位，蓝色区域用于定位完整仪表面板。
  cv::Mat bright =
      (channels[2] > config_.led_value_min) & (channels[1] > config_.led_saturation_min);
  cv::Mat colors = (channels[0] < config_.red_hue_low_max) |
                   (channels[0] > config_.red_hue_high_min) |
                   ((channels[0] > config_.green_hue_min) & (channels[0] < config_.green_hue_max));
  cv::Mat led = (bright & colors) / 255;
  cv::Mat intensity;
  channels[2].convertTo(intensity, CV_32F);
  cv::Mat blue = (channels[0] > config_.panel_hue_min) & (channels[0] < config_.panel_hue_max) &
                 (channels[1] > config_.panel_saturation_min) &
                 (channels[2] > config_.panel_value_min);
  cv::morphologyEx(blue, blue, cv::MORPH_CLOSE, cv::Mat::ones(25, 25, CV_8U));
  cv::morphologyEx(blue, blue, cv::MORPH_OPEN, cv::Mat::ones(15, 15, CV_8U));
  cv::Mat labels, stats, centers;
  int count = cv::connectedComponentsWithStats(blue, labels, stats, centers, 8);
  std::vector<cv::Rect> panels;
  double min_area = config_.panel_min_area_ratio * image.width * image.height,
         max_width = config_.panel_max_width_ratio * image.width,
         max_y = config_.panel_max_y_ratio * image.height;
  for (int i = 1; i < count; ++i) {
    cv::Rect r(stats.at<int>(i, 0), stats.at<int>(i, 1), stats.at<int>(i, 2), stats.at<int>(i, 3));
    if (stats.at<int>(i, 4) >= min_area && r.y <= max_y && r.width < max_width)
      panels.push_back(r);
  }
  std::sort(panels.begin(), panels.end(), [](auto a, auto b) { return a.x < b.x; });
  output->clear();
  for (size_t pi = 0; pi < panels.size(); ++pi) {
    auto panel = panels[pi];
    cv::Mat sub = led(panel), sum;
    cv::reduce(sub, sum, 1, cv::REDUCE_SUM, CV_32S);
    double maxv = 0;
    cv::minMaxLoc(sum, nullptr, &maxv);
    if (maxv <= 0)
      continue;
    int row_threshold = std::max(6, (int)(maxv * .12));
    std::vector<std::pair<int, int>> bands;
    bool inside = false;
    int start = 0;
    for (int y = 0; y < sum.rows; ++y) {
      bool lit = sum.at<int>(y) > row_threshold;
      if (lit && !inside) {
        inside = true;
        start = y;
      } else if (!lit && inside) {
        inside = false;
        if (y - start >= 14)
          bands.push_back({panel.y + start, panel.y + y});
      }
    }
    if (inside && sum.rows - start >= 14)
      bands.push_back({panel.y + start, panel.y + sum.rows});
    std::sort(bands.begin(), bands.end(),
              [](auto a, auto b) { return a.second - a.first > b.second - b.first; });
    if (bands.size() > 2)
      bands.resize(2);
    for (size_t bi = 0; bi < bands.size(); ++bi) {
      int y0 = bands[bi].first, y1 = bands[bi].second;
      cv::Mat rowmask = led(cv::Rect(panel.x, y0, panel.width, y1 - y0));
      std::vector<cv::Point> pts;
      cv::findNonZero(rowmask, pts);
      if (pts.empty())
        continue;
      int xmin = panel.width, xmax = 0;
      for (auto p : pts) {
        xmin = std::min(xmin, p.x);
        xmax = std::max(xmax, p.x);
      }
      int x0 = std::max(0, panel.x + xmin - kPadX),
          x1 = std::min((int)image.width, panel.x + xmax + 1 + kPadX);
      int ry0 = std::max(0, y0 - kPadV), ry1 = std::min((int)image.height, y1 + kPadV);
      RowResult row;
      if (!ReadRow(led, intensity, {x0, ry0, x1 - x0, ry1 - ry0}, config_, &row))
        continue;
      bool ambiguous = row.flags != 0 || row.confidence < minimum_confidence_;
      if (ambiguous && !return_ambiguous_)
        continue;
      CVSDK_DigitalGaugeReading item{};
      item.struct_size = sizeof(item);
      item.panel_index = pi;
      item.role = bi == 0 ? CVSDK_DIGITAL_GAUGE_ROW_PV : CVSDK_DIGITAL_GAUGE_ROW_SV;
      item.panel_x = panel.x;
      item.panel_y = panel.y;
      item.panel_width = panel.width;
      item.panel_height = panel.height;
      item.row_x = row.bounds.x;
      item.row_y = row.bounds.y;
      item.row_width = row.bounds.width;
      item.row_height = row.bounds.height;
      std::snprintf(item.text, sizeof(item.text), "%s", row.text.c_str());
      item.value = row.value;
      item.has_value = row.has_value;
      item.confidence = row.confidence;
      item.segment_specificity = row.specificity;
      item.shear = row.shear;
      item.pitch = row.grid.pitch;
      item.digit_count =
          std::count_if(row.cells.begin(), row.cells.end(), [](const Cell& c) { return c.used; });
      item.flags = row.flags;
      if (row.confidence < minimum_confidence_)
        item.flags |= CVSDK_DIGITAL_GAUGE_FLAG_LOW_CONFIDENCE;
      output->push_back(item);
    }
  }
  return CVSDK_OK;
}
} // namespace cvsdk
