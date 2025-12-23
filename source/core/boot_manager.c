#include "boot_manager.h"
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <stdint.h>

LOG_MODULE_REGISTER(boot_manager, LOG_LEVEL_INF);


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

