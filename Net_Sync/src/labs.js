/* ---------- 交互实验（同步绘制，截图 / PDF 不会空白） ---------- */
(function(){
  "use strict";
  const $ = (el, s) => el.querySelector(s);
  const fmtP = v => v <= 0 ? "0" : (v >= 0.001 ? (v * 100).toFixed(v >= 0.1 ? 1 : 2) + "%" : v.toExponential(2));

  /* ===== 实验 6-1：冗余计算器 —— 公式与图 6-2 的推导框完全一致 ===== */
  function labRedund(el){
    el.innerHTML =
      '<div class="t">交互实验 · 一包带几帧够用？</div>' +
      '<div class="row"><label>平均丢包率 p <input type="range" min="0.5" max="20" step="0.5" value="5" data-k="p"> <b data-v="p"></b></label>' +
      '<label>平均连丢长度 L（1 = 随机丢包）<input type="range" min="1" max="12" step="1" value="4" data-k="L"> <b data-v="L"></b></label></div>' +
      '<div class="row"><label>每包携带帧数 k <input type="range" min="1" max="10" step="1" value="3" data-k="k"> <b data-v="k"></b></label>' +
      '<label>逻辑帧率 <select data-k="hz"><option value="15" selected>15 帧/秒（HOK）</option><option value="30">30 帧/秒</option><option value="60">60 帧/秒</option></select></label></div>' +
      '<svg class="labsvg" viewBox="0 0 820 230" xmlns="http://www.w3.org/2000/svg"></svg>' +
      '<div class="out"></div>';
    const svg = $(el, "svg"), out = $(el, ".out");
    function val(k){ return parseFloat($(el, '[data-k="' + k + '"]').value); }
    function draw(){
      const p = val("p") / 100, L = val("L"), K = val("k"), hz = val("hz");
      $(el, '[data-v="p"]').textContent = (p * 100).toFixed(1) + "%";
      $(el, '[data-v="L"]').textContent = L;
      $(el, '[data-v="k"]').textContent = K;
      const rnd = k => Math.pow(p, k), bst = k => p * Math.pow(1 - 1 / L, k - 1);
      const X0 = 70, X1 = 800, Y0 = 20, Y1 = 190, lo = -8, hi = 0;
      const X = k => X0 + (X1 - X0) * (k - 1) / 9;
      const Y = v => Y0 + (Y1 - Y0) * (hi - Math.log10(Math.max(v, 1e-8))) / (hi - lo);
      let s = '<rect x="' + X0 + '" y="' + Y0 + '" width="' + (X1 - X0) + '" height="' + (Y1 - Y0) + '" fill="#fff" stroke="#CBD5E1"/>';
      for (let e = lo; e <= hi; e += 2) {
        s += '<line x1="' + X0 + '" y1="' + Y(Math.pow(10, e)) + '" x2="' + X1 + '" y2="' + Y(Math.pow(10, e)) + '" stroke="#EEF2F6"/>';
        s += '<text x="' + (X0 - 6) + '" y="' + (Y(Math.pow(10, e)) + 4) + '" text-anchor="end" font-size="11" fill="#6B7280">1e' + e + '</text>';
      }
      for (let k = 1; k <= 10; k++) s += '<text x="' + X(k) + '" y="' + (Y1 + 16) + '" text-anchor="middle" font-size="11" fill="#6B7280">' + k + '</text>';
      s += '<text x="' + ((X0 + X1) / 2) + '" y="226" text-anchor="middle" font-size="12" fill="#4B5563">每包携带帧数 k</text>';
      const line = (f, c, dash) => {
        let pts = [];
        for (let k = 1; k <= 10; k++) pts.push(X(k).toFixed(1) + "," + Y(f(k)).toFixed(1));
        return '<polyline points="' + pts.join(" ") + '" fill="none" stroke="' + c + '" stroke-width="2.2"' + (dash ? ' stroke-dasharray="6 4"' : "") + '/>';
      };
      s += line(rnd, "#2F5BD3", true) + line(bst, "#B91C1C", false);
      s += '<line x1="' + X(K) + '" y1="' + Y0 + '" x2="' + X(K) + '" y2="' + Y1 + '" stroke="#B45309" stroke-dasharray="3 3"/>';
      s += '<circle cx="' + X(K) + '" cy="' + Y(rnd(K)) + '" r="4" fill="#2F5BD3"/><circle cx="' + X(K) + '" cy="' + Y(bst(K)) + '" r="4" fill="#B91C1C"/>';
      s += '<rect x="560" y="100" width="232" height="44" rx="6" fill="#fff" stroke="#E5E7EB"/>';
      s += '<line x1="570" y1="114" x2="596" y2="114" stroke="#2F5BD3" stroke-width="2.2" stroke-dasharray="6 4"/><text x="602" y="118" font-size="11" fill="#374151">若为随机丢包：p^k</text>';
      s += '<line x1="570" y1="132" x2="596" y2="132" stroke="#B91C1C" stroke-width="2.2"/><text x="602" y="136" font-size="11" fill="#374151">突发丢包：p(1−1/L)^(k−1)</text>';
      svg.innerHTML = s;
      const perMin = hz * 60, b = 36 + 24 * K;
      out.textContent =
        "k = " + K + " 时：\n" +
        "  随机丢包  仍需补发比例 " + fmtP(rnd(K)) + "，每分钟约 " + (rnd(K) * perMin).toFixed(2) + " 帧\n" +
        "  突发丢包  仍需补发比例 " + fmtP(bst(K)) + "，每分钟约 " + (bst(K) * perMin).toFixed(2) + " 帧" + (L <= 1 ? "（L=1 时两者相同）" : "") + "\n" +
        "  多带 1 帧：随机丢包失败率 ×" + p.toFixed(3) + "，突发丢包失败率 ×" + (1 - 1 / L).toFixed(3) + "\n" +
        "  每个下行包约 " + b + " B，" + hz + " 帧/秒 → " + (b * hz / 1024).toFixed(2) + " KB/s（每多 1 帧 +" + (24 * hz / 1024).toFixed(2) + " KB/s）";
    }
    el.querySelectorAll("input,select").forEach(i => i.addEventListener("input", draw));
    draw();
  }

  document.querySelectorAll(".lab.lred").forEach(labRedund);
})();
