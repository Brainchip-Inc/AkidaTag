# Project AkidaTag Source
This is AkidaTag complete application source code. This project includes all the necessary configurations to build the MCUboot bootloader, the network core image, and the application core image. The MCUboot secondary image slot is located in external serial flash.

The application uses the LittleFS file system and also keeps track of the number of system restarts the device has undergone.

### LED Indication

This application provides visual status indication using Red and Green LEDs.  
A dedicated Zephyr thread manages LED patterns based on the system state.

---

### Application Threads

This application currently runs the following LED-related thread:

- **LED Indication Thread**
  - **Function:** `led_ind_thread()`
  - **Responsibilities:**
    - Controls the Red and Green LEDs.
    - Displays device status using predefined LED patterns.
    - Monitors the global LED state (`current_state`).
    - Updates LED behavior based on the system state.

---

### LED Thread Synchronization

The LED thread execution is synchronized using a Zephyr semaphore (`led_sem`).

- DMIC thread running at ~60 ms can signal the LED thread using `k_sem_give(&led_sem)` to trigger an LED update.
- The LED thread waits using `k_sem_take(&led_sem, K_MSEC(LED_TICK_MS))`.
- If a signal is received, the LED thread wakes immediately and updates the LED state.
- If no signal is received within `LED_TICK_MS`, the wait call times out and the LED thread continues execution using the timeout as a fallback timing mechanism.

This allows the LED logic to operate in two modes:

1. **Synchronized Mode** – LED updates are triggered by another thread.
2. **Standalone Mode** – LED updates run periodically using the timeout when the signaling thread is not active.

---

### LED State Control

- The LED behavior is controlled using the `led_set_state()` API.
- The LED state is stored using atomic variables to ensure thread-safe access.
- BLE connection status is updated through the `ble_connection_callback()` function.
- Based on these states, the LED thread updates the LED patterns accordingly.

**Note:**  
`CMAKE_EXTRA_ARGS+=(-DCONFIG_DK_BOARD=n)` disables the onboard LED.

---

### LED Pin Configuration (According to schematic)

| LED        | GPIO  | Pin | Active Level |
|------------|-------|-----|--------------|
| Red LED    | GPIO0 | 28  | Active High  |
| Green LED  | GPIO0 | 27  | Active High  |

---

### LED Behavior

| State           | Behavior |
|-----------------|----------|
| NORMAL_APP      | Green slow blink (2s), Red OFF |
| BLE_CONNECTED   | Green ON, Red OFF |
| MODEL_RECEIVING | Green ON, Red fast blink (500ms) |
| FLASH_WRITE / FLASH FULL_ERASE    | Green ON, Red ON |
| UPDATE_SUCCESS  | Both LEDs blink 3 times, then restore runtime state based on BLE status |
| UPDATE_FAILED   | Green OFF, Red ON |
| EL_SPEAK_PROMPT (AkidaTag board)  | Red LED solid ON during each "speak now" prompt window in the edge-learning 5-utterance flow; cleared as soon as speech is detected or the flow is aborted/completed. Green LED behavior preserved. |
| INFERENCE_TRIGGER (AkidaTag board)| Red LED flashes ~500 ms on every keyword prediction in inference mode for at-a-glance trigger confirmation. |

### PDM MIC 
This application uses the DMIC (PDM microphone) interface with PDM_CLK on P1.9 and PDM_DIN on P1.10.
The DMIC peripheral is enabled via DeviceTree using the dmic_dev node and pinctrl configuration.
Required Zephyr flags: CONFIG_AUDIO=y, CONFIG_DMIC=y, CONFIG_MEM_SLAB=y, CONFIG_PRINTK=y.
Audio is captured at 16 kHz, 16-bit mono and RMS is calculated after DC offset removal.

### Toggle Chip Select
Chip Select (CS) handling is managed automatically by Zephyr’s SPI driver via DeviceTree.
Each SPI device has its own CS GPIO defined, and CS is asserted and released automatically
during `spi_write()` and `spi_transceive()` calls. No manual GPIO toggling is required.


### I2C ISM330 accelerometer and gyroscope
- This application interfaces with the ISM330 accelerometer and gyroscope over I2C1.
- I2C1 is configured with SCL on P1.03 and SDA on P1.02 using Zephyr pinctrl.
- The IMU INT1 interrupt pin is connected to GPIO P0.31, configured via devicetree alias.
- Accelerometer (CTRL1_XL) and gyroscope (CTRL2_G) registers are configured in firmware.
- Sensor data can be acquired using FIFO interrupt-driven mode or polling mode.
- The selection between interrupt mode and polling mode is controlled using a Kconfig flag (CONFIG_IMU_USE_INTERRUPT) defined in prj.conf.
- Accelerometer and gyroscope ODR and full-scale (FS) values are configurable via Kconfig options and set at build time.
- Lookup tables defined are used to convert raw sensor data to physical units.
- SPI1 was changed to SPI2 to avoid conflicts while using the I2C1 peripheral.
- reason: SPI was moved to SPI2 because I2C is actively used on I2C1. Moving I2C to I2C2 caused configuration issues, so SPI was relocated instead to preserve stable I2C operation.
- IMU operation can be started or stopped at runtime using shell commands: imu_start and imu_stop.
- CONFIG_IMU_ENABLE_THREAD enables or disables the dedicated IMU processing thread. When disabled, no IMU thread or stack is created.
- A separate configuration file imu_app.conf is added for the imu_la application.This file contains all IMU-related Kconfig options.
- Accelerometer ODR Configuration : CONFIG_IMU_ACC_ODR
	Encoding	Output Data Rate (ODR)
	0			OFF
	1			12.5 Hz
	2			26 Hz
	3			52 Hz
	4			104 Hz
	5			208 Hz
	6			416 Hz
	7			833 Hz
	8			1660 Hz
- Accelerometer Full Scale Configuration: CONFIG_IMU_ACC_FS
	Encoding	Full Scale Range
	0			±2g
	1			±16g
	2			±4g
	3			±8g
- Gyroscope Configuration: CONFIG_IMU_GYRO_ODR (Same encoding as Accelerometer ODR)
- Gyroscope Full Scale Configuration: CONFIG_IMU_GYRO_FS
	Encoding	Full Scale Range
	0			250 dps
	1			500 dps
	2			1000 dps
	3			2000 dps
- FIFO Configuration
	CONFIG_IMU_FIFO_ACC_ODR (Same encoding as ODR.)
	CONFIG_IMU_FIFO_GYRO_ODR (Same encoding as ODR.)
    Must be less than or equal to the corresponding sensor ODR.

	CONFIG_IMU_FIFO_WATERMARK
	Allowed values:8 / 16 /32
	This defines the number of FIFO entries required before the interrupt is triggered.
- Runtime Configuration via Shell
	The IMU parameters can also be configured at runtime using the shell command:
	imu_start <acc_odr> <acc_fs> <gyro_odr> <gyro_fs> <fifo_acc_odr> <fifo_gyro_odr> <watermark>
	Example:
			imu_start 5 3 5 1 5 5 8
			This means:
					ACC ODR = 208 Hz
					ACC FS = ±8g
					GYRO ODR = 208 Hz
					GYRO FS = 500 dps
					FIFO ACC ODR = 208 Hz
					FIFO GYRO ODR = 208 Hz
					Watermark = 8
