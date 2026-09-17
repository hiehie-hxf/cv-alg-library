#!/usr/bin/env bash
#
# 视频演示：把 data/images/test/ 下所有图片按顺序跑一遍推理，
# 生成「左原图 / 右检测结果」的 MP4 视频。
#
# 用法:
#   ./scripts/demo_video.sh [输出路径] [fps] [每帧停留秒数]
#     ./scripts/demo_video.sh                        # docs/demo_video.mp4, 5 fps, 每帧 1 秒
#     ./scripts/demo_video.sh out.mp4 10 2           # 10 fps, 每帧停留 2 秒
#
# 环境变量:
#   CONFIG  指定配置文件，默认 <工程根>/config/leak_rules.json
#
# 说明:
#   用 cv2.VideoWriter 直接写文件，**不使用 cv2.imshow**，
#   因此不依赖图形界面，可在纯 SSH / 无 DISPLAY 的环境下运行。

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MODEL="$ROOT/models/leak_yolov8n_seg.onnx"
TESTDIR="$ROOT/data/images/test"
BIN="$ROOT/build/leak_detect"
WORK="$ROOT/build/demo_work_video"
OUT="$ROOT/docs/demo_video.mp4"
FPS=5
HOLD=1

CONFIG="${CONFIG:-$ROOT/config/leak_rules.json}"

INVOKE_DIR="$(pwd)"

# 输出路径统一转成绝对路径：脚本后面会 cd 到 WORK
if [ $# -ge 1 ]; then
  case "$1" in
    /*) OUT="$1" ;;
    *)  OUT="$INVOKE_DIR/$1" ;;
  esac
fi
if [ $# -ge 2 ]; then FPS="$2"; fi
if [ $# -ge 3 ]; then HOLD="$3"; fi

if [ ! -x "$BIN" ]; then
  echo "找不到可执行文件: $BIN" >&2
  echo "请先构建: cmake -S $ROOT -B $ROOT/build && cmake --build $ROOT/build -j4" >&2
  exit 1
fi
if [ ! -f "$MODEL" ]; then echo "找不到模型: $MODEL" >&2; exit 1; fi
if [ ! -f "$CONFIG" ]; then echo "找不到配置: $CONFIG" >&2; exit 1; fi

echo "配置  : $CONFIG"
echo "输出  : $OUT (fps=$FPS, 每帧停留 ${HOLD}s)"

set -- "$TESTDIR"/*.jpg
if [ ! -e "$1" ]; then
  echo "在 $TESTDIR 下没有找到 jpg 图片" >&2
  exit 1
fi
COUNT=$#
echo "图片  : $COUNT 张"

rm -rf "$WORK"
mkdir -p "$WORK" "$(dirname "$OUT")"

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

echo "开始合成视频..."
python3 - "$WORK" "$OUT" "$FPS" "$HOLD" "$COUNT" "$CONFIG" <<'PYEOF'
import os
import re
import sys

import cv2
import numpy as np

work = sys.argv[1]
out_path = sys.argv[2]
fps = float(sys.argv[3])
hold = float(sys.argv[4])
n_images = int(sys.argv[5])
config_label = os.path.basename(sys.argv[6]) if len(sys.argv) > 6 else '?'

HALF_W = 640
TITLE_H = 48
LABEL_H = 24
FONT = cv2.FONT_HERSHEY_SIMPLEX
WHITE = (255, 255, 255)

with open(os.path.join(work, 'detect.log'), 'r', encoding='utf-8', errors='replace') as fh:
    log_text = fh.read()

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
    sys.stderr.write('没能从日志解析出任何帧\n')
    sys.exit(1)


def result_path(idx):
    return os.path.join(work, 'output.jpg' if n_images == 1 else 'output_%d.jpg' % idx)


def fit_width(img, width):
    h, w = img.shape[:2]
    nh = max(1, int(round(h * float(width) / float(w))))
    interp = cv2.INTER_AREA if w > width else cv2.INTER_LINEAR
    return cv2.resize(img, (width, nh), interpolation=interp)


built = []
n_alert = 0
for idx in sorted(frames):
    info = frames[idx]
    orig = cv2.imread(info['path'])
    res = cv2.imread(result_path(idx))
    if orig is None or res is None:
        sys.stderr.write('跳过 frame %d（读图失败）\n' % idx)
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
    title = 'frame %d  %s   |   Detected %d   |   Alert: %s   (hits=%d, growing=%d, down=%d)' % (
        idx, os.path.basename(info['path']), info['detected'], info['alert'],
        info['hits'], info['growing'], info['down'])
    color = (0, 0, 210) if alert else (0, 120, 0)
    cv2.putText(bar, title, (10, 31), FONT, 0.62, color, 2, cv2.LINE_AA)

    label = np.full((LABEL_H, width, 3), WHITE, np.uint8)
    cv2.putText(label, 'original', (10, 17), FONT, 0.45, (110, 110, 110), 1, cv2.LINE_AA)
    cv2.putText(label, 'detected', (HALF_W + 10, 17), FONT, 0.45, (110, 110, 110), 1, cv2.LINE_AA)

    built.append(cv2.vconcat([bar, label, pair]))

if not built:
    sys.stderr.write('没有可用帧\n')
    sys.exit(1)

cw = max(b.shape[1] for b in built)
ch = max(b.shape[0] for b in built)
cw += cw % 2   # 视频编码器要求偶数尺寸
ch += ch % 2
norm = []
for b in built:
    norm.append(cv2.copyMakeBorder(b, 0, ch - b.shape[0], 0, cw - b.shape[1],
                                   cv2.BORDER_CONSTANT, value=WHITE))

fourcc_fn = getattr(cv2, 'VideoWriter_fourcc', None)
if fourcc_fn is None:
    fourcc_fn = cv2.VideoWriter.fourcc
fourcc = fourcc_fn(*'mp4v')

writer = cv2.VideoWriter(out_path, fourcc, fps, (cw, ch))
if not writer.isOpened():
    sys.stderr.write('无法打开 VideoWriter（mp4v 编码器不可用？）\n')
    sys.exit(1)

hold_n = max(1, int(round(fps * hold)))
written = 0
for img in norm:
    for _ in range(hold_n):
        writer.write(img)
        written += 1
writer.release()

if not os.path.exists(out_path):
    sys.stderr.write('视频未生成: %s\n' % out_path)
    sys.exit(1)

print('视频   : %s' % out_path)
print('尺寸   : %d x %d' % (cw, ch))
print('帧率   : %g fps，每帧停留 %gs（重复 %d 次）' % (fps, hold, hold_n))
print('总帧数 : %d（%d 张图）' % (written, len(norm)))
print('告警帧 : %d / %d' % (n_alert, len(norm)))
print('配置   : %s' % config_label)
PYEOF

echo
echo "===== 产物 ====="
ls -la "$OUT"
du -h "$OUT"
