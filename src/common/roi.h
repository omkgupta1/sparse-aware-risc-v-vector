/*
 * Region-of-interest (ROI) markers for gem5.
 *
 * Built with -DUSE_M5 (and linked against gem5's libm5.a), ROI_BEGIN/ROI_END
 * call m5_dump_reset_stats(), so stats.txt contains three dumps:
 *
 *   dump 0  prologue  program start .. ROI_BEGIN  (startup + input generation)
 *   dump 1  ROI       ROI_BEGIN .. ROI_END        (the measured kernel only)
 *   dump 2  epilogue  ROI_END .. exit             (validation + output)
 *
 * The ROI dump is identical to wrapping the kernel in m5_reset_stats() /
 * m5_dump_stats(); dumping (not just resetting) at ROI_BEGIN additionally
 * preserves the prologue so whole-program totals can still be reconstructed.
 *
 * Without -DUSE_M5 the markers are no-ops, so binaries also run natively/QEMU.
 */
#ifndef SPARSE_RVV_ROI_H
#define SPARSE_RVV_ROI_H

#if defined(ROI_NOOP_CALL)
/* Control build: same call sites, but no m5op is executed. */
static __attribute__((noinline)) void roi_noop(void) { __asm__ volatile("" ::: "memory"); }
#define ROI_BEGIN() roi_noop()
#define ROI_END()   roi_noop()
#elif defined(USE_M5) && defined(ROI_RESET_DUMP)
/* Alternative layout: 2 dumps (roi, then reset..exit); prologue is discarded. */
#include <gem5/m5ops.h>
#define ROI_BEGIN() m5_reset_stats(0, 0)
#define ROI_END()   m5_dump_stats(0, 0)
#elif defined(USE_M5)
#include <gem5/m5ops.h>
#define ROI_BEGIN() m5_dump_reset_stats(0, 0)
#define ROI_END()   m5_dump_reset_stats(0, 0)
#else
#define ROI_BEGIN() ((void)0)
#define ROI_END()   ((void)0)
#endif

#endif /* SPARSE_RVV_ROI_H */
