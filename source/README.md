# Project Akida Tag Source
This is Akida Tag complete application source code. This project includes all the necessary configurations to build the MCUboot bootloader, the network core image, and the application core image. The MCUboot secondary image slot is located in external serial flash.

The application uses the LittleFS file system and also keeps track of the number of system restarts the device has undergone.

##Generate the signing_key File
Run the command below to generate the signing_key.pem file in the spark.env/ directory. This file is required for the build to compile successfully.

Important: This key is intended only for development/testing. Do not commit this file to the Git repository. Extra care must be taken when handling signing keys for production software.

```
./scripts/run.sh -d --key
```

## MCUBoot Integration Changes
The following updates were made to enable the nRF-provided MCUBoot bootloader:
- Added a sysbuild.conf file with `SB_CONFIG_BOOTLOADER_MCUBOOT=y`
- Updated prj.conf to include `CONFIG_BOOTLOADER_MCUBOOT=y` and `CONFIG_NCS_SAMPLE_MCUMGR_BT_OTA_DFU=y`
- Added a sysbuild/ directory containing mcuboot.conf
	- In this file, add `CONFIG_SERIAL=n` option to suppress mcuboot log messages

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
			imu_set 5 3 5 1 5 5 8
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

 The Nordic UART Service (NUS) handles command processing with a sophisticated multi-frame protocol supporting single-frame messages for battery commands and multi-frame fragmentation for larger device information transfers, complete with retry logic and send-state management.


2. Phone-Firmware Communication Flow
The communication between the mobile application and firmware follows a simple command-response protocol over BLE's Nordic UART Service (NUS).
Command Frame Format: [frame_type],[index],[size],[command],[data]

MOBILE APP  ◄────────►   BLE STACK   ◄────────►     FIRMWARE

     │                           │                           │
     ├───Connect & Pair──────────┼────────────────────────────►│
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
     │                           │                           │    "AKIDA,TYPE,5.3,1.2.3"
     │                           │                           │
     │◄──Receive Response────────┼───────────────────────────┤
     │    "DEVICE:AKIDA,TYPE,    │                           │
     │           5.3,1.2.3"      │                           │
     │                           │                           │
     ├───Send Command────────────┼────────────────────────────►│
     │    "CMD_APP_INFO"         │                           │
     │                           │                           ├───Process Command
     │                           │                           │    Prepare application info
     │                           │                           │    (model, memory, version)
     │                           │                           │
     │◄──Receive Response────────┼──────────────────────────────┤
     │    Application metadata   │                           │
     │                           │                           │
	 ├───Send Command────────────┼────────────────────────────►│
     │    "CMD_DEPLOY_START"     │                           │
     │                           │                           ├───Set event_flag = true
     │                           │                           │    (KWS detection send start)
     │                           │                           │
     │◄──Receive KWS Events──────┼──────────────────────────────┤
     │    "KWS:hello"            │                           │
     │    "KWS:stop"             │                           │
     │    "KWS:yes"              │                           │
     │                           │                           │
     ├───Send Command────────────┼────────────────────────────►│
     │    "CMD_STREAM_START"     │                           │
     │                           │                           ├───Set pdm_stream_flag = true
     │                           │                           │    (Audio streaming active)
     │                           │                           │
     │◄──Receive PDM Audio Data──┼──────────────────────────────┤
     │    [Audio chunk 1]        │                           │
     │    [Audio chunk 2]        │                           │
     │    [Audio chunk 3]        │                           │
     │                           │                           │
     ├───Send Command────────────┼────────────────────────────►│
     │    "CMD_DEPLOY_STOP"      │                           │
     │                           │                           ├───Set event_flag = false
     │                           │                           │    (KWS detection sending stopped)
     │                           │                           │
     │◄──Receive Response────────┼──────────────────────────────┤
     │    "DEPLOY_STOP:ACK"      │                           │
     │                           │                           │
     ├───Send Command────────────┼────────────────────────────►│
     │    "CMD_STREAM_STOP"      │                           │
     │                           │                           ├───Set pdm_stream_flag = false
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

3. How It Works

Phone sends a command - The mobile app writes a command string to the NUS characteristic
Firmware parses the command - The nus_received_cb callback identifies which command was received
Firmware prepares response - Based on the command type, the appropriate data is gathered
Response is sent back - Data is formatted and transmitted to the phone via NUS
Phone displays the data - The app receives and presents the information to the user

NOTE: A static MAC address is required for phone app testing. Hence, CONFIG_BT_PRIVACY is disabled(CONFIG_BT_PRIVACY = n). Ensure this is re-enabled for the final production build.

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
     │                              │                             ├───Begin training process
     │                              │                             │   
     │                              │                             │
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

### Build the demo_apps Sample.
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
A CLI command is provided to display the unique Device ID of the SoC via the UART console. The device ID is read from the FICR registers and printed through the shell interface.

### FOTA over BLE using nRF Connect Mobile App
- Copy the updated application image `zephyr.signed.bin` to your mobile device.
- Install and open the nRF Connect Mobile app.
	- Ensure Bluetooth is enabled on your phone.
- Connect to the device NORDIC_LBS.
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

