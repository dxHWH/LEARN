
/* ================= 实验 1：光栅化网格（覆盖规则 / Top-Left / 2x2 Quad） ================= */
(function(){
  const cv = document.getElementById("rs-cv"); if(!cv) return;
  const ctx = cv.getContext("2d");
  const GW = 24, GH = 14, C = 30;
  const PRE = [
    [{x:3,y:10},{x:12.5,y:1.5},{x:12.5,y:12.5},{x:21,y:8}],
    [{x:1,y:2},{x:22.5,y:5.5},{x:2.5,y:4.5},{x:21.5,y:9}],
    [{x:10.25,y:6.25},{x:11.5,y:6.75},{x:10.75,y:7.75},{x:12.25,y:8}],
    [{x:1,y:12.5},{x:12,y:0.5},{x:14,y:13},{x:23,y:6}]
  ];
  let V = PRE[0].map(p=>({...p})), drag = -1;
  const $ = id => document.getElementById(id);
  const rule = () => +$("rs-rule").value;
  // 定点：1/4 像素精度（演示用；硬件是 1/256）
  const fx = v => Math.round(v * 4);
  function triCover(a, b, c){
    let X=[fx(a.x),fx(b.x),fx(c.x)], Y=[fx(a.y),fx(b.y),fx(c.y)];
    let area=(X[1]-X[0])*(Y[2]-Y[0])-(Y[1]-Y[0])*(X[2]-X[0]);
    const cov=new Uint8Array(GW*GH); if(area===0) return {cov,area:0};
    if(area<0){[X[1],X[2]]=[X[2],X[1]];[Y[1],Y[2]]=[Y[2],Y[1]];}
    const tl=(i,j)=>{const dx=X[j]-X[i],dy=Y[j]-Y[i];return dy<0||(dy===0&&dx>0);};
    const E=(i,j,px,py)=>(X[j]-X[i])*(py-Y[i])-(Y[j]-Y[i])*(px-X[i]);
    const ok=(e,i,j)=>{const r=rule(); if(e>0) return true; if(e<0) return false; return r===0||(r===2&&tl(i,j));};
    for(let y=0;y<GH;y++)for(let x=0;x<GW;x++){
      const px=x*4+2, py=y*4+2;
      if(ok(E(1,2,px,py),1,2)&&ok(E(2,0,px,py),2,0)&&ok(E(0,1,px,py),0,1)) cov[y*GW+x]=1;
    }
    return {cov,area:Math.abs(area)/32};
  }
  function draw(){
    const A = triCover(V[0],V[1],V[2]);
    const pair = $("rs-pair").checked;
    const B = pair ? triCover(V[1],V[3],V[2]) : {cov:new Uint8Array(GW*GH),area:0};
    ctx.clearRect(0,0,cv.width,cv.height);
    ctx.fillStyle="#fff"; ctx.fillRect(0,0,cv.width,cv.height);
    let nA=0,nB=0,nAB=0;
    for(let y=0;y<GH;y++)for(let x=0;x<GW;x++){
      const a=A.cov[y*GW+x], b=B.cov[y*GW+x];
      if(a)nA++; if(b)nB++; if(a&&b)nAB++;
      if(a&&b) ctx.fillStyle="rgba(185,28,28,.55)"; else if(a) ctx.fillStyle="rgba(47,91,211,.30)"; else if(b) ctx.fillStyle="rgba(194,65,12,.28)"; else continue;
      ctx.fillRect(x*C,y*C,C,C);
    }
    // quads
    let quads=0;
    if($("rs-quad").checked){
      for(let qy=0;qy<GH;qy+=2)for(let qx=0;qx<GW;qx+=2){
        let any=0; for(let k=0;k<4;k++){const x=qx+(k&1),y=qy+(k>>1); if(x<GW&&y<GH&&A.cov[y*GW+x]) any=1;}
        if(!any) continue; quads++;
        for(let k=0;k<4;k++){const x=qx+(k&1),y=qy+(k>>1); if(x<GW&&y<GH&&!A.cov[y*GW+x]){ctx.fillStyle="rgba(180,83,9,.16)";ctx.fillRect(x*C,y*C,C,C);
          ctx.strokeStyle="rgba(180,83,9,.5)";ctx.beginPath();ctx.moveTo(x*C,y*C+C);ctx.lineTo(x*C+C,y*C);ctx.stroke();}}
        ctx.strokeStyle="#15803D"; ctx.lineWidth=2; ctx.strokeRect(qx*C+1,qy*C+1,2*C-2,2*C-2); ctx.lineWidth=1;
      }
    } else {
      for(let qy=0;qy<GH;qy+=2)for(let qx=0;qx<GW;qx+=2){let any=0;for(let k=0;k<4;k++){const x=qx+(k&1),y=qy+(k>>1);if(A.cov[y*GW+x])any=1;} if(any)quads++;}
    }
    // grid
    ctx.strokeStyle="#E5E7EB";
    for(let x=0;x<=GW;x++){ctx.beginPath();ctx.moveTo(x*C+.5,0);ctx.lineTo(x*C+.5,GH*C);ctx.stroke();}
    for(let y=0;y<=GH;y++){ctx.beginPath();ctx.moveTo(0,y*C+.5);ctx.lineTo(GW*C,y*C+.5);ctx.stroke();}
    // shared-edge samples
    let onEdge=0, e0=0,e1=0,e2=0;
    if(pair){
      const ax=fx(V[1].x),ay=fx(V[1].y),bx=fx(V[2].x),by=fx(V[2].y);
      for(let y=0;y<GH;y++)for(let x=0;x<GW;x++){
        const px=x*4+2,py=y*4+2; const e=(bx-ax)*(py-ay)-(by-ay)*(px-ax);
        if(e!==0) continue; const t=(px-ax)*(bx-ax)+(py-ay)*(by-ay), L=(bx-ax)**2+(by-ay)**2; if(t<0||t>L) continue;
        onEdge++; const k=A.cov[y*GW+x]+B.cov[y*GW+x]; if(k===0)e0++; else if(k===1)e1++; else e2++;
        ctx.strokeStyle="#111827"; ctx.lineWidth=2; ctx.beginPath(); ctx.arc(x*C+C/2,y*C+C/2,6,0,7); ctx.stroke(); ctx.lineWidth=1;
      }
    }
    // centers
    for(let y=0;y<GH;y++)for(let x=0;x<GW;x++){
      ctx.fillStyle=(A.cov[y*GW+x]||B.cov[y*GW+x])?"#111827":"#CBD5E1";
      ctx.beginPath(); ctx.arc(x*C+C/2,y*C+C/2,2.4,0,7); ctx.fill();
    }
    // bbox
    if($("rs-bbox").checked){
      const xs=[V[0].x,V[1].x,V[2].x], ys=[V[0].y,V[1].y,V[2].y];
      ctx.setLineDash([5,4]); ctx.strokeStyle="#6D28D9";
      ctx.strokeRect(Math.floor(Math.min(...xs))*C, Math.floor(Math.min(...ys))*C, (Math.ceil(Math.max(...xs))-Math.floor(Math.min(...xs)))*C, (Math.ceil(Math.max(...ys))-Math.floor(Math.min(...ys)))*C);
      ctx.setLineDash([]);
    }
    // triangles
    const tri=(a,b,c,col)=>{ctx.strokeStyle=col;ctx.lineWidth=2;ctx.beginPath();ctx.moveTo(a.x*C,a.y*C);ctx.lineTo(b.x*C,b.y*C);ctx.lineTo(c.x*C,c.y*C);ctx.closePath();ctx.stroke();ctx.lineWidth=1;};
    tri(V[0],V[1],V[2],"#2F5BD3"); if(pair) tri(V[1],V[3],V[2],"#C2410C");
    V.forEach((p,i)=>{ if(i===3&&!pair) return; ctx.fillStyle=i===3?"#C2410C":"#2F5BD3"; ctx.beginPath(); ctx.arc(p.x*C,p.y*C,7,0,7); ctx.fill();
      ctx.fillStyle="#111827"; ctx.font="12px sans-serif"; ctx.fillText("v"+i+" ("+p.x+","+p.y+")", p.x*C+9, p.y*C-8); });
    const rn=["E ≥ 0（全收）","E > 0（全不收）","Top-Left"][rule()];
    let s="规则："+rn+"   三角形A（蓝）面积 "+A.area.toFixed(2)+" px²，覆盖 "+nA+" 像素";
    s+="\n2x2 Quad："+quads+" 个 → PS 通道 "+(quads*4)+"，Quad 效率 "+(quads?(100*nA/(quads*4)).toFixed(1):"0")+"%（打开\"显示 2×2 Quad\"可见 Helper 像素）";
    if(pair){ s+="\n三角形B（橙）覆盖 "+nB+" 像素；两者重复覆盖 "+nAB+" 像素";
      s+="\n共享边 v1–v2 恰好穿过 "+onEdge+" 个像素中心（黑圈）：被画 0 次 "+e0+" 个，1 次 "+e1+" 个，2 次 "+e2+" 个"+(onEdge? (e0? "  ← 有裂缝":"")+(e2?"  ← 有重复":"")+(!e0&&!e2?"  ← 恰好一次 ✓":""):""); }
    $("rs-out").textContent=s;
  }
  const pos=e=>{const r=cv.getBoundingClientRect();return {x:(e.clientX-r.left)*cv.width/r.width/C, y:(e.clientY-r.top)*cv.height/r.height/C};};
  cv.addEventListener("pointerdown",e=>{const p=pos(e);let best=-1,bd=0.6;V.forEach((v,i)=>{const d=Math.hypot(v.x-p.x,v.y-p.y);if(d<bd){bd=d;best=i;}});drag=best;if(drag>=0)cv.setPointerCapture(e.pointerId);});
  cv.addEventListener("pointermove",e=>{if(drag<0)return;const p=pos(e);V[drag]={x:Math.max(0,Math.min(GW,Math.round(p.x*4)/4)),y:Math.max(0,Math.min(GH,Math.round(p.y*4)/4))};draw();});
  cv.addEventListener("pointerup",()=>{drag=-1;});
  ["rs-rule","rs-quad","rs-bbox","rs-pair"].forEach(id=>$(id).addEventListener("change",draw));
  document.querySelectorAll("#lab-raster [data-pre]").forEach(b=>b.onclick=()=>{V=PRE[+b.dataset.pre].map(p=>({...p}));draw();});
  draw();
})();

