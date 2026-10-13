"use strict";
/* ---- TimerSystem 的 JS 复刻（与 TimerSystem.cs 的 CreateTimer / StopTimer / Tick 逐行一致；float 用 Math.fround 模拟） ---- */
const F = Math.fround;
class TimerSys {
  constructor(gen0) { this.gen0 = gen0 || 0; this.t = []; this.count = 0; this.h2i = []; this.i2h = []; this.gen = []; this.free = []; }
  valid(h) { return h.i >= 0 && h.i < this.gen.length && this.gen[h.i] === h.g; }
  create(o) {
    let hi;
    if (this.free.length) hi = this.free.shift();
    else { hi = this.h2i.length; this.h2i.push(-1); this.i2h.push(-1); this.gen.push(this.gen0); }
    const h = { i: hi, g: this.gen[hi] };
    const real = this.count;
    this.t[real] = Object.assign({ duration: 0, el: 0, interval: -1, iel: 0, loop: false, chase: true, onT: null, onC: null, name: "" }, o);
    this.count++;
    this.h2i[hi] = real;
    if (real >= this.i2h.length) this.i2h.push(hi); else this.i2h[real] = hi;
    return h;
  }
  stop(h) {
    if (!this.valid(h)) return false;
    const hi = h.i, real = this.h2i[hi], last = this.count - 1;
    if (real !== last) { this.t[real] = this.t[last]; const lh = this.i2h[last]; this.i2h[real] = lh; this.h2i[lh] = real; }
    this.count--; this.gen[hi]++; this.free.push(hi);
    return true;
  }
  tick(dt) {
    const exp = [], iv = [], MAX = 5;
    for (let i = 0; i < this.count; i++) {
      const t = this.t[i];
      t.el = F(t.el + dt);
      if (t.interval > 0) {
        t.iel = F(t.iel + dt);
        const hi = this.i2h[i], g = this.gen[hi];
        if (t.chase) { let c = 0; while (t.iel >= t.interval && c < MAX) { iv.push({ i: hi, g }); t.iel = F(t.iel - t.interval); c++; } }
        else if (t.iel >= t.interval) { iv.push({ i: hi, g }); t.iel = F(t.iel - t.interval); }
        if (t.iel >= t.interval) t.iel = 0;
      }
      if (t.el >= t.duration) { const hi = this.i2h[i]; exp.push({ i: hi, g: this.gen[hi] }); if (t.loop) t.el = F(t.el - t.duration); }
    }
    for (const h of iv) { if (!this.valid(h)) continue; const t = this.t[this.h2i[h.i]]; t.onT && t.onT(); }
    for (const h of exp) { if (!this.valid(h)) continue; const t = this.t[this.h2i[h.i]]; t.onC && t.onC(); if (!t.loop && this.valid(h)) this.stop(h); }
  }
}

/* ---- 实验 1：Slot Map 状态机 ---- */
function initSlotLab(el) {
  el.innerHTML = '<div class="t">交互实验 · Slot Map：新建 / 停止 / 旧句柄 / 默认句柄</div>' +
    '<div class="btns"><button class="st-btn" data-a="c">新建计时器</button>' +
    '<select data-k="sel"></select><button class="st-btn" data-a="s">用选中的句柄 StopTimer</button>' +
    '<button class="st-btn" data-a="d">StopTimer(default = (0,0))</button>' +
    '<button class="st-btn" data-a="r">重置</button>' +
    '<label><input type="checkbox" data-k="fix"> generation 从 1 开始（修复版）</label></div>' +
    '<table class="grid" data-k="grid"></table><div class="chips" data-k="chips"></div><div class="log" data-k="log"></div>';
  const q = k => el.querySelector('[data-k="' + k + '"]');
  let ts, issued, logs, n, hot;
  function reset() {
    ts = new TimerSys(q("fix").checked ? 1 : 0); issued = []; logs = []; n = 0; hot = {};
    for (let k = 0; k < 4; k++) { const name = String.fromCharCode(65 + n++); issued.push({ name, h: ts.create({ name, duration: 10 }) }); }
    logs.push("已预先创建 A、B、C、D。default(0,0) 当前" + (ts.valid({ i: 0, g: 0 }) ? "有效 ⚠（会命中 A）" : "无效") + "。试试：选中 B 停止 → 新建 → 再用 B 的旧句柄停止 → StopTimer(default)");
    draw();
  }
  function snap() { return JSON.stringify([ts.t.map(x => x.name), ts.count, ts.h2i, ts.i2h, ts.gen, ts.free]); }
  function diff(before) {
    const b = JSON.parse(before); hot = {};
    const now = [ts.t.map(x => x.name), ts.count, ts.h2i, ts.i2h, ts.gen];
    ["dense", "", "h2i", "i2h", "gen"].forEach((k, r) => { if (!k) return; now[r].forEach((v, i) => { if (b[r][i] !== v) hot[k + i] = 1; }); });
  }
  function draw() {
    const slots = ts.gen.length, w = Math.max(slots, ts.t.length, 1);
    const row = (lab, key, arr, dead) => '<tr><th>' + lab + '</th>' + Array.from({ length: w }, (_, i) => {
      const v = i < arr.length ? arr[i] : ""; const d = dead(i);
      return '<td class="' + (d ? "dead" : (hot[key + i] ? "hot" : "")) + '">' + v + '</td>'; }).join("") + '</tr>';
    q("grid").innerHTML = '<tr><th>下标</th>' + Array.from({ length: w }, (_, i) => '<th style="text-align:center">' + i + '</th>').join("") + '</tr>' +
      row("dense _timers", "dense", ts.t.map(x => x.name), i => i >= ts.count) +
      row("_indexToHandle", "i2h", ts.i2h, i => i >= ts.count) +
      row("_handleToIndex", "h2i", ts.h2i, i => ts.free.includes(i)) +
      row("_generations", "gen", ts.gen, () => false) +
      '<tr><th>_freeIndices</th><td colspan="' + w + '" style="text-align:left">' + (ts.free.join(" ← ") || "（空）") + '</td></tr>';
    q("chips").innerHTML = "已发出的句柄：" + (issued.map(x => {
      const ok = ts.valid(x.h); const where = ok ? "→ dense[" + ts.h2i[x.h.i] + "]=" + ts.t[ts.h2i[x.h.i]].name : "失效";
      const alias = ok && ts.t[ts.h2i[x.h.i]].name !== x.name;
      return '<span class="' + (ok ? (alias ? "bad" : "ok") : "") + '">' + x.name + "(" + x.h.i + "," + x.h.g + ") " + where + (alias ? " ⚠ 别名" : "") + "</span>"; }).join("") || "（无）");
    const sel = q("sel"); const cur = sel.value; sel.innerHTML = issued.map((x, k) => '<option value="' + k + '">' + x.name + " (" + x.h.i + "," + x.h.g + ")</option>").join("");
    if (cur && cur < issued.length) sel.value = cur;
    const dv = ts.valid({ i: 0, g: 0 });
    logs.length && (q("log").textContent = logs.slice(-12).join("\n"));
    if (!logs.length) q("log").textContent = "default(TimerHandle) = (0,0) 当前" + (dv ? "有效 ⚠" : "无效") + "。先新建几个计时器，再停止中间的一个，观察 swap-and-pop 与 free list。";
  }
  el.addEventListener("click", e => {
    const a = e.target.dataset && e.target.dataset.a; if (!a) return;
    if (a === "r") return reset();
    const before = snap();
    if (a === "c") {
      const name = String.fromCharCode(65 + (n++ % 26));
      const h = ts.create({ name, duration: 10 });
      issued.push({ name, h }); logs.push("Create " + name + " → (" + h.i + "," + h.g + ")，dense[" + ts.h2i[h.i] + "]");
    } else if (a === "s" || a === "d") {
      const x = a === "d" ? { name: "default", h: { i: 0, g: 0 } } : issued[+q("sel").value];
      if (!x) return;
      const victim = ts.valid(x.h) ? ts.t[ts.h2i[x.h.i]].name : null;
      const ok = ts.stop(x.h);
      logs.push("StopTimer(" + x.name + " (" + x.h.i + "," + x.h.g + ")) → " + (ok ? "删除了 " + victim + (victim !== x.name ? "  ⚠ 不是它自己的计时器！" : "") : "IsHandleValid=false，忽略"));
    }
    diff(before); draw();
  });
  q("fix").addEventListener("change", reset);
  reset();
}

