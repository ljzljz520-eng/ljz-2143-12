/* JS(geo.js) 与 C(layout/geometry) 数值对拍。
   C 参考值由编译进 src/test_core 的逻辑等价生成；这里直接断言关键值，
   并独立验证旋转互逆，保证三端同构。 */
'use strict';
const path = require('path');
const G = require(path.join(__dirname, '..', 'web', 'geo.js'));

let fail = 0;
const ok = (c, m) => { if (!c) { console.error('FAIL:', m); fail++; } };
const near = (a, b, e = 1e-6) => Math.abs(a - b) < e;

// cover 2000x1000 -> 1000x1000, 居中焦点
let t = G.resolveLayout(
  { image_w: 2000, image_h: 1000, mode: 'cover', focus_x: 0.5, focus_y: 0.5, rotation: 0 },
  { w: 1000, h: 1000 });
ok(near(t.scale, 1), 'cover scale 1');
ok(near(t.crop.x, 500) && near(t.crop.w, 1000), 'cover centered crop');

// contain
t = G.resolveLayout(
  { image_w: 2000, image_h: 1000, mode: 'contain', focus_x: 0.5, focus_y: 0.5 },
  { w: 1000, h: 1000 });
ok(near(t.scale, 0.5) && near(t.target.y, 250), 'contain letterbox');
const hit = G.hitTest(t, { x: 500, y: 500 });
ok(hit.inside && near(hit.imagePoint.x, 1000) && near(hit.imagePoint.y, 500),
   'contain hit -> source');
ok(!G.hitTest(t, { x: 500, y: 100 }).inside, 'letterbox miss');

// 旋转互逆
for (const dpr of [1, 2, 2.5]) {
  for (const rot of [0, 90, 180, 270]) {
    const lg = { w: 640, h: 360 };
    const f = G.logicalToPhysical(lg, dpr, rot);
    const inv = G.physicalToLogical(lg, dpr, rot);
    for (const q of [{ x: 0, y: 0 }, { x: 33.3, y: 200 }, { x: 640, y: 360 }]) {
      const b = G.affineApply(inv, G.affineApply(f, q));
      ok(near(b.x, q.x) && near(b.y, q.y), `rot roundtrip dpr=${dpr} rot=${rot}`);
    }
  }
}

// reportedCrop 整数夹取
t = G.resolveLayout(
  { image_w: 333, image_h: 777, mode: 'cover', focus_x: 0.5, focus_y: 0.5 },
  { w: 501, h: 803 });
const rc = G.reportedCrop(t);
ok(rc.x >= 0 && rc.y >= 0 && rc.x + rc.w <= 333 && rc.y + rc.h <= 777 &&
   rc.w > 0 && rc.h > 0, 'reported crop inside image');

if (fail) { console.error(fail + ' parity failure(s)'); process.exit(1); }
console.log('geo parity (JS) OK');
