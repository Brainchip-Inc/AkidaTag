#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <string.h>
#include <ble_services/file_transfer.h>

static bool notify_enabled = false;

static void ack_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    printk("ACK notify %s\n", notify_enabled ? "enabled" : "disabled");
}
// === BLE File Transfer Service ===
BT_GATT_SERVICE_DEFINE(file_transfer_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_FILE_TRANSFER_SERVICE),

	BT_GATT_CHARACTERISTIC(BT_UUID_FILE_TRANSFER_SIZE_CHAR,
		BT_GATT_CHRC_WRITE,
		BT_GATT_PERM_WRITE,
		NULL, get_file_size, NULL),

    // Write Characteristic (for receiving file chunks)
    BT_GATT_CHARACTERISTIC(BT_UUID_FILE_TRANSFER_CHAR,
        BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_WRITE,
        NULL, file_transfer_write, NULL),

    // Notify Characteristic (for sending ACKs)
    BT_GATT_CHARACTERISTIC(BT_UUID_FILE_TRANSFER_ACK_CHAR,
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_NONE,
        NULL, NULL, NULL),

    // CCC descriptor to enable central notification subscription
    BT_GATT_CCC(ack_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
   
	BT_GATT_CHARACTERISTIC(APP_CHAR_UUID,
		BT_GATT_CHRC_WRITE,
		BT_GATT_PERM_WRITE,
		NULL, get_app_index, NULL)    
);

int file_transfer_init(void)
{
	printf("File transfer service initialized (static definition)\n");
	return 0;
}

void send_ack_to_host(uint8_t ack_code)
{
    if (!notify_enabled) {
        printk("ACK notification skipped: notify not enabled by central\n");
        return;
    }

    uint8_t ack_data[1] = { ack_code };
    int err = bt_gatt_notify(NULL, &file_transfer_svc.attrs[5], ack_data, sizeof(ack_data));
    if (err) {
        printk("Failed to send ACK notification (err %d)\n", err);
    } else {
        printk("ACK (0x%02X) sent to central\n", ack_code);
    }
}

