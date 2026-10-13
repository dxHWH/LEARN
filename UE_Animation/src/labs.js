/* ============================================================ UE 动画电子书 · 交互实验
   所有绘制都是同步完成的（截图 / 打印 PDF 时不会空白）。算法与 demos/anim_demo.cpp 一致。 */
(function(){
"use strict";
const D2R=Math.PI/180, R2D=180/Math.PI;
const COL={b:"#2F5BD3",o:"#C2410C",g:"#15803D",p:"#6D28D9",r:"#B91C1C",a:"#B45309",s:"#6B7280",ink:"#1F2937"};
function el(tag,cls,html){const e=document.createElement(tag); if(cls) e.className=cls; if(html!=null) e.innerHTML=html; return e;}
function canvas(w,h){const c=document.createElement("canvas"); c.width=w*2; c.height=h*2; c.style.aspectRatio=w+" / "+h; const g=c.getContext("2d"); g.scale(2,2); g.font="12px "+getComputedStyle(document.body).fontFamily; return [c,g];}
function pos(c,w,h,e){const r=c.getBoundingClientRect(); return [(e.clientX-r.left)/r.width*w,(e.clientY-r.top)/r.height*h];}
function slider(row,label,min,max,step,val,fmt){
  const l=el("label",null,label+" "); const i=el("input"); i.type="range"; i.min=min; i.max=max; i.step=step; i.value=val;
  const s=el("b",null,""); l.appendChild(i); l.appendChild(s); row.appendChild(l);
  const upd=()=>{s.textContent=" "+(fmt?fmt(+i.value):i.value);}; upd(); i.addEventListener("input",upd); return i;
}
function select(row,label,opts,val){
  const l=el("label",null,label+" "); const s=el("select"); opts.forEach(([v,t])=>{const o=el("option",null,t); o.value=v; s.appendChild(o);}); s.value=val; l.appendChild(s); row.appendChild(l); return s;
}
function shell(root,title,hint){
  root.appendChild(el("div","t",title));
  const row=el("div","row"); root.appendChild(row);
  const stage=el("div","stage"); root.appendChild(stage);
  const left=el("div"); const side=el("div","side"); stage.appendChild(left); stage.appendChild(side);
  if(hint) root.appendChild(el("div","hint",hint));
  return {row,left,side};
}
function text(g,s,x,y,col,align,bold,size){g.fillStyle=col||COL.ink; g.textAlign=align||"left"; g.font=(bold?"700 ":"")+(size||12)+"px "+getComputedStyle(document.body).fontFamily; g.fillText(s,x,y);}
function arrow(g,x1,y1,x2,y2,col,w){
  g.strokeStyle=col; g.fillStyle=col; g.lineWidth=w||2; g.beginPath(); g.moveTo(x1,y1); g.lineTo(x2,y2); g.stroke();
  const a=Math.atan2(y2-y1,x2-x1), L=8; g.beginPath(); g.moveTo(x2,y2); g.lineTo(x2-L*Math.cos(a-0.4),y2-L*Math.sin(a-0.4)); g.lineTo(x2-L*Math.cos(a+0.4),y2-L*Math.sin(a+0.4)); g.closePath(); g.fill();
}
function wrap180(a){a=((a+180)%360+360)%360-180; return a;}
function wbars(side,list){ // list: [name, w, color]
  side.querySelectorAll(".wb").forEach(x=>x.remove());
  list.forEach(([n,w,c])=>{const d=el("div","wb",`<span>${n}</span><span class="bar"><i style="width:${Math.max(0,Math.min(1,w))*100}%;background:${c||COL.b}"></i></span><span>${w.toFixed(3)}</span>`); side.appendChild(d);});
}

/* ---------------------------------------------------------------- 1. 旋转混合 */
function quatLab(root){
  const {row,left,side}=shell(root,"交互实验 · 两个 Yaw 角的混合：欧拉插值 vs 四元数","拖动滑块：把 A、B 放在 ±180° 两侧，看欧拉插值怎么“绕远路”；右图是 nlerp 与 slerp 的角度随 t 的变化（夹角越大，nlerp 越偏离直线）。");
  const sa=slider(row,"A",-180,180,1,170,v=>v+"°"), sb=slider(row,"B",-180,180,1,-170,v=>v+"°"), st=slider(row,"t",0,1,0.01,0.5,v=>v.toFixed(2));
  const W=560,H=280; const [c,g]=canvas(W,H); left.appendChild(c);
  const info=el("div","kv"); side.appendChild(info);
  const nl=(A,B,t)=>{let qa=[Math.cos(A/2*D2R),Math.sin(A/2*D2R)], qb=[Math.cos(B/2*D2R),Math.sin(B/2*D2R)];
    if(qa[0]*qb[0]+qa[1]*qb[1]<0) qb=[-qb[0],-qb[1]]; const w=qa[0]*(1-t)+qb[0]*t, z=qa[1]*(1-t)+qb[1]*t; return wrap180(2*Math.atan2(z,w)*R2D);};
  const sl=(A,B,t)=>wrap180(A+wrap180(B-A)*t);
  function draw(){
    const A=+sa.value,B=+sb.value,t=+st.value; g.clearRect(0,0,W,H);
    const cx=140,cy=145,R=105; const P=(a,r)=>[cx+r*Math.sin(a*D2R), cy-r*Math.cos(a*D2R)];
    g.strokeStyle="#D1D5DB"; g.lineWidth=1; g.beginPath(); g.arc(cx,cy,R,0,7); g.stroke();
    text(g,"0°（前）",cx,cy-R-8,COL.s,"center"); text(g,"±180°",cx,cy+R+16,COL.s,"center");
    // 欧拉路径
    g.strokeStyle=COL.r; g.setLineDash([5,4]); g.lineWidth=2; g.beginPath();
    for(let k=0;k<=60;k++){const a=A+(B-A)*k/60; const [x,y]=P(a,R-10); k?g.lineTo(x,y):g.moveTo(x,y);} g.stroke();
    g.strokeStyle=COL.g; g.beginPath(); for(let k=0;k<=60;k++){const a=sl(A,B,k/60); const [x,y]=P(a,R+8); k?g.lineTo(x,y):g.moveTo(x,y);} g.stroke(); g.setLineDash([]);
    const e=A+(B-A)*t, q=nl(A,B,t);
    [[A,COL.b,"A"],[B,COL.b,"B"]].forEach(([a,col,n])=>{const [x,y]=P(a,R); arrow(g,cx,cy,x,y,col,2); const [lx,ly]=P(a,R+22); text(g,n,lx,ly+4,col,"center",true);});
    {const [x,y]=P(e,R-22); arrow(g,cx,cy,x,y,COL.r,3);} {const [x,y]=P(q,R-30); arrow(g,cx,cy,x,y,COL.g,3);}
    // 右图：角度-t
    const x0=305,x1=545,y0=30,y1=250; g.strokeStyle="#E5E7EB"; g.lineWidth=1; g.strokeRect(x0,y0,x1-x0,y1-y0);
    const d=wrap180(B-A); const lo=Math.min(0,d), hi=Math.max(0,d)||1; const Y=v=>y1-(v-lo)/(hi-lo||1)*(y1-y0);
    text(g,"相对 A 转过的角度（沿最短路径）",x0,y0-10,COL.s);
    g.strokeStyle=COL.s; g.setLineDash([4,3]); g.beginPath(); g.moveTo(x0,Y(0)); g.lineTo(x1,Y(d)); g.stroke(); g.setLineDash([]);
    g.strokeStyle=COL.o; g.lineWidth=2.2; g.beginPath();
    for(let k=0;k<=100;k++){const tt=k/100; const v=wrap180(nl(A,B,tt)-A); const x=x0+(x1-x0)*tt; k?g.lineTo(x,Y(v)):g.moveTo(x,Y(v));} g.stroke();
    const xt=x0+(x1-x0)*t; g.strokeStyle="#9CA3AF"; g.beginPath(); g.moveTo(xt,y0); g.lineTo(xt,y1); g.stroke();
    text(g,"t=0",x0,y1+14,COL.s); text(g,"t=1",x1,y1+14,COL.s,"right");
    text(g,"— nlerp",x0+8,y0+16,COL.o,"left",true); text(g,"- - slerp（匀角速度）",x0+70,y0+16,COL.s);
    let mx=0; for(let k=0;k<=200;k++){const tt=k/200; mx=Math.max(mx,Math.abs(wrap180(nl(A,B,tt)-sl(A,B,tt))));}
    info.textContent=`欧拉插值      ${e.toFixed(1)}°\n四元数 nlerp  ${q.toFixed(1)}°\nslerp         ${sl(A,B,t).toFixed(1)}°\n\n两者夹角      ${Math.abs(d).toFixed(0)}°\nnlerp 最大偏差 ${mx.toFixed(2)}°\n欧拉 vs 正确  ${Math.abs(wrap180(e-sl(A,B,t))).toFixed(1)}°`;
  }
  [sa,sb,st].forEach(i=>i.addEventListener("input",draw)); draw();
}

/* ---------------------------------------------------------------- 2. 2D 混合空间 */
const BS=[["Idle",0,0],["Walk_F",0,160],["Walk_B",0,-140],["Walk_L",-150,0],["Walk_R",150,0],["Jog_F",0,420],["Jog_B",0,-340],["Jog_L",-370,0],["Jog_R",380,0]];
function delaunay(S){const T=[],n=S.length;
  for(let i=0;i<n;i++)for(let j=i+1;j<n;j++)for(let k=j+1;k<n;k++){
    const [ax,ay]=[S[i][1],S[i][2]],[bx,by]=[S[j][1],S[j][2]],[cx,cy]=[S[k][1],S[k][2]];
    const d=2*(ax*(by-cy)+bx*(cy-ay)+cx*(ay-by)); if(Math.abs(d)<1e-9) continue;
    const ux=((ax*ax+ay*ay)*(by-cy)+(bx*bx+by*by)*(cy-ay)+(cx*cx+cy*cy)*(ay-by))/d, uy=((ax*ax+ay*ay)*(cx-bx)+(bx*bx+by*by)*(ax-cx)+(cx*cx+cy*cy)*(bx-ax))/d;
    const r2=(ax-ux)**2+(ay-uy)**2; let ok=true;
    for(let m=0;m<n&&ok;m++) if(m!==i&&m!==j&&m!==k&&((S[m][1]-ux)**2+(S[m][2]-uy)**2)<r2*(1-1e-9)) ok=false;
    if(ok) T.push([i,j,k]);}
  return T;}
function bsWeights(S,T,px,py){
  for(const t of T){const [a,b,c]=t.map(i=>[S[i][1],S[i][2]]); const det=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1]);
    const w0=((b[1]-c[1])*(px-c[0])+(c[0]-b[0])*(py-c[1]))/det, w1=((c[1]-a[1])*(px-c[0])+(a[0]-c[0])*(py-c[1]))/det, w2=1-w0-w1;
    if(w0>=-1e-9&&w1>=-1e-9&&w2>=-1e-9) return {w:{[t[0]]:w0,[t[1]]:w1,[t[2]]:w2},tri:t,p:[px,py]};}
  let best=null;
  for(const t of T) for(const [a,b] of [[t[0],t[1]],[t[1],t[2]],[t[2],t[0]]]){
    if(T.filter(u=>u.includes(a)&&u.includes(b)).length!==1) continue;
    const ax=S[a][1],ay=S[a][2],bx=S[b][1],by=S[b][2]; let tt=((px-ax)*(bx-ax)+(py-ay)*(by-ay))/((bx-ax)**2+(by-ay)**2); tt=Math.max(0,Math.min(1,tt));
    const qx=ax+(bx-ax)*tt,qy=ay+(by-ay)*tt,d=(qx-px)**2+(qy-py)**2; if(!best||d<best.d) best={d,a,b,tt,qx,qy};}
  return {w:{[best.a]:1-best.tt,[best.b]:best.tt},tri:null,p:[best.qx,best.qy]};
}
function bsLab(root){
  const {row,left,side}=shell(root,"交互实验 · 2D 混合空间：拖动输入点看权重","在左图中按住并拖动橙色点（输入速度）。高亮三角形的三个顶点就是参与混合的采样；拖到外圈以外时，输入被投影到最近的边界上。");
  const cb=el("label",null,'<input type="checkbox" checked> 显示三角剖分'); row.appendChild(cb); const chk=cb.querySelector("input");
  const pre=select(row,"预设",[["180,180","右前斜向"],["0,290","前进 290"],["-100,-60","左后慢走"],["500,500","超出范围"],["0,0","原地"]],"180,180");
  const T=delaunay(BS); const W=520,H=440; const [c,g]=canvas(W,H); c.classList.add("drag"); left.appendChild(c);
  const info=el("div","kv"); side.appendChild(el("div",null,"<b>采样权重</b>")); const holder=el("div"); side.appendChild(holder); side.appendChild(info);
  const xr=[-420,560], yr=[-380,560], L=36,TOP=12,PW=W-L-12,PH=H-TOP-32;
  const X=x=>L+PW*(x-xr[0])/(xr[1]-xr[0]), Y=y=>TOP+PH*(1-(y-yr[0])/(yr[1]-yr[0]));
  const IX=x=>xr[0]+(x-L)/PW*(xr[1]-xr[0]), IY=y=>yr[0]+(1-(y-TOP)/PH)*(yr[1]-yr[0]);
  let q=[180,180];
  function draw(){
    g.clearRect(0,0,W,H); g.fillStyle="#FBFBFD"; g.fillRect(L,TOP,PW,PH); g.strokeStyle="#D1D5DB"; g.strokeRect(L,TOP,PW,PH);
    g.strokeStyle="#E5E7EB"; g.beginPath(); g.moveTo(X(0),TOP); g.lineTo(X(0),TOP+PH); g.moveTo(L,Y(0)); g.lineTo(L+PW,Y(0)); g.stroke();
    const r=bsWeights(BS,T,q[0],q[1]);
    if(chk.checked){g.strokeStyle="#93C5FD"; g.lineWidth=1.2; T.forEach(t=>{g.beginPath(); t.forEach((i,k)=>{const x=X(BS[i][1]),y=Y(BS[i][2]); k?g.lineTo(x,y):g.moveTo(x,y);}); g.closePath(); g.stroke();});}
    if(r.tri){g.fillStyle="rgba(194,65,12,.12)"; g.strokeStyle=COL.o; g.lineWidth=1.8; g.beginPath(); r.tri.forEach((i,k)=>{const x=X(BS[i][1]),y=Y(BS[i][2]); k?g.lineTo(x,y):g.moveTo(x,y);}); g.closePath(); g.fill(); g.stroke();}
    g.setLineDash([4,3]); g.strokeStyle=COL.o; g.lineWidth=1.2;
    Object.entries(r.w).forEach(([i,w])=>{if(w>1e-6){g.beginPath(); g.moveTo(X(r.p[0]),Y(r.p[1])); g.lineTo(X(BS[i][1]),Y(BS[i][2])); g.stroke();}}); g.setLineDash([]);
    BS.forEach(([n,x,y])=>{g.fillStyle="#fff"; g.strokeStyle=COL.b; g.lineWidth=2; g.beginPath(); g.arc(X(x),Y(y),5.5,0,7); g.fill(); g.stroke(); text(g,n,X(x),Y(y)+18,"#1E3A8A","center",true,11);});
    if(!r.tri){g.strokeStyle=COL.o; g.lineWidth=2; g.strokeRect(X(q[0])-5,Y(q[1])-5,10,10); arrow(g,X(q[0]),Y(q[1]),X(r.p[0]),Y(r.p[1]),COL.o,1.4);}
    g.fillStyle=COL.o; g.beginPath(); g.arc(X(r.p[0]),Y(r.p[1]),7,0,7); g.fill();
    text(g,"Vx →",L+PW-4,Y(0)-6,COL.s,"right"); text(g,"Vy ↑",X(0)+6,TOP+14,COL.s);
    const list=BS.map((s,i)=>[s[0],r.w[i]||0,COL.o]).filter(x=>x[1]>1e-6).sort((a,b)=>b[1]-a[1]);
    holder.innerHTML=""; wbars(holder,list);
    let rx=0,ry=0,sum=0; Object.entries(r.w).forEach(([i,w])=>{rx+=w*BS[i][1]; ry+=w*BS[i][2]; sum+=w;});
    info.textContent=`输入   (${q[0].toFixed(0)}, ${q[1].toFixed(0)})\nΣw     ${sum.toFixed(3)}\n重建   (${rx.toFixed(0)}, ${ry.toFixed(0)})${r.tri?"":"\n← 在三角网外，已投影"}\n三角形 ${T.length} 个`;
  }
  let drag=false;
  c.addEventListener("pointerdown",e=>{drag=true; c.setPointerCapture(e.pointerId); const [x,y]=pos(c,W,H,e); q=[IX(x),IY(y)]; draw();});
  c.addEventListener("pointermove",e=>{if(!drag) return; const [x,y]=pos(c,W,H,e); q=[IX(x),IY(y)]; draw();});
  c.addEventListener("pointerup",()=>drag=false);
  chk.addEventListener("change",draw); pre.addEventListener("change",()=>{q=pre.value.split(",").map(Number); draw();});
  draw();
}

