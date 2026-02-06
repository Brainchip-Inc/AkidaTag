/*
 * ArduCam Mega 3MP/5MP Register Definitions
 * Based on official FPGA register table
 */

#ifndef SPI_CAMERA_H
#define SPI_CAMERA_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>
#include <stdint.h>

/* ==================== REGISTERS ==================== */
#define ARDUCHIP_TEST1      0x00
#define ARDUCHIP_FIFO       0x04
#define FIFO_CLEAR_ID_MASK  0x01
#define FIFO_START_MASK     0x02
#define ARDUCHIP_TRIG       0x44
#define CAP_DONE_MASK       0x04
#define FIFO_SIZE1          0x45
#define FIFO_SIZE2          0x46
#define FIFO_SIZE3          0x47
#define BURST_FIFO_READ     0x3C

#define CAM_REG_SENSOR_RESET       0x07
#define CAM_REG_FORMAT             0x20
#define CAM_REG_CAPTURE_RESOLUTION 0x21
#define CAM_REG_SENSOR_ID          0x40
#define CAM_REG_SENSOR_STATE       0x44
#define CAM_REG_BRIGHTNESS         0x22
#define CAM_REG_CONTRAST           0x23
#define CAM_REG_SATURATION         0x24
#define CAM_REG_EV                 0x25
#define CAM_REG_WHITE_BALANCE      0x26
#define CAM_REG_SHARPNESS          0x28
#define CAM_REG_EXPOSURE_GAIN_WB_CONTROL  0x30
#define CAM_REG_MANUAL_GAIN_BIT_9_8       0x31
#define CAM_REG_MANUAL_GAIN_BIT_7_0       0x32
#define CAM_REG_MANUAL_EXPOSURE_BIT_15_8  0x34
#define CAM_REG_MANUAL_EXPOSURE_BIT_7_0   0x35

#define CAM_SENSOR_RESET_ALL       0x3C
#define CAM_SENSOR_STATE_IDLE      (1 << 1)
#define CAM_IMAGE_PIX_FMT_RGB      0x02
#define CAM_SET_CAPTURE_MODE       (0 << 7)


#define CAM_IMAGE_MODE_96X96_LEGACY    0x0A
#define CAM_IMAGE_MODE_128X128_LEGACY  0x0B
#define CAM_IMAGE_MODE_QVGA_LEGACY     0x01  /* 320x240 */
#define CAM_IMAGE_MODE_320X320_LEGACY  0x0C
#define CAM_IMAGE_MODE_VGA_LEGACY      0x02  /* 640x480 */

/* ==================== CONFIG ==================== */
#define FRAME_WIDTH   96
#define FRAME_HEIGHT  96
#define FRAME_SIZE    (FRAME_WIDTH * FRAME_HEIGHT)

/* RGB565 format: 2 bytes per pixel */
#define RGB565_FRAME_SIZE   (FRAME_SIZE * 2)

/* RGB888 format: 3 bytes per pixel */
#define RGB888_FRAME_SIZE   (FRAME_SIZE * 3)


/* Enhanced ISP tuning */
#define ISP_BRIGHTNESS      0x0C
#define ISP_CONTRAST        0x10
#define ISP_SATURATION      0x10
#define ISP_EV              0x14  /* Higher EV */
#define ISP_SHARPNESS       0x08
#define ISP_WHITE_BALANCE   0x01
#define MANUAL_GAIN         180   /* Higher gain */
#define MANUAL_EXPOSURE     1200  /* Longer exposure */


int camera_init(void);
int camera_start(void);
void camera_stop(void);
void camera_capture_thread(void *a, void *b, void *c);

#endif /*SPI_CAMERA_H*/