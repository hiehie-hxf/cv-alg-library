#!/usr/bin/env bash
#
# 图片批量演示：对 data/images/test/ 下所有图片跑一遍 leak_detect，
# 生成「原图 | 检测结果」对比图，拼成网格输出。
#
# 用法:
#   ./scripts/demo_images.sh [输出路径] [列数]
#
#   # 默认配置（config/leak_rules.json，window=10 -> 预热期，无告警）
#   ./scripts/demo_images.sh
#
#   # 指定配置（例如 window=3 的小窗口配置，用于演示告警触发）
#   CONFIG=config/leak_rules_demo_w3.json ./scripts/demo_images.sh docs/demo_grid_alert.jpg
#
# 环境变量:
#   CONFIG  指定配置文件路径，默认 <工程根>/config/leak_rules.json
#
# 依赖:
#   - 已构建的 build/leak_detect
#   - python3 + opencv-python (cv2) + numpy
#
# 说明:
#   本脚本调用真实的 C++ 可执行文件，所有图片在同一个进程内按顺序处理，
#   因此 leak_filter 的滑动窗口会连续累积（真正走一遍时序判定链路）。

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MODEL="$ROOT/models/leak_yolov8n_seg.onnx"
TESTDIR="$ROOT/data/images/test"
BIN="$ROOT/build/leak_detect"
WORK="$ROOT/build/demo_work"
OUT="$ROOT/docs/demo_grid.jpg"
COLS=3

CONFIG="${CONFIG:-$ROOT/config/leak_rules.json}"

INVOKE_DIR="$(pwd)"