/* ---------------------------------------------------------------- 3. 同步组 */
function syncLab(root){
  const {row,left,side}=shell(root,"交互实验 · 同步组与同步标记","调 Run 的权重和它的左脚标记位置，切换同步方式，观察两条动画的支撑脚是否一致（红色 = 不一致）。");
  const mode=select(row,"方式",[["none","不同步"],["norm","归一化时间"],["marker","同步标记"]],"norm");
  const sw=slider(row,"Run 权重",0,1,0.05,0.5,v=>v.toFixed(2)), sm=slider(row,"Run 的 L 标记",0,0.45,0.01,0.10,v=>v.toFixed(2));
  const W=560,H=180; const [c,g]=canvas(W,H); left.appendChild(c); const info=el("div","kv"); side.appendChild(info);
  const walkLen=1.2, runLen=0.7, wL=0.0, T=4.0, N=800;
  const foot=(ph,pl)=>(((ph-pl)%1+1)%1)<0.5?0:1;
  function draw(){
    const m=mode.value, w=+sw.value, rL=+sm.value; const gl=(1-w)*walkLen+w*runLen;
    g.clearRect(0,0,W,H); const L=70, PW=W-L-10; let bad=0, mx=0; const rows=[[],[]];
    for(let k=0;k<N;k++){const t=T*k/N; let pw,pr;
      if(m==="none"){pw=(t/walkLen)%1; pr=(t/runLen)%1;} else {pw=(t/gl)%1; pr = m==="norm"? pw : (rL+((pw-wL)%1+1)%1)%1;}
      const a=foot(pw,wL), b=foot(pr,rL); rows[0].push(a); rows[1].push(b); if(a!==b) bad++; const d=Math.abs(pw-pr); mx=Math.max(mx,Math.min(d,1-d));}
    ["Walk","Run"].forEach((n,ri)=>{const y=20+ri*40; text(g,n,L-8,y+14,COL.ink,"right",true);
      let k0=0; for(let k=1;k<=N;k++){ if(k===N||rows[ri][k]!==rows[ri][k0]){const x=L+PW*k0/N, ww=PW*(k-k0)/N; const isL=rows[ri][k0]===0;
        g.fillStyle=isL?"#DBEAFE":"#FFEDD5"; g.strokeStyle=isL?COL.b:COL.o; g.lineWidth=0.8; g.fillRect(x,y,ww,22); g.strokeRect(x,y,ww,22);
        if(ww>18) text(g,isL?"L":"R",x+ww/2,y+15,isL?COL.b:COL.o,"center",true,11); k0=k;}}});
    const y=106; text(g,"不一致",L-8,y+10,COL.s,"right"); g.fillStyle="#EF4444";
    for(let k=0;k<N;k++) if(rows[0][k]!==rows[1][k]) g.fillRect(L+PW*k/N,y+2,PW/N+0.4,10);
    for(let s=0;s<=T;s++) text(g,s+" s",L+PW*s/T,y+34,COL.s,"center",false,11);
    text(g,"支撑脚：L 蓝 / R 橙",L,H-8,COL.s);
    const leader = w>0.5?"Run":(w<0.5?"Walk":"Walk（并列）");
    info.textContent=`支撑脚不一致 ${(100*bad/N).toFixed(1)}%\n最大相位差   ${mx.toFixed(2)}\n周期         ${m==="none"?"各自 1.20 / 0.70 s":gl.toFixed(2)+" s（加权）"}\n权重最高     ${leader}\nWalk L 标记  0.00\nRun  L 标记  ${rL.toFixed(2)}`;
  }
  [mode,sw,sm].forEach(i=>i.addEventListener("input",draw)); mode.addEventListener("change",draw); draw();
}

