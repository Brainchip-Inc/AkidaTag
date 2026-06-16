# Current Measurement During Static-Frame Inference

How the firmware runs a **static-frame inference burst** from the shell and, on the
Spark board, measures the **0V8 (Akida core) rail current** during each inference.
Reflects the implementation on the `test/current_measure` branch.

## Overview

A shell command drives a fixed number of inferences using a stored test vector
(`kws_inputs`) — no live audio/MFCC. The burst is board-neutral; current
measurement is Spark-only:

- **Spark (async Akida)** — feeds the static frame via `akida_enqueue`, samples the
  0V8 rail current during each inference, and logs per-event and per-run stats over
  UART.
- **DK / sync mode** — runs the **same** static frame via the blocking
  `akida_forward`, with **no** current measurement (the current-IC code is not even
  compiled into the DK build; `CONFIG_SPARK_BOARD=n`).

## Commands

```
cmeas_start [N]        # run N static-frame inferences (default INF_RUN_DEFAULT_N = 1000)
cmeas_stop             # abort an in-progress run
cmeas_thresh [hi lo]   # view / set the hi & lo current-band thresholds (whole mA)
cmeas_sample [on|off]  # DEBUG: toggle SAADC sampling during the bench
```

`inference_bench_arm()` errors: `-EINVAL` (`N == 0`/unparseable), `-ENOTSUP`
(`Akida model not loaded`), `-EBUSY` (run already active).

## Sampling — hardware-timed SAADC (zero CPU per sample)

The 0V8 current is sampled by the **nRF5340 SAADC's internal hardware timer +
EasyDMA**, driven by raw **nrfx** (the Zephyr ADC driver is disabled:
`CONFIG_ADC_NRFX_SAADC=n`, `CONFIG_NRFX_SAADC=y`). Per inference:

1. `inference_current_start()` (bench only) arms SAADC advanced mode with
   `internal_timer_cc = 16 × INF_SAMPLE_PERIOD_US` and triggers it. The hardware
   timer samples into a RAM buffer (`inf_samples[]`) with **zero CPU per sample**.
2. At the Akida done-IRQ, `inference_current_stop()` calls `nrfx_saadc_abort()`; the
   nrfx `DONE` event reports the collected count.
3. `inference_current_dump()` waits for completion, **trims** the buffer to the real
   inference window (`inf_time`, the `time_ms` enqueue→fetch measurement), then
   reduces it to stats in one batch pass (thread context).

The sampler only runs while a `cmeas_start` run is active — **live inference is never
sampled** (no overhead, no UART spam). The BLE current-streaming thread
(`current_data_thread`) and battery reads use nrfx **simple blocking** mode.

## Signal chain & conversion

INA190A1 shunt monitor → SAADC (12-bit, single-ended, gain 1/3, internal 0.6 V ref
→ 1.8 V full scale):

```
raw 12-bit code → mV (1800 / 4095) → mA (/ SHUNT_RESISTOR_GAIN_0V8 = 25 × 0.2 = 5)
```

≈ 0.088 mA/code, ≈ 360 mA full scale.

| Rail | SAADC ch | Pin | Shunt gain | Per-inference sampling |
|---|---|---|---|---|
| 1V8_AKD | CH0 | AIN0 | 25 × 1.0 = 25 | no |
| 0V8_AKD | CH1 | AIN1 | 25 × 0.2 = 5  | **yes** |

## Power & energy

Rail voltage is taken as the nominal constant `RAIL_VOLTAGE_0V8 = 0.800 V` (the
chain measures current only). `power_mw = V × avg`; `energy = power × time`
(`mW × ms = µJ`). Reported per inference (`pwr`, `E`), as a whole-capture figure
(`whole`), and as run totals (`E_total`, `E_avg`, `Pavg`).

## Output reference (Spark)

```
INF[0V8] N=<n> inf_time=<ms> Tsamp=<us> avg=<mA> min=<mA> max=<mA> at=<ms> spike=<0|1>
       | tail_N=<n> tail_min=<mA> tail_avg=<mA> tail_max=<mA> tail_spike=<0|1>
       | dma=<us> | pwr=<mW> E=<uJ> | hi=<n> lo=<n>
INF[0V8] max_ctx idx=<i>:  <5 before> [<max>] <5 after> mA
INF[0V8] peaks max2=<mA> at=<ms> | near(>=max-50mA)=<n> run=<n>@<ms>
INF[0V8] max2_ctx idx=<i>: <5 before> [<max2>] <5 after> mA
INF[0V8] first20: <first 20 samples> mA
INF[0V8] last20:  <last 20 samples>  mA
INF[0V8] whole N=<collected> dur=<ms> avg=<mA> pwr=<mW> E=<uJ>
INF[0V8] dbg hw=<us> wake=<us>            # DEBUG (see below)
```

