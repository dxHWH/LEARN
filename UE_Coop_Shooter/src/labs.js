/* ============================================================ 联机射击电子书 · 交互实验
   绘制全部同步完成（截图 / 打印 PDF 不会空白）。规则与 demos/coop_net_demo.cpp 一致，但这里不加抖动、不加随机，
   所以数字会和书中表格（带 20 ms 抖动、多种子平均）略有差异。 */
(function(){
"use strict";
const COL={b:"#2F5BD3",o:"#C2410C",g:"#15803D",p:"#6D28D9",r:"#B91C1C",a:"#B45309",s:"#6B7280",t:"#0F766E",ink:"#1F2937"};
function el(tag,cls,html){const e=document.createElement(tag); if(cls) e.className=cls; if(html!=null) e.innerHTML=html; return e;}
function canvas(w,h){const c=document.createElement("canvas"); c.width=w*2; c.height=h*2; c.style.aspectRatio=w+" / "+h; const g=c.getContext("2d"); g.scale(2,2); return [c,g];}
function font(){return getComputedStyle(document.body).fontFamily;}
function text(g,s,x,y,col,align,bold,size){g.fillStyle=col||COL.ink; g.textAlign=align||"left"; g.font=(bold?"700 ":"")+(size||12)+"px "+font(); g.fillText(s,x,y);}
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

/* ---------------------------------------------------------------- 1. 开火时序 */
const NET_DT=1/30;
function simFire(scheme,rtt){
  const MAG=10,INT=0.12,HOLD=1.5,BV=200, ow=rtt/2000;
  const q=[]; let nid=0; const push=(t,type,a,b)=>{q.push({t,id:nid++,type,a:a||0,b:b||0});};
  const pop=()=>{let k=0; for(let i=1;i<q.length;i++){ if(q[i].t<q[k].t-1e-12||(Math.abs(q[i].t-q[k].t)<1e-12&&q[i].id<q[k].id)) k=i; } return q.splice(k,1)[0];};
  let ammoS=MAG,lastSeqS=0,burstS=0,sentAmmo=MAG,sentSeq=0,remSent=0; let pm=[],ps=[]; let lastArr=0;
  const inT={}, ok={}; let repAmmo=MAG,repSeq=0,seq=0,firing=true,ammoC=MAG; const ev=[]; let ghostOwn=0, rb=0, last=MAG; const own=[],rem=[],pops=[];
  const disp=()=>scheme===0?repAmmo:scheme===1?ammoC:repAmmo-(seq-repSeq);
  const rec=()=>{const d=disp(); if(d>last) rb++; last=d;};
  push(0,0); for(let t=NET_DT;t<4;t+=NET_DT) push(t,2);
  while(q.length){
    const e=pop(), t=e.t;
    if(e.type===0){ // client timer
      if(!firing||t>=HOLD-1e-9) continue;
      const can=scheme===0?repAmmo>0:disp()>0;
      if(!can){firing=false; ev.push([t,0,"dry",0]); continue;}
      seq++; inT[seq]=t; ev.push([t,0,"input",seq]);
      if(scheme!==0){ ev.push([t,0,"fx",seq]); own.push(0); if(scheme===1) ammoC--; rec(); }
      const a=Math.max(t+ow,lastArr+1e-6); lastArr=a; push(a,1,seq); push(t+INT,0);
    } else if(e.type===1){ // server recv
      const s=e.a, k=ammoS>0; ok[s]=k;
      if(scheme===0){ pm.push(k?s:-s); if(k){ammoS--; ps.push(t);} }
      else { if(k){ammoS--; burstS++;} lastSeqS=s; }
      ev.push([t,1,k?"ok":"reject",s]);
    } else if(e.type===2){ // server net tick
      if(ammoS!==sentAmmo||(scheme===2&&lastSeqS!==sentSeq)){push(t+ow,3,ammoS,lastSeqS); sentAmmo=ammoS; sentSeq=lastSeqS;}
      if(scheme!==0&&burstS!==remSent){push(t+ow,6,burstS-remSent,burstS); remSent=burstS;}
      pm.forEach(m=>{push(t+ow,4,m); push(t+ow,5,m);}); pm=[];
      ps.forEach(sp=>{pops.push(BV*(t-sp)); push(t+ow,7,Math.round(BV*(t-sp)*100));}); ps=[];
    } else if(e.type===3){ repAmmo=e.a; repSeq=e.b; if(scheme===1) ammoC=e.a; ev.push([t,0,"rep",e.a]); rec(); }
    else if(e.type===4){ const s=e.a; if(s>0) own.push(t-inT[s]); else ghostOwn++; ev.push([t,0,s>0?"fx":"ghost",Math.abs(s)]); }
    else if(e.type===5){ const s=e.a; if(s>0) rem.push(t-inT[s]); ev.push([t,2,s>0?"fx":"ghost",Math.abs(s)]); }
    else if(e.type===6){ const n=e.a, upto=e.b; const oks=Object.keys(ok).map(Number).filter(s=>ok[s]).sort((x,y)=>x-y);
      for(let k=upto-n+1;k<=upto;k++){ rem.push(t-inT[oks[k-1]]); ev.push([t,2,"fx",k]); } }
    else if(e.type===7){ ev.push([t,0,"bullet",e.a]); }
  }
  if(scheme!==0) Object.keys(ok).forEach(s=>{if(!ok[s]) ghostOwn++;});
  const m=a=>a.length?a.reduce((x,y)=>x+y,0)/a.length:0;
  return {ev,own:m(own)*1000,rem:m(rem)*1000,ghost:ghostOwn,rb,pop:m(pops),shots:Object.values(ok).filter(x=>x).length};
}
function fireLab(root){
  const {row,left,side}=shell(root,"交互实验 · 开火时序：本机 / 服务器 / 另一端","切换方案、拖动 RTT：看蓝三角（按下）到橙点（看到开火）的距离、红叉（空放 / 被拒）和菱形（弹药复制到达）。这里不加抖动，数字与表 4-1 略有差异。");
  const ss=select(row,"方案",[["0","原蓝图方案"],["1","预测 + 直接覆盖"],["2","预测 + 序号对账"]],"0");
  const sr=slider(row,"RTT",0,300,10,200,v=>v+" ms");
  const W=600,H=250; const [c,g]=canvas(W,H); left.appendChild(c);
  const info=el("div","kv"); side.appendChild(info);
  function draw(){
    const sc=+ss.value, rtt=+sr.value, r=simFire(sc,rtt);
    g.clearRect(0,0,W,H); const L=92,R=26,T1=1.5, X=t=>L+(W-L-R)*t/T1, LY=[50,120,190];
    ["本机","服务器","另一端"].forEach((n,i)=>{g.strokeStyle="#CBD5E1"; g.lineWidth=1.2; g.beginPath(); g.moveTo(L,LY[i]); g.lineTo(W-R,LY[i]); g.stroke(); text(g,n,L-10,LY[i]+4,COL.ink,"right",true);});
    for(let k=0;k<=15;k++){const x=X(k/10); g.strokeStyle="#F1F2F5"; g.beginPath(); g.moveTo(x,30); g.lineTo(x,210); g.stroke(); if(k%5===0) text(g,(k/10).toFixed(1)+" s",x,232,COL.s,"center",false,11);}
    r.ev.forEach(([t,lane,kind,v])=>{
      if(t>T1) return; const x=X(t), y=LY[lane];
      if(kind==="input"){g.fillStyle=COL.b; g.beginPath(); g.moveTo(x,y+3); g.lineTo(x-5,y+12); g.lineTo(x+5,y+12); g.fill();}
      else if(kind==="fx"||kind==="ok"){g.fillStyle=kind==="ok"?COL.g:COL.o; g.beginPath(); g.arc(x,y,4.5,0,7); g.fill();}
      else if(kind==="ghost"||kind==="reject"){g.strokeStyle=COL.r; g.lineWidth=2.2; g.beginPath(); g.moveTo(x-5,y-5); g.lineTo(x+5,y+5); g.moveTo(x-5,y+5); g.lineTo(x+5,y-5); g.stroke();}
      else if(kind==="rep"){g.save(); g.translate(x,y-18); g.rotate(Math.PI/4); g.fillStyle="#fff"; g.strokeStyle=COL.s; g.lineWidth=1; g.fillRect(-4,-4,8,8); g.strokeRect(-4,-4,8,8); g.restore(); text(g,String(v),x,y-26,COL.s,"center",false,10);}
      else if(kind==="dry"){g.fillStyle="#111827"; g.fillRect(x-5,y-5,10,10);}
      else if(kind==="bullet"){g.fillStyle=COL.t; g.fillRect(x-2,y+14,4,9);}
    });
    info.textContent="本机看到自己开火  "+r.own.toFixed(0)+" ms\n另一端看到        "+r.rem.toFixed(0)+" ms\n有效子弹          "+r.shots+" 发\n空放 / 被拒        "+r.ghost+" 次\nUI 回跳           "+r.rb+" 次"+(sc===0?"\n子弹突现距离      "+r.pop.toFixed(1)+" m":"");
  }
  ss.addEventListener("change",draw); sr.addEventListener("input",draw); draw();
}

/* ---------------------------------------------------------------- 2. 延迟补偿 */
function lcg(seed){let s=seed>>>0; return ()=>{s=(s*1664525+1013904223)>>>0; return s/4294967296;};}
function lagSim(v,rtt){
  const rnd=lcg(777), flips=[]; let tt=0; while(tt<32){tt+=0.25+0.65*rnd(); flips.push(tt);}
  const P=t=>{let x=0,last=0,dir=1; for(const f of flips){if(f>=t) break; x+=dir*v*(f-last); last=f; dir=-dir;} return x+dir*v*(t-last);};
  const V=t=>{let n=0; for(const f of flips){if(f>=t) break; n++;} return (n%2?-1:1)*v;};
  const ow=rtt/2000, R=30, LEE=200, MAXR=0.2;
  const hist=t=>{const k=Math.floor(t/NET_DT); const a=P(k*NET_DT), b=P((k+1)*NET_DT); return a+(b-a)*(t/NET_DT-k);};
  const shots=[]; let n=0,h0=0,hR=0,hL=0;
  for(let t=1;t<30;t+=0.05){
    const ts=Math.floor((t-ow)/NET_DT)*NET_DT; if(ts<0) continue; const ta=ts+ow;
    const disp=P(ts)+V(ts)*(t-ta), viewT=ts+(t-ta), arrive=t+ow, now=P(arrive), past=hist(Math.max(viewT,arrive-MAXR));
    n++; const a=Math.abs(disp-now)<=R, b=Math.abs(disp-past)<=R, c=Math.abs(disp-now)<=R+LEE; h0+=a; hR+=b; hL+=c;
    shots.push({t,disp,now,past,a,b,c});
  }
  return {none:100*h0/n,rew:100*hR/n,lee:100*hL/n,shots};
}
function lagLab(root){
  const {row,left,side}=shell(root,"交互实验 · 延迟补偿：本机看到的目标 vs 服务器判定时的目标","拖动“第几枪”看单次判定：蓝 = 本机看到并瞄准的位置，红 = 请求到达服务器时目标的真实位置，绿 = 服务器回溯到开火者看到的时刻（上限 200 ms）。圆的半径就是胶囊半径 30 cm。");
  const sv=select(row,"目标速度",[["200","200 cm/s（走）"],["480","480 cm/s（冲刺僵尸）"]],"480");
  const sr=slider(row,"RTT",0,300,10,200,v=>v+" ms");
  const sk=slider(row,"第几枪",0,560,1,123,v=>"#"+v);
  const W=600,H=262; const [c,g]=canvas(W,H); left.appendChild(c);
  const info=el("div","kv"); side.appendChild(info);
  function draw(){
    const v=+sv.value, rtt=+sr.value, r=lagSim(v,rtt), s=r.shots[Math.min(+sk.value,r.shots.length-1)];
    g.clearRect(0,0,W,H);
    const cx=W/2, sc=1.5, X=x=>cx+(x-s.disp)*sc, y=92, RR=30*sc;
    g.strokeStyle="#E5E7EB"; g.beginPath(); g.moveTo(10,y); g.lineTo(W-10,y); g.stroke();
    for(let k=-150;k<=150;k+=50){const x=X(s.disp+k); g.strokeStyle="#F1F2F5"; g.beginPath(); g.moveTo(x,y-RR-6); g.lineTo(x,y+RR+6); g.stroke(); text(g,(k>0?"+":"")+k+" cm",x,16,COL.s,"center",false,10);}
    const circ=(x,col)=>{g.strokeStyle=col; g.lineWidth=2; g.beginPath(); g.arc(X(x),y,RR,0,7); g.stroke(); g.fillStyle=col; g.beginPath(); g.arc(X(x),y,3,0,7); g.fill();};
    circ(s.now,COL.r); circ(s.past,COL.g); circ(s.disp,COL.b);
    text(g,"服务器现在",X(s.now),y-RR-8,COL.r,"center",true,11);
    text(g,"本机看到（瞄准点）",X(s.disp),y+RR+16,COL.b,"center",true,11);
    text(g,"回溯后",X(s.past),y+RR+30,COL.g,"center",true,11);
    g.strokeStyle=COL.b; g.setLineDash([4,3]); g.beginPath(); g.moveTo(X(s.disp),y-RR-2); g.lineTo(X(s.disp),y+RR+2); g.stroke(); g.setLineDash([]);
    const res=[["不补偿",r.none,s.a,COL.r],["服务器回溯",r.rew,s.b,COL.g],["客户端判定 + 容差",r.lee,s.c,COL.b]];
    res.forEach(([n,p,hit,col],i)=>{const yy=172+i*26; text(g,n,128,yy+12,COL.ink,"right",false,12); g.fillStyle="#EEF0F3"; g.fillRect(136,yy,300,16); g.fillStyle=col; g.globalAlpha=0.75; g.fillRect(136,yy,300*p/100,16); g.globalAlpha=1; text(g,p.toFixed(1)+"%",442,yy+12,col,"left",true,12); text(g,hit?"本枪：中":"本枪：空",522,yy+12,hit?COL.g:COL.r,"left",true,11);});
    info.textContent="本机显示误差  "+Math.abs(s.disp-s.now).toFixed(1)+" cm\n回溯后误差    "+Math.abs(s.disp-s.past).toFixed(1)+" cm\n受害者位移    "+Math.abs(s.now-s.past).toFixed(1)+" cm\n（目标在服务器“现在”已离开命中点多远）";
  }
  [sv].forEach(x=>x.addEventListener("change",draw)); [sr,sk].forEach(x=>x.addEventListener("input",draw)); draw();
}

document.querySelectorAll(".lab.firelab").forEach(fireLab);
document.querySelectorAll(".lab.lagcomplab").forEach(lagLab);
})();
