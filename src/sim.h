#ifndef SIM_H
#define SIM_H

#include <stdbool.h>
#include <SDL2/SDL.h>

/*
 * 浏览器/脚本驱动的"多屏幕模拟"：从 SIM_SCRIPT 环境变量读取指令文件，
 * 在独立线程里按时序往事件队列投递 SDL_USEREVENT（不直接动渲染状态）。
 *
 * 指令（每行）：
 *   wait MS
 *   size W H [display]     快速拖动/极窄窗口（同一 burst 内行极密集 -> 合并）
 *   burst N MS W0 H0 W1 H1 在 MS 内推 N 个尺寸事件（验证合并）
 *   rotate 90|180|270|0
 *   move X Y
 *   dpr F                  （仅首帧前有效，e2e 用 --sim-dpr 启动）
 *   publish                触发后端在"缩放进行中"发布新图（e2e 配合钩子）
 *   tap X Y                物理像素点击（命中测试）
 *   shot PATH              截图保存（xwd 在进程外做，这里只打标记时间点）
 *   quit
 */

typedef struct {
    Uint32 event_type;
} SimBridge;

typedef enum {
    SIM_SIZE,
    SIM_ROTATE,
    SIM_MOVE,
    SIM_TAP,
    SIM_SHOT,
    SIM_PUBLISH,
    SIM_QUIT
} SimKind;

typedef struct {
    SimKind kind;
    int a, b, c;
    char text[256];
} SimEvent;

bool sim_start(SimBridge *b, const char *script_path);
void sim_stop(void);

#endif