/* ---------------------------------------------------------------- 4. 分层骨骼混合 */
function layerLab(root){
  const {row,left,side}=shell(root,"交互实验 · 分层骨骼混合：Blend Depth 与 Mesh Space","骨架：pelvis → spine_01 → spine_02 → spine_03 → neck → head，spine_03 → clavicle_r → upperarm_r。Branch Filter = spine_01。上半身瞄准姿势：头朝 0°、右臂 30°（组件空间）。");
  const sp=slider(row,"骨盆扭转",0,90,1,40,v=>v+"°"), sc=slider(row,"每节脊柱回扭",-30,0,1,-10,v=>v+"°");
  const sd=select(row,"Blend Depth",[["0","0"],["1","1"],["2","2"],["3","3"],["4","4"]],"0");
  const sm=select(row,"空间",[["local","Local Space"],["mesh","Mesh Space Rotation"]],"local");
  const sw=slider(row,"Blend Weight",0,1,0.05,1,v=>v.toFixed(2));
  const par=[-1,0,1,2,3,4,5,4,7], names=["root","pelvis","spine_01","spine_02","spine_03","neck","head","clavicle_r","upperarm_r"];
  const show=[1,2,3,4,6,8], target={6:0,8:30};
  const W=560,H=150; const [c,g]=canvas(W,H); left.appendChild(c); const info=el("div","kv"); side.appendChild(info);
  const comp=loc=>{const o=[]; loc.forEach((v,i)=>o.push(v+(par[i]>=0?o[par[i]]:0))); return o;};
  function weights(depth){return par.map((_,i)=>{let k=0,p=i; while(p>=0&&p!==2){p=par[p];k++;} if(p!==2) return 0; if(depth===0) return 1; return Math.min(1,(k+1)/depth);});}
  function draw(){
    const pel=+sp.value, cs=+sc.value, depth=+sd.value, bw=+sw.value, mesh=sm.value==="mesh";
    const base=[0,pel,cs,cs,cs,0,0,0,0], aim=[0,0,0,0,0,0,0,0,30]; const w=weights(depth).map(x=>x*bw);
    let out; if(!mesh) out=comp(base.map((v,i)=>v+(aim[i]-v)*w[i])); else {const cb=comp(base), ca=comp(aim); out=cb.map((v,i)=>v+(ca[i]-v)*w[i]);}
    g.clearRect(0,0,W,H); const cw=W/show.length, R=30;
    show.forEach((bi,j)=>{const cx=cw*j+cw/2, cy=70; const tg=target[bi]; const good=tg!=null&&Math.abs(out[bi]-tg)<0.5;
      g.fillStyle=tg==null?"#F9FAFB":(good?"#EAF7EF":"#FFF4EC"); g.strokeStyle=tg==null?"#9CA3AF":(good?COL.g:COL.o); g.lineWidth=1.4; g.beginPath(); g.arc(cx,cy,R,0,7); g.fill(); g.stroke();
      if(tg!=null){g.setLineDash([3,3]); g.strokeStyle="#9CA3AF"; g.beginPath(); g.moveTo(cx,cy); g.lineTo(cx+R*Math.sin(tg*D2R),cy-R*Math.cos(tg*D2R)); g.stroke(); g.setLineDash([]);}
      arrow(g,cx,cy,cx+(R-4)*Math.sin(out[bi]*D2R),cy-(R-4)*Math.cos(out[bi]*D2R),COL.ink,2.4);
      text(g,names[bi],cx,22,COL.ink,"center",true); text(g,(out[bi]>=0?"+":"")+out[bi].toFixed(1)+"°",cx,cy+R+18,COL.ink,"center",false,12);
      text(g,"w="+w[bi].toFixed(2),cx,cy+R+34,COL.s,"center",false,11);});
    info.textContent=`头部朝向     ${out[6].toFixed(2)}°（期望 0°）\n右上臂朝向   ${out[8].toFixed(2)}°（期望 30°）\n头部误差     ${Math.abs(out[6]).toFixed(2)}°\n\nLocal：取瞄准姿势的局部旋转，\n上半身继承骨盆的扭转。\nMesh：取组件空间朝向，\n与骨盆无关。`;
  }
  [sp,sc,sw].forEach(i=>i.addEventListener("input",draw)); [sd,sm].forEach(i=>i.addEventListener("change",draw)); draw();
}

