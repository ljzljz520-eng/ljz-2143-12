import {EventQueue} from '../../web/events.js';
let n=0,f=0; const ok=(c,m)=>{n++;if(!c){f++;console.log('FAIL',m);}};
let q=new EventQueue();
for(let i=0;i<50;i++) q.pushSize(300+i,600+i,0);
ok(q.coalesced===49,'50 events coalesce to 1');
let e=q.drain(); ok(e.w===349&&e.h===649,'latest size applied');
ok(q.drain()===null,'drained empty');

q=new EventQueue();
q.pushSize(100,200,0);
q.commitConfig();
ok(q.drain()===null,'stale epoch event dropped after save');
ok(q.droppedStale===1,'dropped counted');
q.pushSize(300,400,1);
e=q.drain(); ok(e.w===300&&e.display===1,'new epoch applies');

console.log(`JS events: ${n} checks, ${f} failed`);
process.exit(f?1:0);
