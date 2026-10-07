"""
Read-only live dashboard for the C++ engine (Python standard library only).

The engine writes its state to run/state.json once a second (atomic rename);
this server just serves that file plus a small HTML page that polls it.

    ./build/groww_engine demo          # terminal 1
    python3 python/dashboard.py        # terminal 2 -> http://localhost:8050
"""
import argparse
import http.server
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

PAGE = """<!doctype html><html><head><meta charset="utf-8"><title>Groww Algo Engine</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
:root{--bg:#0f1218;--panel:#171b23;--text:#e6e8ee;--muted:#8a93a6;--line:#262c38;--pos:#22c55e;--neg:#ef4444;--acc:#4f8cff}
@media (prefers-color-scheme: light){:root{--bg:#f6f7fb;--panel:#fff;--text:#141821;--muted:#5d6678;--line:#e3e6ee;--pos:#15803d;--neg:#dc2626;--acc:#2563eb}}
body{margin:0;background:var(--bg);color:var(--text);font:14px/1.45 -apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:1150px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:0}.muted{color:var(--muted)}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:10px;margin:14px 0}
.card,.panel{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:12px}
.card b{display:block;font-size:20px;margin-top:2px}.pos{color:var(--pos)}.neg{color:var(--neg)}
.panel{margin-bottom:12px;overflow-x:auto}h2{font-size:14px;margin:0 0 8px}
table{width:100%;border-collapse:collapse;font-size:13px}th,td{text-align:left;padding:5px 8px;border-bottom:1px solid var(--line);white-space:nowrap}
th{color:var(--muted);font-weight:500}canvas{width:100%;height:160px}.ks{color:var(--neg);font-weight:600}
</style></head><body><main>
<h1>Groww Algo Engine <span class="muted" id="hdr"></span></h1>
<div class="cards" id="cards"></div>
<div class="panel"><h2>Equity (P&amp;L)</h2><canvas id="eq"></canvas></div>
<div class="panel"><h2>Strategies</h2><div id="strategies"></div></div>
<div class="panel"><h2>Positions</h2><div id="positions"></div></div>
<div class="panel"><h2>Orders</h2><div id="orders"></div></div>
<div class="panel"><h2>Closed trades</h2><div id="trades"></div></div>
</main><script>
const $ = id => document.getElementById(id);
const inr = v => (v < 0 ? "-₹" : "₹") + Math.abs(v || 0).toLocaleString("en-IN", {maximumFractionDigits: 2});
const cls = v => v > 0 ? "pos" : v < 0 ? "neg" : "";
const esc = s => String(s ?? "").replace(/[&<>"]/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]));
function table(cols, rows) {
  if (!rows.length) return '<span class="muted">none yet</span>';
  return "<table><tr>" + cols.map(c => `<th>${c[0]}</th>`).join("") + "</tr>" +
    rows.map(r => "<tr>" + cols.map(c => `<td class="${c[2] ? cls(r[c[1]]) : ""}">${c[2] ? inr(r[c[1]]) : esc(r[c[1]])}</td>`).join("") + "</tr>").join("") + "</table>";
}
function chart(vals) {
  const cv = $("eq"), ctx = cv.getContext("2d"), W = cv.clientWidth, H = cv.clientHeight;
  cv.width = W; cv.height = H; ctx.clearRect(0, 0, W, H);
  if (vals.length < 2) return;
  const lo = Math.min(0, ...vals), hi = Math.max(0, ...vals), span = (hi - lo) || 1;
  const X = i => i * W / (vals.length - 1), Y = v => H - 4 - (v - lo) * (H - 8) / span;
  ctx.strokeStyle = getComputedStyle(document.body).getPropertyValue("--line"); ctx.beginPath(); ctx.moveTo(0, Y(0)); ctx.lineTo(W, Y(0)); ctx.stroke();
  ctx.strokeStyle = vals[vals.length - 1] >= 0 ? "#22c55e" : "#ef4444"; ctx.lineWidth = 2; ctx.beginPath();
  vals.forEach((v, i) => i ? ctx.lineTo(X(i), Y(v)) : ctx.moveTo(X(i), Y(v))); ctx.stroke();
}
async function refresh() {
  let S;
  try { S = await (await fetch("/state.json", {cache: "no-store"})).json(); }
  catch (e) { $("hdr").textContent = "· waiting for engine…"; return; }
  $("hdr").innerHTML = `· ${esc(S.mode)} · ${esc(S.clock)} · market ${esc(S.market)}` + (S.kill_switch ? ` · <span class="ks">KILL SWITCH: ${esc(S.kill_reason)}</span>` : "");
  const p = S.pnl, card = (l, v, c = "") => `<div class="card"><span class="muted">${l}</span><b class="${c}">${v}</b></div>`;
  $("cards").innerHTML = card("Total P&L", inr(p.total), cls(p.total)) + card("Day P&L", inr(S.day_pnl), cls(S.day_pnl)) +
    card("Realised", inr(p.realized), cls(p.realized)) + card("Unrealised", inr(p.unrealized), cls(p.unrealized)) +
    card("Charges", inr(p.charges)) + card("Ticks", S.ticks.toLocaleString("en-IN"));
  chart(S.equity);
  $("strategies").innerHTML = table([["Id","id"],["Status","status"],["P&L","pnl_total",1],["Last signal","last_signal"],["State","state_txt"],["Error","error"]],
    S.strategies.map(s => ({...s, pnl_total: s.pnl.total, state_txt: Object.entries(s.state).map(([k, v]) => `${k}=${v}`).join("  ")})));
  $("positions").innerHTML = table([["Strategy","strategy_id"],["Symbol","symbol"],["Qty","qty"],["Avg","avg_price"],["LTP","last_price"],["Unrealised","unrealized",1],["Realised","realized",1],["Net","total",1]], S.positions);
  $("orders").innerHTML = table([["Time","time"],["Strategy","strategy_id"],["Symbol","symbol"],["Side","side"],["Qty","qty"],["Status","status"],["Avg","avg_price"],["Tag","tag"],["Message","message"]], S.orders);
  $("trades").innerHTML = table([["Exit","time"],["Strategy","strategy_id"],["Symbol","symbol"],["Side","direction"],["Qty","qty"],["Entry","entry"],["Exit px","exit"],["P&L","pnl",1]], S.trades);
}
refresh(); setInterval(refresh, 1000);
</script></body></html>"""


def make_handler(state_file: Path):
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path.startswith("/state.json"):
                try:
                    body, ctype = state_file.read_bytes(), "application/json"
                except OSError:
                    self.send_error(503, "engine not running (no state file yet)")
                    return
            else:
                body, ctype = PAGE.encode(), "text/html; charset=utf-8"
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):  # keep the terminal quiet
            pass

    return Handler


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--state", default=str(ROOT / "run" / "state.json"), help="engine state file")
    ap.add_argument("--port", type=int, default=8050)
    args = ap.parse_args()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(Path(args.state)))
    print(f"Dashboard: http://localhost:{args.port}  (reading {args.state})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