/* ================= 实验 2：仿射插值 vs 透视校正 ================= */
(function(){
  const cv=document.getElementById("pc-cv"); if(!cv) return;
  const ctx=cv.getContext("2d"), W=360, H=300, $=id=>document.getElementById(id);
  function render(){
    const tilt=+$("pc-tilt").value*Math.PI/180, seg=+$("pc-seg").value;
    $("pc-tilt-v").textContent=$("pc-tilt").value+"°"; $("pc-seg-v").textContent=seg+"×"+seg+"（"+(2*seg*seg)+" 个三角形）";
    const img=ctx.createImageData(2*W,H), D=img.data;
    for(let i=0;i<D.length;i+=4){D[i]=246;D[i+1]=248;D[i+2]=251;D[i+3]=255;}
    const f=1/Math.tan(Math.PI/6), asp=W/H, S=1.0;
    const vert=(u,v)=>{const x=(u*2-1)*S,y=(v*2-1)*S;const wx=x, wy=y*Math.cos(tilt)-0.15, wz=-2.5-y*Math.sin(tilt);
      const w=-wz; return {sx:(wx/w*f/asp*0.5+0.5)*W, sy:(0.5-0.5*wy/w*f)*H, w, u, v};};
    const tris=[];
    for(let j=0;j<seg;j++)for(let i=0;i<seg;i++){
      const a=vert(i/seg,j/seg),b=vert((i+1)/seg,j/seg),c=vert((i+1)/seg,(j+1)/seg),d=vert(i/seg,(j+1)/seg);
      tris.push([a,b,c],[a,c,d]);
    }
    let errSum=0,errMax=0,n=0;
    const tex=(u,v)=>(((Math.floor(u*8)+Math.floor(v*8))&1)?[235,236,240]:[47,91,211]);
    for(const t of tris){
      const [A,B,Cc]=t; const area=(B.sx-A.sx)*(Cc.sy-A.sy)-(B.sy-A.sy)*(Cc.sx-A.sx); if(area===0) continue;
      const x0=Math.max(0,Math.floor(Math.min(A.sx,B.sx,Cc.sx))), x1=Math.min(W-1,Math.ceil(Math.max(A.sx,B.sx,Cc.sx)));
      const y0=Math.max(0,Math.floor(Math.min(A.sy,B.sy,Cc.sy))), y1=Math.min(H-1,Math.ceil(Math.max(A.sy,B.sy,Cc.sy)));
      for(let y=y0;y<=y1;y++)for(let x=x0;x<=x1;x++){
        const px=x+0.5,py=y+0.5;
        let l0=((B.sx-px)*(Cc.sy-py)-(B.sy-py)*(Cc.sx-px))/area, l1=((Cc.sx-px)*(A.sy-py)-(Cc.sy-py)*(A.sx-px))/area, l2=1-l0-l1;
        if(l0<0||l1<0||l2<0) continue;
        const ua=l0*A.u+l1*B.u+l2*Cc.u, va=l0*A.v+l1*B.v+l2*Cc.v;
        const q0=l0/A.w,q1=l1/B.w,q2=l2/Cc.w,qs=q0+q1+q2;
        const up=(q0*A.u+q1*B.u+q2*Cc.u)/qs, vp=(q0*A.v+q1*B.v+q2*Cc.v)/qs;
        const e=Math.hypot(ua-up,va-vp)*8; errSum+=e; errMax=Math.max(errMax,e); n++;
        let c=tex(ua,va), k=(y*2*W+x)*4; D[k]=c[0];D[k+1]=c[1];D[k+2]=c[2];
        c=tex(up,vp); k=(y*2*W+x+W)*4; D[k]=c[0];D[k+1]=c[1];D[k+2]=c[2];
      }
    }
    ctx.putImageData(img,0,0);
    if($("pc-wire").checked){ctx.strokeStyle="rgba(194,65,12,.75)";ctx.lineWidth=1;
      for(const off of [0,W]) for(const t of tris){ctx.beginPath();ctx.moveTo(t[0].sx+off,t[0].sy);ctx.lineTo(t[1].sx+off,t[1].sy);ctx.lineTo(t[2].sx+off,t[2].sy);ctx.closePath();ctx.stroke();}}
    ctx.fillStyle="#111827"; ctx.font="bold 13px sans-serif"; ctx.fillText("屏幕空间线性插值（仿射）",10,18); ctx.fillText("透视校正插值",W+10,18);
    ctx.strokeStyle="#9CA3AF"; ctx.beginPath(); ctx.moveTo(W+.5,0); ctx.lineTo(W+.5,H); ctx.stroke();
    $("pc-out").textContent="仿射插值相对透视校正的 uv 误差：平均 "+(n?(errSum/n).toFixed(3):0)+" 格，最大 "+errMax.toFixed(3)+" 格（1 格 = 棋盘的一个方块）\n倾角越大，同一三角形三个顶点的 w 相差越大，误差越大；细分能把误差压到每个小三角形内部，但无法消除。";
  }
  ["pc-tilt","pc-seg","pc-wire"].forEach(id=>$(id).addEventListener("input",render));
  render();
})();

