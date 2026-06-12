/* core/common/inference_bench_cli.c
 *
 * Shell front-end for the inference-bench feature (cmeas_start / cmeas_stop).
 * Always compiled — works on both Spark and DK builds by delegating to the
 * board-neutral inference_bench_arm/abort entry points. On Spark these wire
 * up the current sampler aggregator; on DK they only drive the inference
 * burst and skip current measurement entirely.
 */

#include <errno.h>
#include <stdlib.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include "current_ic/current_ic.h"      /* for INF_RUN_DEFAULT_N */
#include "current_ic/inference_bench.h" /* for inference_bench_arm/abort */

static int cmd_cmeas_start(const struct shell *sh, size_t argc, char **argv) {
  uint32_t n = INF_RUN_DEFAULT_N;

  if (argc > 2) {
    shell_error(sh, "usage: cmeas_start [N]");
    return -EINVAL;
  }
  if (argc == 2) {
    char *endptr;
    unsigned long parsed = strtoul(argv[1], &endptr, 10);
    if (*endptr != '\0' || parsed == 0UL || parsed > 0xFFFFFFFFUL) {
      shell_error(sh, "invalid N: %s", argv[1]);
      return -EINVAL;
    }
    n = (uint32_t)parsed;
  }

  int ret = inference_bench_arm(n);
  if (ret == -EBUSY) {
    shell_error(sh, "bench already running (use cmeas_stop to abort)");
    return ret;
  }
  if (ret == -ENOTSUP) {
    shell_error(sh, "bench unavailable: Akida model not loaded");
    return ret;
  }
  if (ret < 0) {
    shell_error(sh, "bench arm failed: %d", ret);
    return ret;
  }
  return 0;
}

static int cmd_cmeas_stop(const struct shell *sh, size_t argc, char **argv) {
  ARG_UNUSED(sh);
  ARG_UNUSED(argv);
  if (argc != 1) {
    shell_error(sh, "usage: cmeas_stop");
    return -EINVAL;
  }
  inference_bench_abort();
  return 0;
}

SHELL_CMD_REGISTER(cmeas_start, NULL,
                   "Start inference bench: cmeas_start [N] (default 1000)",
                   cmd_cmeas_start);
SHELL_CMD_REGISTER(cmeas_stop, NULL, "Abort an in-progress inference bench",
                   cmd_cmeas_stop);