/* ---------------------------------------------------------------- 5. 过渡 */
function inertial(x0,v0,t1,t){let s=x0<0?-1:1; x0*=s; v0*=s; if(x0<1e-9) return 0; v0=Math.min(v0,0); if(v0<0) t1=Math.min(t1,-5*x0/v0); if(t>=t1) return 0;
  const a0=Math.max(0,(-8*v0*t1-20*x0)/(t1*t1)); const A=-(a0*t1*t1+6*v0*t1+12*x0)/(2*t1**5), B=(3*a0*t1*t1+16*v0*t1+30*x0)/(2*t1**4), C=-(3*a0*t1*t1+12*v0*t1+20*x0)/(2*t1**3);
  return s*(A*t**5+B*t**4+C*t**3+a0/2*t*t+v0*t+x0);}
function transLab(root){
  const {row,left,side}=shell(root,"交互实验 · 状态过渡：硬切 / 交叉淡化 / 惯性化","移动切换时刻：源动画正在快速运动时切换（例如 0.40 s），惯性化的优势最明显；在源动画速度为 0 附近切换，几种方法差别不大。");
  const m=select(row,"方式",[["cut","硬切"],["linear","线性交叉淡化"],["smooth","SmoothStep 淡化"],["inertial","惯性化"]],"inertial");
  const s0=slider(row,"切换时刻",0.1,0.8,0.01,0.40,v=>v.toFixed(2)+" s"), sT=slider(row,"过渡时长",0.05,0.5,0.01,0.2,v=>v.toFixed(2)+" s");
  const W=560,H=250; const [c,g]=canvas(W,H); left.appendChild(c); const info=el("div","kv"); side.appendChild(info);
  const a=t=>50*Math.sin(2*Math.PI*t), b=t=>10+20*Math.sin(2*Math.PI*t/0.8+1);
  function draw(){
    const t0=+s0.value, T=+sT.value, md=m.value, dt=1/120; const x0=a(t0)-b(t0), v0=((a(t0)-a(t0-dt))-(b(t0)-b(t0-dt)))/dt;
    const res=t=>{if(t<t0) return a(t); const al=Math.max(0,Math.min(1,(t-t0)/T)); if(md==="cut") return b(t); if(md==="linear") return (1-al)*a(t)+al*b(t);
      if(md==="smooth"){const s=al*al*(3-2*al); return (1-s)*a(t)+s*b(t);} return b(t)+inertial(x0,v0,T,t-t0);};
    g.clearRect(0,0,W,H); const ta=0,tb=1.2, L=34, PW=W-L-10, X=t=>L+PW*(t-ta)/(tb-ta), Y=v=>20+(H-50)*(1-(v+55)/110);
    g.fillStyle="#F3F4F6"; g.fillRect(X(t0),20,X(t0+T)-X(t0),H-50);
    g.strokeStyle="#E5E7EB"; g.beginPath(); g.moveTo(L,Y(0)); g.lineTo(W-10,Y(0)); g.stroke();
    [[a,"#9CA3AF",[5,4]],[b,COL.b,[2,3]]].forEach(([f,col,dash])=>{g.strokeStyle=col; g.lineWidth=1.3; g.setLineDash(dash); g.beginPath(); for(let k=0;k<=300;k++){const t=ta+(tb-ta)*k/300; k?g.lineTo(X(t),Y(f(t))):g.moveTo(X(t),Y(f(t)));} g.stroke();}); g.setLineDash([]);
    g.strokeStyle=COL.g; g.lineWidth=2.6; g.beginPath(); let prev=null;
    for(let k=0;k<=600;k++){const t=ta+(tb-ta)*k/600; const v=res(t); if(md==="cut"&&prev!=null&&prev<t0&&t>=t0){g.stroke(); g.beginPath(); g.moveTo(X(t),Y(v));} else if(k) g.lineTo(X(t),Y(v)); else g.moveTo(X(t),Y(v)); prev=t;} g.stroke();
    text(g,"源 A（灰虚线）· 目标 B（蓝点线）· 输出（绿）",L,H-10,COL.s); text(g,"切换",X(t0)+3,32,COL.s);
    for(let s=0;s<=1.2;s+=0.2) text(g,s.toFixed(1)+"s",X(s),H-26,COL.s,"center",false,10);
    const xs=[]; for(let f=-10;f<=Math.round((T+0.3)/dt);f++) xs.push(res(t0+f*dt));
    let mj=0,mv=0; for(let i=1;i<xs.length;i++) mj=Math.max(mj,Math.abs(xs[i]-xs[i-1])); for(let i=2;i<xs.length;i++) mv=Math.max(mv,Math.abs((xs[i]-xs[i-1])-(xs[i-1]-xs[i-2]))/dt);
    const frames=Math.round(T*60), ev=md==="cut"||md==="inertial"?frames:frames*2;
    info.textContent=`最大单帧跳变 ${mj.toFixed(2)}°\n最大速度突变 ${mv.toFixed(1)}°/s\n过渡期求值   ${ev} 次（60fps）\n\n切换瞬间：\n位置差 x0 = ${x0.toFixed(1)}°\n速度差 v0 = ${v0.toFixed(1)}°/s`;
  }
  [s0,sT].forEach(i=>i.addEventListener("input",draw)); m.addEventListener("change",draw); draw();
}

