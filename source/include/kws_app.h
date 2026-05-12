#ifndef KWS_APP_H
#define KWS_APP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start the KWS inference pipeline. Triggers the DMIC, resets smoothing /
 * DC-filter state, sets cur_kws_edge_state = STATE_INFERENCE, and notifies
 * kws_config so parameter setters will bounce the DMIC again.
 *
 * Idempotent: no-op if already running. Safe to call only *after*
 * start_dmic_audio_proc() has run once at boot (which configures the DMIC
 * and creates the capture / process threads). Returns 0 on success. */
int kws_app_start(void);

/* Stop the KWS inference pipeline. Halts the DMIC (drains the buffer queue),
 * notifies kws_config that the DMIC is off, and sets
 * cur_kws_edge_state = STATE_STOPPED. Keeps mfcc state alive so a subsequent
 * kws_app_start() is instantaneous.
 *
 * Idempotent: no-op if already stopped. Returns 0 on success. */
int kws_app_stop(void);

/* True when the pipeline is currently running. */
bool kws_app_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* KWS_APP_H */
