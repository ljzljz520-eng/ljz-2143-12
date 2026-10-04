#!/usr/bin/env bash
set -euo pipefail

DISPLAY_NUM=":99"
SCREEN_GEOMETRY="${XVFB_GEOMETRY:-1920x1200x24}"
VNC_PORT="5900"
NOVNC_PORT="6080"
LAYOUT_PORT="${LAYOUT_SERVER_PORT:-8080}"

cleanup() {
  local code=$?
  for pid in "${APP_PID:-}" "${SERVER_PID:-}" "${NOVNC_PID:-}" "${VNC_PID:-}" "${WM_PID:-}" "${XVFB_PID:-}"; do
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

# 布局后端（版本入库/发布/上报）
LAYOUT_SERVER_PORT="${LAYOUT_PORT}" python3 /app/server/layout_server.py \
  >/tmp/layout-server.log 2>&1 &
SERVER_PID=$!

x11vnc -display "${DISPLAY}" -forever -shared -rfbport "${VNC_PORT}" \
  -localhost -nopw -noxdamage >/tmp/x11vnc.log 2>&1 &
VNC_PID=$!

if [[ -x /usr/share/novnc/utils/novnc_proxy ]]; then
  cat >/usr/share/novnc/index.html <<'HTML'
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
  /usr/share/novnc/utils/novnc_proxy --listen "${NOVNC_PORT}" \
    --vnc "localhost:${VNC_PORT}" >/tmp/novnc.log 2>&1 &
else
  websockify --web=/usr/share/novnc/ "${NOVNC_PORT}" "localhost:${VNC_PORT}" \
    >/tmp/novnc.log 2>&1 &
fi
NOVNC_PID=$!

cd /app
LAYOUT_SERVER_HOST=127.0.0.1 LAYOUT_SERVER_PORT="${LAYOUT_PORT}" \
  LAYOUT_DEVICE_ID=display-01 ./visual-window-app &
APP_PID=$!

echo
echo "============================================================"
echo " C 展示端(noVNC) : http://localhost:${NOVNC_PORT}/vnc.html"
echo " 管理调试台      : http://localhost:${NOVNC_PORT}/admin-proxy/  (见下)"
echo " 布局后端        : 容器内 127.0.0.1:${LAYOUT_PORT}"
echo " Web 展示终端    : http://localhost:${LAYOUT_PORT}/display.html"
echo " 管理调试台直连  : http://localhost:${LAYOUT_PORT}/admin.html"
echo "============================================================"
echo
echo "快捷键: m 模式 | r 旋转 | d 模拟DPR | 方向键 焦点 | s 保存 | u/i 撤销重做 | F1 调试"

wait "${APP_PID}"
