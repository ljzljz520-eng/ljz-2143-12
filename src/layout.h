/*
 * layout.h - 布局模型：源图像 -> 逻辑窗口的投影
 *
 * 两种显示模式（用户最终选择，必须持久化）：
 *   LAYOUT_COVER  : 铺满。等比缩放使图像完全覆盖视口，超出部分被裁切，
 *                   裁切中心由焦点 (focus_x, focus_y) 决定。
 *   LAYOUT_CONTAIN: 完整。等比缩放使整张图完整可见，视口内留边 (letterbox)。
 *
 * LayoutTransform 同时给出：
 *   - 逻辑视口内图像的目标矩形（视觉位置）
 *   - 源图像上被裁切的矩形（上报给后端/截图比对用）
 *   - 图像坐标 -> 逻辑坐标的仿射（触摸命中换算）
 */
#ifndef LAYOUT_H
#define LAYOUT_H

#include <stdbool.h>
#include <stddef.h>

#include "geometry.h"

typedef enum {
    LAYOUT_COVER = 0,
    LAYOUT_CONTAIN = 1,
} LayoutMode;

typedef struct {
    char image_id[128];
    int image_w;
    int image_h;
    LayoutMode mode;
    double focus_x; /* [0,1] */
    double focus_y;
    Rotation rotation;
    double dpr_override; /* 0 = 自动（取窗口实际 DPR） */
} LayoutConfig;

typedef struct {
    /* 图像在逻辑视口中的目标矩形（COVER 时会超出视口） */
    GRect target;
    /* 源图像实际参与显示的裁切区域（CONTAIN 时为整张图） */
    GRect crop;
    /* 留边颜色容器矩形（= 逻辑视口） */
    GRect viewport;
    /* 源图像像素坐标 -> 逻辑窗口坐标 */
    GAffine image_to_logical;
    /* 逆：逻辑窗口坐标 -> 源图像像素坐标（命中测试） */
    GAffine logical_to_image;
    double scale; /* 图像像素 -> 逻辑像素 的缩放比 */
    bool fully_visible;
} LayoutTransform;

void layout_config_init(LayoutConfig *cfg, const char *image_id, int iw, int ih);
bool layout_config_valid(const LayoutConfig *cfg, char *err, size_t errsz);

const char *layout_mode_str(LayoutMode mode);
bool layout_mode_parse(const char *s, LayoutMode *out);

/* 计算投影。cfg 与 viewport 逻辑尺寸 */
LayoutTransform layout_resolve(const LayoutConfig *cfg, GSize viewport);

/* 逻辑窗口中的点 -> 源图像坐标；返回 false 表示落在图像之外（留边区/视口外） */
bool layout_hit_test(const LayoutTransform *t, GPoint logical, GPoint *image_pt);

/* 上报用裁切区域（图像像素，整数化，夹取在图像范围内） */
GRect layout_reported_crop(const LayoutConfig *cfg, const LayoutTransform *t);

/*
 * 编辑会话：撤销/重做只作用于"本次编辑基础"。
 * baseline 是打开编辑器时（或显式 rebase 后）的配置；
 * undo 最多退到 baseline，再早的历史属于上一次编辑会话，不允许越过。
 * 远程版本推进时（poll 到更新的 version），未保存改动与新版本冲突：
 * 由调用方提示用户（见 edit_session_remote_changed），不自动合并。
 */
#define LAYOUT_HISTORY_CAP 64

typedef struct {
    LayoutConfig stack[LAYOUT_HISTORY_CAP];
    int head;      /* 栈顶下标 */
    int base;      /* baseline 下标，undo 下限 */
    int count;     /* 有效条目数 */
    bool remote_changed;
    int remote_version;
} EditSession;

void edit_session_begin(EditSession *s, const LayoutConfig *baseline, int remote_version);
void edit_session_push(EditSession *s, const LayoutConfig *cfg);
bool edit_session_can_undo(const EditSession *s);
bool edit_session_can_redo(const EditSession *s);
bool edit_session_undo(EditSession *s, LayoutConfig *out);
bool edit_session_redo(EditSession *s, LayoutConfig *out);
void edit_session_notify_remote(EditSession *s, int remote_version);
const LayoutConfig *edit_session_baseline(const EditSession *s);
const LayoutConfig *edit_session_current(const EditSession *s);

#endif
