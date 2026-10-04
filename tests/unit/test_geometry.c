#include "test.h"
#include "geometry.h"
#include <math.h>

static Geometry mk(const char *fit, double iw, double ih, double w, double h,
                   double dpr, int rot, double fx, double fy) {
    Geometry g = {0};
    g.src_w=iw; g.src_h=ih; g.win_w=w; g.win_h=h; g.dpr=dpr;
    g.rotation=rot; g.fit=fit_mode_from_str(fit); g.focus_x=fx; g.focus_y=fy;
    geometry_compute(&g);
    return g;
}

static void test_cover_center(void) {
    Geometry g = mk("cover", 1276, 838, 800, 600, 1, 0, 0.5, 0.5);
    CHECK_NEAR(g.scale, 600.0/838.0, 1e-9);
    CHECK_NEAR(g.crop.x, (1276 - 800.0/g.scale)/2, 1e-6);
    CHECK_NEAR(g.crop.w, 800.0/g.scale, 1e-6);
    CHECK_NEAR(g.crop.h, 838, 1e-6);
    CHECK_NEAR(g.tx, g.crop.x * -g.scale, 1e-6);
    double n[4]; geo_crop_normalized(&g, n);
    CHECK_NEAR(n[0], 0.062173, 1e-4);
    CHECK_NEAR(n[2], 0.875653, 1e-4);
    CHECK(g.phys_w==800 && g.phys_h==600);
    /* cover：内容左上角在逻辑视口外，右下角超出 */
    double lx,ly;
    geo_src_to_logical(&g,0,0,&lx,&ly);
    CHECK(lx<=0 && ly<=0);
    geo_src_to_logical(&g,1276,838,&lx,&ly);
    CHECK(lx>=800 && ly>=600);
}

static void test_cover_focus_clamp(void) {
    /* 焦点顶到左边：crop.x 必须被钳到 0，不允许露出黑边 */
    Geometry g = mk("cover", 1000, 500, 500, 500, 1, 0, 0.0, 0.5);
    CHECK_NEAR(g.crop.x, 0.0, 1e-9);
    CHECK_NEAR(g.crop.w, 500.0, 1e-9);
    Geometry g2 = mk("cover", 1000, 500, 500, 500, 1, 0, 1.0, 0.5);
    CHECK_NEAR(g2.crop.x, 500.0, 1e-9); /* 右侧钳制 */
}

static void test_contain_center(void) {
    Geometry g = mk("contain", 1000, 500, 800, 800, 1, 0, 0.5, 0.5);
    CHECK_NEAR(g.scale, 0.8, 1e-9);
    CHECK_NEAR(g.content_w, 800, 1e-9);
    CHECK_NEAR(g.content_h, 400, 1e-9);
    CHECK_NEAR(g.tx, 0, 1e-9);
    CHECK_NEAR(g.ty, 200, 1e-9); /* 上下各 200 黑边 */
    CHECK_NEAR(g.lb_top, 200, 1e-9);
    CHECK_NEAR(g.lb_bottom, 200, 1e-9);
    CHECK_NEAR(g.crop.x, 0, 1e-9); CHECK_NEAR(g.crop.y, 0, 1e-9);
    CHECK_NEAR(g.crop.w, 1000, 1e-9); CHECK_NEAR(g.crop.h, 500, 1e-9);
}

static void test_contain_focus_align(void) {
    /* 竖图 500x1000 收进 1000x1000：s=1，内容 500x1000，左右分黑边 */
    Geometry g = mk("contain", 500, 1000, 1000, 1000, 1, 0, 0.0, 0.5);
    CHECK_NEAR(g.content_w, 500, 1e-9);
    CHECK_NEAR(g.tx, 0, 1e-9);              /* fx=0 贴左 */
    CHECK_NEAR(g.lb_right, 500, 1e-9);
    CHECK_NEAR(g.lb_left, 0, 1e-9);
    Geometry g2 = mk("contain", 500, 1000, 1000, 1000, 1, 0, 1.0, 0.5);
    CHECK_NEAR(g2.tx, 500, 1e-9);           /* fx=1 贴右 */
    CHECK_NEAR(g2.lb_left, 500, 1e-9);
    /* 横图 1000x500，fy=1 贴底 */
    Geometry g3 = mk("contain", 1000, 500, 1000, 1000, 1, 0, 0.5, 1.0);
    CHECK_NEAR(g3.ty, 500, 1e-9);
    CHECK_NEAR(g3.lb_top, 500, 1e-9);
    CHECK_NEAR(g3.lb_bottom, 0, 1e-9);
}

static void test_stretch(void) {
    Geometry g = mk("stretch", 1000, 500, 800, 800, 1, 0, 0.5, 0.5);
    CHECK_NEAR(g.scale_x, 0.8, 1e-9);
    CHECK_NEAR(g.scale_y, 1.6, 1e-9);
    double lx,ly; geo_src_to_logical(&g,1000,500,&lx,&ly);
    CHECK_NEAR(lx,800,1e-9); CHECK_NEAR(ly,800,1e-9);
}

