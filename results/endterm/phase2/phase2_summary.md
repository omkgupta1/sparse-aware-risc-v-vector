### density 1% (sparsity 99%)

| kernel | status | roi_cycles | speedup_vs_scalar | speedup_vs_rvv_orig | roi_insts | roi_ipc | cycles_per_nnz | max_rel_err | chunks | lane_util | roi_l1d_miss_rate | roi_l1d_primary_miss_rate | roi_lq_full_events | chunk_check |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| scalar | ok | 1,474 | 1.000 | 1.294 | 6,225 | 4.223 | 5.500 | 6.5e-08 | – | – | 0.00% | 0.00% | 6 | n/a |
| rvv_orig | ok | 1,907 | 0.773 | 1.000 | 5,960 | 3.125 | 7.116 | 8.81e-08 | 102 | 32.84% | 0.00% | 0.00% | 0 | ok |
| rvv_opt_m1 | ok | 2,004 | 0.736 | 0.952 | 6,267 | 3.127 | 7.478 | 8.81e-08 | 102 | 32.84% | 0.00% | 0.00% | 0 | ok |
| rvv_opt_m2 | ok | 2,163 | 0.681 | 0.882 | 6,523 | 3.016 | 8.071 | 8.81e-08 | 102 | 16.42% | 0.00% | 0.00% | 0 | ok |
| rvv_opt_m4 | ok | 2,428 | 0.607 | 0.785 | 7,035 | 2.897 | 9.060 | 8.81e-08 | 102 | 8.21% | 0.00% | 0.00% | 0 | ok |
| dense_m1 | ok | 44,623 | 0.033 | 0.0427 | 137,251 | 3.076 | 167 | 8.1e-08 | 8,192 | 100.00% | 0.47% | 0.47% | 0 | ok |
| dense_m2 | ok | 24,156 | 0.061 | 0.0789 | 71,971 | 2.979 | 90.134 | 8.4e-08 | 4,096 | 100.00% | 1.09% | 0.47% | 0 | ok |
| dense_m4 | ok | 17,176 | 0.0858 | 0.111 | 39,715 | 2.312 | 64.090 | 8.4e-08 | 2,048 | 100.00% | 49.24% | 4.72% | 6,773 | ok |

### density 5% (sparsity 95%)

| kernel | status | roi_cycles | speedup_vs_scalar | speedup_vs_rvv_orig | roi_insts | roi_ipc | cycles_per_nnz | max_rel_err | chunks | lane_util | roi_l1d_miss_rate | roi_l1d_primary_miss_rate | roi_lq_full_events | chunk_check |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| scalar | ok | 11,774 | 1.000 | 0.528 | 32,520 | 2.762 | 3.997 | 1.7e-07 | – | – | 0.00% | 0.00% | 3,415 | n/a |
| rvv_orig | ok | 6,218 | 1.894 | 1.000 | 14,270 | 2.295 | 2.111 | 1.47e-07 | 492 | 74.85% | 0.00% | 0.00% | 39 | ok |
| rvv_opt_m1 | ok | 6,261 | 1.881 | 0.993 | 13,536 | 2.162 | 2.125 | 1.48e-07 | 492 | 74.85% | 0.00% | 0.00% | 76 | ok |
| rvv_opt_m2 | ok | 5,471 | 2.152 | 1.137 | 10,102 | 1.846 | 1.857 | 1.48e-07 | 246 | 74.85% | 0.00% | 0.00% | 122 | ok |
| rvv_opt_m4 | ok | 5,688 | 2.070 | 1.093 | 10,464 | 1.840 | 1.931 | 1.48e-07 | 236 | 39.01% | 0.00% | 0.00% | 99 | ok |
| dense_m1 | ok | 44,623 | 0.264 | 0.139 | 137,251 | 3.076 | 15.147 | 1.2e-07 | 8,192 | 100.00% | 0.47% | 0.47% | 0 | ok |
| dense_m2 | ok | 24,156 | 0.487 | 0.257 | 71,971 | 2.979 | 8.200 | 1.33e-07 | 4,096 | 100.00% | 1.11% | 0.47% | 0 | ok |
| dense_m4 | ok | 17,234 | 0.683 | 0.361 | 39,715 | 2.304 | 5.850 | 1.33e-07 | 2,048 | 100.00% | 49.26% | 4.78% | 6,907 | ok |

### density 20% (sparsity 80%)

| kernel | status | roi_cycles | speedup_vs_scalar | speedup_vs_rvv_orig | roi_insts | roi_ipc | cycles_per_nnz | max_rel_err | chunks | lane_util | roi_l1d_miss_rate | roi_l1d_primary_miss_rate | roi_lq_full_events | chunk_check |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| scalar | ok | 57,730 | 1.000 | 0.268 | 122,581 | 2.123 | 4.470 | 3.52e-07 | – | – | 0.24% | 0.08% | 31,999 | n/a |
| rvv_orig | ok | 15,463 | 3.733 | 1.000 | 37,222 | 2.407 | 1.197 | 1.22e-07 | 1,756 | 91.93% | 0.36% | 0.15% | 1,038 | ok |
| rvv_opt_m1 | ok | 15,562 | 3.710 | 0.994 | 32,716 | 2.102 | 1.205 | 1.4e-07 | 1,756 | 91.93% | 0.17% | 0.15% | 1,655 | ok |
| rvv_opt_m2 | ok | 11,319 | 5.100 | 1.366 | 21,572 | 1.906 | 0.876 | 1.28e-07 | 996 | 81.04% | 0.36% | 0.15% | 3,080 | ok |
| rvv_opt_m4 | FAILED(exit=0,val=FAILED) | 11,030 | – | – | 14,674 | 1.330 | 0.854 | 0.000579 | 502 | 80.40% | 0.50% | 0.18% | 3,233 | ok |
| dense_m1 | ok | 44,623 | 1.294 | 0.347 | 137,251 | 3.076 | 3.455 | 1.2e-07 | 8,192 | 100.00% | 0.47% | 0.47% | 0 | ok |
| dense_m2 | ok | 24,156 | 2.390 | 0.64 | 71,971 | 2.979 | 1.870 | 1.55e-07 | 4,096 | 100.00% | 1.10% | 0.47% | 0 | ok |
| dense_m4 | ok | 17,208 | 3.355 | 0.899 | 39,715 | 2.308 | 1.332 | 1.52e-07 | 2,048 | 100.00% | 49.26% | 4.76% | 6,801 | ok |