- The IMU can be configured up to 416 Hz (validated configuration).
- Interrupt Periodicity
	The interrupt interval depends on: Selected ODR
	FIFO watermark level: Interrupt Period ≈ Watermark / (ACC ODR + GYRO ODR)
	1) 26 Hz (ODR = 2)
		Total FIFO rate = 2 × 26 = 52 samples/sec
		Watermark	Interrupt Period
		8			8 / 52 ≈ 154 ms
		16			16 / 52 ≈ 308 ms
		32			32 / 52 ≈ 615 ms
	2) 52 Hz (ODR = 3)
		Total FIFO rate = 2 × 52 = 104 samples/sec
		Watermark	Interrupt Period
		8			8 / 104 ≈ 77 ms
		16			16 / 104 ≈ 154 ms
		32			32 / 104 ≈ 308 ms
	3) 104 Hz (ODR = 4)
		Total FIFO rate = 2 × 104 = 208 samples/sec
		Watermark	Interrupt Period
		8			8 / 208 ≈ 38 ms
		16			16 / 208 ≈ 77 ms
		32			32 / 208 ≈ 154 ms
	4) At 208 Hz (ACC = 208 Hz, GYRO = 208 Hz)
		Combined FIFO write rate = 416 entries/sec
		Watermark	Interrupt Interval (Approx.)
		8			~19 ms
		16			~38 ms
		32			~77 ms
	5) At 416 Hz (ACC = 416 Hz, GYRO = 416 Hz)
		Combined FIFO write rate = 832 entries/sec
		Watermark	Interrupt Interval (Approx.)
		8			~9.6 ms
		16			~19 ms
		32			~38 ms

### To check IMU data 
For testing IMU data, a Python script is provided.
Run the imu_start command from the serial monitor.
Close the serial monitor and run utils/imu_data_screening.py.
The script plots accelerometer and gyroscope data in real time.
Ensure matplotlib and pyserial are installed before running.

### SPI CAMERA
The SPI Camera module enables image capture from an SPI-connected camera on Zephyr RTOS. It uses the SPI3 peripheral (8 MHz, MSB-first, with a dedicated CS pin) to communicate with the camera. On startup, it performs full sensor initialization and reset, then configures ISP settings (brightness, contrast, saturation, sharpness, white balance, EV) and applies manual exposure and gain values.

Frame acquisition follows a **demand  model**. Each frame capture is initiated explicitly using a `FIFO_START` command, allowing frames to be requested only when needed. Multiple frames can be requested in quick succession when higher capture throughput is required.

The capture process is **synchronous** (blocking). After initiating a capture, the software waits for completion by polling the `CAP_DONE` flag. During this period, the calling thread waits until the frame capture is finished.


## Supported Resolutions

Frames are captured based on a CLI command. Due to RAM constraints, only two resolutions are supported:

| Index | Resolution | RGB888 Size |
|-------|------------|-------------|
| 1     | 96 × 96    | 27,648 B    |
| 2     | 128 × 128  | 49,152 B    |

> **WARNING:** 320×240, 320×320, and higher resolutions exceed available RAM and are **not supported**.

## Frame Timing

| Event 				  				  			 | Time 	       	  |
|------------------------------------------------------------|--------------------|
| First frame ready — 96x96(from `camera_start`)  	      | ~956 ms (average)  |
| First frame ready — 128x128(from `camera_start`)	      | ~1042 ms (average) |
| Execution time of the capture_rgb() function
			 — 96×96 resolution 	  	  			 | ~22 ms	            |
| Execution time of the capture_rgb() function
			 — 128×128 resolution 	  	  			 | ~36 ms             |

Sensor Capture Time
The time was calculated based on the duration the firmware waits for the CAP_DONE_MASK flag to be set. The measurement was verified using CRO.

Resolution	Capture Time
|-----------------------|
96×96		~50 µs
128×128		~50 µs

SPI Transfer Time
Measured time required to transfer image data from the camera FIFO to the MCU using SPI @8 MHz.

Resolution			Data Size	SPI Transfer Time
|-------------------------------------------------|
 96×96 (RGB565)		~18 KB		~18 ms
 128×128 (RGB565)	     ~32 KB		~33 ms

Image Conversion Time

Resolution	Conversion Time
|--------------------------|
 96×96		~3 ms
 128×128	     ~5 ms

The first-frame timer starts when the `camera_start` shell command is issued. This includes the warm-up sequence (3 discarded frames) before the first valid frame is delivered.

### SRAM Upload Buffer

`sram_upload_buffer` is a shared memory buffer used between the camera capture thread and the model update process. Access to this buffer is synchronized using a Zephyr `k_event` to ensure that only one module uses the buffer at a time. The buffer state is controlled using `BUF_EVENT_FREE` and `BUF_EVENT_BUSY` flags to prevent concurrent access and data corruption.

## Frame Output

Captured frames are read from the camera FIFO and validated. The raw RGB565 data is converted to RGB888 format. Frames are then Base64-encoded and printed to the console, delimited by:

```
--- RGB888_START_ ---
<base64 data>
--- RGB888_END_ ---
```

## Shell Commands

| Command | Description |
|---------|-------------|
| `camera_set_pixel 1` | Set resolution to 96×96 (must be run before `camera_start`) |
| `camera_set_pixel 2` | Set resolution to 128×128 (must be run before `camera_start`) |
| `camera_start` | Initialize camera, run warm-up, and begin continuous capture |
| `camera_stop` | Stop capture and reset the sensor |

`camera_set_pixel` **must** be called before `camera_start`. If resolution is not set, `camera_start` will return an error.

## Configuration

- **`CONFIG_DK_BOARD`** — Set to `N`. If enabled (`Y`), the DK board pins overlap with the SPI pins used by the camera, causing incorrect image capture.
- **`CONFIG_CAMERA_ENABLE_THREAD`** — Enables or disables the dedicated camera processing thread. When disabled, no camera thread or stack is allocated.
- A separate Kconfig file **`camera_app.conf`** is provided for all camera-related configuration options.


### To check camera image
A Python script `utils/image_display.py` is provided to receive frames over serial, decode them, and save them as PNG images.

## Prerequisites

```bash
pip install pyserial numpy pillow
```

## Usage

1. Close any active serial monitor (e.g. in your IDE or terminal).
2. Run the script:

```bash
python utils/image_display.py
```

The script will prompt for serial port, baud rate, frame dimensions, and output folder. Frames are saved as `frame_00001.png`, `frame_00002.png`, etc. under the specified folder (default: `./frames/`), upscaled 4× for easier viewing.

## Notes

- The script buffers serial data and only processes a frame once both `--- RGB888_START_ ---` and `--- RGB888_END_ ---` markers are found.
- Frames with incorrect byte counts (not equal to `width × height × 3`) are skipped with a warning.
- Each saved PNG is upscaled 4× using nearest-neighbour interpolation for easy visual inspection.
- Press **Ctrl+C** to stop; the script prints a final frame count summary.

