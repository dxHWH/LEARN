// 构建时把 LaTeX 批量渲染成 KaTeX HTML（由 build.py 调用）
// 用法：node katex_render.js in.json out.json
//   in.json  = {"macros": {...}, "items": [["tex", true|false], ...]}   true = display 模式
//   out.json = [{"ok": true, "html": "..."} | {"ok": false, "err": "..."}]
const katex = require("katex");
const fs = require("fs");
const [, , inp, out] = process.argv;
const req = JSON.parse(fs.readFileSync(inp, "utf8"));
const res = req.items.map(([tex, display]) => {
  try {
    return {
      ok: true,
      html: katex.renderToString(tex, {
        displayMode: display,
        throwOnError: true,
        output: "html",
        strict: "ignore",
        trust: false,
        macros: Object.assign({}, req.macros),
      }),
    };
  } catch (e) {
    return { ok: false, err: String(e.message || e) };
  }
});
fs.writeFileSync(out, JSON.stringify(res));
