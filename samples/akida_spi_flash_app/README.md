# Project akida_spi_flash_app
This source code is taken from NCS 3.1.1, under nrf/samples/bluetooth/peripheral_lbs/. For more details about this application, please refer to the README.rst file


## MCUBoot Integration Changes
The following updates were made to enable the nRF-provided MCUBoot bootloader:
- Added a sysbuild.conf file with `SB_CONFIG_BOOTLOADER_MCUBOOT=y`
- Updated prj.conf to include `CONFIG_BOOTLOADER_MCUBOOT=y` and `CONFIG_NCS_SAMPLE_MCUMGR_BT_OTA_DFU=y`
- Added a sysbuild/ directory containing mcuboot.conf
	- In this file, add `CONFIG_SERIAL=n` option to suppress mcuboot log messages
		
### Build the akida_spi_flash_app Sample.
After compiling the project, MCUBoot is automatically built along with the application.
The sysbuild system generates a combined image that includes both MCUBoot and the akida_spi_flash_app application.

```
./scripts/run.sh -d -b --app akida_spi_flash_app
```

### Flash the akida_spi_flash_app Sample.
This flashes both mcuboot and akida_spi_flash_app application together

```
./scripts/run.sh -d -f --app akida_spi_flash_app
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


### Model Loading
To load a model, execute the Python script from another terminal in the repository root folder (AkidaTAG).

Upon execution, the script scans for available Bluetooth devices and displays a list of detected BLE servers.
Select the index corresponding to `Nordic_LBS` to pair with the device. Once pairing is complete, the client will begin transferring the model to the target device. 

For loading `KWS` model:
`python sample/akida_spi_flash_app/utils/send_model_via_ble.py --bin sample/akida_spi_flash_app/external/model_files/kws/kws_program_data.bin`


For loading `MNIST` model:
`python send_model_via_ble.py --bin sample/akida_spi_flash_app/external/model_files/mnist/mnist_program_data.bin`


### Console Logging Information
The MCUboot log messages are output over the same USB cable used to power the board. To view these logs, open minicom and connect to the corresponding USB serial port.
The akida_spi_flash_app log messages are output on the dedicated UART pins. To view these logs, connect the UART TX/RX pins to a USB-to-TTL converter, plug the converter into the host PC, and open the associated serial port in minicom.

### Inference test
This application co-hosts both MNIST and KWS models in serial flash memory:

- MNIST model address: 0x1000
- KWS model address: 0x101000

Use the following commands on the console:

- Erase full serial flash memory: `full_erase`. This erases 16,773,120 bytes out of the total 16 MB serial flash.
- Run MNIST inference: `infer mnist`
- Run KWS inference: `infer kws`