### SPI CAMERA
The SPI Camera module enables continuous image capture from an SPI-connected camera on Zephyr RTOS. It uses the SPI3 peripheral (8 MHz, MSB-first, with dedicated CS pin) to communicate with the camera, performs initialization and sensor reset, configures ISP settings (brightness, contrast, saturation, sharpness, white balance), and sets manual exposure and gain. Frames are captured in 96×96 RGB resolution (legacy mode) using the camera FIFO buffer. Captured frames are read from the FIFO, validated, and optionally converted to Base64 format for safe logging or transmission, marked with --- RGB_START_X --- and --- RGB_END_X ---. A continuous capture thread handles multi-frame capture sequences, while shell commands camera_start and camera_stop allow starting and stopping the camera via Zephyr shell.

### BLE Service Implementation
This firmware implements Bluetooth Low Energy (BLE) services for the AKIDA device platform on the Nordic Semiconductor nRF5340. It enables mobile applications to communicate with the device through the Nordic UART Service (NUS).
NUS provides:
One RX characteristic (write from phone)
One TX characteristic (notify to phone)
It behaves like a bidirectional data pipe. It simplifies protocol scalability and allows structured communication over a single BLE service.
It is used in eg: battery_response and device_info_response etc.
1. Device Advertising
When scanning for devices, phones will see:

Device Name: Configured through CONFIG_BT_DEVICE_NAME
Manufacturer Data (human-readable ASCII):
BLE Version: "53" (version 5.3)
Firmware Version: "241" (version 2.4.1)
Chip ID: "AKD1500"
The manufacturer data is encoded in ASCII text format, allowing phones to display this information directly without needing to convert binary data.

The advertisement deliberately carries no scan response. The permanent hardware serial is never broadcast, because a value that never changes would let a passive scanner track a tag straight through the resolvable private address rotation that `CONFIG_BT_PRIVACY` and `CONFIG_BT_RPA_TIMEOUT` provide. The serial is reported instead in the last frame of the `CMD_DEVICE_INFO` response, over the encrypted and bonded connection, as 16 lowercase hex characters.

 The Nordic UART Service (NUS) handles command processing with a sophisticated multi-frame protocol supporting single-frame messages for battery commands and multi-frame fragmentation for larger device information transfers, complete with retry logic and send-state management.


2. Phone-Firmware Communication Flow
The communication between the mobile application and firmware follows a simple command-response protocol over BLE's Nordic UART Service (NUS).
Command Frame Format: [frame_type],[index],[size],[command],[data]

MOBILE APP  ◄────────►   BLE STACK   ◄────────►     FIRMWARE

     │                           │                           │
     ├───Connect & Pair──────────┼────────────────────────────►│
     ├───Send Command────────────┼────────────────────────────►│
     │    "CMD_APP"              │                           │
     │                           │                           ├───Process Command
     │                           │                           │    Prepare application info
     │                           │                           │
     │◄──Receive Response────────┼───────────────────────────┤
     │ "app_name,description,    │                           │    
     |      app_size,            │                           |
     │                           │                           │
     │                           │                           │
     ├───Send Command───────────┼────────────────────────────►│
     │    "CMD_BATTERY"          │                           │
     │                           │                           ├───Process Command
     │                           │                           │    Read battery (97%)
     │                           │                           │
     │◄──Receive Response────────┼──────────────────────────────┤
     │    "BATTERY:97"           │                           │
     │                           │                           │
     ├───Send Command────────────┼────────────────────────────►│
     │    "CMD_DEVICE_INFO"      │                           │
     │                           │                           ├───Process Command
     │                           │                           │    Gather device data
     │                           │                           │    "AKIDA,TYPE,5.3,1.2.3,
     │                           │                           │     f3a1c05b7d29e846"
     │                           │                           │
     │◄──Receive Response────────┼───────────────────────────┤
     │    "DEVICE:AKIDA,TYPE,    │                           │
     │           5.3,1.2.3,      │                           │
     │      f3a1c05b7d29e846"    │                           │
     │                           │                           │
     ├───Send Command────────────┼────────────────────────────►│
     │    "CMD_APP_INFO"         │                           │
     │                           │                           ├───Process Command
     │                           │                           │    Prepare application info
     │                           │                           │ 
     │                           │                           │
     │◄──Receive Response────────┼───────────────────────────┤
     │    "DEVICE:Processor,     │                           │
     │         Type,             │                           │
     │         Version,          │                           │
     │        Processor_Detail,  │                           │
     │         Model_Name,       │                           │
     │         Model_Version,    │                           │
     │         Model_Size,       |                           │
     │         Input_Shape,      │                           │
     │         Num_Classes,      │                           │
     │         Akida_Nodes,      │                           │
     │         Power_Consumption"│                           │
     │                           │                           │
     ├───Send Command────────────┼────────────────────────────►│
     │    "CMD_CONFIG:GET"       │                           │
     │                           │                           ├───handle_config_command("GET")
     │                           │                           │    6-frame snapshot burst, one
     │                           │                           │    KWS param per frame
     │                           │                           │    (MF_START, 4 × MF_MID, MF_LAST)
     │                           │                           │
     │◄──Receive Snapshot────────┼──────────────────────────────┤
     │    "4:rms:550"            │                           │
     │    "4:debounce_ms:300"    │                           │
     │    "4:smoothing_alpha:0.70"                            │
     │    "4:score_threshold:0.60"                            │
     │    "4:chiming:3"          │                           │
     │    "4:speech_timeout:1300"│                           │
     │                           │                           │
     ├───Send Command────────────┼────────────────────────────►│
     │    "CMD_CONFIG:<id>:<v>"  │                           │
     │   (SET single param)      │                           ├───kws_config_set_from_string()
     │                           │                           │    range-check → write global →
     │                           │                           │    persist NVS → bounce DMIC
     │                           │                           │
     │◄──Receive ACK─────────────┼──────────────────────────────┤
     │    "4:<id>:OK"            │                           │   on success
     │    "4:<id>:ERR:<reason>"  │                           │   reason: ID | RANGE | PARSE | NVS
     │                           │                           │
     ├───Send Command────────────┼────────────────────────────►│
     │    "CMD_CONFIG:RESET"     │                           │
     │                           │                           ├───kws_config_reset_to_defaults()
     │                           │                           │
     │◄──Receive ACK + Snapshot──┼──────────────────────────────┤
     │    "4:RESET:OK"           │                           │
     │    + 6-frame snapshot     │                           │
     │                           │                           │
	 ├───Send Command────────────┼────────────────────────────►│
     │    "CMD_DEPLOY_START"     │                           │
     │                           │                           ├───kws_app_start() + event_flag=1
     │                           │                           │    (DMIC + KWS pipeline resumed,
     │                           │                           │     KWS detections sent to phone)
     │                           │                           │
     │◄──Receive KWS Events──────┼──────────────────────────────┤
     │    "KWS:hello"            │                           │
     │    "KWS:stop"             │                           │
     │    "KWS:yes"              │                           │
     │                           │                           │
     ├───Send Command────────────┼────────────────────────────►│
     │    "CMD_STREAM_START"     │                           │
     │                           │                           ├───Set pdm_stream_flag = FLAG_ENABLE
     |                           |                           |   current_stream_flag = FLAG_DISABLE;
     │                           │                           │    (PCM waveform stream active)
     │                           │                           │
     │◄──Binary PCM-envelope─────┼──────────────────────────────┤
     │    notifications          │                           │   one frame per 60 ms audio block
     │  CMD_STREAM_WAVE (0x0C)   │                           │   header: 'B', 0x0C, u16 seq LE,
     │  ┌─ envelope mode ──────┐ │                           │           u16 n_samples LE
     │  │ n_samples = 64       │ │                           │   envelope:    32 (min,max) pairs
     │  │ 134 B, ~17.8 kbps    │ │                           │                = 134 B / 60 ms
     │  └──────────────────────┘ │                           │   fallback:    every-30th-sample
     │  ┌─ decimation fallback ┐ │                           │                = 70 B / 60 ms
     │  │ n_samples = 32       │ │                           │   auto-fallback when MTU < 140 B or
     │  │  70 B, ~9.3 kbps     │ │                           │   >5 retries in last 50 frames;
     │  └──────────────────────┘ │                           │   recovers after 100 clean frames.
     │                           │                           │
     ├───Send Command────────────┼────────────────────────────►│
     │    "CMD_DEPLOY_STOP"      │                           │
     │                           │                           ├───event_flag=0 + kws_app_stop()
     │                           │                           │    (KWS detections suppressed,
     │                           │                           │     DMIC + pipeline halted)
     │                           │                           │
     │◄──Receive Response────────┼──────────────────────────────┤
     │    "DEPLOY_STOP:ACK"      │                           │
     │                           │                           │
     ├───Send Command────────────┼────────────────────────────►│
     │    "CMD_STREAM_STOP"      │                           │
     │                           │                           ├───Set pdm_stream_flag = FLAG_DISABLE
     │                           │                           │    (Audio streaming stopped)
     │                           │                           │
     │◄──Receive Response────────┼──────────────────────────────┤
     │    "STREAM_STOP:ACK"      │                           │
     │                           │                           │
     ├───Send Command────────────┼────────────────────────────►│
     │    "CMD_RESTART"          │                           │
     │                           │                           ├───Process Command
     │                           │                           │    Trigger system reboot
     │                           │                           │    sys_reboot()
     │                           │                           │
     ├───Send Command────────────┼────────────────────────────►│
     │    "CMD_CURRENT_START"    │                           │
     │                           │                           ├───   pdm_stream_flag = FLAG_DISABLE;
     │                           │                           │      current_stream_flag = FLAG_ENABLE;
     │                           │                           │      (Current streaming active)
     │                           │                           │
     ├───Send Command────────────┼────────────────────────────►│
     │    "CMD_CURRENT_STOP"     │                           │
     │                           │                           ├───current_stream_flag = FLAG_DISABLE;
     │                           │                           │    (Current streaming stopped)
     │                           │                           │
     │◄──Receive Response────────┼──────────────────────────────┤
     │   "CURRENT_STOP:ACK"      │                           │
     │                           │                           │

