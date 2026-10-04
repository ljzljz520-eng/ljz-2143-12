#!/usr/bin/env bash
# 无 root 环境启动 Xvfb（本仓库 e2e 用）。使用方法:
#   source scripts/xvfb_env.sh [DISPLAY_NUM] [WxHxD]
# 需要 SDL_PREFIX 指向含 usr/bin/Xvfb 的本地前缀（Makefile/CI 里设置）。
set -uo pipefail

DISPLAY_NUM="${1:-99}"
GEOMETRY="${2:-1280x800x24}"
LOCAL="${SDL_PREFIX:-/tmp/local/usr}"
export LD_LIBRARY_PATH="$LOCAL/lib/aarch64-linux-gnu:$LOCAL/lib/aarch64-linux-gnu/pulseaudio:${LOCAL%/usr}/lib/aarch64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# 1) xkbcomp wrapper（Xvfb 硬编码调 /usr/bin/xkbcomp，本地环境没有 -> 补丁过的 Xvfb）
mkdir -p /tmp/xbn /tmp/xdgr
cat > /tmp/xbn/xkbcomp <<SH
#!/bin/sh
export LD_LIBRARY_PATH="$LOCAL/lib/aarch64-linux-gnu:${LOCAL%/usr}/lib/aarch64-linux-gnu"
exec $LOCAL/bin/xkbcomp -w 10 "\$@"
SH
chmod +x /tmp/xbn/xkbcomp

if [ ! -x /tmp/Xvfb-patched ]; then
  cp "$LOCAL/bin/Xvfb" /tmp/Xvfb-patched
  python3 - <<'PY'
data=open('/tmp/Xvfb-patched','rb').read()
assert b'/usr/bin\x00' in data
data=data.replace(b'/usr/bin\x00', b'/tmp/xbn\x00', 1)
open('/tmp/Xvfb-patched','wb').write(data)
print('patched Xvfb -> /tmp/Xvfb-patched')
PY
fi

pkill -f "Xvfb-patched :$DISPLAY_NUM" 2>/dev/null || true
rm -f "/tmp/.X${DISPLAY_NUM}-lock" /tmp/server-${DISPLAY_NUM}.xkm 2>/dev/null
XKB_CONFIG_ROOT="$LOCAL/share/X11/xkb" XDG_RUNTIME_DIR=/tmp/xdgr \
  /tmp/Xvfb-patched ":$DISPLAY_NUM" -xkbdir "$LOCAL/share/X11/xkb" \
  -screen 0 "$GEOMETRY" -nolisten tcp -ac >/tmp/xvfb-${DISPLAY_NUM}.log 2>&1 &
XVFB_PID=$!
for _ in $(seq 1 50); do
  if "$LOCAL/bin/xdpyinfo" -display ":$DISPLAY_NUM" >/dev/null 2>&1; then break; fi
  sleep 0.1
done
export DISPLAY=":$DISPLAY_NUM"
echo "Xvfb :$DISPLAY_NUM ($GEOMETRY) pid=$XVFB_PID DISPLAY=$DISPLAY"