Different pin configurations are used for the DK board and the Spark board due to pin availability and hardware connections.

The overlay file is selected during the build using the `--dk` flag.

- **DK board overlay file:** `nrf5340dk_nrf5340_cpuapp.overlay`
- **Spark board overlay file:** `nrf5340_cpuapp_spark.overlay`

### Model Generation and BLE Transfer

### Python Setup

```bash
cd spark
pip install -r scripts/requirements.txt
```

### info.yaml – Model Metadata File

`fetch_model.py` generates an `info.yaml` alongside the binary files. It is the single source of truth for model metadata consumed by `send_model_via_ble.py`.

```yaml
model_name: kws
flash_address: "0x101000"
input_shape: [49, 10, 1]
output_shape: [1, 1, 225]
edge_learning:
  enabled: true
  num_classes: 15
  num_el_classes: 3
  num_neurons: 15
```

| Field | Description |
|-------|-------------|
| `model_name` | Model identifier string (e.g. `kws`, `mnist`) |
| `flash_address` | Target SPI flash address for the model data segment |
| `input_shape` | Model input dimensions read from the Akida model |
| `output_shape` | Model output dimensions read from the Akida model |
| `edge_learning.enabled` | `true` when the model uses on-device edge learning |
| `edge_learning.num_classes` | Total number of classes in the base model |
| `edge_learning.num_el_classes` | Number of novel edge-learning classes to learn on-device (packed into lower 16 bits of the `num_edge_classes` metadata field sent over BLE) |
| `edge_learning.num_neurons` | Neurons per class (packed into upper 16 bits of the `num_edge_classes` metadata field sent over BLE) |

> **Edge learning packing:** `num_edge_classes` (32-bit) = `(num_neurons << 16) | num_el_classes`.
> The firmware unpacks this into `g_num_neurons_per_class` (bits [31:16]) and `g_num_edge_learn_classes` (bits [15:0]).

---

### Step 1 – Generate Bin Files and info.yaml

```bash
cd spark

# KWS model at flash address 0x101000 with default MapMode=1
python source/utils/fetch_model.py \
    --model kws \
    --prefix kws \
    --output_dir source/external/model_files/kws \
    --flash_address 0x101000

# With a direct URL or local .fbz path
python source/utils/fetch_model.py \
    --model kws \
    --prefix kws \
    --output_dir source/external/model_files/kws \
    --model_path http://server/akida_model.fbz \
    --flash_address 0x101000 \
    --map_mode 1

# MNIST model at its flash address
python source/utils/fetch_model.py \
    --model mnist \
    --prefix mnist \
    --output_dir source/external/model_files/mnist \
    --flash_address 0x1000
```

**New arguments:**

| Argument | Default | Description |
|----------|---------|-------------|
| `--flash_address` | `0x1000` | Flash address written into `info.yaml` |
| `--map_mode` | `1` | Akida `MapMode` value passed to `model.map()` |

**Outputs** (in `--output_dir`):
- `<prefix>_program_info.bin` / `_program_data.bin` – binary segments for BLE transfer
- `<prefix>_program_info.cpp` / `_program_data.cpp` – C++ array files for compile-time inclusion
- `info.yaml` – metadata bridge consumed by `send_model_via_ble.py`
- `<prefix>_shapes.json` – shape sidecar (used to skip regeneration on re-runs)

---

### Step 2 – Transfer via BLE

```bash
cd spark

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

### One-Step Wrapper – run_model_transfer.sh

```bash
cd spark

# Default KWS model from BrainChip server
source/utils/run_model_transfer.sh

# KWS at a specific flash address and map mode
source/utils/run_model_transfer.sh \
    http://server/akida_model.fbz kws "" 0x101000 "" "" v1 "" 1

# Edge-learning model (10 classes)
source/utils/run_model_transfer.sh \
    http://server/akida_model_el.fbz kws "" 0x1000 "" 10
```

Positional arguments: `MODEL_PATH MODEL_NAME OUTPUT_DIR FLASH_ADDRESS FS_NAME NUM_CLASSES AKIDA_VERSION NEURONS_PER_CLASS MAP_MODE`

---

### Using run.sh (build + flash + model transfer)

```bash
cd spark

# Fetch model locally → generate bins + info.yaml only (no BLE send)
./scripts/run.sh \
    --model_transfer http://server/akida_model.fbz \
    --model_name kws \
    --model_flash_addr 0x101000

# Fetch inside Docker → generate bins + info.yaml only (no BLE send)
./scripts/run.sh -d \
    --model_transfer http://server/akida_model.fbz \
    --model_name kws \
    --model_flash_addr 0x101000