### density 60% (sparsity 40%)

| kernel | status | roi_cycles | speedup_vs_scalar | speedup_vs_rvv_orig | roi_insts | roi_ipc | cycles_per_nnz | max_rel_err | chunks | lane_util | roi_l1d_miss_rate | roi_l1d_primary_miss_rate | roi_lq_full_events | chunk_check |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| scalar | ok | 188,960 | 1.000 | 0.194 | 359,026 | 1.900 | 4.823 | 4.68e-07 | – | – | 0.28% | 0.09% | 113,937 | n/a |
| rvv_orig | ok | 36,693 | 5.150 | 1.000 | 96,778 | 2.638 | 0.936 | 2.28e-07 | 5,063 | 96.73% | 0.69% | 0.19% | 5,930 | ok |
| rvv_opt_m1 | ok | 43,910 | 4.303 | 0.836 | 82,354 | 1.876 | 1.121 | 1.79e-07 | 5,063 | 96.73% | 0.20% | 0.19% | 15,106 | ok |
| rvv_opt_m2 | ok | 29,143 | 6.484 | 1.259 | 44,885 | 1.540 | 0.744 | 1.91e-07 | 2,548 | 96.11% | 0.52% | 0.19% | 9,092 | ok |
| rvv_opt_m4 | ok | 24,141 | 7.827 | 1.520 | 26,317 | 1.090 | 0.616 | 1.51e-07 | 1,276 | 95.96% | 0.63% | 0.25% | 8,398 | ok |
| dense_m1 | ok | 44,623 | 4.235 | 0.822 | 137,251 | 3.076 | 1.139 | 1.43e-07 | 8,192 | 100.00% | 0.47% | 0.47% | 0 | ok |
| dense_m2 | ok | 24,165 | 7.820 | 1.518 | 71,971 | 2.978 | 0.617 | 1.87e-07 | 4,096 | 100.00% | 1.03% | 0.47% | 0 | ok |
| dense_m4 | ok | 16,220 | 11.650 | 2.262 | 39,715 | 2.449 | 0.414 | 1.48e-07 | 2,048 | 100.00% | 49.07% | 3.64% | 6,088 | ok |

### density 100% (sparsity 0%)

| kernel | status | roi_cycles | speedup_vs_scalar | speedup_vs_rvv_orig | roi_insts | roi_ipc | cycles_per_nnz | max_rel_err | chunks | lane_util | roi_l1d_miss_rate | roi_l1d_primary_miss_rate | roi_lq_full_events | chunk_check |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| scalar | ok | 321,932 | 1.000 | 0.177 | 596,255 | 1.852 | 4.912 | 3.09e-07 | – | – | 0.37% | 0.08% | 197,069 | n/a |
| rvv_orig | ok | 57,034 | 5.645 | 1.000 | 153,120 | 2.685 | 0.87 | 2.27e-07 | 8,192 | 100.00% | 0.92% | 0.20% | 15,109 | ok |
| rvv_opt_m1 | ok | 71,239 | 4.519 | 0.801 | 129,311 | 1.815 | 1.087 | 1.37e-07 | 8,192 | 100.00% | 0.44% | 0.20% | 31,594 | ok |
| rvv_opt_m2 | ok | 47,355 | 6.798 | 1.204 | 68,127 | 1.439 | 0.723 | 1.37e-07 | 4,096 | 100.00% | 1.07% | 0.20% | 17,406 | ok |
| rvv_opt_m4 | ok | 44,385 | 7.253 | 1.285 | 37,919 | 0.854 | 0.677 | 1.37e-07 | 2,048 | 100.00% | 1.12% | 0.20% | 19,688 | ok |
| dense_m1 | ok | 44,623 | 7.214 | 1.278 | 137,251 | 3.076 | 0.681 | 1.37e-07 | 8,192 | 100.00% | 0.47% | 0.47% | 0 | ok |
| dense_m2 | ok | 24,156 | 13.327 | 2.361 | 71,971 | 2.979 | 0.369 | 1.37e-07 | 4,096 | 100.00% | 1.00% | 0.47% | 0 | ok |
| dense_m4 | ok | 14,826 | 21.714 | 3.847 | 39,715 | 2.679 | 0.226 | 1.37e-07 | 2,048 | 100.00% | 48.98% | 2.11% | 4,932 | ok |

Phase 1 standalone midterm kernel vs. harness `rvv_orig` (ROI cycles):

- d=20%: Phase 1 14,782 vs harness 15,463 (+4.6%)
- d=60%: Phase 1 36,058 vs harness 36,693 (+1.8%)

Notes:
- rvv_opt_m4 (LMUL=4 gather) is excluded from performance claims: gem5 25.1 O3 produces
  wrong results intermittently (timing-dependent; correct on MinorCPU and QEMU). See
  results/endterm/phase2/gem5_m4_investigation/ and PROGRESS.md. Its timing is shown only for completeness.
- roi_l1d_miss_rate counts MSHR hits (accesses merging into an in-flight miss) as misses;
  roi_l1d_primary_miss_rate counts only new requests to L2.
