#!/usr/bin/env python3
"""Phase 4B report: Vector-Indirect Prefetcher (VIP) A/B evaluation.

Reads results/endterm/phase4b/<group>/<input>/<kernel>__<pfcfg>/ and writes
results/endterm/phase4b/phase4b_all.csv and phase4b_summary.md.

Every comparison is between runs of the same gem5 binary and the same
workload binary on the same input; only runs that exited cleanly and
validated are used. Prefetcher configurations (L1D; the L2 keeps its stride
prefetcher):
  stride     gem5 StridePrefetcher (baseline, the machine of all earlier phases)
  vip        VectorIndirectPrefetcher (stride + vector-indirect, look-ahead on)
  vip_nola   VIP without look-ahead (immediate target prefetches only)
  vip_noind  VIP with the indirect part off (= stride on virtual addresses + MMU)
  none       no L1D prefetcher
"""
import csv
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from collect import collect, ok  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
P4 = ROOT / "results" / "endterm" / "phase4b"
KERNELS = ["rvv_orig", "rvv_opt_m1", "rvv_acc2_m1", "scalar"]


def fmt(v, kind=""):
    if v is None:
        return "–"
    if kind == "x":
        return f"{v:.2f}×"
    if kind == "pct":
        return f"{v * 100:.1f}%"
    if isinstance(v, float):
        return f"{v:.3g}" if abs(v) < 10 else f"{v:,.0f}"
    if isinstance(v, int):
        return f"{v:,}"
    return str(v)


def table(header, rows):
    out = ["| " + " | ".join(header) + " |",
           "|" + "|".join("---:" if i else "---" for i in range(len(header))) + "|"]
    return out + ["| " + " | ".join(r) + " |" for r in rows]


def input_sort_key(i):
    if "_c" in i:
        return (i[0], int(i.split("_c")[1].split("_")[0]))
    return (i, 0)


