# Project Akida Tag Source
This is Akida Tag complete application source code. This project includes all the necessary configurations to build the MCUboot bootloader, the network core image, and the application core image. The MCUboot secondary image slot is located in external serial flash.

The application uses the LittleFS file system and also keeps track of the number of system restarts the device has undergone.

##Generate the signing_key File
Run the command below to generate the signing_key.pem file in the spark.env/ directory. This file is required for the build to compile successfully.

Important: This key is intended only for development/testing. Do not commit this file to the Git repository. Extra care must be taken when handling signing keys for production software.
```
./scripts/run.sh -d --key --app demo_apps
```

## MCUBoot Integration Changes
The following updates were made to enable the nRF-provided MCUBoot bootloader:
- Added a sysbuild.conf file with `SB_CONFIG_BOOTLOADER_MCUBOOT=y`
- Updated prj.conf to include `CONFIG_BOOTLOADER_MCUBOOT=y` and `CONFIG_NCS_SAMPLE_MCUMGR_BT_OTA_DFU=y`
- Added a sysbuild/ directory containing mcuboot.conf
	- In this file, add `CONFIG_SERIAL=n` option to suppress mcuboot log messages
		
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
| `infer mnist` | Run MNIST inference. |
| `infer kws` | Run KWS inference. |
| `dir` | List all the files in file system. |
| `mkfs` | Clear the file system. |
| `test_file` | Create a test file in file system. |
| `print_file <file_name>` | Print a file content present in file system. |





