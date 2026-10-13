/* ================= A* 寻路交互实验（纯前端，无依赖） =================
 * 地图：{W,H,blk:Uint8Array,S,T}，下标 i = y*W + x
 * 算法是 generator，直接改写共享状态 ctx（g/h/f/par/st），并 yield 事件给可视化器：
 *   push(i) 入 open / 更新    pop(i) 取出并放入 closed    scan(cells) JPS 沿直线扫描过的格子
 *   done(path)  找到路径       fail  无路径
 * 代价：直走 1，斜走 √2；8 方向时禁止切角（两侧直邻格都可走才允许斜走）
 */
(function(){
"use strict";
const SQ2=Math.SQRT2;
const DIRS=[[1,0],[-1,0],[0,1],[0,-1],[1,1],[1,-1],[-1,1],[-1,-1]];
function rng(seed){let s=seed>>>0||1;return ()=>{s^=s<<13;s>>>=0;s^=s>>>17;s^=s<<5;s>>>=0;return s/4294967296;};}

const HF={
  manhattan:(dx,dy)=>dx+dy,
  octile:(dx,dy)=>Math.max(dx,dy)+(SQ2-1)*Math.min(dx,dy),
  euclid:(dx,dy)=>Math.hypot(dx,dy),
  chebyshev:(dx,dy)=>Math.max(dx,dy),
  zero:()=>0,
};
const HNAME={manhattan:"曼哈顿",octile:"Octile（八方向）",euclid:"欧几里得",chebyshev:"切比雪夫",zero:"h = 0"};

/* ---------------- 二叉堆：按 (f, h, 入队序号) 排序 ---------------- */
class Heap{
  constructor(){this.a=[];this.seq=0;}
  get size(){return this.a.length;}
  lt(p,q){ if(Math.abs(p.f-q.f)>1e-9) return p.f<q.f; if(Math.abs(p.h-q.h)>1e-9) return p.h<q.h; return p.s<q.s; }
  push(f,h,i){const a=this.a;a.push({f,h,i,s:this.seq++});let k=a.length-1;while(k>0){const p=(k-1)>>1;if(!this.lt(a[k],a[p]))break;[a[k],a[p]]=[a[p],a[k]];k=p;}}
  pop(){const a=this.a;const top=a[0];const last=a.pop();if(a.length){a[0]=last;let k=0;for(;;){let c=2*k+1;if(c>=a.length)break;if(c+1<a.length&&this.lt(a[c+1],a[c]))c++;if(!this.lt(a[c],a[k]))break;[a[c],a[k]]=[a[k],a[c]];k=c;}}return top.i;}
}

/* ---------------- 地图 ---------------- */
function makeMap(W,H,preset,seed){
  for(let tries=0;tries<30;tries++){
    const R=rng(seed+tries*977), blk=new Uint8Array(W*H);
    const set=(x,y,v=1)=>{if(x>=0&&y>=0&&x<W&&y<H) blk[y*W+x]=v;};
    let sx=2,sy=H>>1,tx=W-3,ty=H>>1;
    if(preset==="random"){ for(let i=0;i<W*H;i++) blk[i]=R()<0.28?1:0; sx=1;sy=1;tx=W-2;ty=H-2; }
    else if(preset==="trap"){
      const x1=Math.round(W*0.62),x0=Math.round(W*0.34),y0=Math.round(H*0.18),y1=Math.round(H*0.82);
      for(let y=y0;y<=y1;y++) set(x1,y);
      for(let x=x0;x<=x1;x++){set(x,y0);set(x,y1);}
      sx=Math.round(W*0.47);sy=H>>1;tx=Math.round(W*0.88);ty=H>>1;
    } else if(preset==="maze"){
      blk.fill(1); const cw=(W-1)>>1, ch=(H-1)>>1, vis=new Uint8Array(cw*ch), st=[0]; vis[0]=1; set(1,1,0);
      while(st.length){const c=st[st.length-1],cx=c%cw,cy=(c/cw)|0;const cand=[];
        for(const [dx,dy] of DIRS.slice(0,4)){const nx=cx+dx,ny=cy+dy;if(nx>=0&&ny>=0&&nx<cw&&ny<ch&&!vis[ny*cw+nx])cand.push([dx,dy]);}
        if(!cand.length){st.pop();continue;}
        const [dx,dy]=cand[Math.floor(R()*cand.length)],nx=cx+dx,ny=cy+dy;vis[ny*cw+nx]=1;set(2*cx+1+dx,2*cy+1+dy,0);set(2*nx+1,2*ny+1,0);st.push(ny*cw+nx);}
      // 打通少量墙，产生环路（纯树形迷宫里所有算法都只有一条路）
      for(let k=0;k<W*H/40;k++){const x=1+Math.floor(R()*(W-2)),y=1+Math.floor(R()*(H-2));if((x+y)%2===1) set(x,y,0);}
      sx=1;sy=1;tx=2*(cw-1)+1;ty=2*(ch-1)+1;
    } else if(preset==="rooms"){
      const RS=8;
      for(let y=0;y<H;y++)for(let x=0;x<W;x++) if(x%RS===0||y%RS===0) set(x,y);
      for(let ry=0;ry*RS<H;ry++)for(let rx=0;rx*RS<W;rx++){
        const ox=rx*RS,oy=ry*RS;
        if(ox+RS<W){const o=1+Math.floor(R()*(RS-3));set(ox+RS,oy+o,0);set(ox+RS,oy+o+1,0);}
        if(oy+RS<H){const o=1+Math.floor(R()*(RS-3));set(ox+o,oy+RS,0);set(ox+o+1,oy+RS,0);}
      }
      sx=2;sy=2;tx=W-3;ty=H-3;
    }
    set(sx,sy,0);set(tx,ty,0);
    const m={W,H,blk,S:sy*W+sx,T:ty*W+tx};
    if(preset!=="random"||reachable(m)) return m;
  }
  const blk=new Uint8Array(W*H); return {W,H,blk,S:(H>>1)*W+2,T:(H>>1)*W+W-3};
}
function walk(m,x,y){return x>=0&&y>=0&&x<m.W&&y<m.H&&!m.blk[y*m.W+x];}
function canMove(m,x,y,dx,dy,diag){ if(!walk(m,x+dx,y+dy)) return false; if(dx&&dy) return diag&&walk(m,x+dx,y)&&walk(m,x,y+dy); return true; }
function reachable(m){ const seen=new Uint8Array(m.W*m.H),q=[m.S];seen[m.S]=1;
  for(let h=0;h<q.length;h++){const i=q[h];if(i===m.T)return true;const x=i%m.W,y=(i/m.W)|0;
    for(const [dx,dy] of DIRS.slice(0,4)) if(walk(m,x+dx,y+dy)){const j=(y+dy)*m.W+x+dx;if(!seen[j]){seen[j]=1;q.push(j);}}}
  return false; }

function newCtx(N){return {g:new Float64Array(N).fill(Infinity),h:new Float64Array(N),f:new Float64Array(N),par:new Int32Array(N).fill(-1),st:new Uint8Array(N),scan:new Uint8Array(N),cur:-1,path:null,jps:false,exp:0};}
const fmt=v=>Number.isFinite(v)?(Math.round(v*100)/100).toString():"∞";
const xy=(m,i)=>"("+(i%m.W)+","+((i/m.W)|0)+")";
function tracePath(ctx,T){const p=[];for(let i=T;i!==-1;i=ctx.par[i])p.push(i);return p.reverse();}

/* ---------------- 算法 ---------------- */
const ALGO={};
const PC_BEST=[
"open = {S}；g[S] = 0",
"while open 非空:",
"  cur = open 中 f 最小的节点（移入 closed）",
"  if cur == G: 沿 parent 回溯得到路径",
"  for nb in cur 的邻居（障碍 / closed 跳过）:",
"    ng = g[cur] + cost(cur, nb)",
"    if nb 不在 open 或 ng < g[nb]:",
"      g[nb] = ng；parent[nb] = cur",
"      f[nb] = ①；放入 / 更新 open",
"return 无路径"];
function bestFirst(mode){
  return function*(m,ctx,o){
    const {W,S,T}=m, tx=T%W, ty=(T/W)|0;
    const hf=i=>mode==="dijkstra"?0:HF[o.h](Math.abs(i%W-tx),Math.abs(((i/W)|0)-ty));
    const fv=(g,h)=>mode==="greedy"?h:mode==="wastar"?g+o.w*h:g+h;
    const heap=new Heap();
    ctx.g[S]=0;ctx.h[S]=hf(S);ctx.f[S]=fv(0,ctx.h[S]);ctx.st[S]=1;heap.push(ctx.f[S],ctx.h[S],S);
    yield {t:"push",i:S,L:0,msg:"起点 S 放入 open，g = 0，h = "+fmt(ctx.h[S])};
    while(heap.size){
      const i=heap.pop(); if(ctx.st[i]===2) continue;
      ctx.st[i]=2;ctx.cur=i;ctx.exp++;
      if(i===T){ctx.path=tracePath(ctx,T);yield {t:"done",L:3,msg:"取出的是终点 G：路径代价 "+fmt(ctx.g[T])+"，共展开 "+ctx.exp+" 个节点"};return;}
      yield {t:"pop",i,L:2,msg:"展开 "+xy(m,i)+"：f = "+fmt(ctx.f[i])+"（g = "+fmt(ctx.g[i])+"，h = "+fmt(ctx.h[i])+"）"};
      const x=i%W,y=(i/W)|0;
      for(const [dx,dy] of o.diag?DIRS:DIRS.slice(0,4)){
        if(!canMove(m,x,y,dx,dy,o.diag)) continue;
        const j=(y+dy)*W+x+dx; if(ctx.st[j]===2) continue;
        const ng=ctx.g[i]+(dx&&dy?SQ2:1);
        if(ctx.st[j]===1&&ng>=ctx.g[j]-1e-9) continue;
        const upd=ctx.st[j]===1;
        ctx.g[j]=ng;ctx.par[j]=i;ctx.h[j]=hf(j);ctx.f[j]=fv(ng,ctx.h[j]);ctx.st[j]=1;heap.push(ctx.f[j],ctx.h[j],j);
        yield {t:"push",i:j,L:8,msg:(upd?"更新 ":"加入 ")+xy(m,j)+"：g = "+fmt(ng)+"，h = "+fmt(ctx.h[j])+"，f = "+fmt(ctx.f[j])};
      }
    }
    yield {t:"fail",L:9,msg:"open 已空，终点不可达（已展开 "+ctx.exp+" 个节点）"};
  };
}
ALGO.astar={name:"A*",code:PC_BEST.map(s=>s.replace("①","g + h")),gen:bestFirst("astar"),useH:true};
ALGO.dijkstra={name:"Dijkstra",code:PC_BEST.map(s=>s.replace("①","g（h ≡ 0）").replace("f 最小","g 最小")),gen:bestFirst("dijkstra")};
ALGO.greedy={name:"贪心最佳优先",code:PC_BEST.map(s=>s.replace("①","h（只看离终点多远）")),gen:bestFirst("greedy"),useH:true};
ALGO.wastar={name:"加权 A*",code:PC_BEST.map(s=>s.replace("①","g + w·h")),gen:bestFirst("wastar"),useH:true,useW:true};
ALGO.bfs={name:"BFS",code:[
"queue = [S]；visited = {S}",
"while queue 非空:",
"  cur = queue.pop_front()",
"  if cur == G: 回溯得到路径",
"  for nb in cur 的邻居:",
"    if nb 未访问: visited += nb",
"      parent[nb] = cur；queue.push_back(nb)",
"return 无路径",
"// 不看代价：斜走和直走都算 1 步"],
gen:function*(m,ctx,o){
  const {W,S,T}=m; const q=[S]; ctx.g[S]=0; ctx.st[S]=1;
  yield {t:"push",i:S,L:0,msg:"起点入队"};
  for(let k=0;k<q.length;k++){
    const i=q[k]; ctx.st[i]=2; ctx.cur=i; ctx.exp++;
    if(i===T){ctx.path=tracePath(ctx,T);yield {t:"done",L:3,msg:"到达终点：走了 "+(ctx.path.length-1)+" 步，真实代价 "+fmt(ctx.g[T])};return;}
    yield {t:"pop",i,L:2,msg:"出队 "+xy(m,i)+"（第 "+fmt(ctx.h[i])+" 层）"};
    const x=i%W,y=(i/W)|0;
    for(const [dx,dy] of o.diag?DIRS:DIRS.slice(0,4)){
      if(!canMove(m,x,y,dx,dy,o.diag)) continue;
      const j=(y+dy)*W+x+dx; if(ctx.st[j]) continue;
      ctx.st[j]=1;ctx.par[j]=i;ctx.g[j]=ctx.g[i]+(dx&&dy?SQ2:1);ctx.h[j]=ctx.h[i]+1;ctx.f[j]=ctx.h[j];q.push(j);
      yield {t:"push",i:j,L:6,msg:"入队 "+xy(m,j)};
    }
  }
  yield {t:"fail",L:7,msg:"不可达"};
}};
ALGO.jps={name:"JPS 跳点搜索",needDiag:true,code:[
"open = {S}",
"while open 非空:",
"  cur = open 中 f 最小的跳点",
"  if cur == G: 依次连接各跳点得到路径",
"  dirs = 根据“从哪个方向来”剪枝后的方向",
"  for d in dirs:",
"    jp = jump(cur, d)",
"    // 沿 d 直走，遇到强制邻居或终点才停",
"    if jp 存在且不在 closed: 更新 g、open",
"return 无路径"],
gen:function*(m,ctx,o){
  const {W,S,T}=m, tx=T%W, ty=(T/W)|0; ctx.jps=true;
  const hf=i=>HF.octile(Math.abs(i%W-tx),Math.abs(((i/W)|0)-ty));
  const w=(x,y)=>walk(m,x,y);
  let cells=[];
  function jump(x,y,dx,dy){
    for(;;){
      if(!w(x,y)) return -1;
      const i=y*W+x; if(!ctx.scan[i]){ctx.scan[i]=1;cells.push(i);}
      if(i===T) return i;
      if(dx&&dy){ if(jump(x+dx,y,dx,0)>=0||jump(x,y+dy,0,dy)>=0) return i; if(!(w(x+dx,y)&&w(x,y+dy))) return -1; }
      else if(dx){ if((w(x,y-1)&&!w(x-dx,y-1))||(w(x,y+1)&&!w(x-dx,y+1))) return i; }
      else { if((w(x-1,y)&&!w(x-1,y-dy))||(w(x+1,y)&&!w(x+1,y-dy))) return i; }
      x+=dx;y+=dy;
    }
  }
  const heap=new Heap(); ctx.g[S]=0;ctx.h[S]=hf(S);ctx.f[S]=ctx.h[S];ctx.st[S]=1;heap.push(ctx.f[S],ctx.h[S],S);
  yield {t:"push",i:S,L:0,msg:"起点放入 open"};
  while(heap.size){
    const i=heap.pop(); if(ctx.st[i]===2) continue;
    ctx.st[i]=2;ctx.cur=i;ctx.exp++;
    if(i===T){ctx.path=tracePath(ctx,T);yield {t:"done",L:3,msg:"到达终点：只展开了 "+ctx.exp+" 个跳点，路径代价 "+fmt(ctx.g[T])};return;}
    yield {t:"pop",i,L:2,msg:"展开跳点 "+xy(m,i)+"，f = "+fmt(ctx.f[i])};
    const x=i%W,y=(i/W)|0, dirs=[];
    if(ctx.par[i]<0){ for(const [dx,dy] of DIRS) if(canMove(m,x,y,dx,dy,true)) dirs.push([dx,dy]); }
    else{
      const px=ctx.par[i]%W,py=(ctx.par[i]/W)|0, dx=Math.sign(x-px), dy=Math.sign(y-py);
      if(dx&&dy){const v=w(x,y+dy),hz=w(x+dx,y); if(v)dirs.push([0,dy]); if(hz)dirs.push([dx,0]); if(v&&hz)dirs.push([dx,dy]);}
      else if(dx){const nx=w(x+dx,y),up=w(x,y+1),dn=w(x,y-1); if(nx){dirs.push([dx,0]); if(up)dirs.push([dx,1]); if(dn)dirs.push([dx,-1]);} if(up)dirs.push([0,1]); if(dn)dirs.push([0,-1]);}
      else {const nx=w(x,y+dy),rt=w(x+1,y),lf=w(x-1,y); if(nx){dirs.push([0,dy]); if(rt)dirs.push([1,dy]); if(lf)dirs.push([-1,dy]);} if(rt)dirs.push([1,0]); if(lf)dirs.push([-1,0]);}
    }
    yield {t:"line",L:4,msg:"剪枝后剩 "+dirs.length+" 个方向"};
    for(const [dx,dy] of dirs){
      cells=[]; const j=jump(x+dx,y+dy,dx,dy);
      yield {t:"scan",L:6,msg:"沿 ("+dx+","+dy+") 扫描 "+cells.length+" 格"+(j>=0?"，找到跳点 "+xy(m,j):"，撞墙，无跳点")};
      if(j<0||ctx.st[j]===2) continue;
      const ddx=Math.abs(j%W-x),ddy=Math.abs(((j/W)|0)-y), ng=ctx.g[i]+Math.max(ddx,ddy)+(SQ2-1)*Math.min(ddx,ddy);
      if(ctx.st[j]===1&&ng>=ctx.g[j]-1e-9) continue;
      ctx.g[j]=ng;ctx.par[j]=i;ctx.h[j]=hf(j);ctx.f[j]=ng+ctx.h[j];ctx.st[j]=1;heap.push(ctx.f[j],ctx.h[j],j);
      yield {t:"push",i:j,L:8,msg:"跳点 "+xy(m,j)+" 进入 open，f = "+fmt(ctx.f[j])};
    }
  }
  yield {t:"fail",L:9,msg:"不可达"};
}};

function runAll(m,key,o){ const ctx=newCtx(m.W*m.H); const it=ALGO[key].gen(m,ctx,o); let n=0; for(const _ of it){ if(++n>5e6) break; } return ctx; }

/* ---------------- 绘制 ---------------- */
const COL={wall:"#4B5563",open:"#BBF7D0",openB:"#4ADE80",closed:"#BFDBFE",scan:"#EDE9FE",cur:"#FBBF24",path:"#C2410C",S:"#2F5BD3",T:"#B91C1C",grid:"#EEF0F3"};
function draw(cv,m,ctx,opt){
  opt=opt||{};
  const dpr=window.devicePixelRatio||1, Wp=cv.clientWidth||600, cs=Wp/m.W, Hp=cs*m.H;
  cv.style.height=Hp+"px"; cv.width=Wp*dpr; cv.height=Hp*dpr;
  const c=cv.getContext("2d"); c.setTransform(dpr,0,0,dpr,0,0); c.clearRect(0,0,Wp,Hp);
  c.fillStyle="#fff"; c.fillRect(0,0,Wp,Hp);
  for(let i=0;i<m.W*m.H;i++){
    const x=(i%m.W)*cs,y=((i/m.W)|0)*cs; let f=null;
    if(m.blk[i]) f=COL.wall;
    else if(ctx){ if(ctx.st[i]===2) f=COL.closed; else if(ctx.st[i]===1) f=COL.open; else if(ctx.scan[i]) f=COL.scan; }
    if(f){c.fillStyle=f;c.fillRect(x,y,cs+.5,cs+.5);}
  }
  if(cs>=7){ c.strokeStyle=COL.grid; c.lineWidth=1; c.beginPath();
    for(let x=0;x<=m.W;x++){c.moveTo(x*cs+.5,0);c.lineTo(x*cs+.5,Hp);} for(let y=0;y<=m.H;y++){c.moveTo(0,y*cs+.5);c.lineTo(Wp,y*cs+.5);} c.stroke(); }
  const ctr=i=>[(i%m.W+.5)*cs,(((i/m.W)|0)+.5)*cs];
  if(ctx&&ctx.cur>=0&&!ctx.path){const [x,y]=ctr(ctx.cur);c.fillStyle=COL.cur;c.fillRect(x-cs/2,y-cs/2,cs,cs);}
  if(ctx&&opt.parents&&cs>=14){ c.strokeStyle="rgba(75,85,99,.45)"; c.lineWidth=1; c.beginPath();
    for(let i=0;i<m.W*m.H;i++){ const p=ctx.par[i]; if(p<0||!ctx.st[i]) continue; const [x,y]=ctr(i),[px,py]=ctr(p); c.moveTo(x,y); c.lineTo(x+(px-x)*.42,y+(py-y)*.42);} c.stroke(); }
  if(ctx&&ctx.path){ c.strokeStyle=COL.path; c.lineWidth=Math.max(2,cs*.22); c.lineJoin="round"; c.lineCap="round"; c.beginPath();
    ctx.path.forEach((i,k)=>{const [x,y]=ctr(i); k?c.lineTo(x,y):c.moveTo(x,y);}); c.stroke();
    if(ctx.jps){ c.fillStyle=COL.path; ctx.path.forEach(i=>{const [x,y]=ctr(i);c.beginPath();c.arc(x,y,Math.max(2.5,cs*.25),0,7);c.fill();}); } }
  for(const [i,col,t] of [[m.S,COL.S,"S"],[m.T,COL.T,"G"]]){ const [x,y]=ctr(i); c.fillStyle=col; c.beginPath(); c.arc(x,y,Math.max(3.5,cs*.42),0,7); c.fill();
    if(cs>=13){c.fillStyle="#fff";c.font="bold "+Math.round(cs*.55)+"px sans-serif";c.textAlign="center";c.textBaseline="middle";c.fillText(t,x,y+.5);} }
  return cs;
}
const LEGEND='<div class="legend"><span><i style="background:'+COL.open+'"></i>open（待展开）</span><span><i style="background:'+COL.closed+'"></i>closed（已展开）</span><span><i style="background:'+COL.cur+'"></i>当前节点</span><span><i style="background:'+COL.scan+'"></i>JPS 扫描过</span><span><i style="background:'+COL.path+'"></i>路径</span><span><i style="background:'+COL.wall+'"></i>障碍</span></div>';
const PRESETS={empty:"空地",random:"随机障碍",trap:"U 型陷阱",maze:"迷宫",rooms:"房间"};
function pathCells(m,ctx){ if(!ctx.path) return 0; let n=0; for(let k=1;k<ctx.path.length;k++){const a=ctx.path[k-1],b=ctx.path[k];n+=Math.max(Math.abs(a%m.W-b%m.W),Math.abs(((a/m.W)|0)-((b/m.W)|0)));} return n; }

/* ---------------- 单算法交互实验 ---------------- */
function GridLab(root){
  const W=+(root.dataset.w||40), H=+(root.dataset.h||24);
  let algo=root.dataset.algo||"astar", preset=root.dataset.map||"trap", seed=+(root.dataset.seed||5);
  let heur=root.dataset.heur||"octile", wgt=+(root.dataset.weight||2), diag=root.dataset.diag!=="0", speed=+(root.dataset.speed||14);
  root.innerHTML='<div class="t">'+(root.dataset.title||"交互实验 · 寻路沙盒（可拖动 S / G，在空格上拖动画墙、在墙上拖动擦除）")+'</div>'+
    '<div class="top"><label>算法 <select class="al">'+Object.keys(ALGO).map(k=>'<option value="'+k+'"'+(k===algo?" selected":"")+'>'+ALGO[k].name+'</option>').join("")+'</select></label>'+
    '<label>启发 <select class="hf">'+["octile","euclid","manhattan","chebyshev"].map(k=>'<option value="'+k+'"'+(k===heur?" selected":"")+'>'+HNAME[k]+'</option>').join("")+'</select></label>'+
    '<label>w <select class="wt">'+[1.5,2,3,5].map(v=>'<option'+(v===wgt?" selected":"")+'>'+v+'</option>').join("")+'</select></label>'+
    '<label><input type="checkbox" class="dg"'+(diag?" checked":"")+'> 8 方向</label>'+
    '<label>地图 <select class="mp">'+Object.entries(PRESETS).map(([k,v])=>'<option value="'+k+'"'+(k===preset?" selected":"")+'>'+v+'</option>').join("")+'</select></label>'+
    '<label>速度 <input type="range" class="sp" min="1" max="60" value="'+speed+'"></label></div>'+
    '<div class="top"><button class="st-btn play">▶ 播放</button><button class="st-btn step">单步</button><button class="st-btn fin">直接出结果</button><button class="st-btn clr">清除结果</button><button class="st-btn sh">换一张图</button></div>'+
    '<div class="stage"><canvas></canvas><div class="pc"></div></div>'+
    '<div class="stats">已展开 <b class="se">0</b>　open 大小 <b class="so">0</b>　路径代价 <b class="sc">—</b>　路径格数 <b class="sl">—</b>　<span class="state"></span></div>'+
    '<div class="msg"></div>'+LEGEND;
  const $=s=>root.querySelector(s), cv=$("canvas"), pc=$(".pc");
  let m=makeMap(W,H,preset,seed), ctx=null, gen=null, L=-1, timer=null, acc=0, last=0, fin=false, cs=10;
  function opts(){return {h:heur,w:wgt,diag:ALGO[algo].needDiag?true:diag};}
  function reset(){
    stop(); ctx=newCtx(W*H); gen=ALGO[algo].gen(m,ctx,opts()); L=-1; fin=false;
    pc.innerHTML=ALGO[algo].code.map(l=>"<div>"+l.replace(/&/g,"&amp;").replace(/</g,"&lt;")+"</div>").join("");
    $(".hf").disabled=!ALGO[algo].useH; $(".wt").disabled=!ALGO[algo].useW;
    $(".msg").textContent=ALGO[algo].needDiag&&!diag?"JPS 只定义在 8 方向格子上，已按 8 方向运行。":"点击「播放」或「单步」开始；鼠标悬停在格子上可查看 g / h / f。";
    render();
  }
  function step(){
    if(fin) return false;
    const r=gen.next();
    if(r.done){fin=true;stop();render();return false;}
    const e=r.value; L=e.L; if(e.msg) $(".msg").textContent=e.msg;
    if(e.t==="done"||e.t==="fail"){fin=true;stop();}
    render(); return !fin;
  }
  function render(){
    cs=draw(cv,m,ctx,{parents:true});
    pc.querySelectorAll("div").forEach((d,i)=>d.classList.toggle("on",i===L));
    let open=0; if(ctx) for(let i=0;i<W*H;i++) if(ctx.st[i]===1) open++;
    $(".se").textContent=ctx?ctx.exp:0; $(".so").textContent=open;
    $(".sc").textContent=ctx&&ctx.path?fmt(ctx.g[m.T]):"—"; $(".sl").textContent=ctx&&ctx.path?pathCells(m,ctx):"—";
    $(".state").textContent=fin?(ctx&&ctx.path?"✔ 找到路径":"✘ 无路径"):"";
  }
  function loop(ts){ if(!timer) return; if(!last) last=ts; acc+=(ts-last)/1000*speed*speed; last=ts;
    let k=0; while(acc>=1&&k<400){acc-=1;k++; if(!stepQuiet()) {render();return;}} render(); timer=requestAnimationFrame(loop); }
  function stepQuiet(){ if(fin) return false; const r=gen.next(); if(r.done){fin=true;stop();return false;} const e=r.value; L=e.L; if(e.msg) $(".msg").textContent=e.msg; if(e.t==="done"||e.t==="fail"){fin=true;stop();return false;} return true; }
  function play(){ if(fin) reset(); if(timer){stop();return;} $(".play").textContent="⏸ 暂停"; last=0; acc=1; timer=requestAnimationFrame(loop); }
  function stop(){ if(timer) cancelAnimationFrame(timer); timer=null; const b=$(".play"); if(b) b.textContent="▶ 播放"; }
  $(".play").onclick=play; $(".step").onclick=()=>{stop();step();};
  $(".fin").onclick=()=>{stop(); let n=0; while(stepQuiet()&&++n<5e6){} render();};
  $(".clr").onclick=reset; $(".sh").onclick=()=>{seed=(seed*7+13)%9973;m=makeMap(W,H,preset,seed);reset();};
  $(".al").onchange=e=>{algo=e.target.value;reset();}; $(".hf").onchange=e=>{heur=e.target.value;reset();};
  $(".wt").onchange=e=>{wgt=+e.target.value;reset();}; $(".dg").onchange=e=>{diag=e.target.checked;reset();};
  $(".mp").onchange=e=>{preset=e.target.value;m=makeMap(W,H,preset,seed);reset();}; $(".sp").oninput=e=>{speed=+e.target.value;};
  // 鼠标编辑
  let drag=null;
  const cellAt=ev=>{const r=cv.getBoundingClientRect();const x=Math.floor((ev.clientX-r.left)/cs),y=Math.floor((ev.clientY-r.top)/cs);return x>=0&&y>=0&&x<W&&y<H?y*W+x:-1;};
  cv.addEventListener("pointerdown",ev=>{const i=cellAt(ev);if(i<0)return; cv.setPointerCapture(ev.pointerId);
    if(i===m.S) drag="S"; else if(i===m.T) drag="T"; else {drag=m.blk[i]?"erase":"wall"; m.blk[i]=drag==="wall"?1:0; reset();}});
  cv.addEventListener("pointermove",ev=>{const i=cellAt(ev); if(i<0) return;
    if(!drag){ if(ctx&&Number.isFinite(ctx.g[i])&&!timer) $(".msg").textContent=xy(m,i)+"：g = "+fmt(ctx.g[i])+"，h = "+fmt(ctx.h[i])+"，f = "+fmt(ctx.f[i])+(ctx.st[i]===2?"（closed）":"（open）"); return; }
    if(drag==="S"||drag==="T"){ if(!m.blk[i]&&i!==(drag==="S"?m.T:m.S)&&i!==m[drag]){m[drag]=i;reset();} }
    else if(i!==m.S&&i!==m.T){ const v=drag==="wall"?1:0; if(m.blk[i]!==v){m.blk[i]=v;reset();} } });
  cv.addEventListener("pointerup",()=>{drag=null;});
  addEventListener("resize",render);
  reset();
}

/* ---------------- 多算法对比（同步绘制，打印可见） ---------------- */
function parseSpec(s){const [k,h,w]=s.split(":");return {k,h:h||"octile",w:w?+w:2};}
function specName(sp){ const a=ALGO[sp.k]; let n=a.name; if(sp.k==="wastar") n+="（w="+sp.w+"）"; if(a.useH&&sp.k!=="greedy") n+=" · "+HNAME[sp.h]; else if(sp.k==="greedy") n+=" · "+HNAME[sp.h]; return n; }
function CmpLab(root){
  const W=+(root.dataset.w||48), H=+(root.dataset.h||28), specs=(root.dataset.algos||"dijkstra,greedy,astar,jps").split(",").map(parseSpec);
  let preset=root.dataset.map||"trap", seed=+(root.dataset.seed||3);
  root.innerHTML='<div class="t">'+(root.dataset.title||"交互实验 · 同一张图，不同算法搜了多大范围")+'</div>'+
    '<div class="top"><label>地图 <select class="mp">'+Object.entries(PRESETS).map(([k,v])=>'<option value="'+k+'"'+(k===preset?" selected":"")+'>'+v+'</option>').join("")+'</select></label>'+
    '<button class="st-btn sh">换一张图</button><span style="font-size:12.5px;color:#6B7280">8 方向、禁止切角；“展开”= 从 open 取出并处理的节点数</span></div>'+
    '<div class="panels">'+specs.map(()=>'<div class="panel"><canvas></canvas><div class="cap"></div></div>').join("")+'</div>'+LEGEND;
  const $=s=>root.querySelector(s);
  function go(){
    const m=makeMap(W,H,preset,seed);
    const ref=runAll(m,"dijkstra",{h:"zero",diag:true}); const opt=ref.path?ref.g[m.T]:null;
    root.querySelectorAll(".panel").forEach((p,k)=>{
      const sp=specs[k], ctx=runAll(m,sp.k,{h:sp.h,w:sp.w,diag:true});
      draw(p.querySelector("canvas"),m,ctx);
      let q="无路径"; if(ctx.path){ const c=ctx.g[m.T]; q="代价 "+fmt(c)+(opt!==null&&c>opt+1e-6?'（<span style="color:#B91C1C">比最优多 '+((c/opt-1)*100).toFixed(1)+'%</span>）':"（最优）"); }
      p.querySelector(".cap").innerHTML="<b>"+specName(sp)+"</b>　展开 <b>"+ctx.exp+"</b>　"+q;
    });
  }
  $(".mp").onchange=e=>{preset=e.target.value;go();}; $(".sh").onclick=()=>{seed=(seed*7+13)%9973;go();};
  addEventListener("resize",go);
  go();
}

document.querySelectorAll(".gridlab").forEach(GridLab);
document.querySelectorAll(".cmplab").forEach(CmpLab);
if(typeof window!=="undefined") window.__ASTAR__={ALGO,makeMap,runAll,newCtx};
})();