/* ---------------------------------------------------------------- 6. 两骨骼 IK */
function ikLab(root){
  const {row,left,side}=shell(root,"交互实验 · 两骨骼 IK","按住并拖动目标（橙色）。灰色圆环是可达范围；把目标拉到最远处附近，再切换 Pole 方向，观察肘部翻转。");
  const pole=select(row,"Pole",[["up","上方"],["down","下方"]],"up");
  const st=el("label",null,'<input type="checkbox"> 允许拉伸（最多 1.2 倍）'); row.appendChild(st); const stretch=st.querySelector("input");
  const W=560,H=320; const [c,g]=canvas(W,H); c.classList.add("drag"); left.appendChild(c); const info=el("div","kv"); side.appendChild(info);
  const A=[230,165], L1=80, L2=68; let T=[340,110];
  function draw(){
    g.clearRect(0,0,W,H);
    g.strokeStyle="#E5E7EB"; g.lineWidth=1; g.beginPath(); g.arc(A[0],A[1],L1+L2,0,7); g.stroke(); g.beginPath(); g.arc(A[0],A[1],Math.abs(L1-L2),0,7); g.stroke();
    if(stretch.checked){g.setLineDash([3,3]); g.beginPath(); g.arc(A[0],A[1],(L1+L2)*1.2,0,7); g.stroke(); g.setLineDash([]);}
    const dx=T[0]-A[0], dy=T[1]-A[1], dist=Math.hypot(dx,dy)||1e-6, dir=[dx/dist,dy/dist];
    let s=1; if(stretch.checked&&dist>L1+L2) s=Math.min(1.2,dist/(L1+L2));
    const l1=L1*s,l2=L2*s; const d=Math.max(Math.abs(l1-l2)+0.5,Math.min(dist,l1+l2-0.01));
    const cosA=(l1*l1+d*d-l2*l2)/(2*l1*d), sinA=Math.sqrt(Math.max(0,1-cosA*cosA));
    const P=pole.value==="up"?[0,-1]:[0,1]; const pd=P[0]*dir[0]+P[1]*dir[1]; let bend=[P[0]-dir[0]*pd,P[1]-dir[1]*pd]; const bl=Math.hypot(bend[0],bend[1])||1e-6; bend=[bend[0]/bl,bend[1]/bl];
    const B=[A[0]+dir[0]*l1*cosA+bend[0]*l1*sinA, A[1]+dir[1]*l1*cosA+bend[1]*l1*sinA], E=[A[0]+dir[0]*d, A[1]+dir[1]*d];
    g.setLineDash([4,4]); g.strokeStyle="#9CA3AF"; g.beginPath(); g.moveTo(A[0],A[1]); g.lineTo(T[0],T[1]); g.stroke(); g.setLineDash([]);
    g.strokeStyle=COL.ink; g.lineCap="round"; g.lineWidth=8; g.beginPath(); g.moveTo(A[0],A[1]); g.lineTo(B[0],B[1]); g.lineTo(E[0],E[1]); g.stroke(); g.lineCap="butt";
    [[A,COL.b,"肩 A"],[B,COL.p,"肘 B"],[E,COL.g,"手"]].forEach(([p,col,n])=>{g.fillStyle=col; g.beginPath(); g.arc(p[0],p[1],6,0,7); g.fill(); text(g,n,p[0]+9,p[1]-9,col,"left",true);});
    g.strokeStyle=COL.o; g.lineWidth=2; g.beginPath(); g.arc(T[0],T[1],9,0,7); g.stroke(); g.beginPath(); g.moveTo(T[0]-13,T[1]); g.lineTo(T[0]+13,T[1]); g.moveTo(T[0],T[1]-13); g.lineTo(T[0],T[1]+13); g.stroke();
    arrow(g,A[0],A[1]+(pole.value==="up"?-40:40)+0,A[0],A[1]+(pole.value==="up"?-80:80),COL.g,1.6); text(g,"Pole",A[0]+6,A[1]+(pole.value==="up"?-70:84),COL.g,"left",true);
    const err=Math.hypot(E[0]-T[0],E[1]-T[1]); const elbowDeg=Math.acos(Math.max(-1,Math.min(1,(l1*l1+l2*l2-d*d)/(2*l1*l2))))*R2D;
    info.textContent=`|T−A|    ${dist.toFixed(1)}\n可达范围 [${Math.abs(L1-L2)}, ${L1+L2}]${stretch.checked?"（拉伸至 "+((L1+L2)*1.2).toFixed(0)+"）":""}\n${dist<=L1+L2*1&&dist>=Math.abs(L1-L2)?"可达":"不可达 → 伸直指向目标"}\n肩角 α   ${(Math.acos(Math.max(-1,Math.min(1,cosA)))*R2D).toFixed(1)}°\n肘角     ${elbowDeg.toFixed(1)}°\n末端误差 ${err.toFixed(2)}\n拉伸比例 ${s.toFixed(2)}`;
  }
  let drag=false;
  c.addEventListener("pointerdown",e=>{drag=true; c.setPointerCapture(e.pointerId); T=pos(c,W,H,e); draw();});
  c.addEventListener("pointermove",e=>{if(drag){T=pos(c,W,H,e); draw();}});
  c.addEventListener("pointerup",()=>drag=false);
  pole.addEventListener("change",draw); stretch.addEventListener("change",draw); draw();
}


