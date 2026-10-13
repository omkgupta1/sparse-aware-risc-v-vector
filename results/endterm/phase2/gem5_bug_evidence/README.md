# First observation of gem5 Issue 1 (historical runs)

These 8 runs are the first Phase 2 runs of `rvv_opt_m2`, `rvv_opt_m4`, `dense_m2` and `dense_m4` (d=20/60). They used the **pre-workaround** kernels, which ended with one `vfredusum` over the whole LMUL>1 group.

- `rvv_opt_m2`, `rvv_opt_m4` and `dense_m2` failed validation (relative errors 0.40–0.77). `dense_m4` passed by chance.
- The pre-workaround source is no longer in the tree. To reproduce the bug, use the directed test (`src/tests/rvv_lmul_check.c`, run by `scripts/run_gem5_bug_checks.sh`).
- These runs are kept only as the original evidence. Their timings are not used anywhere.

See `docs/gem5_rvv_issues.md`.
