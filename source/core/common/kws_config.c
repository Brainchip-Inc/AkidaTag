#include "kws_config.h"

#include "audio/pdm_mic.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(kws_config, CONFIG_LOG_DEFAULT_LEVEL);

/* Globals owned by other translation units. rms_threshold and
 * speech_active_time_ms are declared extern in audio_processor.h but the rest
 * live in main.cpp and have no existing header — declare them here. */
extern int rms_threshold;
extern int speech_active_time_ms;
extern uint32_t kws_debounce_time;
extern float smoothing_alpha;
extern float score_threshold;
extern int chiming_threshold;

/* From main.cpp — clears smoothed_scores[] and chiming_counters[]. Declared
 * extern "C" at the definition site so C linkage is fine. */
extern void reset_stale_inference_data(void);

#define DEFAULT_RMS_THRESHOLD     550
#define DEFAULT_DEBOUNCE_MS       300u
#define DEFAULT_SMOOTHING_ALPHA   0.70f
#define DEFAULT_SCORE_THRESHOLD   0.50f
#define DEFAULT_CHIMING_THRESHOLD 3
#define DEFAULT_SPEECH_TIMEOUT_MS 1300

#define NVS_KEY_RMS      "rms"
#define NVS_KEY_DEBOUNCE "debounce"
#define NVS_KEY_ALPHA    "alpha"
#define NVS_KEY_SCORE    "score"
#define NVS_KEY_CHIMING  "chiming"
#define NVS_KEY_SPEECH   "speech"
#define NVS_KEY_FW_VER   "fw_ver"

static struct mcuboot_img_sem_ver stored_fw_ver;
static bool fw_ver_loaded = false;

/* Tracks whether the DMIC has been kicked off by start_dmic_audio_proc().
 * Parameter setters must only stop/start the DMIC once it's actually running
 * — during kws_config_init() the hardware hasn't been configured yet. Flipped
 * by kws_config_notify_dmic_started(), which main.cpp calls after DMIC init. */
static bool dmic_running = false;

void kws_config_notify_dmic_started(void) { dmic_running = true; }
void kws_config_notify_dmic_stopped(void) { dmic_running = false; }

static int kws_handler_set(const char *name, size_t len,
                           settings_read_cb read_cb, void *cb_arg) {
  const char *next;

  if (settings_name_steq(name, NVS_KEY_RMS, &next) && !next) {
    if (len != sizeof(rms_threshold)) return -EINVAL;
    read_cb(cb_arg, &rms_threshold, sizeof(rms_threshold));
    return 0;
  }
  if (settings_name_steq(name, NVS_KEY_DEBOUNCE, &next) && !next) {
    if (len != sizeof(kws_debounce_time)) return -EINVAL;
    read_cb(cb_arg, &kws_debounce_time, sizeof(kws_debounce_time));
    return 0;
  }
  if (settings_name_steq(name, NVS_KEY_ALPHA, &next) && !next) {
    if (len != sizeof(smoothing_alpha)) return -EINVAL;
    read_cb(cb_arg, &smoothing_alpha, sizeof(smoothing_alpha));
    return 0;
  }
  if (settings_name_steq(name, NVS_KEY_SCORE, &next) && !next) {
    if (len != sizeof(score_threshold)) return -EINVAL;
    read_cb(cb_arg, &score_threshold, sizeof(score_threshold));
    return 0;
  }
  if (settings_name_steq(name, NVS_KEY_CHIMING, &next) && !next) {
    if (len != sizeof(chiming_threshold)) return -EINVAL;
    read_cb(cb_arg, &chiming_threshold, sizeof(chiming_threshold));
    return 0;
  }
  if (settings_name_steq(name, NVS_KEY_SPEECH, &next) && !next) {
    if (len != sizeof(speech_active_time_ms)) return -EINVAL;
    read_cb(cb_arg, &speech_active_time_ms, sizeof(speech_active_time_ms));
    return 0;
  }
  if (settings_name_steq(name, NVS_KEY_FW_VER, &next) && !next) {
    if (len != sizeof(stored_fw_ver)) return -EINVAL;
    read_cb(cb_arg, &stored_fw_ver, sizeof(stored_fw_ver));
    fw_ver_loaded = true;
    return 0;
  }

  return -ENOENT;
}

static struct settings_handler kws_conf = {
    .name = "kws",
    .h_set = kws_handler_set,
};

static void apply_defaults_to_globals(void) {
  rms_threshold = DEFAULT_RMS_THRESHOLD;
  kws_debounce_time = DEFAULT_DEBOUNCE_MS;
  smoothing_alpha = DEFAULT_SMOOTHING_ALPHA;
  score_threshold = DEFAULT_SCORE_THRESHOLD;
  chiming_threshold = DEFAULT_CHIMING_THRESHOLD;
  speech_active_time_ms = DEFAULT_SPEECH_TIMEOUT_MS;
}

