#ifndef FALL_APP_H
#define FALL_APP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start fall/no-fall detection. if already running.
 * Returns 0 on success, a negative error code if no fall model is flashed
 * at APP_SLOT_FALL or programming failed. Builds with
 * CONFIG_IMU_ENABLE_THREAD=n always return -EINVAL. */
int fall_app_start(void);

/* Stop fall/no-fall detection and reprogram the mesh back to KWS (starting
 * the DMIC pipeline so KWS is actually listening again).
 * if fall detection is not running. Returns 0 on success. */
int fall_app_stop(void);

/* True when fall/no-fall detection is the currently active/running app —
 * i.e. the fall model is the one programmed on the Akida mesh right now. */
bool fall_app_is_running(void);

/* Number of output classes the fall model reports (2: no_fall, fall). */
int fall_class_count(void);

/* Class label for `index` (from fall_tags[], see sample_input/fall).*/
const char* fall_class_name(int index);

/* After calibration only fall detection must start. Runs the
 * full calibration flow (imu_calibrate_for_fall(): configure IMU at 208 Hz /
 * +-8g / 208 Hz / +-1000 dps, then calibrate accel+gyro offsets), marks
 * calibration done for this boot on success, and then calls fall_app_start()
 * so fall detection actually engages once calibrated.*/
int fall_calibrate_and_start(void);

/* True once fall_calibrate_and_start() has completed successfully at least
 * once since the last firmware boot. A plain RAM flag — reset only by an
 * actual reboot, never by the app disconnecting/reconnecting or by fall
 * detection being stopped and restarted*/
bool fall_calibration_is_done(void);

#ifdef __cplusplus
}
#endif

#endif /* FALL_APP_H */
