#include "boot_manager.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(boot_manager, LOG_LEVEL_INF);

static uint32_t total_boot_count = 0;
static uint32_t fw_boot_count = 0;
static uint32_t wdt_boot_count = 0;
static struct mcuboot_img_sem_ver stored_fw_ver;
static bool fw_ver_loaded = false;

/* In an MCUboot-based system, a new image is often booted in a "test" mode.
If the application does not confirm itself during this first boot,
the bootloader will automatically revert to the previous version upon the next
reset. This function prevents that rollback by writing a confirmation flag to
the image trailer in flash */
void confirm_image_if_needed(void) {
  if (!boot_is_img_confirmed()) {
    LOG_INF("New image detected. Confirming to prevent revert...");
    int rc = boot_write_img_confirmed();
    if (rc) {
      LOG_ERR("Image confirm failed: %d\n", rc);
    }
  } else {
    LOG_INF("Image is already confirmed\n");
  }
}

/*
Settings handler callback for the 'boot' subtree.
Loads persisted boot counters and firmware version from NVS.
*/
static int boot_handler_set(const char *name, size_t len,
                            settings_read_cb read_cb, void *cb_arg) {
  const char *next;

  if (settings_name_steq(name, "total_count", &next) && !next) {
    if (len != sizeof(total_boot_count)) {
      return -EINVAL;
    }
    read_cb(cb_arg, &total_boot_count, sizeof(total_boot_count));
    return 0;
  }
  if (settings_name_steq(name, "fw_count", &next) && !next) {
    if (len != sizeof(fw_boot_count)) {
      return -EINVAL;
    }
    read_cb(cb_arg, &fw_boot_count, sizeof(fw_boot_count));
    return 0;
  }
  if (settings_name_steq(name, "wdt_count", &next) && !next) {
    if (len != sizeof(wdt_boot_count)) {
      return -EINVAL;
    }
    read_cb(cb_arg, &wdt_boot_count, sizeof(wdt_boot_count));
    return 0;
  }
  if (settings_name_steq(name, "fw_ver", &next) && !next) {
    if (len != sizeof(stored_fw_ver)) {
      return -EINVAL;
    }
    read_cb(cb_arg, &stored_fw_ver, sizeof(stored_fw_ver));
    fw_ver_loaded = true;
    return 0;
  }

  /* Legacy keys from previous implementation — accept silently */
  if (settings_name_steq(name, "count", &next) && !next) {
    return 0;
  }
  if (settings_name_steq(name, "build_id", &next) && !next) {
    return 0;
  }

  return -ENOENT;
}

/* Define the structure for the 'boot' settings subtree */
static struct settings_handler boot_conf = {.name = "boot",
                                            .h_set = boot_handler_set};

/*
This function is a wrapper used to initialize the persistent storage
subsystem and register custom settings handlers.
*/
int init_setting_sub_system(void) {
  int rc;
  /* 1. Initialize the subsystem */
  rc = settings_subsys_init();
  if (rc) {
    LOG_ERR("Settings subsys init failed: %d", rc);
    return rc;
  }

  /* 2. Register our module's handler */
  settings_register(&boot_conf);

  return rc;
}

/*
Manages three persistent boot counters:
  - total_boot_count:  lifetime counter, never resets
  - fw_boot_count:     per-firmware counter, resets on version change
  - wdt_boot_count:    per-firmware watchdog reset counter
Firmware version changes are detected via MCUboot image header semver.
Watchdog resets are detected via Zephyr hwinfo API.
*/
int init_boot_count(void) {
  int rc = 0;

  /* 1. Read current firmware version from MCUboot image header */
  struct mcuboot_img_header header;
  struct mcuboot_img_sem_ver current_ver = {0};

  if (boot_read_bank_header(FLASH_AREA_ID(image_0), &header, sizeof(header)) ==
      0) {
    current_ver = header.h.v1.sem_ver;
  } else {
    LOG_ERR("Failed to read MCUboot header; version check skipped");
  }

  /* 2. Detect firmware version change */
  bool fw_changed = !fw_ver_loaded || memcmp(&stored_fw_ver, &current_ver,
                                             sizeof(current_ver)) != 0;

  if (fw_changed) {
    LOG_INF("Firmware version change detected: %d.%d.%d+%d -> %d.%d.%d+%d",
            stored_fw_ver.major, stored_fw_ver.minor, stored_fw_ver.revision,
            stored_fw_ver.build_num, current_ver.major, current_ver.minor,
            current_ver.revision, current_ver.build_num);
    fw_boot_count = 0;
    wdt_boot_count = 0;
    stored_fw_ver = current_ver;
    fw_ver_loaded = true;
    settings_save_one("boot/fw_ver", &stored_fw_ver, sizeof(stored_fw_ver));
  }

  /* 3. Check if this boot was caused by a watchdog reset */
  uint32_t reset_cause = 0;

  if (hwinfo_get_reset_cause(&reset_cause) == 0) {
    if (reset_cause & RESET_WATCHDOG) {
      wdt_boot_count++;
      LOG_INF("Watchdog reset detected (cause: 0x%08x)", reset_cause);
    }
    hwinfo_clear_reset_cause();
  } else {
    LOG_WRN("hwinfo_get_reset_cause() failed");
  }

  /* 4. Increment boot counters */
  total_boot_count++;
  fw_boot_count++;

  LOG_INF("Boot counts — total: %u, firmware: %u, watchdog: %u",
          total_boot_count, fw_boot_count, wdt_boot_count);

  /* 5. Persist all counters */
  rc |= settings_save_one("boot/total_count", &total_boot_count,
                          sizeof(total_boot_count));
  rc |=
      settings_save_one("boot/fw_count", &fw_boot_count, sizeof(fw_boot_count));
  rc |= settings_save_one("boot/wdt_count", &wdt_boot_count,
                          sizeof(wdt_boot_count));

  if (rc) {
    LOG_ERR("Failed to save boot counters: %d", rc);
  }

  return rc;
}

void print_image_version(uint8_t area_id, const char *name) {
  struct mcuboot_img_header header;

  if (boot_read_bank_header(area_id, &header, sizeof(header)) == 0) {
    LOG_INF("%s Version: %d.%d.%d+%d\n", name, header.h.v1.sem_ver.major,
            header.h.v1.sem_ver.minor, header.h.v1.sem_ver.revision,
            header.h.v1.sem_ver.build_num);
  } else {
    LOG_ERR("Failed to read %s image header\n", name);
  }
}

uint32_t boot_count_get_total(void) { return total_boot_count; }
uint32_t boot_count_get_firmware(void) { return fw_boot_count; }
uint32_t boot_count_get_watchdog(void) { return wdt_boot_count; }
