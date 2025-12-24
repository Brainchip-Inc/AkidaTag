#ifndef BLE_INITIALIZATION_H
#define BLE_INITIALIZATION_H
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <bluetooth/services/lbs.h>

#include <zephyr/settings/settings.h>

#include <dk_buttons_and_leds.h>
int ble_init(void );
void prcess_led(void);
#endif /* BLE_INITIALIZATION_H */