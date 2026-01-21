/*
 * CAMERA Continuous Frame Capture
 * Zephyr RTOS
 */

#include "spi_camera.h"
#include "error.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/shell/shell.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <stdint.h>


/* ==================== SPI ==================== */
const struct device *spi4_dev = DEVICE_DT_GET(DT_NODELABEL(spi4));
static struct spi_config spi_cfg_camera = {
    .frequency = 8000000,
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 0,
    .cs = {
        .gpio = SPI_CS_GPIOS_DT_SPEC_GET(DT_NODELABEL(camera)),
        .delay = 0,
    },
};

/* ==================== SPI HELPERS ==================== */
static void camera_write_reg(uint8_t addr, uint8_t val)
{
    uint8_t tx[2] = { addr | 0x80, val };
    struct spi_buf buf = { .buf = tx, .len = 2 };
    struct spi_buf_set set = { .buffers = &buf, .count = 1 };
    spi_write(spi4_dev, &spi_cfg_camera, &set);
}

static uint8_t camera_read_reg(uint8_t addr)
{
    uint8_t tx[3] = { addr & 0x7F, 0, 0 };
    uint8_t rx[3] = { 0 };
    struct spi_buf txb = { .buf = tx, .len = 3 };
    struct spi_buf rxb = { .buf = rx, .len = 3 };
    struct spi_buf_set txs = { .buffers = &txb, .count = 1 };
    struct spi_buf_set rxs = { .buffers = &rxb, .count = 1 };
    spi_transceive(spi4_dev, &spi_cfg_camera, &txs, &rxs);
    return rx[2];
}

static void wait_i2c_idle(void)
{
    for (int i = 0; i < 200; i++) {
        if ((camera_read_reg(CAM_REG_SENSOR_STATE) & 0x03) == CAM_SENSOR_STATE_IDLE)
            return;
        k_msleep(2);
    }
}

static uint32_t fifo_length(void)
{
    return (camera_read_reg(FIFO_SIZE3) << 16) |
           (camera_read_reg(FIFO_SIZE2) << 8)  |
            camera_read_reg(FIFO_SIZE1);
}

static void fifo_read(uint8_t *buf, uint32_t len)
{
    uint8_t cmd = BURST_FIFO_READ;
    uint8_t dummy = 0;
    struct spi_buf txb[] = {
        { .buf = &cmd, .len = 1 },
        { .buf = &dummy, .len = 1 },
        { .buf = NULL, .len = len }
    };
    struct spi_buf rxb[] = {
        { .buf = NULL, .len = 1 },
        { .buf = NULL, .len = 1 },
        { .buf = buf,  .len = len }
    };
    struct spi_buf_set tx = { .buffers = txb, .count = 3 };
    struct spi_buf_set rx = { .buffers = rxb, .count = 3 };
    spi_transceive(spi4_dev, &spi_cfg_camera, &tx, &rx);
}

/* ==================== CAMERA ==================== */
static int camera_init(void)
{
    if (!device_is_ready(spi4_dev)) {
        printk("ERROR: SPI not ready\n");
        return -ENODEV;
    }
    camera_write_reg(ARDUCHIP_TEST1, 0x55);
    if (camera_read_reg(ARDUCHIP_TEST1) != 0x55) {
        printk("ERROR: SPI test failed\n");
        return -1;
    }

    /* Full reset */
    camera_write_reg(CAM_REG_SENSOR_RESET, CAM_SENSOR_RESET_ALL);
    wait_i2c_idle();
    k_msleep(250);

    uint8_t id = camera_read_reg(CAM_REG_SENSOR_ID);
    printk("Camera ID: 0x%02X\n", id);
    
    /* ISP tuning */
    camera_write_reg(CAM_REG_BRIGHTNESS,  ISP_BRIGHTNESS);
    wait_i2c_idle();
    camera_write_reg(CAM_REG_CONTRAST,    ISP_CONTRAST);
    wait_i2c_idle();
    camera_write_reg(CAM_REG_SATURATION,  ISP_SATURATION);
    wait_i2c_idle();
    camera_write_reg(CAM_REG_EV,          ISP_EV);
    wait_i2c_idle();
    camera_write_reg(CAM_REG_SHARPNESS,   ISP_SHARPNESS);
    wait_i2c_idle();
    camera_write_reg(CAM_REG_WHITE_BALANCE, ISP_WHITE_BALANCE);
    wait_i2c_idle();

    /* Manual exposure/gain */
    camera_write_reg(CAM_REG_EXPOSURE_GAIN_WB_CONTROL, 0x01);
    wait_i2c_idle();
    camera_write_reg(CAM_REG_MANUAL_EXPOSURE_BIT_15_8, (MANUAL_EXPOSURE >> 8) & 0xFF);
    wait_i2c_idle();
    camera_write_reg(CAM_REG_MANUAL_EXPOSURE_BIT_7_0, MANUAL_EXPOSURE & 0xFF);
    wait_i2c_idle();
    camera_write_reg(CAM_REG_MANUAL_GAIN_BIT_9_8, (MANUAL_GAIN >> 8) & 0xFF);
    wait_i2c_idle();
    camera_write_reg(CAM_REG_MANUAL_GAIN_BIT_7_0, MANUAL_GAIN & 0xFF);
    wait_i2c_idle();

    return (id == 0 || id == 0xFF) ? -1 : 0;
}

