#include "boot_manager.h"
#include <stdint.h>
#include <string.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(boot_manager, LOG_LEVEL_INF);

#define BUILD_ID (__DATE__ " " __TIME__) /* changes on every rebuild */

static uint32_t boot_count = 0;
static char stored_build_id[24] = {0};

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
This Callback function is a Settings Handler callback used to retrieve value of
'boot/count' key from non-volatile storage during system initialization.
*/
static int boot_handler_set(const char *name, uint32_t len,
                            settings_read_cb read_cb, void *cb_arg) {
  const char *next;
  if (settings_name_steq(name, "count", &next) && !next) {
    if (len == sizeof(boot_count)) {
      read_cb(cb_arg, &boot_count, sizeof(boot_count));
      return 0;
    }
    return -EINVAL;
  }
  if (settings_name_steq(name, "build_id", &next) && !next) {
    if (len < sizeof(stored_build_id)) {
      read_cb(cb_arg, stored_build_id, len);
      stored_build_id[len] = '\0';
      return 0;
    }
    return -EINVAL;
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
This function manages the persistence of a boot cycle counter.
It increments the current runtime counter and commits the updated value to
non-volatile storage (Flash).  Resets the counter when a new firmware build
is detected (compile-time BUILD_ID changes on every rebuild).
*/
int init_boot_count(void) {
  if (strcmp(stored_build_id, BUILD_ID) != 0) {
    LOG_INF("New build detected: '%s' -> '%s'. Resetting boot count.",
            stored_build_id, BUILD_ID);
    boot_count = 0;
    settings_save_one("boot/build_id", BUILD_ID, strlen(BUILD_ID));
  }

  boot_count++;
  LOG_INF("--- Device Bootup Count: %u ---", boot_count);

  int rc = settings_save_one("boot/count", &boot_count, sizeof(boot_count));
  if (rc) {
    LOG_ERR("Save failed: %d", rc);
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
