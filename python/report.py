"""
Render a C++ backtest result as a self-contained HTML report (standard library only).

    ./build/groww_engine backtest --days 20
    python3 python/report.py reports/backtest.json          # writes reports/backtest.html
"""
import html
import json
import sys
from pathlib import Path


def svg_line(vals, width=900, height=240):
    if len(vals) < 2:
        return "<p>Not enough data.</p>"
    lo, hi = min(vals + [0.0]), max(vals + [0.0])
    span = (hi - lo) or 1.0
    pts = " ".join(f"{i * width / (len(vals) - 1):.1f},{8 + (hi - v) * (height - 16) / span:.1f}"
                   for i, v in enumerate(vals))
    zero = 8 + hi * (height - 16) / span
    color = "#16a34a" if vals[-1] >= 0 else "#dc2626"
    return (f"<svg viewBox='0 0 {width} {height}' style='width:100%;height:auto'>"
            f"<line x1='0' x2='{width}' y1='{zero:.1f}' y2='{zero:.1f}' stroke='#999' stroke-dasharray='4'/>"
            f"<polyline points='{pts}' fill='none' stroke='{color}' stroke-width='2'/></svg>")


def main() -> None:
    src = Path(sys.argv[1] if len(sys.argv) > 1 else "reports/backtest.json")
    r = json.loads(src.read_text())
    m = r["metrics"]
    cards = "".join(f"<div class='card'><span>{html.escape(k)}</span><b>{v}</b></div>" for k, v in m.items())
    per = "".join(f"<tr><td>{html.escape(k)}</td><td>₹{v['net_pnl']:,.0f}</td><td>{v['trades']}</td>"
                  f"<td>{v['win_rate_pct']}%</td></tr>" for k, v in r["per_strategy"].items())
    trades = "".join(f"<tr><td>{html.escape(t['strategy_id'])}</td><td>{html.escape(t['symbol'])}</td>"
                     f"<td>{t['direction']}</td><td>{t['qty']}</td><td>{t['entry_time'][:16]}</td>"
                     f"<td>{t['entry_price']:,.2f}</td><td>{t['exit_time'][:16]}</td><td>{t['exit_price']:,.2f}</td>"
                     f"<td style='color:{'#16a34a' if t['pnl'] >= 0 else '#dc2626'}'>₹{t['pnl']:,.0f}</td></tr>"
                     for t in r["trades"][-300:])
    doc = f"""<!doctype html><html><head><meta charset="utf-8"><title>Backtest report</title>
<meta name="viewport" content="width=device-width,initial-scale=1"><style>
body{{font:14px/1.45 -apple-system,Segoe UI,Roboto,sans-serif;max-width:1050px;margin:24px auto;padding:0 16px;color:#141821}}
.grid{{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:8px}}
.card{{border:1px solid #e3e6ee;border-radius:8px;padding:10px}}.card span{{color:#5d6678;font-size:12px}}.card b{{display:block;font-size:17px}}
table{{width:100%;border-collapse:collapse;font-size:13px}}th,td{{text-align:left;padding:5px 8px;border-bottom:1px solid #e3e6ee}}
</style></head><body>
<h1>Backtest: {html.escape(", ".join(r["strategy_ids"]))}</h1>
<p>{r["start"][:10]} → {r["end"][:10]} · {r["interval"]}-minute bars · {r["bars"]} bars replayed in {r["elapsed_sec"]}s (C++ engine)</p>
<div class="grid">{cards}</div>
<h2>Equity curve (P&amp;L)</h2>{svg_line([v for _, v in r["equity_curve"]])}
<h2>By strategy</h2><table><tr><th>Strategy</th><th>Net P&amp;L</th><th>Trades</th><th>Win rate</th></tr>{per}</table>
<h2>Trades</h2><table><tr><th>Strategy</th><th>Symbol</th><th>Side</th><th>Qty</th><th>Entry</th><th>Price</th>
<th>Exit</th><th>Price</th><th>P&amp;L</th></tr>{trades}</table></body></html>"""
    out = src.with_suffix(".html")
    out.write_text(doc, encoding="utf-8")
    print(f"Report: {out}")


if __name__ == "__main__":
    main()