/* ---------------------------------------------------------------- 7/8. 三维小工具（四元数、欧拉角） */
function cross3(a,b){return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];}
function qAxis(n,deg){const h=deg*D2R/2,s=Math.sin(h);return [Math.cos(h),n[0]*s,n[1]*s,n[2]*s];}      // [w,x,y,z]
function qMul(a,b){const [aw,ax,ay,az]=a,[bw,bx,by,bz]=b;return [aw*bw-ax*bx-ay*by-az*bz, aw*bx+ax*bw+ay*bz-az*by, aw*by-ax*bz+ay*bw+az*bx, aw*bz+ax*by-ay*bx+az*bw];}
function qRot(q,v){const [w,x,y,z]=q,u=[x,y,z];const t=cross3(u,v).map(c=>2*c);const c2=cross3(u,t);return [v[0]+w*t[0]+c2[0],v[1]+w*t[1]+c2[1],v[2]+w*t[2]+c2[2]];}
function camera(az,el){const a=az*D2R,e=el*D2R;const d=[Math.cos(e)*Math.cos(a),Math.cos(e)*Math.sin(a),Math.sin(e)];
  let r=cross3([0,0,1],d);const rl=Math.hypot(r[0],r[1],r[2]);r=r.map(c=>c/rl);const u=cross3(d,r);
  return v=>[v[0]*r[0]+v[1]*r[1]+v[2]*r[2], v[0]*u[0]+v[1]*u[1]+v[2]*u[2], v[0]*d[0]+v[1]*d[1]+v[2]*d[2]];}