def main():
    idx = defaultdict(dict)   # (group, input) -> "kernel__cfg" -> record
    recs = []
    for log in sorted(P4.rglob("run.log")):
        if "EXIT_STATUS=" not in log.read_text(errors="replace"):
            continue
        group, inp, name = log.parent.relative_to(P4).parts
        kernel, cfg = name.split("__")
        r = collect(log.parent)
        r.update(group=group, input=inp, kernel=kernel, pfcfg=cfg, valid=ok(r))
        recs.append(r)
        idx[(group, inp)][name] = r
    if not recs:
        sys.exit("no Phase 4B runs found")

    def get(g, i, k, c):
        r = idx[(g, i)].get(f"{k}__{c}")
        return r if r and r["valid"] else None

    def cyc(g, i, k, c):
        r = get(g, i, k, c)
        return r["roi_cycles"] if r else None

    def sp(g, i, k, c, base="stride"):
        a, b = cyc(g, i, k, base), cyc(g, i, k, c)
        return a / b if a and b else None

    bad = [r for r in recs if not r["valid"]]
    md = ["# Phase 4B results: Vector-Indirect Prefetcher (VIP)", "",
          "ROI cycles from gem5 25.1 + `gem5_patches/sparse_aware.patch` (O3, VLEN=256 unless stated).",
          "Speedup = cycles with the L1D stride prefetcher / cycles with the named configuration, "
          "same binaries, same input. Only validated runs are used.", "",
          f"Runs: {len(recs)} completed, {len(recs) - len(bad)} validated, {len(bad)} failed."]
    for r in bad:
        md.append(f"- FAILED `{Path(r['run']).relative_to(ROOT)}` exit={r.get('exit_status')} "
                  f"validation={r.get('res_validation')} max_rel_err={r.get('res_max_rel_err')}")

    main_inputs = sorted(((g, i) for (g, i) in idx if g in ("size", "suitesparse", "density")),
                         key=lambda gi: (("size", "suitesparse", "density").index(gi[0]), input_sort_key(gi[1])))

    # ------------------------------------------------------------ headline
    md += ["", "## VIP speedup over the stride prefetcher", ""]
    rows = []
    for g, i in main_inputs:
        r0 = get(g, i, "rvv_orig", "stride") or get(g, i, "scalar", "stride")
        label = i
        if g == "size":
            label = ("uniform" if i[0] == "u" else "power-law") + f", x={r0['mat_x_bytes'] // 1024} KiB" if r0 else i
        rows.append([g, label] +
                    [f"{fmt(cyc(g, i, k, 'stride'))} → {fmt(cyc(g, i, k, 'vip'))} ({fmt(sp(g, i, k, 'vip'), 'x')})"
                     for k in KERNELS])
    md += table(["group", "input"] + [f"{k}: stride → VIP cycles (speedup)" for k in KERNELS], rows)

    # ------------------------------------------------------------ gap closed
    md += ["", "## How much of the cache-miss penalty VIP removes (size sweep, `rvv_orig`)", "",
           "Gap = stride cycles − stride cycles of the same kernel on the 8 KiB-`x` input "
           "(same rows/nnz, gathers hit in L1). Gap closed = (stride − VIP) / gap.", ""]
    rows = []
    for g, i in main_inputs:
        if g != "size" or i.split("_c")[1].startswith("002048"):
            continue
        small = cyc("size", f"{i[0]}_r16384_c002048_npr16", "rvv_orig", "stride")
        s, v = cyc(g, i, "rvv_orig", "stride"), cyc(g, i, "rvv_orig", "vip")
        r0 = get(g, i, "rvv_orig", "stride")
        gap = s - small if s and small else None
        rows.append([("uniform" if i[0] == "u" else "power-law"),
                     f"{r0['mat_x_bytes'] // 1024} KiB" if r0 else "–", fmt(small), fmt(s), fmt(v),
                     fmt(s / small, "x") if s and small else "–",
                     fmt((s - v) / gap, "pct") if gap and v else "–"])
    md += table(["dist", "x size", "8 KiB-x cycles", "stride cycles", "VIP cycles",
                 "upper-bound speedup", "gap closed by VIP"], rows)

    # ------------------------------------------------------------ memory behaviour
    md += ["", "## Memory behaviour, `rvv_orig` (stride → VIP)", ""]
    rows = []
    for g, i in main_inputs:
        a, b = get(g, i, "rvv_orig", "stride"), get(g, i, "rvv_orig", "vip")
        if not (a and b):
            continue
        def pair(k, kind=""):
            return f"{fmt(a.get(k), kind)} → {fmt(b.get(k), kind)}"
        rows.append([i, pair("roi_l1d_primary_miss_rate", "pct"), pair("roi_l1d_mpki"),
                     pair("roi_l2_miss_rate", "pct"),
                     f"{a['roi_dram_bytes_read'] // 1024:,} → {b['roi_dram_bytes_read'] // 1024:,}",
                     pair("roi_lq_full_events"),
                     pair("pf_l1d_accuracy", "pct"), pair("pf_l1d_coverage", "pct"),
                     pair("pf_l1d_pfLate")])
    md += table(["input", "L1D primary miss", "L1D MPKI", "L2 miss rate", "DRAM read (KiB)",
                 "LQ-full events", "L1D pf accuracy", "L1D pf coverage", "late prefetches"], rows)

    md += ["", "VIP counters in the ROI (`rvv_orig`, VIP). Patterns are normally learned during the "
           "untimed warm-up run, so `patternsEnabled` in the ROI is usually 0.", ""]
    keys = ["vip_indexAccesses", "vip_indexAccessesWithData", "vip_immediateTargets", "vip_lookaheadFills",
            "vip_lookaheadTargets", "vip_targetsDeduplicated", "vip_gatherChecksOk", "vip_gatherChecksFailed",
            "vip_patternsEnabled", "vip_patternsDisabled", "vip_lookaheadPendingDropped"]
    rows = []
    for g, i in main_inputs:
        b = get(g, i, "rvv_orig", "vip")
        if b:
            rows.append([i] + [fmt(b.get("pf_l1d_" + k)) for k in keys])
    md += table(["input"] + [k.replace("vip_", "") for k in keys], rows)

    # ------------------------------------------------------------ ablation
    md += ["", "## Ablation (`rvv_orig`): speedup over stride", ""]
    rows = []
    for g, i in main_inputs:
        rows.append([i] + [fmt(sp(g, i, "rvv_orig", c), "x") for c in ("none", "vip_noind", "vip_nola", "vip")])
    md += table(["input", "no L1D prefetcher", "VIP, indirect off", "VIP, no look-ahead", "VIP (full)"], rows)

    # ------------------------------------------------------------ sensitivity / vlen
    sens = sorted(i for (g, i) in idx if g == "sensitivity")
    if sens:
        md += ["", "## Look-ahead window sensitivity (`rvv_orig`, VIP; default 1 KiB)", ""]
        rows = []
        for i in sens:
            c256 = idx[("sensitivity", i)].get("rvv_orig__vip_win256B")
            c4k = idx[("sensitivity", i)].get("rvv_orig__vip_win4KiB")
            base = cyc("size", i, "rvv_orig", "stride")
            def s_(r):
                return fmt(base / r["roi_cycles"], "x") if r and r["valid"] and base else "–"
            rows.append([i, s_(c256), fmt(sp("size", i, "rvv_orig", "vip"), "x"), s_(c4k)])
        md += table(["input", "256 B", "1 KiB (default)", "4 KiB"], rows)
    vl = sorted(i for (g, i) in idx if g == "vlen512")
    if vl:
        md += ["", "## VLEN=512 (`rvv_orig`)", ""]
        md += table(["input", "stride", "VIP", "speedup"],
                    [[i, fmt(cyc("vlen512", i, "rvv_orig", "stride")), fmt(cyc("vlen512", i, "rvv_orig", "vip")),
                      fmt(sp("vlen512", i, "rvv_orig", "vip"), "x")] for i in vl])

    # ------------------------------------------------------------ history depth
    hist_inputs = sorted((i for (g, i) in idx if g == "history"), key=input_sort_key)
    if hist_inputs:
        depths = [4, 8, 16, 32, 64, 128]
        md += ["", "## VIP history-depth sensitivity (speedup over stride; target prefetches issued in the ROI)", "",
               "Default depth is 32. Its rows re-run the main configuration with the rebuilt gem5 and must "
               "reproduce the main Phase 4B VIP cycles exactly (check column).", ""]
        rows = []
        for i in hist_inputs:
            for k in ("rvv_orig", "rvv_opt_m1", "rvv_acc2_m1"):
                base = cyc("size", i, k, "stride")
                cells = []
                for h in depths:
                    r = get("history", i, k, f"vip_h{h}")
                    if r and base:
                        t = (r.get("pf_l1d_vip_immediateTargets") or 0) + (r.get("pf_l1d_vip_lookaheadTargets") or 0)
                        cells.append(f"{fmt(base / r['roi_cycles'], 'x')} ({t:,})")
                    else:
                        cells.append("–")
                h32, main = get("history", i, k, "vip_h32"), cyc("size", i, k, "vip")
                check = "–" if not (h32 and main) else ("identical" if h32["roi_cycles"] == main else
                                                        f"DIFFERS ({h32['roi_cycles']:,} vs {main:,})")
                rows.append([i, k] + cells + [check])
        md += table(["input", "kernel"] + [f"depth {h}" for h in depths] + ["depth 32 = main run?"], rows)

    # ------------------------------------------------------------ MSHRs
    mshr_inputs = sorted((i for (g, i) in idx if g == "mshr"), key=input_sort_key)
    if mshr_inputs:
        md += ["", "## L1D MSHR sensitivity (cycles; VIP speedup over stride at the same MSHR count)", ""]
        rows = []
        for i in mshr_inputs:
            for k in ("rvv_orig", "rvv_acc2_m1"):
                cells = []
                for m in (16, 32, 64):
                    if m == 16:
                        s_, v_ = cyc("size", i, k, "stride"), cyc("size", i, k, "vip")
                    else:
                        s_, v_ = cyc("mshr", i, k, f"stride_mshr{m}"), cyc("mshr", i, k, f"vip_mshr{m}")
                    cells.append(f"{fmt(s_)} / {fmt(v_)} ({fmt(s_ / v_, 'x') if s_ and v_ else '–'})")
                rows.append([i, k] + cells)
        md += table(["input", "kernel", "16 MSHRs (default): stride / VIP", "32 MSHRs", "64 MSHRs"], rows)

    cols = ["group", "input", "kernel", "pfcfg", "valid", "exit_status", "res_validation", "res_max_rel_err",
            "host_seconds", "sim_vlen", "sim_l1d_pf", "sim_l1d_pf_params", "mat_rows", "mat_cols", "mat_nnz",
            "mat_row_len_mean", "mat_row_len_cv", "mat_x_bytes", "mat_csr_bytes"]
    extra = sorted({k for r in recs for k in r if k.startswith(("roi_", "pf_l1d_", "pf_l2_", "op_"))})
    with open(P4 / "phase4b_all.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols + extra, extrasaction="ignore")
        w.writeheader()
        w.writerows(sorted(recs, key=lambda r: (r["group"], r["input"], r["kernel"], r["pfcfg"])))
    (P4 / "phase4b_summary.md").write_text("\n".join(md) + "\n")
    print("\n".join(md))


if __name__ == "__main__":
    main()