/* ---- 实验 2：掉帧时 interval / loop 触发几次 ---- */
function initChaseLab(el) {
  el.innerHTML = '<div class="t">交互实验 · 卡顿一帧之后，interval 与 loop 计时器各触发几次</div>' +
    '<div class="row"><label>interval / loop 周期 <input type="number" data-k="iv" value="0.1" step="0.01" min="0.01"> s</label>' +
    '<label>卡顿帧真实时长 <input type="number" data-k="hitch" value="0.6" step="0.05" min="0.02"> s</label>' +
    '<label>模式 <select data-k="mode"><option value="1">isChase = true（默认）</option><option value="0">isChase = false</option></select></label>' +
    '<label><input type="checkbox" data-k="cap" checked> scaled 计时器（Unity 把 deltaTime 截断到 maximumDeltaTime = 0.3333333）</label></div>' +
    '<div class="out" data-k="out"></div>';
  const q = k => el.querySelector('[data-k="' + k + '"]');
  function run() {
    const iv = F(+q("iv").value || 0.1), hitch = +q("hitch").value || 0.6, chase = q("mode").value === "1", cap = q("cap").checked;
    const dts = [hitch].concat(Array(9).fill(1 / 60)).map(x => F(cap ? Math.min(x, 0.3333333) : x));
    const ts = new TimerSys(0); let a = 0, b = 0;
    ts.create({ duration: 1e6, interval: iv, chase, onT: () => a++ });
    ts.create({ duration: iv, loop: true, onC: () => b++ });
    let lines = ["帧   dt(s)     interval 回调   loop 回调   理想(累计)"], sumA = 0, sumB = 0, T = 0;
    dts.forEach((dt, k) => {
      const a0 = a, b0 = b; ts.tick(dt); T += dt; sumA += a - a0; sumB += b - b0;
      lines.push(String(k).padEnd(5) + dt.toFixed(4).padEnd(10) + String(a - a0).padStart(8) + String(b - b0).padStart(12) + String(Math.floor(T / iv + 1e-9)).padStart(14));
    });
    lines.push("");
    lines.push("10 帧累计：interval " + sumA + " 次，loop " + sumB + " 次，理想 " + Math.floor(T / iv + 1e-9) + " 次（总时长 " + T.toFixed(4) + " s）");
    lines.push(chase ? "isChase=true：一帧最多补 5 次（MAX_CHASE_COUNT），补完仍超出则把余量清零 → 丢相位" : "isChase=false：一帧最多 1 次，超出部分清零 → 丢次数");
    lines.push("loop：一帧最多 1 次，但不清零 → 欠账分摊到后面的帧（语义与 interval 不一致）");
    q("out").textContent = lines.join("\n");
  }
  el.addEventListener("input", run); el.addEventListener("change", run);
  run();
}

document.querySelectorAll(".lab.slotlab").forEach(initSlotLab);
document.querySelectorAll(".lab.chaselab").forEach(initChaseLab);
/* ---- 绘制结束 ---- */
