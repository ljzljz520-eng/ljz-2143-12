#!/usr/bin/env bash
set -euo pipefail

DISPLAY_NUM=":99"
SCREEN_GEOMETRY="${SCREEN_GEOMETRY:-1280x800x24}"
VNC_PORT="5900"
NOVNC_PORT="6080"
BACKEND_PORT="${BACKEND_PORT:-8080}"

cleanup() {
  local code=$?
  for pid in "${APP_PID:-}" "${BACKEND_PID:-}" "${NOVNC_PID:-}" "${VNC_PID:-}" "${WM_PID:-}" "${XVFB_PID:-}"; do
    if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
      kill "${pid}" 2>/dev/null || true
      wait "${pid}" 2>/dev/null || true
    fi
  done
  exit "${code}"
}
trap cleanup EXIT INT TERM

Xvfb "${DISPLAY_NUM}" -screen 0 "${SCREEN_GEOMETRY}" -ac +extension GLX +render -noreset &
XVFB_PID=$!
export DISPLAY="${DISPLAY_NUM}"

for _ in $(seq 1 50); do
  if xdpyinfo >/dev/null 2>&1; then break; fi
  sleep 0.1
done

openbox >/tmp/openbox.log 2>&1 &
WM_PID=$!

# 布局后端（版本入库/撤销/上报）
python3 /app/server/app.py >/tmp/layoutd.log 2>&1 &
BACKEND_PID=$!
for _ in $(seq 1 50); do
  if curl -fsS "http://127.0.0.1:${BACKEND_PORT}/api/health" >/dev/null 2>&1; then break; fi
  sleep 0.1
done

# 先把默认背景登记为 rev=1（后端启动已 seed，这里幂等）
x11vnc \
  -display "${DISPLAY}" \
  -forever \
  -shared \
  -rfbport "${VNC_PORT}" \
  -localhost \
  -nopw \
  -noxdamage \
  >/tmp/x11vnc.log 2>&1 &
VNC_PID=$!

if [[ -x /usr/share/novnc/utils/novnc_proxy ]]; then
  cat > /usr/share/novnc/index.html <<'HTML'
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta http-equiv="refresh" content="0; url=/vnc.html?autoconnect=1&resize=scale&path=websockify" />
    <title>noVNC Redirect</title>
  </head>
  <body><p>Redirecting to noVNC...</p></body>
</html>
HTML
  /usr/share/novnc/utils/novnc_proxy --listen "${NOVNC_PORT}" --vnc "localhost:${VNC_PORT}" >/tmp/novnc.log 2>&1 &
else
  websockify --web=/usr/share/novnc/ "${NOVNC_PORT}" "localhost:${VNC_PORT}" >/tmp/novnc.log 2>&1 &
fi
NOVNC_PID=$!

# C 展示端连本地后端；管理端在 http://localhost:6080/api 之外可直接访问 8080
./visual-window-app --host 127.0.0.1 --port "${BACKEND_PORT}" --device terminal-1 &
APP_PID=$!

# 管理端静态页与 API 同在 8080；noVNC 在 6080 仅作为现场画面镜像
wait "${APP_PID}"