static int persist_all(void) {
  int rc = 0;
  rc |= settings_save_one("kws/" NVS_KEY_RMS, &rms_threshold,
                          sizeof(rms_threshold));
  rc |= settings_save_one("kws/" NVS_KEY_DEBOUNCE, &kws_debounce_time,
                          sizeof(kws_debounce_time));
  rc |= settings_save_one("kws/" NVS_KEY_ALPHA, &smoothing_alpha,
                          sizeof(smoothing_alpha));
  rc |= settings_save_one("kws/" NVS_KEY_SCORE, &score_threshold,
                          sizeof(score_threshold));
  rc |= settings_save_one("kws/" NVS_KEY_CHIMING, &chiming_threshold,
                          sizeof(chiming_threshold));
  rc |= settings_save_one("kws/" NVS_KEY_SPEECH, &speech_active_time_ms,
                          sizeof(speech_active_time_ms));
  return rc;
}

int kws_config_init(void) {
  int rc = settings_register(&kws_conf);
  if (rc) {
    LOG_ERR("settings_register(kws) failed: %d", rc);
    return rc;
  }

  rc = settings_load_subtree("kws");
  if (rc) {
    LOG_WRN("settings_load_subtree(kws) returned %d — using defaults", rc);
  }

  struct mcuboot_img_header header;
  struct mcuboot_img_sem_ver current_ver = {0};
  if (boot_read_bank_header(FLASH_AREA_ID(image_0), &header, sizeof(header)) ==
      0) {
    current_ver = header.h.v1.sem_ver;
  } else {
    LOG_WRN("boot_read_bank_header failed — treating as firmware change");
  }

  bool fw_changed =
      !fw_ver_loaded ||
      memcmp(&stored_fw_ver, &current_ver, sizeof(current_ver)) != 0;

  if (fw_changed) {
    LOG_INF("kws_config: firmware change detected, resetting to defaults");
    apply_defaults_to_globals();
    int prc = persist_all();
    if (prc) {
      LOG_ERR("kws_config: persist_all failed: %d", prc);
    }
    int vrc = settings_save_one("kws/" NVS_KEY_FW_VER, &current_ver,
                                sizeof(current_ver));
    if (vrc) {
      LOG_ERR("kws_config: save fw_ver failed: %d", vrc);
    }
    stored_fw_ver = current_ver;
    fw_ver_loaded = true;
  } else {
    LOG_INF("kws_config: loaded persisted values (same firmware)");
  }

  return 0;
}

static int parse_int(const char *s, int *out) {
  if (!s || !*s) return -1;
  char *end = NULL;
  errno = 0;
  long v = strtol(s, &end, 10);
  if (errno == ERANGE || end == s) return -1;
  while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
  if (*end != '\0') return -1;
  *out = (int)v;
  return 0;
}

static int parse_uint32(const char *s, uint32_t *out) {
  int v;
  if (parse_int(s, &v) != 0) return -1;
  if (v < 0) return -1;
  *out = (uint32_t)v;
  return 0;
}

static int parse_float(const char *s, float *out) {
  if (!s || !*s) return -1;
  char *end = NULL;
  errno = 0;
  float v = strtof(s, &end);
  if (errno == ERANGE || end == s) return -1;
  while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
  if (*end != '\0') return -1;
  *out = v;
  return 0;
}

/* Stop the DMIC (drain buffers), clear EMA / DC state, restart the DMIC.
 * Only runs when the pipeline is actually active; during init the audio
 * thread hasn't been started yet and there is nothing to stop. */
static void pipeline_bounce_begin(void) {
  if (!dmic_running) return;
  stop_dmic();
}

static void pipeline_bounce_end(void) {
  if (!dmic_running) return;
  reset_stale_inference_data();
  dmic_reset_dc_state();
  int rc = dmic_start();
  if (rc) {
    LOG_ERR("dmic_start after config change failed: %d", rc);
  }
}