static int capture_rgb(uint8_t *buf, uint32_t max_len)
{
    /* Set RGB format */
    camera_write_reg(CAM_REG_FORMAT, CAM_IMAGE_PIX_FMT_RGB);
    wait_i2c_idle();
    k_msleep(100);

    camera_write_reg(CAM_REG_CAPTURE_RESOLUTION, CAM_SET_CAPTURE_MODE | CAM_IMAGE_MODE_96X96_LEGACY);
    wait_i2c_idle();
    k_msleep(100);

    /* Verify */
    uint8_t res_check = camera_read_reg(CAM_REG_CAPTURE_RESOLUTION);
    printk("Resolution register: 0x%02X (should be 0x%02X)\n", 
           res_check, CAM_SET_CAPTURE_MODE | CAM_IMAGE_MODE_96X96_LEGACY);

    /* Clear and start */
    camera_write_reg(ARDUCHIP_FIFO, FIFO_CLEAR_ID_MASK);
    k_msleep(150);
    camera_write_reg(ARDUCHIP_FIFO, FIFO_START_MASK);

    /* Wait for capture */
    bool done = false;
    for (int i = 0; i < 2000; i++) {
        if (camera_read_reg(ARDUCHIP_TRIG) & CAP_DONE_MASK) {
            done = true;
            break;
        }
        k_msleep(5);
    }

    if (!done) {
        printk("ERROR: Capture timeout\n");
        return -1;
    }

    uint32_t len = fifo_length();
    printk("FIFO: %u bytes (expected: %u)\n", len, FRAME_SIZE);

    if (len == 153600) {
        printk("\nwrong Resolution\n");
        return -1;
    }

    if (len == 0 || len > max_len) {
        printk("ERROR: Invalid FIFO length\n");
        return -1;
    }

    fifo_read(buf, len);
    return len;
}

/* ==================== BASE64 ==================== */
static const char b64[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void send_base64(uint8_t *buf, uint32_t len, int capture_num)
{
    printk("\n--- RGB_START_%d ---\n", capture_num);
    for (uint32_t i = 0; i < len; i += 3) {
        uint32_t n = buf[i] << 16;
        if (i + 1 < len) n |= buf[i + 1] << 8;
        if (i + 2 < len) n |= buf[i + 2];
        printk("%c%c%c%c",
            b64[(n >> 18) & 63],
            b64[(n >> 12) & 63],
            (i + 1 < len) ? b64[(n >> 6) & 63] : '=',
            (i + 2 < len) ? b64[n & 63] : '=');
    }
    printk("\n--- RGB_END_%d ---\n", capture_num);
}

int camera_start(void)
{
    if (spi4_dev == NULL) {
        return -ENODEV;
    }
    /* Clear and start */
    camera_write_reg(ARDUCHIP_FIFO, FIFO_CLEAR_ID_MASK);
    k_msleep(150);
    camera_write_reg(ARDUCHIP_FIFO, FIFO_START_MASK);

}

void camera_stop(void)
{
    printk("camera stopped\n");
}


void camera_capture_thread(void *a, void *b, void *c)
{

    /* Warm-up */
    printk("Warming up (5 frames)...\n");
    for (int i = 0; i < 5; i++) {
        int len = capture_rgb(frame, sizeof(frame));
        if (len > 0) {
            printk(" Warm-up %d: %d bytes\n", i + 1, len);
        } else {
            printk(" Warm-up %d: FAILED\n", i + 1);
        }
        k_msleep(200);
    }
    printk("Warm-up complete!\n\n");

    /* Main loop */
    while (1) {
        printk("--- Sequence start ---\n");
        for (int i = 0; i < NUM_CAPTURES; i++) {
            int len = capture_rgb(frame, sizeof(frame));
            if (len > 0) {
                printk(" Image %d/%d: %d bytes\n", i + 1, NUM_CAPTURES, len);
                send_base64(frame, len, i + 1);
            } else {
                printk(" Image %d/%d: FAILED\n", i + 1, NUM_CAPTURES);
            }
            if (i < NUM_CAPTURES - 1)
                k_msleep(200);
        }
        printk("--- Sequence complete ---\n\n");
        k_msleep(10);
    }
}



static int cmd_camera_start(const struct shell *shell,
                            size_t argc, char **argv)
{
    if (argc > 1) {
        printk("invalid command\n");
        return -EINVAL;
    }

    if (camera_init() < 0) {
        printk("Camera init failed\n");
        return -1;
    }

    camera_start();
    return 0;
}

static int cmd_camera_stop(const struct shell *shell,
                           size_t argc, char **argv)
{
    if (argc > 1) {
        printk("invalid command\n");
        return -EINVAL;
    }
    camera_stop();
    return 0;
}

SHELL_CMD_REGISTER(camera_start, NULL, "camera_start", cmd_camera_start);
SHELL_CMD_REGISTER(camera_stop, NULL, "camera_stop", cmd_camera_stop);




