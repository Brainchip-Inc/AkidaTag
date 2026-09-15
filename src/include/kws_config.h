#ifndef KWS_CONFIG_H
#define KWS_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  KWS_PARAM_RMS = 0,
  KWS_PARAM_DEBOUNCE_MS = 1,
  KWS_PARAM_SMOOTHING_ALPHA = 2,
  KWS_PARAM_SCORE_THRESHOLD = 3,
  KWS_PARAM_CHIMING = 4,
  KWS_PARAM_SPEECH_TIMEOUT = 5,
  KWS_PARAM_COUNT
} kws_param_id_t;

typedef enum {
  KWS_CFG_OK = 0,
  KWS_CFG_ERR_ID = -1,
  KWS_CFG_ERR_RANGE = -2,
  KWS_CFG_ERR_PARSE = -3,
  KWS_CFG_ERR_NVS = -4,
} kws_cfg_err_t;

/* Call once during boot, right after init_setting_sub_system() and before
 * dmic_init(). Registers the "kws" settings subtree and loads persisted
 * values into the existing globals. On firmware change (detected via the
 * MCUboot sem_ver marker at NVS key "kws/fw_ver") resets every parameter
 * back to its compile-time default. */
int kws_config_init(void);

/* Apply a value by numeric id. Parses value_str into the correct numeric
 * type for the id, range-checks it, stops the DMIC, writes the global,
 * resets stale inference state, persists to NVS, restarts the DMIC. The
 * stop/start window is ~READ_TIMEOUT (120 ms). */
kws_cfg_err_t kws_config_set_from_string(kws_param_id_t id,
                                         const char *value_str);

/* Restore all six parameters to compile-time defaults, persist to NVS,
 * update kws/fw_ver, and bounce the DMIC once. Used by the UART "app reset"
 * shell command and the BLE CMD_CONFIG RESET payload. */
kws_cfg_err_t kws_config_reset_to_defaults(void);

/* Format "<id>:<value>" into out_buf (no trailing newline). Returns bytes
 * written (excluding NUL) on success, negative on error. */
int kws_config_format(kws_param_id_t id, char *out_buf, size_t out_len);

/* Main.cpp calls this once, right after start_dmic_audio_proc() has fired.
 * Before this call, kws_config_set_from_string() / _reset_to_defaults()
 * skip the stop/start DMIC dance because the hardware isn't running yet. */
void kws_config_notify_dmic_started(void);

/* Companion to kws_config_notify_dmic_started(). Called by kws_app_stop()
 * so parameter SETs while the pipeline is stopped skip the DMIC bounce. */
void kws_config_notify_dmic_stopped(void);

#ifdef __cplusplus
}
#endif

#endif /* KWS_CONFIG_H */