kws_cfg_err_t kws_config_set_from_string(kws_param_id_t id,
                                         const char *value_str) {
  if (id >= KWS_PARAM_COUNT) {
    return KWS_CFG_ERR_ID;
  }
  if (!value_str) {
    return KWS_CFG_ERR_PARSE;
  }

  /* Parse + validate up front. If anything fails we return without touching
   * the global, so a bad BLE payload cannot half-apply. */
  int v_i = 0;
  uint32_t v_u = 0;
  float v_f = 0.0f;

  switch (id) {
  case KWS_PARAM_RMS:
    if (parse_int(value_str, &v_i) != 0) return KWS_CFG_ERR_PARSE;
    if (v_i < 0) return KWS_CFG_ERR_RANGE;
    break;
  case KWS_PARAM_DEBOUNCE_MS:
    if (parse_uint32(value_str, &v_u) != 0) return KWS_CFG_ERR_PARSE;
    break;
  case KWS_PARAM_SMOOTHING_ALPHA:
    if (parse_float(value_str, &v_f) != 0) return KWS_CFG_ERR_PARSE;
    if (v_f < 0.0f || v_f > 1.0f) return KWS_CFG_ERR_RANGE;
    break;
  case KWS_PARAM_SCORE_THRESHOLD:
    if (parse_float(value_str, &v_f) != 0) return KWS_CFG_ERR_PARSE;
    if (v_f < 0.0f || v_f > 1.0f) return KWS_CFG_ERR_RANGE;
    break;
  case KWS_PARAM_CHIMING:
    if (parse_int(value_str, &v_i) != 0) return KWS_CFG_ERR_PARSE;
    if (v_i < 1) return KWS_CFG_ERR_RANGE;
    break;
  case KWS_PARAM_SPEECH_TIMEOUT:
    if (parse_int(value_str, &v_i) != 0) return KWS_CFG_ERR_PARSE;
    if (v_i < 0) return KWS_CFG_ERR_RANGE;
    break;
  default:
    return KWS_CFG_ERR_ID;
  }

  pipeline_bounce_begin();

  const void *nvs_src = NULL;
  size_t nvs_size = 0;
  const char *nvs_key = NULL;

  switch (id) {
  case KWS_PARAM_RMS:
    rms_threshold = v_i;
    nvs_src = &rms_threshold;
    nvs_size = sizeof(rms_threshold);
    nvs_key = "kws/" NVS_KEY_RMS;
    break;
  case KWS_PARAM_DEBOUNCE_MS:
    kws_debounce_time = v_u;
    nvs_src = &kws_debounce_time;
    nvs_size = sizeof(kws_debounce_time);
    nvs_key = "kws/" NVS_KEY_DEBOUNCE;
    break;
  case KWS_PARAM_SMOOTHING_ALPHA:
    smoothing_alpha = v_f;
    nvs_src = &smoothing_alpha;
    nvs_size = sizeof(smoothing_alpha);
    nvs_key = "kws/" NVS_KEY_ALPHA;
    break;
  case KWS_PARAM_SCORE_THRESHOLD:
    score_threshold = v_f;
    nvs_src = &score_threshold;
    nvs_size = sizeof(score_threshold);
    nvs_key = "kws/" NVS_KEY_SCORE;
    break;
  case KWS_PARAM_CHIMING:
    chiming_threshold = v_i;
    nvs_src = &chiming_threshold;
    nvs_size = sizeof(chiming_threshold);
    nvs_key = "kws/" NVS_KEY_CHIMING;
    break;
  case KWS_PARAM_SPEECH_TIMEOUT:
    speech_active_time_ms = v_i;
    nvs_src = &speech_active_time_ms;
    nvs_size = sizeof(speech_active_time_ms);
    nvs_key = "kws/" NVS_KEY_SPEECH;
    break;
  default:
    pipeline_bounce_end();
    return KWS_CFG_ERR_ID;
  }

  pipeline_bounce_end();

  int rc = settings_save_one(nvs_key, nvs_src, nvs_size);
  if (rc) {
    LOG_WRN("settings_save_one(%s) failed: %d (value still applied)", nvs_key,
            rc);
    return KWS_CFG_ERR_NVS;
  }
  return KWS_CFG_OK;
}

kws_cfg_err_t kws_config_reset_to_defaults(void) {
  pipeline_bounce_begin();
  apply_defaults_to_globals();
  pipeline_bounce_end();

  int rc = persist_all();

  struct mcuboot_img_header header;
  struct mcuboot_img_sem_ver current_ver = {0};
  if (boot_read_bank_header(FLASH_AREA_ID(image_0), &header, sizeof(header)) ==
      0) {
    current_ver = header.h.v1.sem_ver;
    int vrc = settings_save_one("kws/" NVS_KEY_FW_VER, &current_ver,
                                sizeof(current_ver));
    if (vrc) {
      LOG_WRN("settings_save_one(kws/fw_ver) failed: %d", vrc);
    }
    stored_fw_ver = current_ver;
    fw_ver_loaded = true;
  }

  return rc ? KWS_CFG_ERR_NVS : KWS_CFG_OK;
}

int kws_config_format(kws_param_id_t id, char *out_buf, size_t out_len) {
  if (!out_buf || out_len == 0) return -1;

  int n;
  switch (id) {
  case KWS_PARAM_RMS:
    n = snprintf(out_buf, out_len, "%d:%d", (int)id, rms_threshold);
    break;
  case KWS_PARAM_DEBOUNCE_MS:
    n = snprintf(out_buf, out_len, "%d:%u", (int)id, kws_debounce_time);
    break;
  case KWS_PARAM_SMOOTHING_ALPHA:
    n = snprintf(out_buf, out_len, "%d:%.2f", (int)id, (double)smoothing_alpha);
    break;
  case KWS_PARAM_SCORE_THRESHOLD:
    n = snprintf(out_buf, out_len, "%d:%.2f", (int)id, (double)score_threshold);
    break;
  case KWS_PARAM_CHIMING:
    n = snprintf(out_buf, out_len, "%d:%d", (int)id, chiming_threshold);
    break;
  case KWS_PARAM_SPEECH_TIMEOUT:
    n = snprintf(out_buf, out_len, "%d:%d", (int)id, speech_active_time_ms);
    break;
  default:
    return -1;
  }
  if (n < 0 || (size_t)n >= out_len) return -1;
  return n;
}
