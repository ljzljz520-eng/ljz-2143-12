// node tests/unit/geometry.test.js  (无第三方依赖的极简断言)
import {computeGeometry, srcToLogical, logicalToPhys, physToLogical,
        physToSrc, hitTest, cropNormalized} from '../../web/geometry.js';

let n=0, f=0;
const ok=(c,m)=>{n++; if(!c){f++; console.log('FAIL',m);}};
const near=(a,b,eps=1e-6,m)=>{n++; if(Math.abs(a-b)>eps){f++;console.log(`FAIL ${m}: ${a} vs ${b}`);}};

// 与 C 端 test_geometry 同口径的关键用例（交叉实现验证）
let g=computeGeometry({src_w:1276,src_h:838,win_w:800,win_h:600,dpr:1,rotation:0,fit:'cover'});
near(g.scale,600/838,1e-9,'cover scale');
let c=cropNormalized(g); near(c[0],0.062173,1e-4,'cover norm x'); near(c[2],0.875653,1e-4,'cover norm w');

g=computeGeometry({src_w:1000,src_h:500,win_w:800,win_h:800,fit:'contain'});
near(g.ty,200,1e-9,'contain letterbox top'); near(g.lb_bottom,200,1e-9,'contain lb bottom');
near(g.crop.w,1000,1e-9,'contain full image');

g=computeGeometry({src_w:500,src_h:1000,win_w:1000,win_h:1000,fit:'contain',focus_x:0,focus_y:0.5});
near(g.tx,0,1e-9,'contain fx=0 left');
g=computeGeometry({src_w:500,src_h:1000,win_w:1000,win_h:1000,fit:'contain',focus_x:1,focus_y:0.5});
near(g.tx,500,1e-9,'contain fx=1 right');

g=computeGeometry({src_w:100,src_h:200,win_w:390,win_h:844,dpr:1,rotation:90,fit:'cover'});
ok(g.phys_w===844&&g.phys_h===390,'rot90 swapped framebuffer');
let [px,py]=logicalToPhys(g,0,0); near(px,844,1e-9,'rot90 TL->TR x'); near(py,0,1e-9,'rot90 TL y');
let [lx,ly]=physToLogical(g,px,py); near(lx,0,1e-9,'rot90 inv x'); near(ly,0,1e-9,'rot90 inv y');

for(const rot of [0,90,180,270]) for(const dpr of [1,2,3]) {
  g=computeGeometry({src_w:123,src_h:456,win_w:321,win_h:654,dpr,rotation:rot,
                     fit: rot%180?'cover':'contain',focus_x:0.3,focus_y:0.7});
  const [sx,sy]=physToSrc(g,55.5,77.7);
  const [a,b]=srcToLogical(g,sx,sy);
  const [p2x,p2y]=logicalToPhys(g,a,b);
  const [l0]=physToLogical(g,55.5,77.7);
  near(p2x,55.5,1e-5,`roundtrip x rot${rot} dpr${dpr}`);
  near(p2y,77.7,1e-5,`roundtrip y rot${rot} dpr${dpr}`);
}

// 高 DPI 命中：热区源坐标，物理点命中视觉中心
g=computeGeometry({src_w:1000,src_h:1000,win_w:500,win_h:500,dpr:2,fit:'cover'});
const z={x:250,y:250,w:500,h:500};
{const [qx,qy]=srcToLogical(g,500,500); const [rx,ry]=logicalToPhys(g,qx,qy);
 ok(hitTest(g,rx,ry,z),'center hit dpr2');}
g=computeGeometry({src_w:1000,src_h:1000,win_w:500,win_h:500,dpr:2,rotation:90,fit:'cover'});
{const [qx,qy]=srcToLogical(g,500,500); const [rx,ry]=logicalToPhys(g,qx,qy);
 ok(hitTest(g,rx,ry,z),'center hit rot90 dpr2');}

g=computeGeometry({src_w:100,src_h:100,win_w:0,win_h:500,dpr:2,fit:'cover'});
ok(g.degenerate===1&&isFinite(g.scale),'degenerate narrow window');

console.log(`JS geometry: ${n} checks, ${f} failed`);
process.exit(f?1:0);
