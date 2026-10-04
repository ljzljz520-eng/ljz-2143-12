// 几何变换的 JS 镜像，必须与 src/geometry.c 保持同口径（见 docs/geometry.md）。
// 管理端用它做预览；验收不能只信这里——C 端有独立实现 + 真机截图比对。
'use strict';

export function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }

export function computeGeometry(input) {
  const g = {
    src_w: input.src_w || 0, src_h: input.src_h || 0,
    win_w: input.win_w || 0, win_h: input.win_h || 0,
    dpr: input.dpr > 0 ? input.dpr : 1,
    rotation: ((input.rotation|0) % 360 + 360) % 360,
    fit: input.fit || 'cover',
    focus_x: input.focus_x == null ? 0.5 : clamp(input.focus_x, 0, 1),
    focus_y: input.focus_y == null ? 0.5 : clamp(input.focus_y, 0, 1),
  };
  g.degenerate = 0;
  const {src_w:iw, src_h:ih, win_w:w, win_h:h} = g;
  const phys0 = [Math.round((w||0)*g.dpr), Math.round((h||0)*g.dpr)];
  g.phys_w = (g.rotation===90||g.rotation===270) ? phys0[1] : phys0[0];
  g.phys_h = (g.rotation===90||g.rotation===270) ? phys0[0] : phys0[1];
  if (w<16||h<16||iw<=0||ih<=0) {
    g.degenerate = 1;
    g.scale=g.scale_x=g.scale_y=1; g.tx=g.ty=0;
    g.content_w=g.content_h=0; g.crop={x:0,y:0,w:iw,h:ih};
    g.lb_top=g.lb_left=g.lb_bottom=g.lb_right=0;
    return g;
  }
  let s=1,tx=0,ty=0,cw,ch,sx=1,sy=1;
  let crop={x:0,y:0,w:iw,h:ih};
  let lb={t:0,l:0,b:0,r:0};
  if (g.fit==='stretch') { sx=w/iw; sy=h/ih; s=(sx+sy)/2; cw=w; ch=h; }
  else if (g.fit==='contain') {
    s=Math.min(w/iw,h/ih); cw=iw*s; ch=ih*s;
    tx=g.focus_x*(w-cw); ty=g.focus_y*(h-ch); sx=sy=s;
    lb.l=tx; lb.t=ty; lb.r=w-cw-lb.l; lb.b=h-ch-lb.t;
  } else {
    s=Math.max(w/iw,h/ih); cw=iw*s; ch=ih*s;
    tx=g.focus_x*(w-cw); ty=g.focus_y*(h-ch); sx=sy=s;
    crop={x:-tx/s,y:-ty/s,w:w/s,h:h/s};
  }
  Object.assign(g,{scale:s,scale_x:sx,scale_y:sy,tx,ty,content_w:cw,content_h:h,crop,
    lb_top:lb.t,lb_left:lb.l,lb_bottom:lb.b,lb_right:lb.r});
  return g;
}

export function srcToLogical(g,sx,sy){return [sx*g.scale_x+g.tx, sy*g.scale_y+g.ty];}
export function logicalToSrc(g,lx,ly){return [(lx-g.tx)/g.scale_x,(ly-g.ty)/g.scale_y];}
export function logicalToPhys(g,lx,ly){
  const x=lx*g.dpr,y=ly*g.dpr, wp=g.win_w*g.dpr, hp=g.win_h*g.dpr;
  switch(g.rotation){
    case 90: return [hp-y,x];
    case 180: return [wp-x,hp-y];
    case 270: return [y,wp-x];
    default: return [x,y];
  }
}
export function physToLogical(g,px,py){
  const wp=g.win_w*g.dpr, hp=g.win_h*g.dpr; let x,y;
  switch(g.rotation){
    case 90: x=py;y=hp-px;break;
    case 180: x=wp-px;y=hp-py;break;
    case 270: x=wp-py;y=px;break;
    default: x=px;y=py;
  }
  return [x/g.dpr,y/g.dpr];
}
export function physToSrc(g,px,py){const[lx,ly]=physToLogical(g,px,py);return logicalToSrc(g,lx,ly);}
export function hitTest(g,px,py,z){
  const [sx,sy]=physToSrc(g,px,py);
  return sx>=z.x&&sx<=z.x+z.w&&sy>=z.y&&sy<=z.y+z.h;
}
export function cropNormalized(g){
  if(!g.src_w||!g.src_h) return [0,0,0,0];
  return [clamp(g.crop.x/g.src_w,0,1),clamp(g.crop.y/g.src_h,0,1),
          clamp(g.crop.w/g.src_w,0,1),clamp(g.crop.h/g.src_h,0,1)];
}