```

**Flags for `run.sh` model transfer:**

| Flag | Default | Description |
|------|---------|-------------|
| `--model_transfer <url/path>` | — | Fetch `.fbz`, generate bins + `info.yaml` (fetch only) |
| `--send_ble` | off | Send model via BLE; requires `--info`, `--bin`, and `--yaml` (cannot be used with `--model_transfer`) |
| `--model_name <name>` | `kws` | Model name prefix for output files |
| `--model_flash_addr <addr>` | `0x1000` | Flash address passed to `fetch_model.py` |
| `--map_mode <int>` | `1` | Akida `MapMode` value |
| `--info <path>` | — | Path to `_program_info.bin` (use with `--send_ble`) |
| `--bin <path>` | — | Path to `_program_data.bin` (use with `--send_ble`) |
| `--yaml <path>` | — | Path to `info.yaml` (use with `--send_ble`) |

Output files are written to `source/external/model_files/<model_name>/`.

---

### Two-Step Workflow (fetch in Docker, BLE send on host)

Run fetch and BLE transfer as two independent commands — useful when the Akida SDK is only available inside Docker but BLE hardware is on the host.

```bash
cd spark

# Step 1: Fetch model inside Docker → generates bins + info.yaml (no BLE send) 
For edge learning model
./scripts/run.sh -d \
    --model_transfer http://server/akida_model.fbz \
    --model_name kws \
    --model_flash_addr 0x101000 \
    --neurons_per_class 15 \
    --num_el_classes 3 \
    --map_mode 1

For non-edge learning model
./scripts/run.sh -d \
    --model_transfer http://server/akida_model.fbz \
    --model_name kws \
    --model_flash_addr 0x101000 \
    --neurons_per_class 1 \
    --num_el_classes 0 \
    --map_mode 1

# Step 2: Send pre-generated files via BLE on the host (no Docker)
./scripts/run.sh --send_ble \
    --info source/external/model_files/kws/kws_program_info.bin \
    --bin  source/external/model_files/kws/kws_program_data.bin \
    --yaml source/external/model_files/kws/info.yaml
```


### Application Security
This project utilizes Secure Boot and Secure DFU (Device Firmware Update) via MCUboot. Security is enforced through an RSA-3072 digital signature.
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

__Inference mode__
 -  Default mode, in this mode it captures live audio data and shows the inferred class id.
 -  Below CLI command changes mode from __Inference ---> Learn_select__.
```
>> kws_el evt 0
```
__Learn Select mode__
 - In this mode it allows user to select the novel class to be learned.
 - Below CLI command changes mode the application mode from __Learn_select ---> Learning__
```
>> kws_el evt 1
```
 - Below CLI command cycles between novel class selection which are to be learned. class_33 **-->** class_34 **-->** class_35 **-->** class_33.
```
>> kws_el evt 3
```
 - Below CLI command changes application mode from __Learn_select ---> Inference__ and the learned weights will be written to flash
```
>> kws_el evt 0
```
 - Below CLI command resets the learned weights. After this, restart the controller.
```
>> kws_el evt 2
```
 - The previously learned weights will be lost if a reboot of the controller occur in __Learn_select mode

__Learning mode__
 - Switching to learning mode is preceded with a forced delay to avoid learning button push sounds (incase learning happens through buttons)
 - In this mode it captures live audio data and use it for training the selected class.
 - Application continues to stay in **__Learning** mode until user switches the mode or if no valid samples are available for last 5 sec. In the later case, application switches from **__Learning ---> __Learn_select** after waiting for 5 sec.

### KWS Pipeline Configuration Commands

The `kws_el` command provides runtime configuration for the KWS (Keyword Spotting) pipeline:

| Command | Default | Description |
| --- | --- | --- |
| `kws_el verbose <0\|1>` | 0 | Enable/disable verbose logging (shows per-inference class and voting score) |
| `kws_el rms <threshold>` | 550 | Set RMS energy threshold for speech detection (higher = less sensitive) |
| `kws_el debounce <ms>` | 300 | Set debounce cooldown after keyword detection (prevents rapid re-triggers) |
| `kws_el window <n>` | 5 | Set sliding window size for score smoothing (1–5) |
| `kws_el score <f>` | 0.60 | Set trigger threshold as fraction of matching inferences (0.0–1.0) |
| `kws_el min_frames <n>` | 16 | Set minimum frames needed before inference starts (~320ms at default) |
| `kws_el speech <ms>` | 1300 | Set maximum speech duration window (silence resets state if exceeded) |
| `kws_el metrics <0\|1>` | 0 | Enable/disable detailed metrics output (confidence %, voting score, timing) |
| `kws_el show` | — | Print all current KWS parameters |

#### Keyword Detection Output

**Default (metrics disabled):**
```
Keyword Detected: up
```

**With metrics enabled (kws_el metrics 1):**
```
Keyword Detected: up
  confidence=98.5% vote=1.00 cpu=45ms dma=112us
```

Where:
- `confidence` — Softmax-based SNN confidence score (0–100%)
- `vote` — Sliding window voting score (fraction of recent matches)
- `cpu` — Inference execution time in milliseconds
- `dma` — Akida DMA cycle time in microseconds

#### Tuning Guide

**Quieter room (more sensitive):**
```
kws_el rms 400     (lower threshold detects quieter speech)
```

**Stricter detection (fewer false positives):**
```
kws_el window 3
kws_el score 0.67  (2 of 3 matches required instead of 3 of 5)
```

**Faster debounce (allowing repeated keywords):**
```
kws_el debounce 200
```

**Faster first detection:**
```
kws_el min_frames 10  (detection ~200ms instead of ~320ms)
```