3. How It Works

Phone sends a command - The mobile app writes a command string to the NUS characteristic
Firmware parses the command - The nus_received_cb callback identifies which command was received
Firmware prepares response - Based on the command type, the appropriate data is gathered
Response is sent back - Data is formatted and transmitted to the phone via NUS
Phone displays the data - The app receives and presents the information to the user


## Current Monitoring IC & Battery Status
This module provides battery charger status monitoring and current sensing capabilities using GPIO status pins and an external current monitoring IC (ADC-based). It enables real-time tracking of battery charging state and current consumption for power management and diagnostics.

1. Current Monitoring IC (INA190, ADC-based)
Two INA190 current-sense amplifiers drive the nRF5340 SAADC: U25 senses VDD_1V8 through shunt R117 (0.1 Ω), U26 senses VDD_0V8_AKD through shunt R118 (0.02 Ω). Current is derived as `I(mA) = Vout(mV) / (Rshunt × gain)`.

## Configuration
Channels: 2 (1V8_AKD rail on AIN0, 0V8_AKD rail on AIN1)
Resolution: 12-bit (0-4095)
Reference: 1800 mV internal
SAADC gain: 1/3

INA190 variant: the A1/A3 part difference is amplifier gain only (A1 = 25 V/V, A3 = 100 V/V); the shunts are unchanged. It cannot be auto-detected, so it is chosen at build time via `CONFIG_INA190_VARIANT` (default A1) and overridable at runtime with `power variant a1|a3`.

Sampling & Averaging:
- A background sampler reads both rails at `CONFIG_CURRENT_DEFAULT_RATE_HZ` (default 10 Hz, runtime-tunable via `power rate`), averaging 8 back-to-back ADC conversions per rail read.
- `power read` returns an 8-sample moving average for a stable value; energy integrates the instantaneous per-loop power.
- `power measure [ms]` runs a synchronous averaged burst over a window, independent of the background rate — the most accurate spot reading. Use it after an `akd_coreclk` change or to bracket an inference.

### `power` shell commands
| Command | Description |
|---|---|
| `power read` | Moving-average current & power for both rails |
| `power measure [ms]` | Averaged current/power/energy over a window (default 500 ms), both rails |
| `power rate [hz]` | Get/set the background sampler rate (default 10 Hz) |
| `power energy` | Accumulated energy & average power since the last reset |
| `power reset` | Reset the energy accumulator |
| `power variant [a1\|a3]` | Get/set the fitted INA190 variant (gain) |

2. Battery Charger Status Monitoring
The battery charger status is monitored using two GPIO input pins (chgr_sts1 and chgr_sts2) connected to the charger IC. These pins provide real-time charging state and fault detection.

## Pin Status:
STS1	STS2	Status	                    Description

High	High	BAT_NOT_CHARGING	        Battery not charging (idle/standby)
High	Low	    BAT_CHARGING	            Battery actively charging
Low	    High	BAT_FAULT_RECOVERABLE	    Recoverable fault (e.g., over-temperature, timeout)
Low	    Low	    BAT_FAULT_NON_RECOVERABLE	Non-recoverable fault (e.g., battery over-voltage)

## Fuel Gauge (Battery SOC Monitoring)
This module provides battery State of Charge (SOC) monitoring using the BQ27427 Impedance Track™ fuel gauge via I2C communication.
Reads the current battery State of Charge (SOC) percentage from the BQ27427 fuel gauge and Send to Mobile app

ISR-Based SOC Notification
The BQ27427 fuel gauge can generate hardware interrupts on the SOC_INT pin whenever the State of Charge (SOC) changes by a configured delta (default 1%). This enables event-driven battery level reporting to the phone app instead of continuous polling, reducing I2C traffic and power consumption

## Configuration
Battery Parameters (1100mAh 4.2V Li-ion)

Parameter	            Value	    Description
Design Capacity	        1100 mAh	Battery capacity
Design Energy	        4070 mWh	Capacity × 3.7V
Terminate Voltage	    3000 mV	    0% SOC cutoff
Taper Rate	            100	        Full charge detection
Taper Voltage           4150        Charge termination voltage

**NOTE**: These values are for testing purposes only using a 1100mAh test battery.
Always connect the battery before connecting USB power.

### Edge Learning BLE Service

