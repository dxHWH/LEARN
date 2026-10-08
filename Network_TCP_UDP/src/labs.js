
/* ---------- 实验 1：TCP 连接管理场景回放 ---------- */
(function(){
  const $ = id => document.getElementById(id);
  if(!$("hs-svg")) return;
  const SC = [
    {name:"① 正常三次握手", c0:"CLOSED", s0:"LISTEN", steps:[
      {d:"c", t:"SYN  seq=x", c:"SYN_SENT", s:"SYN_RCVD", n:"客户端 connect() 发出 SYN；服务器收到后进入 SYN_RCVD，请求放入半连接队列，内核立即回 SYN+ACK。"},
      {d:"s", t:"SYN+ACK  seq=y  ack=x+1", c:"ESTABLISHED", n:"服务器的 SYN 与对 x 的确认合并发送。客户端收到后进入 ESTABLISHED，connect() 返回。"},
      {d:"c", t:"ACK  seq=x+1  ack=y+1", s:"ESTABLISHED", n:"服务器收到第三次握手，连接移入全连接队列，等待 accept() 取走。"}]},
    {name:"② 第一次握手 SYN 丢失", c0:"CLOSED", s0:"LISTEN", steps:[
      {d:"c", t:"SYN  seq=x", lost:1, c:"SYN_SENT", n:"SYN 在网络中丢失，服务器毫不知情，仍在 LISTEN。"},
      {d:"w", t:"⏱ 客户端等待 RTO（首次 1 s，之后 2、4、8…… s）", n:"客户端在 SYN_SENT 中等待重传定时器。Linux 默认最多重传 tcp_syn_retries=6 次，总计约 127 s 后 connect 返回 ETIMEDOUT。"},
      {d:"c", t:"SYN  seq=x（重传，序号不变）", s:"SYN_RCVD", n:"重传的 SYN 到达服务器。"},
      {d:"s", t:"SYN+ACK  seq=y  ack=x+1", c:"ESTABLISHED", n:"握手继续。"},
      {d:"c", t:"ACK  ack=y+1", s:"ESTABLISHED", n:"握手完成，代价是多等了 1 秒——这就是弱网下\"打开网页卡 1 秒\"的常见原因。"}]},
    {name:"③ 第二次握手 SYN+ACK 丢失", c0:"CLOSED", s0:"LISTEN", steps:[
      {d:"c", t:"SYN  seq=x", c:"SYN_SENT", s:"SYN_RCVD", n:"服务器正常收到 SYN。"},
      {d:"s", t:"SYN+ACK  seq=y  ack=x+1", lost:1, n:"SYN+ACK 丢失：客户端以为自己的 SYN 丢了；服务器以为自己的 SYN+ACK 丢了。"},
      {d:"w", t:"⏱ 双方的重传定时器都在走", n:"客户端在 SYN_SENT 准备重传 SYN；服务器在 SYN_RCVD 准备重传 SYN+ACK（tcp_synack_retries=5）。"},
      {d:"b", t:"SYN（客户端重传）", t2:"SYN+ACK（服务器重传）", c:"ESTABLISHED", n:"谁先到都行：客户端收到 SYN+ACK 即进入 ESTABLISHED；服务器收到重复 SYN 会再回一次 SYN+ACK，客户端对重复的 SYN+ACK 再回 ACK 即可。"},
      {d:"c", t:"ACK  ack=y+1", s:"ESTABLISHED", n:"握手完成。"}]},
    {name:"④ 第三次握手 ACK 丢失，随后客户端发数据", c0:"CLOSED", s0:"LISTEN", steps:[
      {d:"c", t:"SYN  seq=x", c:"SYN_SENT", s:"SYN_RCVD", n:"正常。"},
      {d:"s", t:"SYN+ACK  seq=y  ack=x+1", c:"ESTABLISHED", n:"客户端进入 ESTABLISHED。"},
      {d:"c", t:"ACK  ack=y+1", lost:1, n:"第三次握手丢失：客户端已认为连接建立，服务器仍在 SYN_RCVD。"},
      {d:"c", t:"数据  seq=x+1  ack=y+1  len=100", s:"ESTABLISHED", n:"数据段同样带 ACK 标志和正确的确认号，服务器据此完成握手并接收数据——第三次 ACK 丢了通常没有影响。"},
      {d:"s", t:"ACK  ack=x+101", n:"一切正常。若客户端一直不发数据，服务器会重传 SYN+ACK，客户端收到后再回 ACK。"}]},
    {name:"⑤ 连接一个没人监听的端口", c0:"CLOSED", s0:"（无监听）", steps:[
      {d:"c", t:"SYN  seq=x", c:"SYN_SENT", n:"目标端口没有 LISTEN socket。"},
      {d:"s", t:"RST+ACK  ack=x+1", c:"CLOSED", n:"内核直接回 RST，connect() 返回 ECONNREFUSED。Linux 立即失败；Windows 会再重试几次 SYN，d1 实测耗时约 2 秒。"}]},
    {name:"⑥ 正常四次挥手（含半关闭）", c0:"ESTABLISHED", s0:"ESTABLISHED", steps:[
      {d:"c", t:"FIN  seq=u", c:"FIN_WAIT_1", s:"CLOSE_WAIT", n:"客户端 close() 发出 FIN。服务器收到后进入 CLOSE_WAIT，它的 read() 将返回 0。"},
      {d:"s", t:"ACK  ack=u+1", c:"FIN_WAIT_2", n:"由服务器内核自动回复，不需要应用参与。"},
      {d:"s", t:"数据  seq=v  len=200（半关闭期间）", n:"服务器仍可以发送剩余数据，客户端照常接收并确认（d1 · E5 实测）。"},
      {d:"s", t:"FIN  seq=w  ack=u+1", s:"LAST_ACK", c:"TIME_WAIT", n:"服务器应用调用 close() 后才发出 FIN。客户端收到后进入 TIME_WAIT。"},
      {d:"c", t:"ACK  ack=w+1", s:"CLOSED", n:"服务器收到最后的 ACK，立即 CLOSED。"},
      {d:"w", t:"⏱ 客户端等待 2MSL（Linux 固定 60 s）", c:"CLOSED", n:"TIME_WAIT 结束后，这个四元组才能被复用。"}]},
    {name:"⑦ 三次挥手（ACK 与 FIN 合并）", c0:"ESTABLISHED", s0:"ESTABLISHED", steps:[
      {d:"c", t:"FIN  seq=u", c:"FIN_WAIT_1", s:"CLOSE_WAIT", n:"服务器正好没有数据要发，应用读到 EOF 后马上 close()。"},
      {d:"s", t:"FIN+ACK  seq=w  ack=u+1", s:"LAST_ACK", c:"TIME_WAIT", n:"延迟确认的 ACK 还没发出，FIN 也要发了，于是合并。客户端在 FIN_WAIT_1 收到 FIN+ACK，跳过 FIN_WAIT_2 直接进入 TIME_WAIT。"},
      {d:"c", t:"ACK  ack=w+1", s:"CLOSED", n:"只用了 3 个报文。"},
      {d:"w", t:"⏱ 2MSL", c:"CLOSED", n:"结束。"}]},
    {name:"⑧ 最后一个 ACK 丢失", c0:"ESTABLISHED", s0:"ESTABLISHED", steps:[
      {d:"c", t:"FIN  seq=u", c:"FIN_WAIT_1", s:"CLOSE_WAIT", n:"正常。"},
      {d:"s", t:"ACK  ack=u+1", c:"FIN_WAIT_2", n:"正常。"},
      {d:"s", t:"FIN  seq=w", s:"LAST_ACK", c:"TIME_WAIT", n:"正常。"},
      {d:"c", t:"ACK  ack=w+1", lost:1, n:"最后的 ACK 丢失，服务器仍在 LAST_ACK。"},
      {d:"w", t:"⏱ 服务器的重传定时器超时", n:"服务器以为自己的 FIN 丢了。"},
      {d:"s", t:"FIN  seq=w（重传）", n:"客户端还在 TIME_WAIT，能认出这是同一条连接的 FIN，于是重发 ACK 并重置 2MSL 计时器。若客户端已经 CLOSED，只能回 RST，服务器会以错误结束。"},
      {d:"c", t:"ACK  ack=w+1（重发）", s:"CLOSED", n:"服务器正常关闭。"},
      {d:"w", t:"⏱ 重新计时的 2MSL", c:"CLOSED", n:"这就是 TIME_WAIT 存在的第一个理由。"}]},
    {name:"⑨ 同时关闭", c0:"ESTABLISHED", s0:"ESTABLISHED", steps:[
      {d:"b", t:"FIN  seq=u", t2:"FIN  seq=w", c:"CLOSING", s:"CLOSING", n:"双方几乎同时 close()，都进入 FIN_WAIT_1，随后各自收到的是对方的 FIN 而不是 ACK，于是进入 CLOSING。"},
      {d:"b", t:"ACK  ack=w+1", t2:"ACK  ack=u+1", c:"TIME_WAIT", s:"TIME_WAIT", n:"收到对自己 FIN 的确认后，双方都进入 TIME_WAIT——这是唯一一种两端都有 TIME_WAIT 的情况。"},
      {d:"w", t:"⏱ 2MSL", c:"CLOSED", s:"CLOSED", n:"结束。"}]},
    {name:"⑩ SO_LINGER{1,0}：用 RST 关闭", c0:"ESTABLISHED", s0:"ESTABLISHED", steps:[
      {d:"c", t:"RST  seq=u", c:"CLOSED", s:"CLOSED", n:"不经过挥手：双方立即释放连接，未发送的数据被丢弃，服务器下一次 recv 得到 ECONNRESET，客户端也不进入 TIME_WAIT（与 d1 · E6 实测一致）。"}]}
  ];
  const NS = "http://www.w3.org/2000/svg";
  const CX = 170, SX = 690;
  const col = s => /ESTAB/.test(s) ? "#B45309" : /TIME_WAIT/.test(s) ? "#B91C1C" : /CLOSED|无监听/.test(s) ? "#4B5563" : /CLOSING/.test(s) ? "#6D28D9" : "#2F5BD3";
  function el(tag, a, txt){ const e = document.createElementNS(NS, tag); for(const k in a) e.setAttribute(k, a[k]); if(txt!==undefined) e.textContent = txt; return e; }
  function label(svg, x, y, txt, color){
    const w = Math.max(40, [...txt].reduce((s,ch)=>s+(ch.charCodeAt(0)>255?12.5:7), 0) + 12);
    svg.appendChild(el("rect", {x:x-w/2, y:y-13, width:w, height:18, rx:4, fill:"#fff", stroke:color, "stroke-width":0.8, opacity:0.95}));
    svg.appendChild(el("text", {x:x, y:y, "text-anchor":"middle", "font-size":12, "font-weight":700, fill:color, "font-family":"Consolas, 'Microsoft YaHei', sans-serif"}, txt));
  }
  function arrow(svg, fromC, y, txt, lost, lx){
    const x1 = fromC ? CX : SX, x2 = fromC ? SX : CX, color = lost ? "#B91C1C" : (fromC ? "#15803D" : "#2F5BD3");
    if(lost){
      const mx = x1 + (x2 - x1) * 0.55, my = y + 30 * 0.55;
      svg.appendChild(el("line", {x1:x1, y1:y, x2:mx, y2:my, stroke:color, "stroke-width":1.8, "stroke-dasharray":"6 4"}));
      svg.appendChild(el("text", {x:mx, y:my+6, "text-anchor":"middle", "font-size":18, "font-weight":700, fill:color}, "✕"));
    } else {
      svg.appendChild(el("line", {x1:x1, y1:y, x2:x2 + (fromC ? -2 : 2), y2:y+30, stroke:color, "stroke-width":1.8, "marker-end": fromC ? "url(#ag)" : "url(#ab)"}));
    }
    label(svg, lx !== undefined ? lx : (CX+SX)/2, y + 12, txt, color);
  }
  let cur = 0, k = 0;
  function render(){
    const sc = SC[cur], svg = $("hs-svg"), n = sc.steps.length;
    const H = 80 + n * 46;
    svg.setAttribute("viewBox", "0 0 860 " + H);
    while(svg.firstChild) svg.removeChild(svg.firstChild);
    svg.appendChild(el("text", {x:CX, y:24, "text-anchor":"middle", "font-size":14, "font-weight":700, fill:"#1F2937"}, "客户端"));
    svg.appendChild(el("text", {x:SX, y:24, "text-anchor":"middle", "font-size":14, "font-weight":700, fill:"#1F2937"}, "服务器"));
    svg.appendChild(el("line", {x1:CX, y1:34, x2:CX, y2:H-8, stroke:"#9CA3AF"}));
    svg.appendChild(el("line", {x1:SX, y1:34, x2:SX, y2:H-8, stroke:"#9CA3AF"}));
    const st = (x, y, s, left) => svg.appendChild(el("text", {x:x, y:y, "text-anchor": left ? "end" : "start", "font-size":12.5, "font-weight":700, fill:col(s), "font-family":"Consolas, monospace"}, s));
    st(CX-10, 52, sc.c0, true); st(SX+10, 52, sc.s0, false);
    const log = [];
    for(let i = 0; i < k; i++){
      const s = sc.steps[i], y = 66 + i * 46;
      if(s.d === "c") arrow(svg, true, y, s.t, s.lost);
      else if(s.d === "s") arrow(svg, false, y, s.t, s.lost);
      else if(s.d === "b"){ arrow(svg, true, y, s.t, false, CX + 150); arrow(svg, false, y, s.t2, false, SX - 150); }
      else {
        svg.appendChild(el("rect", {x:CX+20, y:y+4, width:SX-CX-40, height:24, rx:6, fill:"#FEF6E4", stroke:"#F3DDA8"}));
        svg.appendChild(el("text", {x:(CX+SX)/2, y:y+21, "text-anchor":"middle", "font-size":12.5, fill:"#B45309"}, s.t));
      }
      if(s.c) st(CX-10, y+34, s.c, true);
      if(s.s) st(SX+10, y+34, s.s, false);
      log.push((i === k-1 ? "▶ " : "  ") + "第 " + (i+1) + " 步：" + (s.n || ""));
    }
    $("hs-log").textContent = log.length ? log.join("\n") : "共 " + n + " 步，点击\"下一步\"开始。";
    $("hs-prev").disabled = k <= 0; $("hs-next").disabled = k >= n;
  }
  const sel = $("hs-sc");
  SC.forEach((s, i) => { const o = document.createElement("option"); o.value = i; o.textContent = s.name; sel.appendChild(o); });
  sel.onchange = () => { cur = +sel.value; k = 0; render(); };
  $("hs-next").onclick = () => { if(k < SC[cur].steps.length) k++; render(); };
  $("hs-prev").onclick = () => { if(k > 0) k--; render(); };
  $("hs-all").onclick = () => { k = SC[cur].steps.length; render(); };
  $("hs-reset").onclick = () => { k = 0; render(); };
  k = SC[0].steps.length; render();
})();