/* ================= 实验 3：深度精度 ================= */
(function(){
  const cv=document.getElementById("dp-cv"); if(!cv) return;
  const ctx=cv.getContext("2d"), $=id=>document.getElementById(id);
  const f32=new Float32Array(1), u32=new Uint32Array(f32.buffer);
  const ulpUp=x=>{f32[0]=x;const a=f32[0];u32[0]+=1;return Math.abs(f32[0]-a);};
  const ulpDn=x=>{f32[0]=x;const a=f32[0];if(a===0) return 1.4e-45; u32[0]-=1;return Math.abs(a-f32[0]);};
  const L=60,R=700, T1=24,B1=190, T2=232,B2=398;
  const fmt=v=>v>=100?v.toFixed(0):v>=1?v.toFixed(2):v.toPrecision(3);
  function draw(){
    let n=Math.pow(10,+$("dp-near").value), f=Math.pow(10,+$("dp-far").value); if(f<n*10) f=n*10;
    let q=Math.pow(10,+$("dp-q").value); q=Math.min(Math.max(q,n*1.0001),f*0.9999);
    $("dp-near-v").textContent=fmt(n)+" m"; $("dp-far-v").textContent=fmt(f)+" m"; $("dp-q-v").textContent=fmt(q)+" m";
    const zs=d=>f*(d-n)/(d*(f-n)), zr=d=>n*(f-d)/(d*(f-n)), zg=d=>(f+n)/(f-n)-2*f*n/((f-n)*d), der=d=>f*n/((f-n)*d*d);
    const res={
      std:d=>ulpUp(zs(d))/der(d),
      d24:d=>(1/16777215)/der(d),
      gl:d=>ulpUp(zg(d))/(2*der(d)),
      rev:d=>ulpDn(zr(d))/der(d)
    };
    ctx.clearRect(0,0,cv.width,cv.height); ctx.fillStyle="#fff"; ctx.fillRect(0,0,cv.width,cv.height);
    const lx=d=>L+(Math.log10(d)-Math.log10(n))/(Math.log10(f)-Math.log10(n))*(R-L);
    // axes
    ctx.strokeStyle="#9CA3AF"; ctx.strokeRect(L,T1,R-L,B1-T1); ctx.strokeRect(L,T2,R-L,B2-T2);
    ctx.fillStyle="#4B5563"; ctx.font="11px sans-serif";
    for(let e=Math.ceil(Math.log10(n));e<=Math.floor(Math.log10(f));e++){const x=lx(Math.pow(10,e));
      ctx.strokeStyle="#EEF0F3"; ctx.beginPath(); ctx.moveTo(x,T1); ctx.lineTo(x,B1); ctx.moveTo(x,T2); ctx.lineTo(x,B2); ctx.stroke();
      ctx.fillText(fmt(Math.pow(10,e))+"m",x-12,B2+14); }
    ctx.fillText("1",L-14,T1+8); ctx.fillText("0",L-14,B1); ctx.fillText("深度值",4,(T1+B1)/2);
    const N=320, ds=[]; for(let i=1;i<N;i++) ds.push(n*Math.pow(f/n,i/N));
    const plot=(fn,col,y0,y1,map,dash)=>{ctx.strokeStyle=col;ctx.lineWidth=2;ctx.setLineDash(dash||[]);ctx.beginPath();ds.forEach((d,i)=>{const y=map(fn(d));i?ctx.lineTo(lx(d),y):ctx.moveTo(lx(d),y);});ctx.stroke();ctx.setLineDash([]);ctx.lineWidth=1;};
    const m1=v=>B1-v*(B1-T1);
    plot(d=>(d-n)/(f-n),"#9CA3AF",T1,B1,m1,[4,4]);
    plot(zs,"#2F5BD3",T1,B1,m1); plot(zr,"#15803D",T1,B1,m1);
    // bottom log plot
    let lo=1e30,hi=0; for(const d of ds) for(const k in res){const v=res[k](d); if(v>0){lo=Math.min(lo,v);hi=Math.max(hi,v);}}
    const elo=Math.max(-10,Math.floor(Math.log10(lo))), ehi=Math.ceil(Math.log10(hi));
    const m2=v=>B2-(Math.max(Math.log10(Math.max(v,1e-40)),elo)-elo)/(ehi-elo)*(B2-T2);
    for(let e=elo;e<=ehi;e+=Math.max(1,Math.round((ehi-elo)/6))){const y=m2(Math.pow(10,e));ctx.strokeStyle="#EEF0F3";ctx.beginPath();ctx.moveTo(L,y);ctx.lineTo(R,y);ctx.stroke();ctx.fillStyle="#4B5563";ctx.fillText("1e"+e,L-40,y+4);}
    ctx.fillText("可分辨距离",2,T2-6);
    plot(res.std,"#2F5BD3",T2,B2,m2); plot(res.d24,"#C2410C",T2,B2,m2,[6,4]); plot(res.gl,"#6D28D9",T2,B2,m2,[2,3]); plot(res.rev,"#15803D",T2,B2,m2);
    // query line
    const qx=lx(q); ctx.strokeStyle="#B91C1C"; ctx.setLineDash([3,3]); ctx.beginPath(); ctx.moveTo(qx,T1); ctx.lineTo(qx,B2); ctx.stroke(); ctx.setLineDash([]);
    // legend
    const lg=[["#2F5BD3","标准 Z（D3D, float）"],["#15803D","Reversed-Z（float）"],["#9CA3AF","线性参考 (d-n)/(f-n)"],["#C2410C","标准 Z 24 位定点"],["#6D28D9","OpenGL [-1,1] float"]];
    lg.forEach((l,i)=>{ctx.fillStyle=l[0];ctx.fillRect(L+8+(i%3)*210,T1+6+Math.floor(i/3)*0+(i>=3?T2-T1:0),14,4);ctx.fillStyle="#1F2937";ctx.fillText(l[1],L+26+(i%3)*210,T1+11+(i>=3?T2-T1:0));});
    const half=n*f/(f-0.5*(f-n));
    $("dp-out").textContent="near="+fmt(n)+"m, far="+fmt(f)+"m；标准 Z 下 z=0.5 对应 "+fmt(half)+"m（一半深度值用在这之前）\n在 "+fmt(q)+"m 处：标准Z深度值 "+zs(q).toFixed(7)+"，Reversed 深度值 "+zr(q).toExponential(3)+
      "\n可分辨的最小前后距离：标准Z float "+res.std(q).toExponential(2)+"m | 24位 "+res.d24(q).toExponential(2)+"m | GL float "+res.gl(q).toExponential(2)+"m | Reversed float "+res.rev(q).toExponential(2)+"m（约好 "+Math.round(res.std(q)/res.rev(q))+" 倍）";
  }
  ["dp-near","dp-far","dp-q"].forEach(id=>$(id).addEventListener("input",draw));
  draw();
})();