MOBILE APP                    BLE STACK                    FIRMWARE
     │                              │                             │
     │───Subscribe to ACK───────────┼────────────────────────────►│
     │    (Enable notifications)    │                             │
     │                              │                             ├───Set notify_enabled = true
     │                              │                             │
     ├───Write Command──────────────┼────────────────────────────►│
     │    [CMD=0] Enter Inference   │                             │
     │                              │                             ├───edge_cmd_write() called
     │                              │                             ├───Process command
     │                              │                             │
     ├───Write Command──────────────┼────────────────────────────►│
     │    [CMD=1] Start Learning    │                             │
     │◄──Receive ACK────────────────┼─────────────────────────────┤───Begin training process
     │    [ACK=0xA6]                │                             │   
     │    (Learning started)        │                             │
     │                              │                             │
     │◄──Receive ACK────────────────┼─────────────────────────────┤
     │    [ACK=0xA7]                │                             │   learning_completed()
     │    (Learning complete)       │                             │   send_ack(ACK_LEARNING_DONE)
     │                              │                             │
     ├───Write Command──────────────┼────────────────────────────►│
     │    [CMD=2] Delete Class      │                             │
     │                              │                             │
     ├───Write Command──────────────┼────────────────────────────►│
     │    [CMD=3] Select Next Class │                             │
     │                              │                             │

### Build the demo_apps Sample with AkidaTag board overlay file.
After compiling the project, MCUBoot is automatically built along with the application.
The sysbuild system generates a combined image that includes both MCUBoot and the demo_apps application.

```
./scripts/run.sh -d -b --app demo_apps
```
### Build the demo_apps Sample with dk board overlay file.
After compiling the project, MCUBoot is automatically built along with the application.
The sysbuild system generates a combined image that includes both MCUBoot and the demo_apps application.

```
./scripts/run.sh -d -b --dk --app demo_apps
```

### Flash the demo_apps Sample.
This flashes both mcuboot and demo_apps application together

```
./scripts/run.sh -d -f --app demo_apps
```

### To reset the board

```
./scripts/run.sh -d -r
```

### Device ID Display via UART CLI
A CLI command is provided to display the unique Device ID of the SoC via the UART console. The device ID is read from the FICR registers and printed through the shell interface. The serial console is a trusted local channel, so the full identifier is printed here even though it is never advertised over the air.

### FOTA over BLE using nRF Connect Mobile App
- Copy the updated application image `zephyr.signed.bin` to your mobile device.
- Install and open the nRF Connect Mobile app.
	- Ensure Bluetooth is enabled on your phone.
- Connect to the device by the name it advertises (`CONFIG_BT_DEVICE_NAME`).
- Tap the DFU icon in the top-right corner of the app.
- Browse and select the zephyr.signed.bin file.
- Perform a `Test and Confirm` upload of the application image.
- Once the FOTA update completes, the app will display `Done` and `Application has been sent successfully`.
- The nRF board will automatically restart and boot with the new application image.

### Firmware Update over UART (MCUboot)
- UART0 is configured for firmware updates using external GPIO pins. The UART interface operates at 115200 baud and is mapped to dedicated TX/RX pins as defined in the device configuration (TX- P0.29 and RX- P1.4). A UART-to-USB adapter is required on the host PC to perform DFU.
- MCUboot is enabled as the bootloader and configured. Firmware updates are performed over UART. Serial Recovery mode is entered by holding the configured recovery button while resetting the device. An indicator LED 2 turns ON to signal that the device is in recovery mode.

Firmware images are uploaded from the host PC using AuTerm over the configured UART interface. The signed application image generated by the build system is used for the update. After a successful upload, resetting the device boots the new firmware immediately.
### Watchdog Configuration
- The WDT_ENABLE Kconfig option is used to enable or disable the watchdog feature at build time.
	When enabled, the watchdog is initialized during system startup and feeding is performed by the application.
	When disabled, watchdog initialization and feeding are skipped entirely.
- The Watchdog (WDT) monitors only the threads that are explicitly enabled at runtime using wdt_enable_thread function. Each enabled thread must periodically update its health status otherwise, the WDT will trigger a system reset. If no threads are enabled, the system will feed the Watchdog.
- The watchdog is enabled using Zephyr’s watchdog driver to automatically reset the SoC if the application becomes unresponsive. 
- A timeout channel is installed during initialization and must be periodically fed from the main loop. 
- The watchdog pauses automatically when the CPU is halted by a debugger or while CPU sleeps to prevent unintended resets during debugging.
- The wdt_disable CLI command is implemented for watchdog validation testing. It performs an invalid memory access to generate a system fault. As watchdog feeding stops after the crash, the watchdog timeout occurs and forces a system reset, confirming correct watchdog functionality.
- An additional CLI command (threads_stop) is available to terminate all running threads for testing purposes, allowing validation of watchdog recovery behavior when the system becomes unresponsive.

### External SPI NOR Flash Integration
Added DeviceTree configuration for the external flash (ext_flash) to enable access through Zephyr flash APIs.

## Board-specific Overlay Configuration

Different pin configurations are used for the DK board and the AkidaTag board due to pin availability and hardware connections.

Both the **application overlay** and the **MCUboot overlay** are selected during the build using the `--dk` flag.

### DK Board
- **Application overlay:** `boards/nrf5340dk_nrf5340_cpuapp.overlay`
- **MCUboot overlay:** `sysbuild/mcuboot_dk.overlay`

### AkidaTag Board
- **Application overlay:** `boards/nrf5340_cpuapp_akidatag.overlay`
- **MCUboot overlay:** `sysbuild/mcuboot_akidatag.overlay`

### Model Generation and BLE Transfer

### Python Setup

```bash
cd AkidaTag
pip install -r scripts/requirements.txt
```

### info.yaml – Model Metadata File

`generate_info.py` writes an app-specific `info.yaml` from the converted model's shapes sidecar. It is the single source of truth for model metadata consumed by `send_model_via_ble.py`.

```yaml
app: kws
flash_address: "0x101000"
input_shape: [49, 10, 1]
output_shape: [1, 1, 225]
mfcc_fs: 123.56967163085938
silence_class: 10
unknown_class: 11
inference_mode: async
edge_learning:
  enabled: true
  num_classes: 15
  num_el_classes: 3
  num_neurons: 15
```

| Field | Description |
|-------|-------------|
| `app` | Model identifier / output-file prefix (e.g. `kws`, `mnist`) |
| `flash_address` | Target SPI flash address for the model data segment |
| `input_shape` | Model input dimensions read from the Akida model |
| `output_shape` | Model output dimensions read from the Akida model |
| `mfcc_fs` | MFCC normalisation scalar — divides every input feature. **Required for kws**: the firmware rejects a kws model whose `mfcc_fs` is missing/zero |
| `silence_class` | Output index of the silence class (skipped during keyword detection). **Required for kws** |
| `unknown_class` | Output index of the unknown/garbage class (skipped during keyword detection). **Required for kws** |
| `inference_mode` | `sync` or `async` — the Akida API mode the firmware applies for this model at load. On a DK board `async` falls back to `sync` with a warning. **Required for kws** |
| `edge_learning.enabled` | `true` when the model uses on-device edge learning |
| `edge_learning.num_classes` | Total number of classes in the base model |
| `edge_learning.num_el_classes` | Number of novel edge-learning classes to learn on-device (packed into lower 16 bits of the `num_edge_classes` metadata field sent over BLE) |
| `edge_learning.num_neurons` | Neurons per class (packed into upper 16 bits of the `num_edge_classes` metadata field sent over BLE) |

