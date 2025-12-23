#include "boot_manager.h"
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <stdint.h>

LOG_MODULE_REGISTER(boot_manager, LOG_LEVEL_INF);
static uint32_t boot_count = 0;

void confirm_image_if_needed(void)
{
    if (!boot_is_img_confirmed()) {
        printk("Confirming image\n");
        int rc = boot_write_img_confirmed();
        if (rc) {
            printk("Image confirm failed: %d\n", rc);
        }
    }
	else{
		printk("Image is already confirmed\n");
	}
}


/* Callback: Triggered when settings_load() finds our 'boot/count' key */
static int boot_handler_set(const char *name, uint32_t len, settings_read_cb read_cb, void *cb_arg)
{
    const char *next;
    if (settings_name_steq(name, "count", &next) && !next) {
        if (len == sizeof(boot_count)) {
            read_cb(cb_arg, &boot_count, sizeof(boot_count));
            return 0; // Success
        }
        return -EINVAL; // Data in flash is wrong size
    }
    return -ENOENT; // Key not recognized by this handler
}

/* Define the structure for the 'boot' settings subtree */
static struct settings_handler boot_conf = {
    .name = "boot",
    .h_set = boot_handler_set
};


int init_setting_sub_system(void)
{
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

int init_boot_count(void)
{

    /* 1. Increment and save the new count */
    boot_count++;
    LOG_INF("--- Device Bootup Count: %u ---", boot_count);

    int rc = settings_save_one("boot/count", &boot_count, sizeof(boot_count));
    if (rc) {
        LOG_ERR("Save failed: %d", rc);
    }

    return rc;
}