/* ---------- 实验 2：ARQ 协议仿真器（与 demos/d2_rudp_sim.cpp 同构的简化版） ---------- */
(function(){
  const $ = id => document.getElementById(id);
  if(!$("arq-run")) return;
  function rng(seed){ return function(){ seed |= 0; seed = seed + 0x6D2B79F5 | 0; let t = Math.imul(seed ^ seed >>> 15, 1 | seed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }; }
  const MODES = {
    sw:  {name:"停等 ARQ", w:1, conn:true, gbn:true, fast:0, minRto:200, back:2},
    gbn: {name:"回退 N 步 GBN", conn:true, gbn:true, fast:0, minRto:200, back:2},
    sr:  {name:"选择重传 SR", conn:false, gbn:false, fast:0, minRto:200, back:2},
    srf: {name:"SR + 快速重传(3)", conn:false, gbn:false, fast:3, minRto:200, back:2},
    kcp: {name:"类 KCP 极速模式", conn:false, gbn:false, fast:2, minRto:30, back:1.5, kcpFast:true}
  };
  function sim(key, N, loss, wnd, rtt, seed){
    const m = MODES[key], W = m.w || wnd, R = rng(seed), half = rtt / 2, fifo = [0, 0];
    const arrive = (d, now) => { let t = now + half + 0.5 * R(); if(R() < 0.01) return t + 10 * R(); t = Math.max(t, fifo[d] + 0.001); fifo[d] = t; return t; };
    const segs = new Map(), xm = new Array(N).fill(0);
    let next = 0, una = 0, srtt = 0, rttvar = 0, rto = 200, has = false, connRto = 200, connTimer = -1, sends = 0;
    let rcvNxt = 0; const rbuf = new Set(); let toR = [], toS = [];
    const sendData = (sn, now) => { sends++; xm[sn]++; if(R() >= loss) toR.push({t:arrive(0, now), sn}); };
    const sendAck = (now, sns) => { if(R() >= loss) toS.push({t:arrive(1, now), una:rcvNxt, sns}); };
    const sample = r => { if(!has){ srtt = r; rttvar = r/2; has = true; } else { rttvar = .75*rttvar + .25*Math.abs(srtt - r); srtt = .875*srtt + .125*r; } rto = Math.min(60000, Math.max(m.minRto, srtt + Math.max(1, 4*rttvar))); };
    let budget = 0;
    const xmit = (s, now) => { s.x++; s.last = now; s.fast = 0; s.need = false; s.at = now + s.rto; sendData(s.sn, now); budget--; };
    const take = (q, now) => { const r = [], k = []; for(const p of q) (p.t <= now ? r : k).push(p); r.sort((a,b)=>a.t-b.t); return [r, k]; };
    let now = 0;
    for(; now < 300000; now++){
      let r; [r, toR] = take(toR, now);
      for(const p of r){
        if(m.gbn){ if(p.sn === rcvNxt) rcvNxt++; sendAck(now, []); continue; }
        if(p.sn >= rcvNxt) rbuf.add(p.sn);
        while(rbuf.has(rcvNxt)){ rbuf.delete(rcvNxt); rcvNxt++; }
        sendAck(now, [p.sn]);
      }
      if(rcvNxt >= N) break;
      [r, toS] = take(toS, now);
      for(const a of r){
        const old = una; let maxAck = -1, maxSent = -1;
        for(const [sn, s] of segs) if(sn < a.una && !s.acked){ s.acked = true; if(sn === a.una - 1 && s.x === 1) sample(now - s.last); }
        for(const sn of a.sns){ const s = segs.get(sn); if(s){ if(!s.acked){ maxSent = Math.max(maxSent, s.last); s.acked = true; if(s.x === 1) sample(now - s.last); } maxAck = Math.max(maxAck, sn); } }
        if(m.fast) for(const [sn, s] of segs) if(!s.acked && sn < maxAck && (m.kcpFast || maxSent > s.last)) s.fast++;
        let lo = Infinity; for(const [sn, s] of segs){ if(s.acked) segs.delete(sn); else lo = Math.min(lo, sn); }
        una = lo === Infinity ? next : lo;
        if(m.conn && una > old){ connRto = rto; connTimer = segs.size ? now + connRto : -1; }
      }
      budget = 1;
      if(m.fast) for(const [, s] of segs) if(!s.need && s.fast >= m.fast && (!m.kcpFast || s.x <= 5)) s.need = true;
      if(m.conn){
        if(connTimer >= 0 && now >= connTimer){ for(const [, s] of segs) s.need = true; connRto = Math.min(connRto * m.back, 60000); connTimer = now + connRto; }
      } else {
        for(const [, s] of segs) if(!s.need && now >= s.at){ s.rto = Math.min(s.rto * m.back, 60000); s.need = true; }
      }
      const keys = [...segs.keys()].sort((a,b)=>a-b);
      for(const sn of keys){ if(budget <= 0) break; const s = segs.get(sn); if(s.need) xmit(s, now); }
      while(budget > 0 && next < N && next < una + W){
        const s = {sn:next, x:0, last:0, rto:rto, at:0, fast:0, acked:false, need:false};
        segs.set(next, s); xmit(s, now);
        if(m.conn && connTimer < 0){ connRto = rto; connTimer = now + connRto; }
        next++;
      }
    }
    return {done:now, sends, xm};
  }
  function grid(xm){
    const box = $("arq-grid"); box.innerHTML = "";
    xm.forEach((x, i) => {
      const d = document.createElement("span");
      const c = x <= 1 ? "#BBF7D0" : x === 2 ? "#FDE68A" : x === 3 ? "#FDBA74" : "#FCA5A5";
      d.style.cssText = "display:inline-block;width:22px;height:18px;margin:1px;border-radius:3px;font:10px Consolas,monospace;line-height:18px;text-align:center;color:#374151;background:" + c;
      d.title = "包 " + i + "：发送 " + x + " 次"; d.textContent = x > 1 ? x : "";
      box.appendChild(d);
    });
  }
  function run(){
    const key = $("arq-mode").value, loss = +$("arq-loss").value / 100, wnd = +$("arq-wnd").value, rtt = +$("arq-rtt").value, N = 200, seed = +$("arq-seed").value;
    $("arq-loss-v").textContent = $("arq-loss").value + "%"; $("arq-wnd-v").textContent = wnd; $("arq-rtt-v").textContent = rtt + " ms";
    const r = sim(key, N, loss, wnd, rtt, seed);
    grid(r.xm);
    const ideal = Math.max(N, rtt) + rtt;
    let s = MODES[key].name + "：传输 " + N + " 个包，耗时 " + r.done + " ms（理想值约 " + ideal + " ms，慢 " + (r.done / ideal).toFixed(1) + " 倍）\n";
    s += "发送 " + r.sends + " 次，重传 " + (r.sends - N) + " 次，重传占比 " + (100 * (r.sends - N) / r.sends).toFixed(1) + "%\n\n同参数下各协议对比：\n";
    for(const k in MODES){ const q = sim(k, N, loss, wnd, rtt, seed); s += (k === key ? "▶ " : "  ") + MODES[k].name.padEnd(14, "　") + "耗时 " + String(q.done).padStart(6) + " ms　发送 " + String(q.sends).padStart(4) + " 次　重传占比 " + (100 * (q.sends - N) / q.sends).toFixed(1).padStart(5) + "%\n"; }
    $("arq-out").textContent = s;
  }
  ["arq-mode", "arq-loss", "arq-wnd", "arq-rtt", "arq-seed"].forEach(id => $(id).addEventListener("input", run));
  $("arq-run").onclick = () => { $("arq-seed").value = (+$("arq-seed").value % 99) + 1; run(); };
  run();
})();
