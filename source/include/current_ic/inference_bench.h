#ifndef __INFERENCE_BENCH_H__
#define __INFERENCE_BENCH_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Arm a fixed-N inference burst driven by the audio thread using the stored
 * test vector instead of live audio. Returns 0 on success, -EBUSY if a burst
 * is already in flight, -ENOTSUP if Akida is not in async mode, -EINVAL if
 * n == 0. */
int inference_bench_arm(uint32_t n);

/* Abort an in-flight burst. Idempotent. */
void inference_bench_abort(void);

/* True while a burst has remaining inferences. Polled by the audio path. */
bool inference_bench_active(void);

#ifdef __cplusplus
}
#endif

#endif
