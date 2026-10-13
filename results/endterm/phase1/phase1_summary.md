| Metric | 80% sparse (d=20%) | 40% sparse (d=60%) |
|---|---:|---:|
| density_pct | 20 | 60 |
| sparsity_pct | 80 | 40 |
| nnz | 12,915 | 39,181 |
| validation | PASSED | PASSED |
| max_error | 4.768e-06 | 1.907e-05 |
| roi_cycles | 14,782 | 36,058 |
| roi_insts | 34,174 | 93,706 |
| roi_ops | 52,357 | 148,076 |
| roi_ipc | 2.312 | 2.599 |
| roi_cycles_per_nnz | 1.145 | 0.9203 |
| roi_insts_per_nnz | 2.646 | 2.392 |
| roi_vec_insts | 32,733 | 95,382 |
| roi_indexed_load_uops | 12,915 | 39,181 |
| roi_l1d_miss_rate | 0.008048 | 0.007652 |
| roi_l2_miss_rate | 0.0005949 | 0.0005983 |
| roi_lq_full_events | 1,496 | 6,019 |
| roi_dram_bytes_read | 512 | 704 |
| prologue_cycles | 399,392 | 448,001 |
| epilogue_cycles | 42,945 | 67,519 |
| sum_of_dumps_cycles | 457,119 | 551,578 |
| sum_of_dumps_insts | 1,365,941 | 1,846,054 |
| roi_share_of_total_cycles | 0.03234 | 0.06537 |
| e2e_same_toolchain_cycles | 447,458 | 515,275 |
| e2e_same_toolchain_insts | 1,365,927 | 1,846,040 |
| e2e_same_toolchain_ipc | 3.053 | 3.583 |
| midterm_e2e_cycles | 569,394 | 744,568 |
| midterm_e2e_insts | 1,409,447 | 2,073,307 |
| midterm_e2e_ipc | 2.475 | 2.785 |

Scaling from d=20% to d=60%:

- NNZ ratio: 3.034x
- Kernel (ROI) cycles ratio: 2.439x; marginal kernel cost 0.810 cycles per extra nonzero
- Midterm end-to-end cycles ratio: 1.308x; marginal 6.669 cycles per extra nonzero
- Kernel load-queue-full events ratio: 4.023x

ROI-marker controls (whole-program cycles = sum of dumps; ROI = kernel dump):

| Build | d | dumps | total cycles | ROI cycles | total insts |
|---|---:|---:|---:|---:|---:|
| e2e (no markers) | 20 | 1 | 447,458 | – | 1,365,927 |
| e2e (no markers) | 60 | 1 | 515,275 | – | 1,846,040 |
| no-op call control | 20 | 1 | 460,421 | – | 1,366,094 |
| no-op call control | 60 | 1 | 554,814 | – | 1,846,207 |
| m5_dump_reset_stats x2 (used) | 20 | 3 | 457,119 | 14,782 | 1,365,941 |
| m5_dump_reset_stats x2 (used) | 60 | 3 | 551,578 | 36,058 | 1,846,054 |
| m5_reset_stats + m5_dump_stats | 20 | 2 | n/a | 14,782 | n/a |
| m5_reset_stats + m5_dump_stats | 60 | 2 | n/a | 35,955 | n/a |

n/a: with m5_reset_stats + m5_dump_stats the prologue is discarded by the reset and the final dump (ROI_BEGIN..exit) overlaps the ROI dump, so no whole-program total exists.