const BOX=(()=>{const v=[];for(const x of[-.6,.6])for(const y of[-.3,.3])for(const z of[-.18,.18])v.push([x,y,z]);
  const e=[];for(let i=0;i<8;i++)for(let j=i+1;j<8;j++){const d=v[i].map((c,k)=>c!==v[j][k]).filter(Boolean).length;if(d===1)e.push([i,j]);}return {v,e};})();
function draw3D(g,P,cx,cy,S,rotF,opt){
  const pr=v=>{const p=P(v);return [cx+S*p[0],cy-S*p[1]];};
  [[[1.6,0,0],"#FCA5A5","X"],[[0,1.6,0],"#86EFAC","Y"],[[0,0,1.6],"#93C5FD","Z"]].forEach(([a,c,n])=>{const o=pr([0,0,0]),p=pr(a);g.strokeStyle=c;g.lineWidth=1;g.setLineDash([4,3]);g.beginPath();g.moveTo(o[0],o[1]);g.lineTo(p[0],p[1]);g.stroke();g.setLineDash([]);text(g,n,p[0]+4,p[1],c,"left",true,11);});
  if(opt&&opt.ghost){g.strokeStyle="#D1D5DB";g.lineWidth=1;BOX.e.forEach(([i,j])=>{const a=pr(BOX.v[i]),b=pr(BOX.v[j]);g.beginPath();g.moveTo(a[0],a[1]);g.lineTo(b[0],b[1]);g.stroke();});}
  const V=BOX.v.map(rotF);
  g.strokeStyle="#111827";g.lineWidth=1.6;BOX.e.forEach(([i,j])=>{const a=pr(V[i]),b=pr(V[j]);g.beginPath();g.moveTo(a[0],a[1]);g.lineTo(b[0],b[1]);g.stroke();});
  [[[1.05,0,0],COL.r,"前"],[[0,.7,0],COL.g,"右"],[[0,0,.6],COL.b,"上"]].forEach(([a,c,n])=>{const o=pr([0,0,0]),p=pr(rotF(a));arrow(g,o[0],o[1],p[0],p[1],c,2.4);text(g,n,p[0]+5,p[1]-3,c,"left",true,11);});
  return pr;
}
function fq(x){return (x>=0?" ":"")+x.toFixed(4);}
function qaxisLab(root){
  const {row,left,side}=shell(root,"交互实验 · 轴角 → 四元数","调轴的方向和转角：紫色虚线是转轴，灰框是没转之前的样子，橙色细线是“前”方向的尖端在转动过程中走过的圆弧。把角度拉到 360° 以上，看四元数怎么变号。");
  const sa=slider(row,"轴·方位",-180,180,1,30,v=>v+"°"), se=slider(row,"轴·仰角",-90,90,1,60,v=>v+"°"), st=slider(row,"转角 θ",0,720,1,90,v=>v+"°");
  const W=560,H=330;const cv=canvas(W,H),c=cv[0],g=cv[1];left.appendChild(c);const info=el("div","kv");side.appendChild(info);
  const P=camera(-35,22);
  function draw(){
    const az=+sa.value*D2R, elv=+se.value*D2R, th=+st.value; const n=[Math.cos(elv)*Math.cos(az),Math.cos(elv)*Math.sin(az),Math.sin(elv)];
    const q=qAxis(n,th); g.clearRect(0,0,W,H);
    const pr=draw3D(g,P,W/2,H/2+10,95,v=>qRot(q,v),{ghost:true});
    const a1=pr(n.map(x=>x*1.7)),a2=pr(n.map(x=>-x*1.7));g.strokeStyle=COL.p;g.lineWidth=2;g.setLineDash([6,4]);g.beginPath();g.moveTo(a2[0],a2[1]);g.lineTo(a1[0],a1[1]);g.stroke();g.setLineDash([]);text(g,"转轴 n",a1[0]+6,a1[1],COL.p,"left",true);
    g.strokeStyle=COL.o;g.lineWidth=1.4;g.beginPath();for(let k=0;k<=120;k++){const p=pr(qRot(qAxis(n,th*k/120),[1.05,0,0]));k?g.lineTo(p[0],p[1]):g.moveTo(p[0],p[1]);}g.stroke();
    const back=2*Math.acos(Math.max(-1,Math.min(1,q[0])))*R2D;
    const nTxt=n.map(x=>x.toFixed(3)).join(", "), negTxt=q.map(x=>(-x).toFixed(3)).join(", ");
    info.textContent="轴 n = ("+nTxt+")\nθ = "+th+"°   θ/2 = "+(th/2).toFixed(1)+"°\n\nq = (w, x, y, z)\nw = cos(θ/2) = "+fq(q[0])+"\nx = nx·sin    = "+fq(q[1])+"\ny = ny·sin    = "+fq(q[2])+"\nz = nz·sin    = "+fq(q[3])+
      "\n|q| = "+Math.hypot(q[0],q[1],q[2],q[3]).toFixed(4)+"\n\n−q = ("+negTxt+")\n   表示同一个旋转\n反推 2·acos(w) = "+back.toFixed(1)+"°"+(th>360?"\n（θ>360°：与 θ−360° 朝向相同，\n  四元数差一个负号）":"");
  }
  [sa,se,st].forEach(i=>i.addEventListener("input",draw));draw();
}
function gimbalLab(root){
  const {row,left,side}=shell(root,"交互实验 · 欧拉角与万向节锁","约定与 demo 相同：先 roll（绕 X）、再 pitch（绕 Y）、最后 yaw（绕 Z）。蓝 / 绿 / 橙三个环分别是 yaw / pitch / roll 的转动平面。把 pitch 拉到 90°，再分别拖 yaw 和 roll：盒子转的是同一个方向。");
  const pre=select(row,"预设",[["20,30,10","任意 (20, 30, 10)"],["30,90,0","(30, 90, 0)"],["0,90,-30","(0, 90, −30)"],["60,90,30","(60, 90, 30)"]],"20,30,10");
  const sy=slider(row,"yaw",-180,180,1,20,v=>v+"°"), sp=slider(row,"pitch",-90,90,1,30,v=>v+"°"), sr=slider(row,"roll",-180,180,1,10,v=>v+"°");
  const W=560,H=330;const cv=canvas(W,H),c=cv[0],g=cv[1];left.appendChild(c);const info=el("div","kv");side.appendChild(info);
  const P=camera(-35,22);
  function ring(pr,qq,plane,rad,col,w){g.strokeStyle=col;g.lineWidth=w;g.beginPath();for(let k=0;k<=96;k++){const t=k/96*2*Math.PI;const v=plane==="xy"?[Math.cos(t)*rad,Math.sin(t)*rad,0]:plane==="xz"?[Math.cos(t)*rad,0,Math.sin(t)*rad]:[0,Math.cos(t)*rad,Math.sin(t)*rad];const p=pr(qq?qRot(qq,v):v);k?g.lineTo(p[0],p[1]):g.moveTo(p[0],p[1]);}g.stroke();}
  function draw(){
    const y=+sy.value,p=+sp.value,r=+sr.value;
    const qy=qAxis([0,0,1],y), qp=qAxis([0,1,0],p), qr=qAxis([1,0,0],r); const q=qMul(qy,qMul(qp,qr));
    g.clearRect(0,0,W,H);
    const pr=draw3D(g,P,W/2,H/2+10,80,v=>qRot(q,v),{ghost:false});
    ring(pr,null,"xy",1.75,"rgba(47,91,211,.75)",3); ring(pr,qy,"xz",1.5,"rgba(21,128,61,.75)",3); ring(pr,qMul(qy,qp),"yz",1.25,"rgba(194,65,12,.8)",3);
    const lock=Math.abs(Math.abs(p)-90)<0.5;
    if(lock){g.fillStyle="#FDEEEE";g.fillRect(10,10,256,40);text(g,"万向节锁：roll 环与 yaw 环共面",20,28,COL.r,"left",true);text(g,"拖 yaw 和拖 roll 转的是同一个方向",20,44,COL.r,"left",false,11);}
    const ra=qRot(qMul(qy,qp),[1,0,0]);
    info.textContent="(yaw, pitch, roll) = ("+y+", "+p+", "+r+")\n\n四元数 q = qz·qy·qx\nw "+q[0].toFixed(4)+"  x "+q[1].toFixed(4)+"\ny "+q[2].toFixed(4)+"  z "+q[3].toFixed(4)+
      "\n\nroll 轴在世界中的方向：\n("+ra.map(v=>v.toFixed(2)).join(", ")+")\nyaw 轴：(0, 0, 1)\n\nyaw − roll = "+(y-r)+"°\nyaw + roll = "+(y+r)+"°\n"+(lock?(p>0?"pitch=+90°：只有 yaw − roll 起作用":"pitch=−90°：只有 yaw + roll 起作用"):"pitch≠±90°：三个角各自独立");
  }
  [sy,sp,sr].forEach(i=>i.addEventListener("input",draw));
  pre.addEventListener("change",()=>{const v=pre.value.split(",").map(Number);sy.value=v[0];sp.value=v[1];sr.value=v[2];[sy,sp,sr].forEach(i=>i.dispatchEvent(new Event("input")));});
  draw();
}

const MAP={quatlab:quatLab,bslab:bsLab,synclab:syncLab,layerlab:layerLab,translab:transLab,iklab:ikLab,qaxislab:qaxisLab,gimballab:gimbalLab};
document.querySelectorAll(".lab.alab").forEach(d=>{for(const k in MAP) if(d.classList.contains(k)){try{MAP[k](d);}catch(e){d.textContent="实验加载失败："+e.message;}}});
})();
