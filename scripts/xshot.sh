#!/usr/bin/env bash
# xshot.sh out.xwd [display] : 抓根窗口（展示终端真实帧缓冲，非管理端预览）
set -euo pipefail
LOCAL="${SDL_PREFIX:-/tmp/local/usr}"
D="${2:-${DISPLAY:-:99}}"
export LD_LIBRARY_PATH="$LOCAL/lib/aarch64-linux-gnu:${LOCAL%/usr}/lib/aarch64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$LOCAL/bin/xwd" -display "$D" -root -silent -out "$1"
echo "captured $1"