> **Edge learning packing:** `num_edge_classes` (32-bit) = `(num_neurons << 16) | num_el_classes`.
> The firmware unpacks this into `g_num_neurons_per_class` (bits [31:16]) and `g_num_edge_learn_classes` (bits [15:0]).

> ⚠️ **Metadata format / reflash note:** the model metadata struct (`model_meta_t`) and its
> CRC layout are versioned together by the firmware and `send_model_via_ble.py`. When fields
> are added (e.g. `inference_mode`), you must **rebuild + reflash the firmware and re-upload the
> model with the matching `send_model_via_ble.py`**. A model already in flash from an older
> format will fail the boot CRC check and won't load until re-uploaded.

---

### Model build config (`--config`)

`fetch_model.py` and `generate_info.py` take **only** `--config` — a per-(app, model) YAML file
at `.env/<app>/<model>.yaml` that holds every parameter. This keeps the values out of git and lets
CI run the exact same commands as local dev. The file is git-ignored and lives locally / on the
self-hosted runner.

```yaml
# .env/demo_apps/kws.yaml
app: demo_apps                                   # generate_info profile
model_name: kws                                  # file prefix for bins/cpp/.h + shapes sidecar
output_dir: source/external/model_files/kws      # converted artifacts + info.yaml are written here
model_url: http://<internal-host>/path/to/akida_model.fbz   # .fbz to download (VPN required)
map_mode: 2                                      # Akida MapMode (optional, default 1)
neurons_per_class: 1                             # regular kws: 1, edge-learning: e.g. 10
num_el_classes: 0                                # edge-learning novel classes (regular: 0)
flash_address: "0x101000"                        # quote so info.yaml keeps the hex form
mfcc_fs: 123.56967163085938                      # MFCC normalisation scalar
silence_class: 10
unknown_class: 11
inference_mode: async                            # sync | async (DK board falls back to sync)
```

Required keys: `app`, `model_name`, `output_dir` plus everything the chosen `app` profile needs
(for `demo_apps`: `flash_address`, `neurons_per_class`, `num_el_classes`, `mfcc_fs`,
`silence_class`, `unknown_class`, `inference_mode`). Optional: `map_mode` (default 1),
`akida_version` (default `v1`). The scripts report any missing keys by name.

### Step 1 – Fetch & Convert the Model

`fetch_model.py --config <file>` downloads the `.fbz` named by the config's `model_url` (or falls
back to `models.conf`) and converts it into the binary + C++ artifacts. It does **not** write
`info.yaml` — that is Step 2.

```bash
cd AkidaTag

# Regular KWS model → bins/cpp in model_files/kws (akida SDK lives in Docker, so use -d via run.sh)
python source/utils/fetch_model.py --config .env/demo_apps/kws.yaml

# Edge-learning KWS model → its own dir
python source/utils/fetch_model.py --config .env/demo_apps/kws_edge_learning.yaml
```

The config keys read here are `model_name`, `output_dir`, `model_url`, `map_mode`,
`neurons_per_class`, `akida_version`.

**Outputs** (in the config's `output_dir`):
- `<prefix>_program_info.bin` / `_program_data.bin` – binary segments for BLE transfer
- `<prefix>_program_info.cpp` / `_program_data.cpp` (+ `.h`) – C++ array files for compile-time inclusion
- `<prefix>_shapes.json` – shape sidecar (input/output shapes + edge-learning flag); consumed by Step 2 and used to skip regeneration on re-runs

---

### Step 2 – Generate info.yaml (per app)

`generate_info.py --config <file>` builds an **app-specific** `info.yaml` from the shapes sidecar
written in Step 1. It needs **no Akida SDK**, so it can run anywhere — e.g. on the host even when
Step 1 ran in Docker. The profile is taken from the config's `app:` key (only `demo_apps` exists
today); it decides which fields `info.yaml` carries.

```bash
cd AkidaTag

# Regular KWS model
python source/utils/generate_info.py --config .env/demo_apps/kws.yaml

# Edge-learning KWS model
python source/utils/generate_info.py --config .env/demo_apps/kws_edge_learning.yaml
```

The config keys read here (for the `demo_apps` profile) are `app`, `model_name`, `output_dir`,
`flash_address`, `neurons_per_class`, `num_el_classes`, `mfcc_fs`, `silence_class`,
`unknown_class`, `inference_mode`. The script reports any missing keys by name. `mfcc_fs` is the
model's normalisation scalar — use the value your model was trained with.

**Output:** `info.yaml` in the config's `output_dir`.

---

### Step 3 – Transfer via BLE

```bash
cd AkidaTag

# Recommended: pass info.yaml as the metadata source
python source/utils/send_model_via_ble.py \
    --info source/external/model_files/kws/kws_program_info.bin \
    --bin  source/external/model_files/kws/kws_program_data.bin \
    --yaml source/external/model_files/kws/info.yaml

# Legacy: explicit CLI args (still supported, override YAML values)
python source/utils/send_model_via_ble.py \
    --info source/external/model_files/kws/kws_program_info.bin \
    --bin  source/external/model_files/kws/kws_program_data.bin \
    --flash_address 0x101000 \
    --input_shape 49,10,1 \
    --output_shape 1,1,12
```

**New argument:** `--yaml <path>` — path to `info.yaml`. Explicit CLI args (`--flash_address`, `--input_shape`, etc.) take priority over YAML values when provided.

---

### Using run.sh (build + flash + model workflow)

`run.sh` wraps the steps above: `--fetch_model <config>` runs Step 1 (fetch + convert) and `--generate_info <config>` runs Step 2 (write `info.yaml`). Each takes its per-(app, model) YAML config (`.env/<app>/<model>.yaml`) as its argument. They are separate flags but can be combined (name the config on each); `--send_ble` (Step 3) is separate. Add `-d` to run the fetch step inside Docker (where the Akida SDK lives).

```bash
cd AkidaTag

# Step 1: Fetch + convert only (bins/cpp, no info.yaml)
./scripts/run.sh -d --fetch_model .env/demo_apps/kws.yaml

# Step 2: Generate the app-specific info.yaml for the converted model
./scripts/run.sh --generate_info .env/demo_apps/kws.yaml

# Both steps in one invocation (fetch runs first, then info.yaml)
./scripts/run.sh -d \
    --fetch_model .env/demo_apps/kws.yaml \
    --generate_info .env/demo_apps/kws.yaml
```

**Flags for the `run.sh` model workflow:**

