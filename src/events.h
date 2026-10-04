#ifndef EVENTS_H
#define EVENTS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 尺寸事件合并 + 配置代际防护。
 *
 * 场景：
 *  - 快速拖动窗口产生大量 RESIZE：只对"最新一次"做重布局（coalesce）。
 *  - 用户在管理端把 cover 改成 contain 并保存；一个在旧模式下排队的迟到 RESIZE
 *    绝不能把布局改回旧值。所有尺寸事件携带它入队时的配置 epoch，
 *    应用快照前校验 epoch == 当前 epoch，过期直接丢弃。
 *  - 用户保存配置 = 不可被覆盖的"最终选择"（commit），bump epoch。
 */

typedef struct {
    int width, height;
    int display_index;   /* 跨显示器移动 */
    uint64_t epoch;      /* 该事件产生时的配置代际 */
    uint64_t seq;
} SizeEvent;

typedef struct {
    SizeEvent pending;      /* 被合并的最新事件 */
    bool has_pending;
    uint64_t epoch;         /* 当前配置代际 */
    uint64_t seq_gen;
    uint64_t dropped_stale; /* 统计：因代际过期丢弃数 */
    uint64_t coalesced;     /* 统计：被合并掉的事件数 */
} EventQueue;

void event_queue_init(EventQueue *q);

/* 配置提交（用户最终选择）：推进代际，使所有在途旧事件失效。 */
void event_queue_commit_config(EventQueue *q);

/* 入队一个尺寸事件；若已有挂起事件则合并（旧事件计入 coalesced）。 */
void event_queue_push_size(EventQueue *q, int w, int h, int display_index);

/*
 * 取出一个可应用的快照：
 *  - 无挂起事件 -> 返回 false；
 *  - 挂起事件 epoch 与当前不符 -> 丢弃并继续，返回 false（迟到事件不覆盖新模式）；
 *  - 匹配 -> 写入 out，返回 true。
 */
bool event_queue_drain(EventQueue *q, SizeEvent *out);

uint64_t event_queue_current_epoch(const EventQueue *q);

#endif
