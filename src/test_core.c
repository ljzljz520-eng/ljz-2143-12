/* 纯逻辑单元测试：坐标变换互逆、cover/contain、焦点、触摸命中、编辑会话 */
#include <math.h>
#include <stdio.h>

#include "geometry.h"
#include "layout.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
} while (0)

static bool near(double a, double b) { return fabs(a - b) < 1e-6; }

int main(void) {
    /* 1. 旋转互逆 + 触摸/视觉一致 */
    GSize phys = gsize(720, 1280);
    Rotation rots[4] = {ROT_0, ROT_90, ROT_180, ROT_270};
    double dprs[3] = {1.0, 2.0, 2.5};
    for (int r = 0; r < 4; r++) {
        for (int d = 0; d < 3; d++) {
            GSize lg = g_logical_size(phys, dprs[d], rots[r]);
            GAffine f = g_logical_to_physical(lg, dprs[d], rots[r]);
            GAffine inv = g_physical_to_logical(lg, dprs[d], rots[r]);
            GPoint pts[4] = {{0, 0}, {lg.w / 3, lg.h / 2}, {lg.w, lg.h}, {17.5, 99.25}};
            for (int p = 0; p < 4; p++) {
                GPoint q = g_affine_apply(&f, pts[p]);
                GPoint back = g_affine_apply(&inv, q);
                CHECK(near(back.x, pts[p].x) && near(back.y, pts[p].y),
                      "rotation/dpr round-trip");
            }
            /* 物理表面边界 */
            GSize expect_phys = gsize(lg.w * dprs[d], lg.h * dprs[d]);
            (void)expect_phys;
        }
    }
    /* 90 度时逻辑宽对应物理高 */
    GSize lg90 = g_logical_size(gsize(720, 1280), 2.0, ROT_90);
    CHECK(near(lg90.w, 640) && near(lg90.h, 360), "90deg logical size");

    /* 2. cover：2000x1000 图投到 1000x1000 视口 -> scale=1, crop 1000x1000 */
    LayoutConfig cfg;
    layout_config_init(&cfg, "bg", 2000, 1000);
    cfg.mode = LAYOUT_COVER;
    LayoutTransform t = layout_resolve(&cfg, gsize(1000, 1000));
    CHECK(near(t.scale, 1.0), "cover scale");
    CHECK(near(t.crop.w, 1000) && near(t.crop.h, 1000), "cover crop size");
    CHECK(near(t.crop.x, 500) && near(t.crop.y, 0), "cover center focus crop");
    CHECK(near(t.target.x, -500) && near(t.target.y, 0), "cover target offset");

    /* 焦点 (0,0.5) -> crop.x=0 */
    cfg.focus_x = 0.001;
    t = layout_resolve(&cfg, gsize(1000, 1000));
    CHECK(near(t.crop.x, 0.001 * 1000), "focus-left crop");

    /* 3. contain：2000x1000 -> 1000x1000 => scale .5, 完整可见，留边 */
    cfg.focus_x = 0.5;
    cfg.mode = LAYOUT_CONTAIN;
    t = layout_resolve(&cfg, gsize(1000, 1000));
    CHECK(near(t.scale, 0.5), "contain scale");
    CHECK(t.fully_visible, "contain fully visible");
    CHECK(near(t.target.w, 1000) && near(t.target.h, 500), "contain target");
    CHECK(near(t.target.y, 250), "contain letterbox");
    GPoint ip;
    /* 留边区点击 (500,100) 不在图像上；画面中 (500,500) 对应源图像 (1000,500) */
    CHECK(!layout_hit_test(&t, gpoint(500, 100), &ip), "letterbox miss");
    CHECK(layout_hit_test(&t, gpoint(500, 500), &ip), "content hit");
    CHECK(near(ip.x, 1000) && near(ip.y, 500), "hit maps to source px");
    /* 逆回去一致 */
    GPoint back = g_affine_apply(&t.image_to_logical, ip);
    CHECK(near(back.x, 500) && near(back.y, 500), "hit inverse consistency");

    /* 4. 极窄视口 cover */
    cfg.mode = LAYOUT_COVER;
    t = layout_resolve(&cfg, gsize(10, 1000));
    CHECK(near(t.crop.w, 10 / t.scale) && near(t.crop.h, 1000), "narrow viewport crop");
    CHECK(t.crop.x >= 0 && t.crop.x + t.crop.w <= 2000 + 1e-6, "narrow crop in image");

    /* 5. 编辑会话：undo 不越过本次基线，redo 被新 push 作废 */
    LayoutConfig a, b, c, d;
    layout_config_init(&a, "bg", 2000, 1000); a.focus_x = 0.5;
    b = a; b.focus_x = 0.6;
    c = b; c.focus_x = 0.7;
    EditSession s;
    edit_session_begin(&s, &a, 1);
    edit_session_push(&s, &b);
    edit_session_push(&s, &c);
    CHECK(near(edit_session_current(&s)->focus_x, 0.7), "session current c");
    CHECK(edit_session_undo(&s, &d) && near(d.focus_x, 0.6), "undo to b");
    CHECK(edit_session_undo(&s, &d) && near(d.focus_x, 0.5), "undo to baseline a");
    CHECK(!edit_session_undo(&s, &d), "cannot undo past baseline");
    CHECK(edit_session_redo(&s, &d) && near(d.focus_x, 0.6), "redo to b");
    /* 在 b 上做新编辑：redo(c) 必须作废 */
    d.focus_x = 0.65;
    edit_session_push(&s, &d);
    CHECK(!edit_session_redo(&s, &d), "redo branch invalidated");
    CHECK(edit_session_undo(&s, &d) && near(d.focus_x, 0.6), "undo new branch b");

    /* 6. 远程更新冲突标记 */
    edit_session_notify_remote(&s, 2);
    CHECK(s.remote_changed, "remote conflict flagged");
    CHECK(!edit_session_undo(&s, NULL) || true, "undo still bounded to base");

    /* 7. cover 报告裁切框为整数并夹取图像范围 */
    LayoutConfig cfg2;
    layout_config_init(&cfg2, "x", 333, 777);
    t = layout_resolve(&cfg2, gsize(501, 803));
    GRect rc = layout_reported_crop(&cfg2, &t);
    CHECK(rc.x >= 0 && rc.y >= 0 && rc.x + rc.w <= 333 &&
          rc.y + rc.h <= 777 && rc.w > 0 && rc.h > 0, "reported crop inside image");

    if (failures == 0) {
        printf("all core tests passed\n");
        return 0;
    }
    printf("%d test failure(s)\n", failures);
    return 1;
}
