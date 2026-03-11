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
This application uses the DMIC (PDM microphone) interface with PDM_CLK on P0.26 and PDM_DIN on P0.25.
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

### Build the demo_apps Sample.
After compiling the project, MCUBoot is automatically built along with the application.
The sysbuild system generates a combined image that includes both MCUBoot and the demo_apps application.

```
./scripts/run.sh -d -b --app demo_apps
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

### Model Loading
To load a model, execute the Python script from another terminal in the repository root folder (spark).

Upon execution, the script scans for available Bluetooth devices and displays a list of detected BLE servers.
Select the index corresponding to `Nordic_LBS` to pair with the device. Once pairing is complete, the client will begin transferring the model to the target device. 

For loading `KWS` model:
```
./scripts/run.sh -d --app demo_apps --bin source/external/model_files/kws/kws_program_data.bin
```


For loading `MNIST` model:
```
./scripts/run.sh -d --app demo_apps --bin source/external/model_files/mnist/mnist_program_data.bin
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







