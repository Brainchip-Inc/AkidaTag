#include "ble_services/file_transfer.h"
#include "akd_spi_flash_handler.h"
#include "led_init.h"
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>
LOG_MODULE_REGISTER(file_transfer, CONFIG_LOG_DEFAULT_LEVEL);

static ssize_t get_app_index(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr, const void *app,
                             uint16_t len, uint16_t offset, uint8_t flags);

static ssize_t file_transfer_write(struct bt_conn *conn,
                                   const struct bt_gatt_attr *attr,
                                   const void *buf, uint16_t len,
                                   uint16_t offset, uint8_t flags);
static ssize_t get_file_size(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr, const void *buf,
                             uint16_t len, uint16_t offset, uint8_t flags);

static ssize_t get_file_crc(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr, const void *buf,
                            uint16_t len, uint16_t offset, uint8_t flags);

// UUID definitions, the client must have the same UUIDs during communication
#define BT_UUID_FILE_TRANSFER_SERVICE_VAL                                      \
  BT_UUID_128_ENCODE(0xf000aa00, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_FILE_TRANSFER_CHAR_VAL                                         \
  BT_UUID_128_ENCODE(0xf000aa01, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_FILE_TRANSFER_ACK_CHAR_VAL                                     \
  BT_UUID_128_ENCODE(0xf000aa02, 0x0451, 0x4000, 0xb000, 0x000000000000)

/* this macro is unused in code */
#define BT_UUID_FILE_TRANSFER_CTRL_CHAR_VAL                                    \
  BT_UUID_128_ENCODE(0xf000aa03, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_FILE_TRANSFER_SIZE_CHAR_VAL                                    \
  BT_UUID_128_ENCODE(0xf000aa04, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define APP_CHAR_UUID_VAL                                                      \
  BT_UUID_128_ENCODE(0xf000aa05, 0x0451, 0x4000, 0xb000, 0x000000000000)
#define BT_UUID_FILE_CRC_CHAR_VAL                                              \
  BT_UUID_128_ENCODE(0xf000aa06, 0x0451, 0x4000, 0xb000, 0x000000000000)

// 1. Define the 128-bit structures
static struct bt_uuid_128 file_transfer_service_uuid =
    BT_UUID_INIT_128(BT_UUID_FILE_TRANSFER_SERVICE_VAL);
static struct bt_uuid_128 file_transfer_char_uuid =
    BT_UUID_INIT_128(BT_UUID_FILE_TRANSFER_CHAR_VAL);
static struct bt_uuid_128 file_transfer_ack_uuid =
    BT_UUID_INIT_128(BT_UUID_FILE_TRANSFER_ACK_CHAR_VAL);
static struct bt_uuid_128 file_transfer_size_uuid =
    BT_UUID_INIT_128(BT_UUID_FILE_TRANSFER_SIZE_CHAR_VAL);
static struct bt_uuid_128 app_char_uuid_struct =
    BT_UUID_INIT_128(APP_CHAR_UUID_VAL);
static struct bt_uuid_128 file_crc_uuid =
    BT_UUID_INIT_128(BT_UUID_FILE_CRC_CHAR_VAL);

// 2. Create the pointer macro for the GATT table
#define APP_CHAR_UUID_PTR (&app_char_uuid_struct.uuid)
// 2. Create pointers to the base 'bt_uuid' for use in GATT definitions
// Use these in your BT_GATT_SERVICE_DEFINE
#define FILE_SVC_UUID (&file_transfer_service_uuid.uuid)
#define FILE_CHAR_UUID (&file_transfer_char_uuid.uuid)
#define FILE_ACK_UUID (&file_transfer_ack_uuid.uuid)
#define FILE_SIZE_UUID (&file_transfer_size_uuid.uuid)
#define FILE_CRC_UUID (&file_crc_uuid.uuid)

volatile static size_t total_pgm_size = 0;
volatile static size_t ble_pgm_offset = 0;
volatile static size_t total_received = 0;
#define ACK_FLASH_ERASE_DONE 0xEE
#define ACK_FLASH_WRITE_DONE 0xCC
uint32_t app_flash_offset = AKD_FLASH_OFFSET;
static uint32_t expected_crc = 0;

/* Define SRAM buffer in a named section */
__attribute__((section(".sram_upload_buf"),
               used)) uint8_t sram_upload_buffer[BUFFER_SIZE];

static void send_ack_to_host(uint8_t ack_code);
static void ack_ccc_cfg_changed(const struct bt_gatt_attr *attr,
                                uint16_t value);
static bool notify_enabled = false;

BT_GATT_SERVICE_DEFINE(
    file_transfer_svc, BT_GATT_PRIMARY_SERVICE(FILE_SVC_UUID),

    BT_GATT_CHARACTERISTIC(FILE_SIZE_UUID, BT_GATT_CHRC_WRITE,
#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
                           BT_GATT_PERM_WRITE_ENCRYPT,
#else
                           BT_GATT_PERM_WRITE,
#endif
                           NULL, get_file_size, NULL),

    BT_GATT_CHARACTERISTIC(FILE_CHAR_UUID, BT_GATT_CHRC_WRITE,
#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
                           BT_GATT_PERM_WRITE_ENCRYPT,
#else
                           BT_GATT_PERM_WRITE,
#endif
                           NULL, file_transfer_write, NULL),

    BT_GATT_CHARACTERISTIC(FILE_ACK_UUID, BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE, NULL, NULL, NULL),

    BT_GATT_CCC(ack_ccc_cfg_changed,
#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT
#else
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE
#endif
                ),

    BT_GATT_CHARACTERISTIC(APP_CHAR_UUID_PTR, BT_GATT_CHRC_WRITE,
#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
                           BT_GATT_PERM_WRITE_ENCRYPT,
#else
                           BT_GATT_PERM_WRITE,
#endif

                           NULL, get_app_index, NULL),
    BT_GATT_CHARACTERISTIC(FILE_CRC_UUID, BT_GATT_CHRC_WRITE,
#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
                           BT_GATT_PERM_WRITE_ENCRYPT,
#else
                           BT_GATT_PERM_WRITE,
#endif
                           NULL, get_file_crc, NULL));

int file_transfer_init(void) {
  LOG_INF("File transfer service initialized (static definition)\n");
  return 0;
}

/* function to set the allocated buffer data to zero */
static void reset_buffer(void) {
  ble_pgm_offset = 0;
  memset(sram_upload_buffer, 0, sizeof(sram_upload_buffer));
}

/* CRC Init and verification utils */
typedef struct {
  uint32_t crc; // CRC computed value
  bool crc_ok;  // CRC status flag
} crc32_ctx_t;

crc32_ctx_t crc_ctx;

void crc32_init(crc32_ctx_t *ctx) {
  ctx->crc = 0xFFFFFFFF;
  ctx->crc_ok = true;
}

uint32_t crc32_finalize(crc32_ctx_t *ctx) { return ctx->crc ^ 0xFFFFFFFF; }

static ssize_t get_file_crc(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr, const void *buf,
                            uint16_t len, uint16_t offset, uint8_t flags) {
  if (len != 4) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  }

  memcpy(&expected_crc, buf, 4);
  LOG_INF("CRC write len=%d\n", len);

  LOG_INF("Expected CRC32: 0x%08X\n", expected_crc);
  return len;
}

/* This function implements a BLE service that receives the application index.
The client must send this request first before any other communication
 */
ssize_t get_app_index(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                      const void *app, uint16_t len, uint16_t offset,
                      uint8_t flags) {
  if ((uint32_t *)app == NULL) {
    LOG_ERR(" app index is NULL \n\r");
    return -1;
  }

  uint8_t app_index_local = *(uint8_t *)app;
  if (app_index_local > 1) {
    LOG_ERR("illegal app request: %d\n", app_index_local);
    return -1;
  }
  app_index = app_index_local;
  app_flash_offset = flash_offsets[app_index];

  /* Initialize CRC context */
  crc32_init(&crc_ctx);
  LOG_INF("CRC init is done, CRC = 0x%x \n\r", crc_ctx.crc);

  LOG_INF(" app index is %d \n\r", app_index);
  return len;
}

/* This function is a BLE service that receives data and its length from the BLE
 * client host application. The data is first stored in an SRAM buffer in chunks
 * of size BUFFER_SIZE (or the remaining bytes), and later written to the SPI
 * flash.
 */

ssize_t file_transfer_write(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr, const void *buf,
                            uint16_t len, uint16_t offset, uint8_t flags) {
  memcpy(&sram_upload_buffer[ble_pgm_offset], buf, len);

  ble_pgm_offset += len;

  total_received += len;
  led_set_state(LED_STATE_MODEL_RECEIVING);
  LOG_INF("Rx B %d, len %d\n", total_received, len);

  if (ble_pgm_offset == BUFFER_SIZE || total_received == total_pgm_size) {

    crc_ctx.crc = crc32_ieee_update(crc_ctx.crc, (uint8_t *)sram_upload_buffer,
                                    ble_pgm_offset);

    LOG_INF("CRC update done CRC = 0x%x\n", crc_ctx.crc);
    LOG_INF("ble_pgm_offset = %u\n", ble_pgm_offset);

    if (total_received == total_pgm_size) {
      /* Compute the final CRC and compare it against expected CRC received from
       * HOST */
      uint32_t computed_crc = crc32_finalize(&crc_ctx);
      if (computed_crc != expected_crc) {
        /* Abort flash write, erase the flash and return the error */
        crc_ctx.crc_ok = false;
        led_set_state(LED_STATE_UPDATE_FAILED);
        LOG_ERR("CRC check failed, Computed = 0x%x, Expected = 0x%x\n",
                computed_crc, expected_crc);

        spi_flash_erase_helper_func(flash_offsets[app_index], total_pgm_size);
        LOG_ERR("Erasing the flash at index 0x%x\n", flash_offsets[app_index]);
        total_received = 0;
        app_flash_offset = 0;
        ble_pgm_offset = 0;

        return -1;
      } else
        LOG_INF("CRC check passed\n");
    }
    led_set_state(LED_STATE_FLASH_WRITE);
    spi_flash_write_helper_func((uint8_t *)sram_upload_buffer, app_flash_offset,
                                ble_pgm_offset);
    app_flash_offset += ble_pgm_offset;
    reset_buffer();
    send_ack_to_host(ACK_FLASH_WRITE_DONE);

    if (total_received == total_pgm_size && crc_ctx.crc_ok) {

      if (akida_program_infer() != 0) {
        /*there is an error here*/
        led_set_state(LED_STATE_UPDATE_FAILED);
        LOG_ERR("akida model program/inference failed\n");
        return 1;
      }
      led_set_state(LED_STATE_UPDATE_SUCCESS);
      total_received = 0;
      app_flash_offset = 0;
      ble_pgm_offset = 0;
    }
  } else if (ble_pgm_offset > BUFFER_SIZE) {
    ble_pgm_offset = 0;
    LOG_ERR("Data exceeds buffer size %d\n", BUFFER_SIZE);
  }
  return len;
}

/* This function is a BLE service that receives the size of the model passed
 * using BLE host application and erase the spi-flash content of the given size
 */
ssize_t get_file_size(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                      const void *buf, uint16_t len, uint16_t offset,
                      uint8_t flags) {

  memcpy((void *)&total_pgm_size, buf, len);

  LOG_INF("size = %d\n", total_pgm_size);

  if (total_pgm_size == 0 ||
      total_pgm_size > (FLASH_MAX_16_MB_SIZE - flash_offsets[app_index])) {
    LOG_ERR("Invalid size. Must be > 0 and <= %d\n",
            (FLASH_MAX_16_MB_SIZE - flash_offsets[app_index]));
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  }
  LOG_INF("Received file size: %u bytes\n", total_pgm_size);
  if (spi_flash_erase_helper_func(flash_offsets[app_index], total_pgm_size)) {
    LOG_ERR("returning due to error");
    return 1;
  }
  send_ack_to_host(ACK_FLASH_ERASE_DONE);
  return len;
}

void ack_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value) {
  notify_enabled = (value == BT_GATT_CCC_NOTIFY);
  LOG_INF("ACK notify %s\n", notify_enabled ? "enabled" : "disabled");
}

/*
This function sends ack notification to client
*/
void send_ack_to_host(uint8_t ack_code) {
  if (!notify_enabled) {
    LOG_ERR("ACK notification skipped: notify not enabled by central\n");
    return;
  }

  uint8_t ack_data[1] = {ack_code};
  int err = bt_gatt_notify(NULL, &file_transfer_svc.attrs[5], ack_data,
                           sizeof(ack_data));
  if (err) {
    LOG_ERR("Failed to send ACK notification (err %d)\n", err);
  } else {
    LOG_INF("ACK (0x%02X) sent to central\n", ack_code);
  }
}