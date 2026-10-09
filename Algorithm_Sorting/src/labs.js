/* ================= 排序可视化实验（纯前端，无依赖） =================
 * 每个算法是一个 generator：直接在数组上操作，并 yield 事件给可视化器：
 *   cmp(i,j)  比较       swap(i,j) 交换      set(i) 写入        line(L,msg) 只高亮伪代码
 *   done(i..) 已就位     range(lo,hi) 当前子区间   pivot(obj) 基准   key(i) 当前元素
 * 元素是 {v,id}：v 参与比较，id 用来观察稳定性。
 */
(function(){
"use strict";
const ev = {
  cmp:(i,j,L,msg)=>({t:"cmp",i,j,L,msg}),
  swap:(a,i,j,L,msg)=>{const t=a[i];a[i]=a[j];a[j]=t;return {t:"swap",i,j,L,msg};},
  set:(a,i,x,L,msg)=>{a[i]=x;return {t:"set",i,L,msg};},
  put:(a,i,x)=>{a[i]=x;return {t:"put",i};},   // 仅为观察方便把 key 放进空位，不计入写次数
  line:(L,msg)=>({t:"line",L,msg}),
  done:(...idx)=>({t:"done",idx}),
  all:()=>({t:"all"}),
  range:(lo,hi)=>({t:"range",lo,hi}),
  pivot:(obj)=>({t:"pivot",obj}),
  key:(i)=>({t:"key",i}),
};
function rng(seed){let s=seed>>>0||1;return ()=>{s^=s<<13;s>>>=0;s^=s>>>17;s^=s<<5;s>>>=0;return s/4294967296;};}

/* ---------------- 算法定义 ---------------- */
const A = {};
A.bubble = {name:"冒泡排序", code:[
"for end = n-1 downto 1:",
"  swapped = false",
"  for j = 0 to end-1:",
"    if a[j] > a[j+1]:",
"      swap(a[j], a[j+1]); swapped = true",
"  // 本趟最大值已沉到 a[end]",
"  if not swapped: break    // 提前结束"],
gen:function*(a){const n=a.length;
  for(let end=n-1;end>0;end--){
    yield ev.range(0,end); yield ev.line(1,"第 "+(n-end)+" 趟：把 [0,"+end+"] 中最大的元素冒泡到末尾");
    let sw=false;
    for(let j=0;j<end;j++){
      yield ev.cmp(j,j+1,3,"比较 a["+j+"]="+a[j].v+" 与 a["+(j+1)+"]="+a[j+1].v);
      if(a[j].v>a[j+1].v){yield ev.swap(a,j,j+1,4,"逆序 → 交换");sw=true;}
    }
    yield ev.done(end); yield ev.line(5,"a["+end+"]="+a[end].v+" 就位");
    if(!sw){yield ev.line(6,"本趟没有任何交换 → 剩余部分已有序，提前结束（最好情况 O(n)）");break;}
  }
  yield ev.all();}};

A.selection = {name:"选择排序", code:[
"for i = 0 to n-2:",
"  m = i",
"  for j = i+1 to n-1:",
"    if a[j] < a[m]: m = j",
"  swap(a[i], a[m])   // 远距离交换 → 不稳定"],
gen:function*(a){const n=a.length;
  for(let i=0;i<n-1;i++){
    yield ev.range(i,n-1); let m=i; yield ev.key(m); yield ev.line(1,"在 ["+i+","+(n-1)+"] 里找最小值，先假设 m="+i);
    for(let j=i+1;j<n;j++){
      yield ev.cmp(j,m,3,"a["+j+"]="+a[j].v+" vs 当前最小 a["+m+"]="+a[m].v);
      if(a[j].v<a[m].v){m=j;yield ev.key(m);}
    }
    yield ev.key(-1);
    if(m!==i) yield ev.swap(a,i,m,4,"把最小值 "+a[i].v+" 换到位置 "+i+"（注意：被换走的 a["+i+"] 可能跳过了与它相等的元素）");
    yield ev.done(i);
  }
  yield ev.all();}};

A.insertion = {name:"插入排序", code:[
"for i = 1 to n-1:",
"  key = a[i]; j = i-1",
"  while j >= 0 and a[j] > key:",
"    a[j+1] = a[j]; j--      // 大的后移",
"  a[j+1] = key"],
gen:function*(a){const n=a.length;
  for(let i=1;i<n;i++){
    yield ev.range(0,i); const key=a[i]; let j=i-1; yield ev.key(i);
    yield ev.line(1,"抓起 key=a["+i+"]="+key.v+"，在左侧有序区里找插入位置");
    while(j>=0){
      yield ev.cmp(j,j+1,2,"a["+j+"]="+a[j].v+" > key="+key.v+" ?");
      if(a[j].v>key.v){yield ev.set(a,j+1,a[j],3,"后移 a["+j+"]");j--;yield ev.put(a,j+1,key);yield ev.key(j+1);} else break;
    }
    yield ev.key(-1); yield ev.set(a,j+1,key,4,"key="+key.v+" 落在位置 "+(j+1)+"（相等不越过 → 稳定）");
  }
  yield ev.all();}};

A.shell = {name:"希尔排序", code:[
"for gap in [... 10, 4, 1]:       // Ciura 序列",
"  for i = gap to n-1:            // 步长 gap 的插入排序",
"    key = a[i]; j = i",
"    while j >= gap and a[j-gap] > key:",
"      a[j] = a[j-gap]; j -= gap",
"    a[j] = key"],
gen:function*(a){const n=a.length; const gaps=[57,23,10,4,1].filter(g=>g<n);
  for(const gap of gaps){
    yield ev.line(0,"gap = "+gap+"：相距 "+gap+" 的元素组成一组，各组分别插入排序");
    for(let i=gap;i<n;i++){
      const key=a[i]; let j=i; yield ev.key(i);
      while(j>=gap){
        yield ev.cmp(j-gap,j,3,"a["+(j-gap)+"]="+a[j-gap].v+" > key="+key.v+" ?（跨 "+gap+" 格）");
        if(a[j-gap].v>key.v){yield ev.set(a,j,a[j-gap],4,"大元素一次跳 "+gap+" 格");j-=gap;yield ev.put(a,j,key);yield ev.key(j);} else break;
      }
      yield ev.key(-1); yield ev.set(a,j,key,5,"");
    }
  }
  yield ev.all();}};

A.merge = {name:"归并排序", code:[
"mergeSort(lo, hi):              // 区间 [lo, hi)",
"  if hi - lo < 2: return",
"  mid = (lo + hi) / 2",
"  mergeSort(lo, mid); mergeSort(mid, hi)",
"  tmp = a[lo..hi); i = lo; j = mid",
"  while i < mid and j < hi:",
"    a[k++] = (R[j] < L[i]) ? R[j++] : L[i++]  // 相等取左→稳定",
"  把剩下的一侧依次写回"],
gen:function*(a){const n=a.length;
  function* ms(lo,hi){
    if(hi-lo<2) return;
    const mid=(lo+hi)>>1;
    yield ev.range(lo,hi-1); yield ev.line(2,"拆分 ["+lo+","+hi+") → ["+lo+","+mid+") + ["+mid+","+hi+")");
    yield* ms(lo,mid); yield* ms(mid,hi);
    yield ev.range(lo,hi-1); yield ev.line(4,"合并 ["+lo+","+mid+") 与 ["+mid+","+hi+")");
    const tmp=a.slice(lo,hi); let i=0,j=mid-lo,k=lo; const m=mid-lo,h=hi-lo;
    while(i<m&&j<h){
      yield ev.cmp(k,k,6,"左 "+tmp[i].v+" vs 右 "+tmp[j].v);
      if(tmp[j].v<tmp[i].v){yield ev.set(a,k,tmp[j],6,"右边更小，写入 "+tmp[j].v);j++;}
      else{yield ev.set(a,k,tmp[i],6,"左边 ≤ 右边，写入左边的 "+tmp[i].v);i++;}
      k++;
    }
    while(i<m){yield ev.set(a,k++,tmp[i++],7,"左侧剩余写回");}
    while(j<h){yield ev.set(a,k++,tmp[j++],7,"右侧剩余写回");}
  }
  yield* ms(0,n); yield ev.all();}};

A.quickL = {name:"快排 · Lomuto（末元素作 pivot）", code:[
"quick(lo, hi):",
"  if lo >= hi: return",
"  p = a[hi]; i = lo          // 末元素作 pivot",
"  for j = lo to hi-1:",
"    if a[j] < p:",
"      swap(a[i], a[j]); i++",
"  swap(a[i], a[hi])          // pivot 归位",
"  quick(lo, i-1); quick(i+1, hi)"],
gen:function*(a){
  function* q(lo,hi){
    if(lo>hi) return;
    if(lo===hi){yield ev.done(lo);return;}
    yield ev.range(lo,hi); const p=a[hi]; yield ev.pivot(p); let i=lo;
    yield ev.line(2,"子区间 ["+lo+","+hi+"]，pivot = 末元素 "+p.v);
    for(let j=lo;j<hi;j++){
      yield ev.cmp(j,hi,4,"a["+j+"]="+a[j].v+" < pivot "+p.v+" ?");
      if(a[j].v<p.v){ if(i!==j) yield ev.swap(a,i,j,5,"放进左侧“小于区”"); else yield ev.line(5,"已在小于区，i 右移"); i++; }
    }
    yield ev.swap(a,i,hi,6,"pivot "+p.v+" 归位到 "+i); yield ev.pivot(null); yield ev.done(i);
    yield ev.line(7,"左 "+(i-lo)+" 个 / 右 "+(hi-i)+" 个"+((i-lo===0||hi-i===0)&&hi-lo>2?"  ← 一边为空：退化！":""));
    yield* q(lo,i-1); yield* q(i+1,hi);
  }
  yield* q(0,a.length-1); yield ev.all();}};

A.quickH = {name:"快排 · Hoare + 三数取中", code:[
"quick(lo, hi):",
"  if lo >= hi: return",
"  三数取中：排好 a[lo], a[mid], a[hi]；p = a[mid]",
"  i = lo - 1; j = hi + 1",
"  loop:",
"    do i++ while a[i] < p",
"    do j-- while a[j] > p",
"    if i >= j: break",
"    swap(a[i], a[j])",
"  quick(lo, j); quick(j+1, hi)  // 工程上：递归短边，循环长边"],
gen:function*(a){
  function* q(lo,hi){
    if(lo>=hi){ if(lo===hi) yield ev.done(lo); return; }
    yield ev.range(lo,hi);
    const mid=lo+((hi-lo)>>1);
    if(hi-lo>=2){
      yield ev.cmp(lo,mid,2,"三数取中"); if(a[mid].v<a[lo].v) yield ev.swap(a,lo,mid,2,"");
      yield ev.cmp(lo,hi,2,"三数取中"); if(a[hi].v<a[lo].v) yield ev.swap(a,lo,hi,2,"");
      yield ev.cmp(mid,hi,2,"三数取中"); if(a[hi].v<a[mid].v) yield ev.swap(a,mid,hi,2,"");
    }
    const p=a[mid]; yield ev.pivot(p); yield ev.line(2,"pivot = "+p.v+"（lo/mid/hi 三者的中位数）");
    let i=lo-1,j=hi+1;
    for(;;){
      do{ i++; yield ev.cmp(i,i,5,"i 右移：a["+i+"]="+a[i].v+" < "+p.v+" ?"); }while(a[i].v<p.v);
      do{ j--; yield ev.cmp(j,j,6,"j 左移：a["+j+"]="+a[j].v+" > "+p.v+" ?"); }while(a[j].v>p.v);
      if(i>=j){yield ev.line(7,"i、j 相遇，分区结束于 j="+j);break;}
      yield ev.swap(a,i,j,8,"交换 a["+i+"] 与 a["+j+"]");
    }
    yield ev.pivot(null);
    yield* q(lo,j); yield* q(j+1,hi);
  }
  yield* q(0,a.length-1); yield ev.all();}};

A.quick3 = {name:"快排 · 三路划分（荷兰国旗）", code:[
"quick3(lo, hi):",
"  if lo >= hi: return",
"  随机选 pivot 换到 lo；p = a[lo]",
"  lt = lo; i = lo+1; gt = hi",
"  while i <= gt:",
"    if a[i] < p:   swap(a[lt++], a[i++])",
"    elif a[i] > p: swap(a[i], a[gt--])",
"    else:          i++",
"  // [lt..gt] 全部等于 p，一次就位",
"  quick3(lo, lt-1); quick3(gt+1, hi)"],
gen:function*(a){ const R=rng(7);
  function* q(lo,hi){
    if(lo>=hi){ if(lo===hi) yield ev.done(lo); return; }
    yield ev.range(lo,hi);
    const r=lo+Math.floor(R()*(hi-lo+1)); if(r!==lo) yield ev.swap(a,lo,r,2,"随机 pivot 换到 lo");
    const p=a[lo]; yield ev.pivot(p); yield ev.line(2,"pivot = "+p.v);
    let lt=lo,i=lo+1,gt=hi;
    while(i<=gt){
      yield ev.cmp(i,lt,5,"a["+i+"]="+a[i].v+" 与 p="+p.v);
      if(a[i].v<p.v){yield ev.swap(a,lt,i,5,"< p：换进左区");lt++;i++;}
      else if(a[i].v>p.v){yield ev.swap(a,i,gt,6,"> p：换到右区");gt--;}
      else {i++; yield ev.line(7,"= p：留在中间");}
    }
    yield ev.pivot(null); const idx=[]; for(let k=lt;k<=gt;k++) idx.push(k); yield ev.done(...idx);
    yield ev.line(8,"等于 "+p.v+" 的 "+(gt-lt+1)+" 个元素一次性就位");
    yield* q(lo,lt-1); yield* q(gt+1,hi);
  }
  yield* q(0,a.length-1); yield ev.all();}};

A.heap = {name:"堆排序", code:[
"for i = n/2-1 downto 0: siftDown(i, n)   // 建大顶堆 O(n)",
"for end = n-1 downto 1:",
"  swap(a[0], a[end])        // 堆顶(最大)放到末尾",
"  siftDown(0, end)",
"siftDown(i, n):",
"  while 2i+1 < n:",
"    c = 2i+1 与 2i+2 中较大者",
"    if a[i] >= a[c]: break",
"    swap(a[i], a[c]); i = c"],
gen:function*(a){const n=a.length;
  function* sd(i,m,L){
    while(2*i+1<m){
      let c=2*i+1;
      if(c+1<m){yield ev.cmp(c,c+1,6,"两个孩子 "+a[c].v+" / "+a[c+1].v+" 取较大"); if(a[c+1].v>a[c].v) c++;}
      yield ev.cmp(i,c,7,"父 "+a[i].v+" vs 大孩子 "+a[c].v);
      if(a[i].v>=a[c].v) break;
      yield ev.swap(a,i,c,8,"下沉"); i=c;
    }
  }
  yield ev.range(0,n-1); yield ev.line(0,"阶段一：自底向上建堆");
  for(let i=(n>>1)-1;i>=0;i--){ yield ev.key(i); yield* sd(i,n); }
  yield ev.key(-1);
  for(let end=n-1;end>0;end--){
    yield ev.swap(a,0,end,2,"堆顶最大值 "+a[0].v+" 换到末尾 "+end); yield ev.done(end);
    yield ev.range(0,end-1); yield* sd(0,end);
  }
  yield ev.all();}};

A.counting = {name:"计数排序", code:[
"cnt[0..k] = 0",
"for x in a: cnt[x]++",
"for v = 1..k: cnt[v] += cnt[v-1]   // 前缀和 = 该值的结束位置",
"for i = n-1 downto 0:             // 倒序扫描 → 稳定",
"  out[--cnt[a[i]]] = a[i]",
"a = out"],
gen:function*(a){const n=a.length; let k=0; for(const x of a) k=Math.max(k,x.v);
  const cnt=new Array(k+1).fill(0);
  for(let i=0;i<n;i++){ yield ev.cmp(i,i,1,"统计：值 "+a[i].v+" 的个数 +1（不做比较，只是读取）"); cnt[a[i].v]++; }
  for(let v=1;v<=k;v++) cnt[v]+=cnt[v-1];
  yield ev.line(2,"前缀和完成：cnt[v] = 小于等于 v 的元素个数");
  const src=a.slice(); for(let i=0;i<n;i++) a[i]=null; yield ev.line(3,"开始按位置放置（原数组视为输入，这里显示输出数组）");
  for(let i=n-1;i>=0;i--){ const p=--cnt[src[i].v]; yield ev.set(a,p,src[i],4,"输入第 "+i+" 个(值 "+src[i].v+") → 输出位置 "+p); }
  yield ev.all();}};

A.radix = {name:"基数排序（LSD，十进制）", code:[
"for exp = 1, 10, 100, ...:          // 个位 → 十位 → 百位",
"  cnt[0..9] = 0; 按当前位计数",
"  前缀和",
"  for i = n-1 downto 0:",
"    out[--cnt[digit(a[i])]] = a[i]  // 每一趟必须稳定！",
"  a = out"],
gen:function*(a){const n=a.length; let mx=0; for(const x of a) mx=Math.max(mx,x.v);
  for(let exp=1; Math.floor(mx/exp)>0; exp*=10){
    const d=x=>Math.floor(x.v/exp)%10, nm=exp===1?"个位":exp===10?"十位":"百位";
    const cnt=new Array(10).fill(0);
    for(let i=0;i<n;i++){ yield ev.cmp(i,i,1,"按"+nm+"计数：a["+i+"]="+a[i].v+" 的"+nm+"是 "+d(a[i])); cnt[d(a[i])]++; }
    for(let b=1;b<10;b++) cnt[b]+=cnt[b-1];
    const src=a.slice(); for(let i=0;i<n;i++) a[i]=null; yield ev.line(3,"按"+nm+"分配（显示输出数组）");
    for(let i=n-1;i>=0;i--){ const p=--cnt[d(src[i])]; yield ev.set(a,p,src[i],4,src[i].v+" 的"+nm+"="+d(src[i])+" → 位置 "+p); }
    yield ev.line(5,nm+"这一趟结束：现在按“低 "+(String(exp).length)+" 位”有序");
  }
  yield ev.all();}};

/* ---------------- 数据 ---------------- */
const PAT = {random:"随机", sorted:"已有序", reversed:"逆序", nearly:"近乎有序", few:"大量重复"};
function makeData(n,pat,seed,vmax){
  const R=rng(seed); vmax=vmax||99; let v=[];
  if(pat==="few"){ const vals=[20,40,60,80,vmax].map(x=>Math.round(x*vmax/99)); for(let i=0;i<n;i++) v.push(vals[Math.floor(R()*5)]); }
  else { for(let i=0;i<n;i++) v.push(5+Math.floor(R()*(vmax-4))); }
  if(pat==="sorted"||pat==="nearly") v.sort((x,y)=>x-y);
  if(pat==="reversed") v.sort((x,y)=>y-x);
  if(pat==="nearly") for(let k=0;k<Math.max(1,n/12);k++){const i=Math.floor(R()*n),j=Math.min(n-1,i+1+Math.floor(R()*2));const t=v[i];v[i]=v[j];v[j]=t;}
  return v.map((x,id)=>({v:x,id}));
}

/* ---------------- 单算法动画实验 ---------------- */
const C = {base:"#9DB2EC", cmp:"#F59E0B", swap:"#EF4444", set:"#F97316", done:"#34D399", pivot:"#8B5CF6", key:"#2563EB", out:"#DCE3F1"};
function SortLab(root){
  const algos=(root.dataset.algos||"bubble").split(",");
  let n=+(root.dataset.n||16), pat=root.dataset.pattern||"random", seed=+(root.dataset.seed||3), speed=+(root.dataset.speed||8);
  const sel=algos.length>1?'<label>算法 <select class="al">'+algos.map(k=>'<option value="'+k+'">'+A[k].name+'</option>').join("")+'</select></label>':'<b style="font-size:14px">'+A[algos[0]].name+'</b>';
  root.innerHTML='<div class="t">'+(root.dataset.title||"交互实验 · 排序过程可视化")+'</div>'+
   '<div class="top">'+sel+
   '<label>数据 <select class="pt">'+Object.entries(PAT).map(([k,v])=>'<option value="'+k+'"'+(k===pat?" selected":"")+'>'+v+'</option>').join("")+'</select></label>'+
   '<label>规模 <input type="range" class="nn" min="6" max="48" value="'+n+'"> <b class="nv">'+n+'</b></label>'+
   '<label>速度 <input type="range" class="sp" min="1" max="60" value="'+speed+'"></label>'+
   '<button class="st-btn play">▶ 播放</button><button class="st-btn step">单步</button><button class="st-btn rs">重置</button><button class="st-btn sh">换一组</button></div>'+
   '<div class="stage"><canvas></canvas><div class="pc"></div></div>'+
   '<div class="stats">比较 <b class="sc">0</b>　交换/写入 <b class="sw">0</b>　步数 <b class="ss">0</b>　<span class="state"></span></div>'+
   '<div class="msg"></div>'+
   '<div class="legend"><span><i style="background:'+C.cmp+'"></i>比较/读取</span><span><i style="background:'+C.swap+'"></i>交换</span><span><i style="background:'+C.set+'"></i>写入</span><span><i style="background:'+C.pivot+'"></i>pivot</span><span><i style="background:'+C.key+'"></i>当前元素</span><span><i style="background:'+C.done+'"></i>已就位</span><span><i style="background:'+C.out+'"></i>不在当前子区间</span></div>';
  const $=s=>root.querySelector(s), cv=$("canvas"), ctx=cv.getContext("2d"), pc=$(".pc");
  let key=algos[0], a, gen, st, timer=null, acc=0, last=0;
  function reset(){
    stop();
    const vmax = key==="radix"?999:99;
    a=makeData(n,pat,seed,vmax);
    gen=A[key].gen(a);
    st={cmp:0,wr:0,steps:0,hl:{},done:new Set(),range:null,pivot:null,key:-1,L:-1,fin:false};
    pc.innerHTML=A[key].code.map(l=>"<div>"+l.replace(/&/g,"&amp;").replace(/</g,"&lt;")+"</div>").join("");
    $(".msg").textContent="点击「播放」或「单步」开始。"; render();
  }
  function apply(e){
    switch(e.t){
      case "cmp": st.cmp++; st.hl={}; st.hl[e.i]="cmp"; st.hl[e.j]="cmp"; break;
      case "swap": st.wr+=2; st.hl={}; st.hl[e.i]="swap"; st.hl[e.j]="swap"; break;
      case "set": st.wr++; st.hl={}; st.hl[e.i]="set"; break;
      case "done": e.idx.forEach(i=>st.done.add(i)); return false;
      case "all": for(let i=0;i<a.length;i++) st.done.add(i); st.hl={}; st.range=null; st.pivot=null; st.fin=true; return true;
      case "range": st.range=[e.lo,e.hi]; return false;
      case "pivot": st.pivot=e.obj; return false;
      case "key": st.key=e.i; return false;
      case "put": return false;
      case "line": st.hl={}; break;
    }
    if(e.L!==undefined) st.L=e.L;
    if(e.msg) $(".msg").textContent=e.msg;
    return true;
  }
  function step(){
    if(st.fin) return false;
    for(;;){ const r=gen.next(); if(r.done){st.fin=true;break;} if(apply(r.value)) break; }
    st.steps++; if(st.fin){ $(".msg").textContent="完成：比较 "+st.cmp+" 次，交换/写入 "+st.wr+" 次。"; stop(); }
    render(); return !st.fin;
  }
  function render(){
    const dpr=window.devicePixelRatio||1, W=cv.clientWidth||640, H=cv.clientHeight||260;
    cv.width=W*dpr; cv.height=H*dpr; ctx.setTransform(dpr,0,0,dpr,0,0); ctx.clearRect(0,0,W,H);
    let mx=1; a.forEach(x=>{if(x&&x.v>mx) mx=x.v;});
    const pad=10, bw=(W-2*pad)/a.length, top=22, bot=H-18;
    if(st.range){ ctx.fillStyle="#EEF2FB"; ctx.fillRect(pad+st.range[0]*bw, 4, (st.range[1]-st.range[0]+1)*bw, H-8); }
    for(let i=0;i<a.length;i++){
      const x=a[i]; const bx=pad+i*bw+1;
      if(!x){ ctx.strokeStyle="#CBD5E1"; ctx.setLineDash([3,3]); ctx.strokeRect(bx,bot-6,bw-2,6); ctx.setLineDash([]); continue; }
      const h=(bot-top)*x.v/mx;
      let c=C.base;
      if(st.range&&(i<st.range[0]||i>st.range[1])) c=C.out;
      if(st.done.has(i)) c=C.done;
      if(st.pivot&&x===st.pivot) c=C.pivot;
      if(i===st.key) c=C.key;
      if(st.hl[i]) c=C[st.hl[i]];
      ctx.fillStyle=c; ctx.fillRect(bx,bot-h,Math.max(1,bw-2),h);
      if(bw>=15){ ctx.fillStyle="#1F2937"; ctx.font=(bw>=22?"11px":"9px")+" Consolas,monospace"; ctx.textAlign="center"; ctx.fillText(x.v,bx+(bw-2)/2,bot-h-4); }
      if(bw>=18){ ctx.fillStyle="#9CA3AF"; ctx.font="9px Consolas,monospace"; ctx.textAlign="center"; ctx.fillText(i,bx+(bw-2)/2,H-5); }
    }
    pc.querySelectorAll("div").forEach((d,i)=>d.classList.toggle("on",i===st.L));
    const on=pc.querySelector("div.on"); if(on&&timer){ const t=on.offsetTop-pc.offsetTop; if(t<pc.scrollTop||t>pc.scrollTop+pc.clientHeight-20) pc.scrollTop=t-40; }
    $(".sc").textContent=st.cmp; $(".sw").textContent=st.wr; $(".ss").textContent=st.steps;
    $(".state").textContent=st.fin?"✔ 已完成":"";
  }
  function loop(ts){
    if(!timer) return;
    if(!last) last=ts; acc+=(ts-last)*speed/1000*3; last=ts;
    let k=0; while(acc>=1&&k<200){ acc-=1; k++; if(!step()) return; }
    timer=requestAnimationFrame(loop);
  }
  function play(){ if(st.fin) reset(); if(timer){stop();return;} $(".play").textContent="⏸ 暂停"; last=0; acc=1; timer=requestAnimationFrame(loop); }
  function stop(){ if(timer) cancelAnimationFrame(timer); timer=null; const b=$(".play"); if(b) b.textContent="▶ 播放"; }
  $(".play").onclick=play; $(".step").onclick=()=>{stop();step();}; $(".rs").onclick=reset;
  $(".sh").onclick=()=>{seed=(seed*7+13)%9973;reset();};
  $(".pt").onchange=e=>{pat=e.target.value;reset();};
  $(".nn").oninput=e=>{n=+e.target.value;$(".nv").textContent=n;reset();};
  $(".sp").oninput=e=>{speed=+e.target.value;};
  const al=$(".al"); if(al) al.onchange=e=>{key=e.target.value;reset();};
  addEventListener("resize",()=>render());
  reset();
}

/* ---------------- 统计：跑完不播动画 ---------------- */
function runCount(key,data){
  const a=data.map(x=>({v:x.v,id:x.id})); let cmp=0,wr=0;
  for(const e of A[key].gen(a)){ if(e.t==="cmp") cmp++; else if(e.t==="swap") wr+=2; else if(e.t==="set") wr++; }
  return {cmp,wr,a};
}
const RACE=["bubble","selection","insertion","shell","merge","quickL","quickH","quick3","heap"];
const RCOL={bubble:"#EF4444",selection:"#F97316",insertion:"#F59E0B",shell:"#84CC16",merge:"#10B981",quickL:"#06B6D4",quickH:"#2F5BD3",quick3:"#6D28D9",heap:"#DB2777"};
const SHORT={bubble:"冒泡",selection:"选择",insertion:"插入",shell:"希尔",merge:"归并",quickL:"快排Lomuto",quickH:"快排Hoare",quick3:"三路快排",heap:"堆"};
function RaceLab(root){
  root.innerHTML='<div class="t">交互实验 · 九种比较排序同场竞技（只计数，不播动画）</div>'+
   '<div class="row"><label>数据 <select class="pt">'+Object.entries(PAT).map(([k,v])=>'<option value="'+k+'">'+v+'</option>').join("")+'</select></label>'+
   '<label>规模 N <select class="nn"><option>200</option><option selected>1000</option><option>2000</option></select></label>'+
   '<button class="st-btn go">开跑</button><span class="xs" style="font-size:12.5px;color:#6B7280">同一份数据，统计比较次数与写入次数（交换记 2 次写）</span></div>'+
   '<canvas class="c1" style="height:330px"></canvas>'+
   '<div class="row" style="margin-top:12px"><b style="font-size:13.5px">比较次数随 N 增长（对数纵轴）</b><span style="font-size:12.5px;color:#6B7280">N = 50 … 1000，同一种数据分布</span></div>'+
   '<canvas class="c2" style="height:300px"></canvas>';
  const $=s=>root.querySelector(s);
  function setup(cv,H){const dpr=window.devicePixelRatio||1,W=cv.clientWidth||800;cv.width=W*dpr;cv.height=H*dpr;const c=cv.getContext("2d");c.setTransform(dpr,0,0,dpr,0,0);c.clearRect(0,0,W,H);return [c,W];}
  function go(){
    const pat=$(".pt").value, N=+$(".nn").value;
    const data=makeData(N,pat,11,9999);
    const res=RACE.map(k=>[k,runCount(k,data)]);
    const [c,W]=setup($(".c1"),330);
    const mx=Math.max(...res.map(r=>Math.max(r[1].cmp,r[1].wr)),1);
    const L=110,R=110,rowH=34;
    c.font="12.5px "+getComputedStyle(document.body).fontFamily;
    res.forEach(([k,r],i)=>{
      const y=12+i*rowH;
      c.fillStyle="#1F2937"; c.textAlign="right"; c.fillText(SHORT[k],L-10,y+16);
      const w1=(W-L-R)*r.cmp/mx, w2=(W-L-R)*r.wr/mx;
      c.fillStyle=RCOL[k]; c.fillRect(L,y+2,Math.max(1,w1),13);
      c.globalAlpha=.35; c.fillRect(L,y+16,Math.max(1,w2),11); c.globalAlpha=1;
      c.textAlign="left"; c.fillStyle="#374151"; c.font="11.5px Consolas,monospace";
      c.fillText("比较 "+r.cmp.toLocaleString(),L+w1+6,y+13); c.fillText("写 "+r.wr.toLocaleString(),L+w2+6,y+26);
      c.font="12.5px "+getComputedStyle(document.body).fontFamily;
    });
    // 增长曲线
    const [c2,W2]=setup($(".c2"),300); const Ns=[]; for(let x=50;x<=1000;x+=50) Ns.push(x);
    const L2=60,R2=100,T2=12,B2=30,H2=300;
    const series=RACE.map(k=>[k,Ns.map(nn=>Math.max(1,runCount(k,makeData(nn,pat,11,9999)).cmp))]);
    const ymin=1, ymax=Math.max(...series.map(s=>Math.max(...s[1])));
    const lx=x=>L2+(W2-L2-R2)*(x-50)/950, ly=v=>T2+(H2-T2-B2)*(1-Math.log10(v/ymin)/Math.log10(ymax/ymin));
    c2.strokeStyle="#E5E7EB"; c2.fillStyle="#6B7280"; c2.font="11px Consolas,monospace"; c2.textAlign="right";
    for(let p=0;Math.pow(10,p)<=ymax*1.01;p++){const y=ly(Math.pow(10,p));c2.beginPath();c2.moveTo(L2,y);c2.lineTo(W2-R2,y);c2.stroke();c2.fillText("1e"+p,L2-6,y+4);}
    c2.textAlign="center"; [50,250,500,750,1000].forEach(x=>c2.fillText(x,lx(x),H2-10));
    const ends=[];
    series.forEach(([k,ys])=>{c2.strokeStyle=RCOL[k];c2.lineWidth=2;c2.beginPath();ys.forEach((v,i)=>{const x=lx(Ns[i]),y=ly(v);i?c2.lineTo(x,y):c2.moveTo(x,y);});c2.stroke();ends.push([k,ly(ys[ys.length-1])]);});
    ends.sort((p,q)=>p[1]-q[1]); let prev=-99; c2.font="12px "+getComputedStyle(document.body).fontFamily; c2.textAlign="left";
    ends.forEach(([k,y])=>{y=Math.max(y,prev+13);prev=y;c2.fillStyle=RCOL[k];c2.fillText(SHORT[k],W2-R2+6,y+4);});
  }
  $(".go").onclick=go; $(".pt").onchange=go; $(".nn").onchange=go;
  go();   // 同步计算，保证打印 / 截图时图表已画好
}

/* ---------------- 稳定性：扑克牌 ---------------- */
function CardLab(root){
  const base=[[4,"♠"],[2,"♥"],[4,"♥"],[1,"♠"],[3,"♠"],[2,"♠"],[1,"♥"],[3,"♥"],[4,"♦"],[2,"♦"]];
  const keys=["insertion","bubble","merge","counting","selection","heap","quickH","quick3"];
  root.innerHTML='<div class="t">交互实验 · 稳定性：同点数的牌，花色顺序还在吗？</div>'+
   '<div style="font-size:13.5px;color:#4B5563">原始顺序（同点数下：先出现的在前）：</div><div class="cardrow src"></div>'+
   '<div class="row">'+keys.map(k=>'<button class="st-btn" data-k="'+k+'">'+A[k].name.replace(/（.*?）/,"")+'</button>').join("")+'</div>'+
   '<div class="cardrow dst"></div><div class="msg out" style="font-size:14px;min-height:24px"></div>';
  const $=s=>root.querySelector(s);
  const card=x=>'<div class="pcard '+(x.s==="♥"||x.s==="♦"?"r":"k")+'">'+x.v+'<small>'+x.s+'</small></div>';
  const data=base.map(([v,s],id)=>({v,s,id}));
  $(".src").innerHTML=data.map(card).join("");
  root.querySelectorAll("button[data-k]").forEach(b=>b.onclick=()=>{
    const k=b.dataset.k; const a=data.map(x=>({...x}));
    for(const _ of A[k].gen(a)){}
    $(".dst").innerHTML=a.map(card).join("");
    const bad=[]; for(let i=1;i<a.length;i++) if(a[i].v===a[i-1].v&&a[i].id<a[i-1].id) bad.push(a[i-1].v+a[i-1].s+" 跑到了 "+a[i].v+a[i].s+" 前面");
    $(".out").innerHTML="<b>"+A[k].name+"</b>："+(bad.length?'<span style="color:#B91C1C">不稳定</span>（'+bad.join("；")+"）":'<span style="color:#15803D">稳定</span>，同点数的牌保持了原来的先后顺序');
  });
  root.querySelector('button[data-k="selection"]').click();
}

document.querySelectorAll(".sortlab").forEach(SortLab);
document.querySelectorAll(".racelab").forEach(RaceLab);
document.querySelectorAll(".cardlab").forEach(CardLab);
})();