static void test_dpr_phys(void) {
    Geometry g = mk("cover", 100, 100, 390, 844, 3.0, 0, 0.5, 0.5);
    CHECK(g.phys_w==1170 && g.phys_h==2532);
    double lx=10, ly=20, px, py;
    geo_logical_to_phys(&g,lx,ly,&px,&py);
    CHECK_NEAR(px,30,1e-9); CHECK_NEAR(py,60,1e-9);
    double bx,by; geo_phys_to_logical(&g,px,py,&bx,&by);
    CHECK_NEAR(bx,lx,1e-9); CHECK_NEAR(by,ly,1e-9);
}

static void test_rotation(void) {
    /* 逻辑 390x844，旋转 90：物理帧缓冲 844*dpr x 390*dpr（横竖屏互换） */
    Geometry g = mk("cover", 100, 200, 390, 844, 1, 90, 0.5, 0.5);
    CHECK(g.phys_w==844 && g.phys_h==390);
    double px,py,bx,by;
    /* 逻辑左上 -> 物理右上 */
    geo_logical_to_phys(&g,0,0,&px,&py);
    CHECK_NEAR(px,844,1e-9); CHECK_NEAR(py,0,1e-9);
    geo_phys_to_logical(&g,px,py,&bx,&by);
    CHECK_NEAR(bx,0,1e-9); CHECK_NEAR(by,0,1e-9);
    /* 逻辑右下 -> 物理左下 */
    geo_logical_to_phys(&g,390,844,&px,&py);
    CHECK_NEAR(px,0,1e-9); CHECK_NEAR(py,390,1e-9);
    /* 180/270 round trip */
    for (int r=0;r<360;r+=90) {
        Geometry q = mk("cover",123,456,321,654,2.5,r,0.3,0.7);
        geo_logical_to_phys(&q,111.1,222.2,&px,&py);
        geo_phys_to_logical(&q,px,py,&bx,&by);
        CHECK_NEAR(bx,111.1,1e-6); CHECK_NEAR(by,222.2,1e-6);
    }
}

static void test_hit_zone(void) {
    /* 热区用源坐标声明；任意 DPI/旋转下物理命中必须与视觉一致 */
    Geometry g = mk("cover", 1000, 1000, 500, 500, 2.0, 0, 0.5, 0.5);
    GeoRect z = {250,250,500,500}; /* 中央半区 */
    /* 源中心 -> 物理 */
    double px,py; geo_src_to_logical(&g,500,500,&px,&py);
    { double tx2,ty2; geo_logical_to_phys(&g,px,py,&tx2,&ty2); px=tx2; py=ty2; }
    CHECK(geo_hit_test(&g,px,py,&z));
    /* 源左上角（cover 被裁），物理点 (0,0) 对应的源点应在 z 外 */
    CHECK(!geo_hit_test(&g,0,0,&z));
    /* 旋转 90 + dpr 2：中心仍命中 */
    Geometry g2 = mk("cover",1000,1000,500,500,2.0,90,0.5,0.5);
    double qx,qy; geo_src_to_logical(&g2,500,500,&qx,&qy);
    geo_logical_to_phys(&g2,qx,qy,&qx,&qy);
    CHECK(geo_hit_test(&g2,qx,qy,&z));
}

static void test_degenerate(void) {
    Geometry g = mk("cover", 100, 100, 0, 500, 2.0, 0, 0.5, 0.5);
    CHECK(g.degenerate==1);
    CHECK(isfinite(g.scale));
    Geometry g2 = mk("cover", 0, 0, 500, 500, 1, 0, 0.5, 0.5);
    CHECK(g2.degenerate==1);
    CHECK(isfinite(g2.tx));
}

static void test_roundtrip_chain(void) {
    /* 任意参数组合 P->L->S->L->P 恒等 */
    const char *fits[]={"cover","contain","stretch"};
    for (int fi=0; fi<3; fi++)
      for (int r=0;r<360;r+=90)
        for (int k=0;k<8;k++) {
            double iw=100+k*37, ih=200+k*53, w=80+k*61, h=300-k*19, dpr=1+k*0.5;
            Geometry g=mk(fits[fi],iw,ih,w,h>0?h:1,dpr,r,(k%7)/7.0,((k*3)%7)/7.0);
            double px=12.5+k*17.3, py=7.7+k*23.1;
            double sx,sy,lx,ly,p2x,p2y;
            geo_phys_to_src(&g,px,py,&sx,&sy);
            geo_src_to_logical(&g,sx,sy,&lx,&ly);
            geo_logical_to_phys(&g,lx,ly,&p2x,&p2y);
            CHECK_NEAR(p2x,px,1e-6); CHECK_NEAR(p2y,py,1e-6);
        }
}

void test_geometry_all(void) {
    RUN(test_cover_center);
    RUN(test_cover_focus_clamp);
    RUN(test_contain_center);
    RUN(test_contain_focus_align);
    RUN(test_stretch);
    RUN(test_dpr_phys);
    RUN(test_rotation);
    RUN(test_hit_zone);
    RUN(test_degenerate);
    RUN(test_roundtrip_chain);
}
