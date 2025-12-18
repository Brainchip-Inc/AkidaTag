# Project akida_simple_app
This source code is taken from NCS 3.1.1, under nrf/samples/bluetooth/peripheral_lbs/. For more details about this application, please refer to the README.rst file


## MCUBoot Integration Changes
The following updates were made to enable the nRF-provided MCUBoot bootloader:
- Added a sysbuild.conf file with `SB_CONFIG_BOOTLOADER_MCUBOOT=y`
- Updated prj.conf to include `CONFIG_BOOTLOADER_MCUBOOT=y` and `CONFIG_NCS_SAMPLE_MCUMGR_BT_OTA_DFU=y`
- Added a sysbuild/ directory containing mcuboot.conf
	- In this file, add `CONFIG_SERIAL=n` option to suppress mcuboot log messages
		
### Build the akida_simple_app Sample.
After compiling the project, MCUBoot is automatically built along with the application.
The sysbuild system generates a combined image that includes both MCUBoot and the akida_simple_app application. This application performs a static KWS frame inference upon flashing.

```
./scripts/run.sh -d -b --app akida_simple_app
```

### Flash the akida_simple_app Sample.
This flashes both mcuboot and akida_simple_app application together

```
./scripts/run.sh -d -f --app akida_simple_app
```

### To reset the board

```
./scripts/run.sh -d -r
```

### Console Logging Information
The MCUboot log messages are output over the same USB cable used to power the board. To view these logs, open minicom and connect to the corresponding USB serial port.
The akida_simple_app log messages are output on the dedicated UART pins. To view these logs, connect the UART TX/RX pins to a USB-to-TTL converter, plug the converter into the host PC, and open the associated serial port in minicom.
