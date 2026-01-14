# Commands specific to demo_apps
This README contains specific commands related to demo_apps

## Capture raw audio and generate the wave file

- Build the application by passing -DCONFIG_AUDIO_CAPTURE_TEST=y during compilation. In this mode, only raw frames are captured, up to the configured CONFIG_SRAM_BUFFER_SIZE bytes (ensure that sufficient memory is available). After building and flashing the application to the nRF device, run the following commands:
  - `cap_start`: initiates raw audio frame capture into the buffer. The user must speak into the PDM microphone during the capture period. Recording automatically terminates when the buffer is full.
  - `dmic_stop`: Stops the dmic.
  - `dump_uart`: Dumps the captured frames to the UART port. Ensure that the port is open before using the following command:

  ```
  minicom -D /dev/ttyUSB0 -b 115200 -C pcm.txt
  ```

- When the dump completes (as indicated by the “dump completed” message on the terminal):
  - Open the pcm.txt file and remove all characters except the captured audio frames.
  - After cleaning the file, run the Python script to generate the WAV file:
  
  ```
  python source/utils/pcm_to_wav.py <input_pcm.txt> <output_name>
  ```

	