| Field | Meaning |
|---|---|
| `N` | samples in the trimmed inference window (`inf_time / Tsamp`) |
| `inf_time` | measured enqueue→fetch time (ms) |
| `Tsamp` | effective per-sample period (= `inf_time / N`, ≈ `INF_SAMPLE_PERIOD_US`) |
| `avg`/`min`/`max` + `at` | full-window current stats and the time of the peak |
| `spike` | 1 if `max > avg + INF_SPIKE_THRESH_MA` |
| `tail_min`/`avg`/`max` | last `INF_TAIL_LEN` samples (≈ the Akida compute phase) |
| `dma` | Akida compute time (µs) from the 400 MHz clock counter |
| `pwr`/`E` | mean power (mW) and energy (µJ) for the inference |
| `hi`/`lo` | samples above `INF_HI_THRESH_MA` / below `INF_LO_THRESH_MA` |
| `max_ctx`/`max2_ctx` | ±5 samples around the top two peaks (peak bracketed) |
| `peaks` | 2nd-highest sample + near-max cluster (count + longest back-to-back run) |
| `whole` | avg/power/energy over **all** collected samples (HW-exact duration) |

`RUN_DONE` aggregates the run: run-wide avg/min/max + tail, `spike_events`,
`dma_avg`, `Tsamp_avg`, `E_total`/`E_avg`/`Pavg`, and `hi>`/`lo<` band counts with
percentages. (All floats are printed manually — this build's `printk` lacks `%f`.)

On DK the only output is `RUN_START N=<n>` … `RUN_DONE N=<n>`.

## Tuning constants — [current_ic.h](../source/include/current_ic/current_ic.h)

| Constant | Value | Note |
|---|---|---|
| `INF_SAMPLE_PERIOD_US` | 40 | HW timer cc = 16 × 40 = 640 (valid 80–2047 → 5–127 µs); ~7 samples in the ~285 µs compute |
| `INF_CAP_WINDOW_US` | 25000 | capture window cap (samples = window / period, ≤ buffer) |
| `INF_MAX_SAMPLES` | 512 | EasyDMA buffer (int16) |
| `INF_TAIL_LEN` | 6 | tail window = 6 × 40 = 240 µs ≈ compute phase |
| `INF_SPIKE_THRESH_MA` | 50 | spike = excess over mean; also the near-max band |
| `INF_HI_/LO_THRESH_MA` | 120 / 50 | hi/lo band counters (runtime-settable via `cmeas_thresh`) |
| `RAIL_VOLTAGE_0V8` | 0.800 V | assumed rail voltage for power/energy |

## Known limitation / open item

During a `cmeas` run the measured inference reads **~4 ms longer** than production
(~12 ms → ~16 ms). Investigation showed this is **not** CPU/abort cost (the HW timer
uses zero CPU; the fetch-thread wake delay is ~50 µs) — the Akida HW inference itself
runs longer while the SAADC is active. Production/live inference is **unaffected**
(sampler gated off). Use `cmeas_sample off` then `cmeas_start` and read the
`dbg hw=` line to compare with sampling disabled.

> The `dbg hw=/wake=` line and the `cmeas_sample` toggle are temporary diagnostics
> for the timing investigation and will be removed once it is concluded.

## Key files

| Area | File / symbol |
|---|---|
| CLI | [inference_bench_cli.c](../source/core/common/inference_bench_cli.c) — `cmeas_start`/`cmeas_stop` |
| Bench entry | [main.cpp](../source/apps/demo_apps/main.cpp) — `inference_bench_arm/abort/active`, `do_inference` |
| Dispatch | [audio_processor.c](../source/core/common/audio/audio_processor.c) — `audio_processor_bench_tick` |
| Sampler | [current_ic.c](../source/core/interface/current_ic/current_ic.c) — `inference_current_start/stop/dump`, run aggregator, `cmeas_thresh`/`cmeas_sample` |
| Done-IRQ | [gpio.c](../source/core/interface/gpio/gpio.c) — `akd_async_isr_handler` |
| Config | [prj.conf](../source/prj.conf) — `CONFIG_NRFX_SAADC`; build gating in [CMakeLists.txt](../source/CMakeLists.txt) (`CONFIG_SPARK_BOARD`) |