| Flag | Default | Description |
|------|---------|-------------|
| `--fetch_model <config>` | off | Step 1: fetch `.fbz` + convert to bins/cpp (no `info.yaml`). Arg is the per-(app, model) YAML (`.env/<app>/<model>.yaml`) |
| `--generate_info <config>` | off | Step 2: write the app-specific `info.yaml` from the shapes sidecar. Arg is the same YAML config |
| `--send_ble` | off | Step 3: send model via BLE; requires `--info`, `--bin`, and `--yaml` (separate step; cannot be combined with `--fetch_model`) |
| `--app <name>` | `demo_apps` | Build/flash app (the `info.yaml` profile now comes from the config's `app:` key) |
| `--info <path>` | — | Path to `_program_info.bin` (use with `--send_ble`) |
| `--bin <path>` | — | Path to `_program_data.bin` (use with `--send_ble`) |
| `--yaml <path>` | — | Path to `info.yaml` (use with `--send_ble`) |

Output files are written to the directory given by the config's `output_dir`.

---

### Split Workflow (convert in Docker, BLE send on host)

Run the steps as independent commands — useful when the Akida SDK is only available inside Docker but BLE hardware is on the host. `--generate_info` needs no SDK, so it can run on the host too.

```bash
cd AkidaTag

# Step 1+2: Fetch, convert, and generate info.yaml for an edge-learning model inside Docker
./scripts/run.sh -d \
    --fetch_model .env/demo_apps/kws_edge_learning.yaml \
    --generate_info .env/demo_apps/kws_edge_learning.yaml

# Step 1+2: Same for a regular (non-edge-learning) model
./scripts/run.sh -d \
    --fetch_model .env/demo_apps/kws.yaml \
    --generate_info .env/demo_apps/kws.yaml

# Step 3: Send pre-generated files via BLE on the host (no Docker)
./scripts/run.sh --send_ble \
    --info source/external/model_files/kws/kws_program_info.bin \
    --bin  source/external/model_files/kws/kws_program_data.bin \
    --yaml source/external/model_files/kws/info.yaml
```
### UICR Configuration: nfct-pins-as-gpios
 This setting repurposes the NFC antenna pins (P0.02 and P0.03) as GPIOs.
 In this design, P0.03 is used for AKD async functionality, so enabling this
 configuration is required.
```
&uicr {
    nfct-pins-as-gpios;
};
```
**Note:** 
 - On nRF5340, P0.02 and P0.03 are dedicated NFC pins by default.
 - Enabling 'nfct-pins-as-gpios' writes to UICR (User Information Configuration Registers).
 - This is a one-time programmable setting and permanently disables NFC functionality
   on the chip until a full chip erase is performed.

### Application Security
This project signs its firmware images and verifies them via MCUboot, both at boot and on DFU (Device Firmware Update). Security is enforced through an RSA-3072 digital signature. Note that secure boot, an immutable first-stage bootloader that verifies MCUboot itself, is not enabled; it is listed on the roadmap.
The signing_key.pem file contains a Private Key used to cryptographically sign your firmware binaries. During the boot process and OTA updates, the bootloader (MCUboot) uses a corresponding Public Key (embedded in its own code) to verify that the firmware is authentic and has not been tampered with.

To generate this file run

```
./scripts/run.sh -d --key
```

KEEP THIS FILE SECRET. If an attacker gains access to signing_key.pem, they can sign and install malicious firmware on your devices. Never commit this file to public repositories.

### Console Logging Information
The MCUboot log messages are output over the same USB cable used to power the board. To view these logs, open minicom and connect to the corresponding USB serial port.
The demo_apps log messages are output on the dedicated UART pins. To view these logs, connect the UART TX/RX pins to a USB-to-TTL converter, plug the converter into the host PC, and open the associated serial port in minicom.

### Inference test
This application co-hosts both MNIST and KWS models in serial flash memory:

- MNIST model address: 0x1000
- KWS model address: 0x101000

Use the following commands on the console:

| Command | Description |
| --- | --- |
| `full_erase` | Erase full serial flash memory. This erases 16,773,120 bytes out of the total 16 MB serial flash. |
| `infer mnist` | Run MNIST inference. The DMIC must be stopped before running this inference by issuing `dmic_stop`. After the MNIST inference completes, issue `dmic_start` to restart DMIC and resume KWS inference |
| `infer kws` | Run KWS inference. |
| `dir` | List all the files in file system. |
| `mkfs` | Clear the file system. |
| `test_file` | Create a test file in file system. |
| `print_file <file_name>` | Print a file content present in file system. |
| `dmic_stop` | Stops the dmic. |
| `dmic_start` | Starts the dmic. |
| `threads_stop` | Terminate all running threads for testing the WDT. |
| `wdt_disable` | System crash for watchdog validation. |
| `device_id` | Print device ID |
| `kws_mode async` | Switches the system to Async mode. |
| `kws_mode sync` | Switches the system to Sync mode. |
| `kws_mode_get` | Displays the currently active KWS mode (Sync or Async). |

__Inference mode__
 -  Default mode, in this mode it captures live audio data and shows the inferred class id.
 -  Below CLI command changes mode from __Inference ---> Learn_select__.
```
>> app el 0
```
__Learn Select mode__
 - In this mode it allows user to select the novel class to be learned.
 - Below CLI command changes mode the application mode from __Learn_select ---> Learning__
```
>> app el 1
```
 - Below CLI command cycles between novel class selection which are to be learned. class_33 **-->** class_34 **-->** class_35 **-->** class_33.
```
>> app el 3
```
 - Below CLI command changes application mode from __Learn_select ---> Inference__ and the learned weights will be written to flash
```
>> app el 0
```
 - Below CLI command resets the learned weights. After this, restart the controller.
```
>> app el 2
```
 - The previously learned weights will be lost if a reboot of the controller occur in __Learn_select mode

__Learning mode__
 - Switching to learning mode is preceded with a forced delay to avoid learning button push sounds (incase learning happens through buttons)
 - Learning uses a **structured multi-utterance** flow: the user must speak the keyword **5 times**. Each utterance is captured, augmented, and fed to the on-chip learning engine before prompting for the next.
 - The sub-state machine for each utterance cycles through:
   1. **WAITING_FOR_SPEECH** — listens for RMS energy above threshold (uses a shorter 400ms VAD timeout for tighter capture)
   2. **CAPTURING** — records up to ~1.6s of MFCC frames into a float buffer
   3. **PROCESSING** — speech end detected (500ms gap); generates augmented training samples and calls `fit()` for each
   4. **COMPLETE** — all 5 utterances collected; weights are saved to flash and the system returns to inference mode
 - If no speech is detected for 5 seconds, the system re-prompts for the current utterance
 - Minimum utterance length is ~200ms (10 MFCC frames); shorter utterances are discarded with a re-prompt

#### Data Augmentation

Each utterance generates `2 × neurons_per_class` augmented training samples. Samples are created by combining **time-shifting** (spreading the keyword across different positions in the spectrogram window) with one of **8 augmentation types**, cycled per sample:

| Type | Description |
| --- | --- |
| 0 | Clean — no augmentation |
| 1 | Background noise (3–8% of full scale) |
| 2 | Gain scaling (0.75–1.25×) |
| 3 | Background noise + gain combined |
| 4 | Time-stretch (duplicate 1–2 frames) |
| 5 | Time-compress (skip 1–2 frames) |
| 6 | Frequency masking (zero out 1 random MFCC bin) |
| 7 | Heavy combined (noise + gain + frequency mask) |

### App Configuration Commands

The `app` command provides runtime configuration for the KWS (Keyword Spotting) pipeline:

| Command | Default | Description |
| --- | --- | --- |
| `app verbose <0\|1\|2>` | 0 | Verbose logging: 0=off, 1=pipeline trace, 2=adds idle RMS |
| `app rms <val>` | 550 | Set RMS energy threshold for speech detection (higher = less sensitive) |
| `app debounce <ms>` | 300 | Set debounce cooldown after keyword detection (prevents rapid re-triggers) |
| `app alpha <0.0-1.0>` | 0.70 | Set EMA smoothing factor for softmax scores (higher = less smoothing) |
| `app score <0.0-1.0>` | 0.60 | Set smoothed softmax score threshold for chiming counter |
| `app chiming <n>` | 3 | Set consecutive detections needed to trigger keyword |
| `app speech <ms>` | 1300 | Set speech active timeout (resets to idle if RMS stays low) |
| `app metrics <0\|1>` | 0 | Enable/disable detailed metrics output (confidence %, timing) |
| `app show` | — | Print all current parameters with usage |
| `app start` | — | Resume the KWS pipeline (starts the DMIC, re-arms the learning gate). On AkidaTag, restores the AKD1500 operating clocks (PLL on, 400 MHz core, 8 MHz host) |
| `app stop` | — | Halt the KWS pipeline (stops the DMIC, clears the learning gate). On AkidaTag, drops the AKD1500 to its lowest-power state (PLL off, asleep) |
| `app reset` | — | Restore all KWS params to compile-time defaults and persist to NVS |
| `app el <n>` | — | Edge learning commands (mode transitions) |

All numeric `app <param> <val>` setters above are routed through `kws_config_set_from_string()` — values are range-checked, written into NVS-backed settings, and the DMIC is briefly bounced (~120 ms) so the new value takes effect on the next inference frame. Persisted values are reloaded on boot; a firmware version change (tracked at NVS key `kws/fw_ver`) automatically resets every parameter back to its compile-time default.

#### Keyword Detection Output

**Default (metrics disabled):**
```
Keyword Detected: up
```

**With metrics enabled (app metrics 1):**
```
Keyword Detected: up
  confidence=100.0% smoothed=97.3% chiming=3 cpu=9ms dma=155us
```

Where:
- `confidence` — Raw softmax score from model output (0–100%)
- `smoothed` — EMA smoothed score used for trigger gating (0–100%)
- `chiming` — Consecutive detections that reached threshold
- `cpu` — Inference execution time in milliseconds
- `dma` — Akida DMA cycle time in microseconds

#### Tuning Guide

**Quieter room (more sensitive):**
```
app rms 400     (lower threshold detects quieter speech)
```

**Stricter detection (fewer false positives):**
```
app chiming 5   (require 5 consecutive detections instead of 3)
app score 0.70  (higher smoothed score threshold)
```

**Faster debounce (allowing repeated keywords):**
```
app debounce 200
```

### AKD1500 Clock, SPI & Low-Power

Runtime control of the AKD1500 host SPI, internal clocks, and low-power state. By default the AKD1500 core runs at 400 MHz off the 800 MHz PLL and the host SPI at 8 MHz; the chip is put to sleep between inferences, and `app stop` additionally turns the PLL off for the lowest-power idle. The SLEEP pin and the `app stop` power-down are AkidaTag-only; the clock/SPI commands work on both boards.

| Command | Description |
| --- | --- |
| `spi_freq <hz>` | Set the host SPI clock (1–32 MHz); the AKD1500 SPI_S core auto-scales to hold the ¼-rule margin |
| `akd_coreclk <hz>` | Set the AKD1500 core clock (5–400 MHz): divides the 800 MHz PLL, or reprograms the PLL for off-grid targets |
| `akd_pll <hz>` | Reprogram the PLL output directly (600–800 MHz, 12.5 MHz steps) |
| `akd_sysdiv <n>` | Set the core divider off the current PLL clock |
| `akd_clkref <0\|1>` | Clock source: `0` = PLL, `1` = 25 MHz reference (PLL off, lowest power) |
| `akd_sleep <0\|1>` | Take (`0`) or release (`1`) the shell's AKD1500 wake reference. SLEEP follows a reference count, so `1` gates the clocks only once every other holder (KWS inference, learning session, an in-progress flash access) has released too; the command reports the remaining count |
| `akd_clkinfo` | Dump the AKD1500 clock/PLL state and the operating host clock |

The clock/PLL/ref commands refuse while KWS is running (they would race the per-inference duty cycle) — stop it first with `app stop`. `akd_sleep` is exempt: it only takes or releases the shell's own wake reference and so cannot fight that duty cycle. Additional bring-up diagnostics: `akd_probe`, `akd_rdtest`, `akida_rd`/`akida_wr`, `spi_rxdelay`, `akd_pll_on`.

**Configuration (Kconfig):**
- **`CONFIG_AKD_SPI_FREQ_HZ`** (default `8000000`) — boot host SPI clock; runtime-tunable via `spi_freq`.
- **`CONFIG_AKD_CORE_CLOCK_HZ`** (default `400000000`) — boot AKD1500 core clock; runtime-tunable via `akd_coreclk`.
- **`CONFIG_SYS_CPU_128MHZ`** (default `y`) — run the nRF5340 app core at 128 MHz (required for SPIM4 at ≥ 16 MHz).

### Additional GPIO Configuration

The following GPIOs are added in the board overlay to control **power enabling for onboard sensors and peripherals on the AkidaTag board**.

| GPIO Label   | Pin   | Description |
|---------------|-------|-------------|
| `imui`        | P0.31 | IMU interrupt signal |
| `akd_enb`     | P0.19 | Enable pin for the AKIDA device |
| `acc_enb`     | P0.20 | Enable pin for the accelerometer |
| `pdm_enb`     | P0.21 | Enable pin for the PDM microphone |
| `akd_0v_enb`  | P0.22 | Enable control for AKIDA 0V supply |
| `cam_enb`     | P1.15 | Enable pin for the camera module |
| `akd_async`   | P0.03 | Enable pin for the AKIDA ASYNC |
| `fg_int`      | P0.30 | Fuel Gauge interrupt signal |
| `chgr_sts1`   | P0.23 | Read pin for battery status |
| `chgr_sts2`   | P0.24 | Read pin for battery status  |

These GPIOs are defined in the **DeviceTree overlay** and are used to manage power enabling of onboard components in the AkidaTag board.

### Inference Pipeline

The scoring pipeline uses **dequantized inference** with **softmax EMA smoothing** to produce stable, confidence-based keyword triggers:

1. **Dequantized inference** — `predict` runs inference on the Akida SNN and applies per-neuron shift and scale factors (from the model's program info) to convert discrete spike potentials into float values suitable for softmax.

2. **Per-class max pooling** — For models with multiple neurons per class, the maximum dequantized value across all neurons for each class is selected. This produces one logit per class.

3. **Softmax normalization** — Standard softmax (with max-subtraction for numerical stability) converts per-class logits into a probability distribution.

4. **EMA smoothing** — An exponential moving average filter smooths the softmax scores across consecutive inference frames, controlled by the `alpha` parameter (`smoothed = alpha × current + (1 - alpha) × previous`). Higher alpha values respond faster but are noisier.

5. **Chiming trigger** — A per-class counter increments each time the smoothed score exceeds the `score` threshold and resets to zero when it falls below. A keyword is triggered when any class counter reaches the `chiming` threshold. All counters reset after a trigger and during the debounce cooldown.
