#include "test.h"
#include "events.h"

static void test_coalesce(void) {
    EventQueue q; event_queue_init(&q);
    /* 快速拖动产生 50 个尺寸事件：只 drain 出最新一次 */
    for (int i=0;i<50;i++) event_queue_push_size(&q, 300+i, 600+i, 0);
    CHECK(q.coalesced == 49);
    SizeEvent e;
    CHECK(event_queue_drain(&q, &e));
    CHECK(e.width==349 && e.height==649);
    CHECK(!event_queue_drain(&q, &e)); /* 无残留 */
}

static void test_stale_epoch_dropped(void) {
    EventQueue q; event_queue_init(&q);
    uint64_t e0 = event_queue_current_epoch(&q);
    /* 在旧模式（epoch=e0）下入队一个迟到尺寸事件 */
    event_queue_push_size(&q, 100, 200, 0);
    /* 用户保存新配置：cover->contain，epoch 推进 */
    event_queue_commit_config(&q);
    CHECK(event_queue_current_epoch(&q) == e0+1);
    /* drain 时旧 epoch 事件必须被丢弃，不能覆盖新模式 */
    SizeEvent e;
    CHECK(!event_queue_drain(&q, &e));
    CHECK(q.dropped_stale == 1);
    /* 新 epoch 的事件正常应用 */
    event_queue_push_size(&q, 300, 400, 1);
    CHECK(event_queue_drain(&q, &e));
    CHECK(e.width==300 && e.height==400 && e.display_index==1);
}

static void test_burst_then_commit(void) {
    EventQueue q; event_queue_init(&q);
    event_queue_push_size(&q, 800, 600, 0);
    event_queue_push_size(&q, 801, 600, 0);   /* 与上一个合并：pending 仍记旧 epoch */
    CHECK(q.coalesced==1);
    event_queue_commit_config(&q);             /* 用户最终选择保存 */
    SizeEvent e;
    /* 旧 epoch 的迟到尺寸事件被丢弃，不能覆盖新模式 */
    CHECK(!event_queue_drain(&q, &e));
    CHECK(q.dropped_stale==1);
    /* 新模式下的尺寸事件正常应用 */
    event_queue_push_size(&q, 900, 700, 0);
    CHECK(event_queue_drain(&q, &e));
    CHECK(e.width==900 && e.height==700);
}

void test_events_all(void) {
    RUN(test_coalesce);
    RUN(test_stale_epoch_dropped);
    RUN(test_burst_then_commit);
}
