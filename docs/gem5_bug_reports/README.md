# gem5 bug reports (drafts, not filed)

Four issue drafts for **github.com/gem5/gem5**, one per bug found in this project. Each has a standalone reproducer in `repro/` (no project harness needed).

**All four were reproduced on the *unmodified* gem5 v25.1.0.1 binary** (commit `a28ae2f8b2`, the backup taken before the project's prefetcher patch). Evidence logs: `results/endterm/gem5_bug_reports/*.log`.

| Draft | Bug | gem5 O3 | gem5 Minor | QEMU 8.2.2 |
|---|---|---|---|---|
| [issue1.md](issue1.md) | `vfredusum.vs` (LMUL ≥ 2) reads a stale register when `vd` overlaps `vs2` | **FAIL** (55.5) | PASS (63.5) | PASS (63.5) |
| [issue2.md](issue2.md) | LMUL > 1 `vluxei32` gathers return wrong data with many element micro-ops in flight | **FAIL** (225/1024 rows; 4/1024 for LMUL=4) | PASS (0) | PASS (0) |
| [issue3.md](issue3.md) | Tail-undisturbed `vslideup`/`vslidedown` buffer later read in full → page-table fault | **panic** | PASS | PASS |
| [issue4.md](issue4.md) | `IndirectMemoryPrefetcher` panics on read hits (`PrefetchInfo` has no data) | **panic** | **panic** | n/a |

## Duplicate check (done 2026-10-14)

Searched gem5 issues for `vfredusum`, `vluxei`, `indexed load`, `vslideup`, `IndirectMemoryPrefetcher` and `PrefetchInfo::get` (open and closed). **None of the four is a duplicate**, but Issue 3 **must be re-tested on `develop` first** (see #3313 below). Related reports are cited in each draft's "Related issues" section:

| Draft | Closest existing reports | Why it is still new |
|---|---|---|
| issue1 | #1956 (closed, fixed in 24.1), #2267 (user error) | Reproduces on 25.1.0.1; needs a tail-undisturbed partial write plus `vd`/`vs2` overlap |
| issue2 | #3278 (open design discussion), #2248 (fixed) | No existing report of wrong gather results |
| issue3 | #2575 (fixed on develop), PR #3513 (open), **PR #3313 (merged into develop 2026-07-28: slide refactor removing pin/copy micro-ops)** | Different symptom (`vslideup` tu at LMUL=1, no overlap, O3 only), **but #3313 may already fix it: re-test on `develop` before filing** |
| issue4 | **#1487** (same panic; closed as not reproducible; fix PR #2003 not merged), PR #3514 (open, new IMP V2) | Deterministic minimal reproducer; the original IMP is still broken and #3514 leaves it unchanged |

## Before filing

1. **Look for duplicates:** search gem5's open and closed issues for `vfredusum`, `vluxei`, `indexed load`, `pinned`, `vslideup`, `IndirectMemoryPrefetcher`, `PrefetchInfo::get`.
2. **Optionally re-test on the latest `develop`** (`git clone https://github.com/gem5/gem5 && cd gem5 && scons build/RISCV/gem5.opt -j8`). Then:
   - update "Affects version" in the drafts;
   - drop any bug that is already fixed.
3. **Attach the reproducer files** (drag and drop into the GitHub issue, or link a gist), or keep the inline `<details>` blocks.
4. **File from your own GitHub account.** The `gh` CLI on this machine is currently logged in as `khushisahu1111`.

## Filing

Reproduce first (inside the project container, or any machine with a RISC-V GCC and gem5):

```bash
GEM5_BIN=/path/to/gem5/build/RISCV/gem5.opt OUT=/tmp/gem5-repro \
    docs/gem5_bug_reports/repro/run_repros.sh
```

Then file each issue:

```bash
gh auth login        # log in with YOUR GitHub account first
cd docs/gem5_bug_reports
gh issue create --repo gem5/gem5 --title "arch-riscv: vfredusum.vs with LMUL>=2 reads stale vs2 register when vd overlaps vs2 (O3CPU)" --body-file issue1.md
gh issue create --repo gem5/gem5 --title "arch-riscv: LMUL>1 indexed vector loads (vluxei32) return wrong data on O3CPU" --body-file issue2.md
gh issue create --repo gem5/gem5 --title "arch-riscv: tail-undisturbed vslideup/vslidedown result read later at full width gives stale data (O3CPU page fault)" --body-file issue3.md
gh issue create --repo gem5/gem5 --title "mem-cache: IndirectMemoryPrefetcher panics 'PrefetchInfo::get called with a request with no data' on read hits" --body-file issue4.md
```

Issue 4 has a small, clear root cause. Sending the suggested fix as a pull request to `develop` (see the gem5 CONTRIBUTING guide) is the most likely to be accepted. The suggested fix in the draft has **not** been tested.