# 输出路径统一转成绝对路径：脚本后面会 cd 到 WORK，相对路径会解析错
if [ $# -ge 1 ]; then
  case "$1" in
    /*) OUT="$1" ;;
    *)  OUT="$INVOKE_DIR/$1" ;;
  esac
fi
if [ $# -ge 2 ]; then COLS="$2"; fi

if [ ! -x "$BIN" ]; then
  echo "找不到可执行文件: $BIN" >&2
  echo "请先构建: cmake -S $ROOT -B $ROOT/build && cmake --build $ROOT/build -j4" >&2
  exit 1
fi
if [ ! -f "$MODEL" ]; then echo "找不到模型: $MODEL" >&2; exit 1; fi
if [ ! -f "$CONFIG" ]; then echo "找不到配置: $CONFIG" >&2; exit 1; fi

echo "配置  : $CONFIG"
echo "输出  : $OUT"

# 收集测试图（绝对路径，glob 排序稳定）
set -- "$TESTDIR"/*.jpg
if [ ! -e "$1" ]; then
  echo "在 $TESTDIR 下没有找到 jpg 图片" >&2
  exit 1
fi
COUNT=$#
echo "图片  : $COUNT 张"

rm -rf "$WORK"
mkdir -p "$WORK" "$(dirname "$OUT")"

# 在 WORK 下运行，让 leak_detect 把 output_*.jpg 写到那里
cd "$WORK"
LOG="$WORK/detect.log"

echo "开始推理（单进程、多帧连续）..."
set +e
"$BIN" "$MODEL" "$CONFIG" "$@" > "$LOG" 2>&1
RC=$?
set -e
if [ $RC -ne 0 ]; then
  echo "推理失败 (exit $RC)，日志:" >&2
  cat "$LOG" >&2
  exit $RC
fi

echo "开始拼图..."
python3 - "$WORK" "$OUT" "$COLS" "$COUNT" "$CONFIG" <<'PYEOF'
import os
import re
import sys

import cv2
import numpy as np

work = sys.argv[1]
out_path = sys.argv[2]
cols = int(sys.argv[3])
n_images = int(sys.argv[4])
config_label = os.path.basename(sys.argv[5]) if len(sys.argv) > 5 else '?'

HALF_W = 600      # 原图 / 结果图各自的宽度
TITLE_H = 42      # 标题栏高度
LABEL_H = 24      # original / detected 小标签高度
GAP = 8
FONT = cv2.FONT_HERSHEY_SIMPLEX
WHITE = (255, 255, 255)

with open(os.path.join(work, 'detect.log'), 'r', encoding='utf-8', errors='replace') as fh:
    log_text = fh.read()

# ---- 解析 leak_detect 输出 ----
frames = {}
cur = None
for line in log_text.splitlines():
    m = re.match(r'^--- frame (\d+): (.*) ---\s*$', line)
    if m:
        cur = int(m.group(1))
        frames[cur] = {'path': m.group(2), 'detected': 0, 'alert': 'NO',
                       'hits': 0, 'growing': 0, 'down': 0}
        continue
    if cur is None:
        continue
    m = re.match(r'^Detected (\d+) leak', line)
    if m:
        frames[cur]['detected'] = int(m.group(1))
        continue
    m = re.match(r'^Alert: (YES|NO), hits=(\d+), growing=(\d+), down=(\d+)', line)
    if m:
        frames[cur]['alert'] = m.group(1)
        frames[cur]['hits'] = int(m.group(2))
        frames[cur]['growing'] = int(m.group(3))
        frames[cur]['down'] = int(m.group(4))

if not frames:
    sys.stderr.write('没能从日志解析出任何帧。日志前 20 行:\n')
    sys.stderr.write('\n'.join(log_text.splitlines()[:20]) + '\n')
    sys.exit(1)


def result_path(idx):
    name = 'output.jpg' if n_images == 1 else 'output_%d.jpg' % idx
    return os.path.join(work, name)


def fit_width(img, width):
    h, w = img.shape[:2]
    nh = max(1, int(round(h * float(width) / float(w))))
    interp = cv2.INTER_AREA if w > width else cv2.INTER_LINEAR
    return cv2.resize(img, (width, nh), interpolation=interp)


cells = []
n_alert = 0
for idx in sorted(frames):
    info = frames[idx]
    orig = cv2.imread(info['path'])
    if orig is None:
        sys.stderr.write('跳过（读不到原图）: %s\n' % info['path'])
        continue
    res = cv2.imread(result_path(idx))
    if res is None:
        sys.stderr.write('跳过（读不到结果图）: %s\n' % result_path(idx))
        continue

    left = fit_width(orig, HALF_W)
    right = fit_width(res, HALF_W)
    hh = max(left.shape[0], right.shape[0])
    left = cv2.copyMakeBorder(left, 0, hh - left.shape[0], 0, 0, cv2.BORDER_CONSTANT, value=WHITE)
    right = cv2.copyMakeBorder(right, 0, hh - right.shape[0], 0, 0, cv2.BORDER_CONSTANT, value=WHITE)
    pair = cv2.hconcat([left, right])
    width = pair.shape[1]

    alert = (info['alert'] == 'YES')
    if alert:
        n_alert += 1
    bar = np.full((TITLE_H, width, 3), (240, 240, 240), np.uint8)
    title = '%s   |   Detected %d   |   Alert: %s   (hits=%d, growing=%d, down=%d)' % (
        os.path.basename(info['path']), info['detected'], info['alert'],
        info['hits'], info['growing'], info['down'])
    color = (0, 0, 210) if alert else (0, 120, 0)
    cv2.putText(bar, title, (10, 29), FONT, 0.58, color, 2, cv2.LINE_AA)

    label = np.full((LABEL_H, width, 3), WHITE, np.uint8)
    cv2.putText(label, 'original', (10, 17), FONT, 0.45, (110, 110, 110), 1, cv2.LINE_AA)
    cv2.putText(label, 'detected', (HALF_W + 10, 17), FONT, 0.45, (110, 110, 110), 1, cv2.LINE_AA)

    cell = cv2.vconcat([bar, label, pair])
    cell = cv2.copyMakeBorder(cell, GAP, 0, GAP, GAP, cv2.BORDER_CONSTANT, value=WHITE)
    cells.append(cell)

if not cells:
    sys.stderr.write('没有任何可用的对比格\n')
    sys.exit(1)

# ---- 统一尺寸后拼网格 ----
cw = max(c.shape[1] for c in cells)
ch = max(c.shape[0] for c in cells)
norm = []
for c in cells:
    norm.append(cv2.copyMakeBorder(c, 0, ch - c.shape[0], 0, cw - c.shape[1],
                                   cv2.BORDER_CONSTANT, value=WHITE))

rows = []
for start in range(0, len(norm), cols):
    chunk = norm[start:start + cols]
    while len(chunk) < cols:
        chunk.append(np.full((ch, cw, 3), 255, np.uint8))
    rows.append(cv2.hconcat(chunk))
grid = cv2.vconcat(rows)

header = np.full((60, grid.shape[1], 3), (250, 250, 250), np.uint8)
cv2.putText(header,
            'leak_detection demo  -  %d images  -  config: %s  -  Alert: %d/%d' % (
                len(norm), config_label, n_alert, len(norm)),
            (14, 27), FONT, 0.78, (45, 45, 45), 2, cv2.LINE_AA)
cv2.putText(header,
            'left: original   /   right: detected (box + mask + alert banner)',
            (14, 52), FONT, 0.55, (110, 110, 110), 1, cv2.LINE_AA)
footer = np.full((GAP, grid.shape[1], 3), 255, np.uint8)
grid = cv2.vconcat([header, grid, footer])

if not cv2.imwrite(out_path, grid, [int(cv2.IMWRITE_JPEG_QUALITY), 88]):
    sys.stderr.write('写入失败: %s\n' % out_path)
    sys.exit(1)

print('对比图 : %s' % out_path)
print('尺寸   : %d x %d (宽 x 高)' % (grid.shape[1], grid.shape[0]))
print('格数   : %d 个，%d 列 x %d 行' % (len(norm), cols, len(rows)))
print('告警格 : %d / %d' % (n_alert, len(norm)))
PYEOF

echo
echo "===== 产物 ====="
ls -la "$OUT"
du -h "$OUT"
