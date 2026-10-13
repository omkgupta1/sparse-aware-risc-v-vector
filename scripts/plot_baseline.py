import csv
from pathlib import Path

import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[1]
CSV_PATH = ROOT / "results" / "midterm" / "baseline_results.csv"
OUT_DIR = ROOT / "results" / "midterm" / "figures"
OUT_DIR.mkdir(parents=True, exist_ok=True)

with CSV_PATH.open(newline="") as f:
    rows = sorted(
        csv.DictReader(f),
        key=lambda r: int(r["sparsity_percent"]),
    )

labels = [f'{r["sparsity_percent"]}% sparsity' for r in rows]
times = [float(r["simulated_time_us"]) for r in rows]
instructions = [int(r["sim_instructions"]) for r in rows]
nnz = [int(r["nnz"]) for r in rows]


def make_bar_chart(values, ylabel, title, filename, formatter):
    fig, ax = plt.subplots(figsize=(7, 4.8))
    bars = ax.bar(labels, values, width=0.55)

    ax.set_ylabel(ylabel)
    ax.set_title(title, pad=14)
    ax.grid(axis="y", alpha=0.25)
    ax.set_axisbelow(True)

    ax.bar_label(
        bars,
        labels=[formatter(v) for v in values],
        padding=4,
    )

    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    fig.tight_layout()
    fig.savefig(OUT_DIR / filename, dpi=220, bbox_inches="tight")
    plt.close(fig)
    print(f"Saved: {OUT_DIR / filename}")


make_bar_chart(
    times,
    "Simulated time (µs)",
    "Baseline RVV SpMV — Simulated Time",
    "simulated_time.png",
    lambda v: f"{v:.3f}",
)

make_bar_chart(
    instructions,
    "Simulated instructions",
    "Baseline RVV SpMV — Instruction Count",
    "simulated_instructions.png",
    lambda v: f"{v:,}",
)

make_bar_chart(
    nnz,
    "Nonzero elements (NNZ)",
    "SpMV Matrix Nonzero Elements",
    "nonzero_elements.png",
    lambda v: f"{v:,}",
)