// 事件合并 + 配置代际防护的 JS 镜像，与 src/events.c 同语义。
'use strict';
export class EventQueue {
  constructor(){this.epoch=1;this.pending=null;this.droppedStale=0;this.coalesced=0;}
  commitConfig(){this.epoch++;}
  pushSize(w,h,display=0){
    if(this.pending) this.coalesced++;
    this.pending={w,h,display,epoch:this.epoch};
  }
  drain(){
    if(!this.pending) return null;
    const e=this.pending; this.pending=null;
    if(e.epoch!==this.epoch){this.droppedStale++; return this.drain();}
    return e;
  }
}
