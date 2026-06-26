#!/usr/bin/env python3
"""Aggregate bench JSON results into markdown report."""
import json
import sys
from pathlib import Path
from statistics import median


METRICS = ["mean_us", "p50_us", "p95_us", "p99_us", "ops_sec"]


def load_group(out_dir: Path, prefix: str):
    rows = []
    for p in sorted(out_dir.glob(f"{prefix}_r*.json")):
        try:
            rows.append(json.loads(p.read_text()))
        except Exception as e:
            print(f"warn: skipping {p}: {e}", file=sys.stderr)
    return rows


def summarize(rows):
    if not rows:
        return None
    return {m: median(r[m] for r in rows) for m in METRICS}


def fmt(v, m):
    if m == "ops_sec":
        return f"{v:,.0f}"
    return f"{v:.3f}"


def main():
    if len(sys.argv) < 2:
        print("usage: bench_report.py <out_dir>", file=sys.stderr)
        sys.exit(2)
    out_dir = Path(sys.argv[1])
    base_rows = load_group(out_dir, "baseline")
    pre_rows = load_group(out_dir, "preload")
    base = summarize(base_rows)
    pre = summarize(pre_rows)
    if base is None or pre is None:
        print("error: missing baseline or preload results", file=sys.stderr)
        sys.exit(1)

    cfg = base_rows[0]
    lines = []
    lines.append("# LD_PRELOAD 死锁检测库性能开销测试报告\n")
    lines.append(f"- producers × consumers: {cfg['producers']} × {cfg['consumers']}")
    lines.append(f"- warmup / run: {cfg['warmup_s']}s / {cfg['run_s']}s")
    lines.append(f"- rounds: baseline={len(base_rows)}, preload={len(pre_rows)} (取中位数)")
    lines.append(f"- 每轮样本上限: {cfg['samples']:,}（reservoir）\n")

    lines.append("## 核心结果\n")
    lines.append("| 指标 | baseline | preload | 相对变化 |")
    lines.append("|---|---:|---:|---:|")
    for m in METRICS:
        b, p = base[m], pre[m]
        if m == "ops_sec":
            delta = (p - b) / b * 100 if b > 0 else 0.0
            sign = "+" if delta >= 0 else ""
            lines.append(f"| {m} | {fmt(b, m)} | {fmt(p, m)} | {sign}{delta:.2f}% |")
        else:
            delta = (p - b) / b * 100 if b > 0 else 0.0
            factor = p / b if b > 0 else 0.0
            sign = "+" if delta >= 0 else ""
            lines.append(
                f"| {m} | {fmt(b, m)} | {fmt(p, m)} | {sign}{delta:.2f}% (×{factor:.2f}) |"
            )
    lines.append("")

    lines.append("## 各轮原始数据\n")
    for name, rows in [("baseline", base_rows), ("preload", pre_rows)]:
        lines.append(f"### {name}\n")
        lines.append("| round | mean_us | p50_us | p95_us | p99_us | ops_sec | total_ops |")
        lines.append("|---|---:|---:|---:|---:|---:|---:|")
        for r in rows:
            lines.append(
                f"| {r['tag']} | {r['mean_us']:.3f} | {r['p50_us']:.3f} | "
                f"{r['p95_us']:.3f} | {r['p99_us']:.3f} | {r['ops_sec']:,.0f} | "
                f"{r['total_ops']:,} |"
            )
        lines.append("")

    lines.append("## 解读提示\n")
    lines.append('- `mean_us` 升幅最贴近"平均每次加锁多花多久"；p99 反映长尾（栈采集/锁争用叠加）')
    lines.append("- `ops_sec` 降幅 = 吞吐损失；可近似作为整体性能损耗上限")
    lines.append("- 系统频率调节未关闭，结果为稳态相对值，跨机器对比仅看百分比")

    report = out_dir / "report.md"
    report.write_text("\n".join(lines))
    print(report)


if __name__ == "__main__":
    main()
