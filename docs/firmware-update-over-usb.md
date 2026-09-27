# Firmware Update Over The USB-C Cable

Updating an AkidaTag board needs nothing but the USB-C cable it already ships with. No
debug probe, no J-Link, no Bluetooth, and no buttons.

This page is the one place that describes the procedure. Other documents point here.

---

## What this uses

The USB-C connector is wired to an on-board CP2105 USB-to-UART bridge, and that bridge's
UART reaches the nRF5340 on the same pins MCUboot is configured to listen on. The
bootloader's own serial recovery mode therefore answers over the cable.

The nRF5340's native USB pins do **not** reach the USB-C connector on this board, so USB
CDC flashing is not possible and is not what this page describes.

Listening at every boot is not free. `CONFIG_BOOT_SERIAL_WAIT_FOR_DFU_TIMEOUT` in
`src/sysbuild/mcuboot.conf` makes MCUboot hold the board for about a second before it starts
the application in every release: measured, the application banner
appears about 1.5 s after a reset. That timeout is a budget which also covers the slot 0
signature check, which happens whether or not this option is set, so raising it does not add
delay one for one.

---

## Before you start

Install the host tools once:

```sh
uv tool install smpmgr        # talks to the bootloader
pip install pyserial          # used by src/utils/usb_dfu_enter.py
```

You need a signed application image. Every build produces one at
`build_docker/demo_apps/src/zephyr/zephyr.signed.bin`. It must be signed with the key the
board's bootloader was built with, or the board will refuse it; see
[Application Security](../src/README.md#application-security).

---

## Step 1: Find the port

The CP2105 is a dual bridge, so the board presents **two** serial ports. Only one of them
is the debug UART. On macOS they look like this:

```
/dev/cu.usbserial-01D9C7390     the bridge's other interface, silent
/dev/cu.usbserial-01D9C7391     the debug UART
```

The trailing digit is the USB interface number, and the debug UART is the CP2105's
Standard interface, which is interface **1**. So it is the higher-numbered of the pair.

If you would rather confirm than trust the numbering, open each at 115200 baud and press
Enter while the application is running. The debug UART answers with the shell prompt:

```
uart:~$
```

The other port stays completely silent. Silence means you have the wrong port, not a
broken board.

---

## Step 2: Put the board into update mode

```sh
python src/utils/usb_dfu_enter.py --port /dev/cu.usbserial-01D9C7391
```

It prints `bootloader is in serial recovery` when the board is ready, after about four
seconds, and then keeps running for another twenty or so before it exits. **Both parts are
normal.** The board is already in update mode when that line appears.

The script reboots the running application over the same cable and then repeats a request
until the bootloader answers. It has to repeat, because MCUboot only listens for a short
window at each boot and `smpmgr` sends its request once and gives up. Once the bootloader
answers, it stays in update mode until the next reset, so there is no hurry afterwards.

The extra twenty seconds are the script clearing up after itself. Every repeated request the
bootloader accepted is owed a reply, and each reply costs it a full signature check over the
image, so they trail out slowly. The script waits for the last of them, which is what leaves
the port clean for `smpmgr`. If it cannot, it says so on stderr and still exits 0, because the
board is in update mode either way.

### The second entrance, for a board that cannot boot

`src/sysbuild/mcuboot.conf` deliberately sets `CONFIG_BOOT_SERIAL_NO_APPLICATION` alongside
the boot-time listening window. It gives the bootloader a second, independent entrance: when
the image in slot 0 fails signature validation, the bootloader stays in serial recovery over
the USB-C cable instead of halting, and waits there without any command from the host. That
is what makes a board carrying a bad image recoverable without a debug probe.

### What you see on the board

In update mode the red LED is on and steady, and the green LED stops blinking. A blinking
green LED means the application is running normally and the board is not in update mode.

---

## Step 3: List what is on the board

```sh
smpmgr --port /dev/cu.usbserial-01D9C7391 --baudrate 115200 \
       --line-length 128 --line-buffers 8 --timeout 20 \
       image state-read
```

```
slot=0,
version='1.1.1',
```

This takes a few seconds. Answering it makes the bootloader verify the whole image
signature, which is not instant.

---

## Step 4: Upload the new firmware

```sh
smpmgr --port /dev/cu.usbserial-01D9C7391 --baudrate 115200 \
       --line-length 128 --line-buffers 8 --timeout 30 \
       image upload --slot 0 build_docker/demo_apps/src/zephyr/zephyr.signed.bin
```

Expect about two minutes for a 450 KB image at 115200 baud. Run `image state-read` again
afterwards to see the new version in the slot.

The upload overwrites the application in place. The external flash holding the model is
not touched.

---

## Step 5: Start the new firmware

```sh
smpmgr --port /dev/cu.usbserial-01D9C7391 --baudrate 115200 \
       --line-length 128 --line-buffers 8 --timeout 10 \
       os reset
```

The board reboots into the uploaded image and prints its version on the same port:

```
*** Booting nRF Connect SDK v3.1.1 ***
I: App Core Version: 9.9.9+0
uart:~$
```

---

## What a refused image looks like

The bootloader accepts the upload itself without checking the signature, so an image
signed with the wrong key uploads to 100% and reports success. The refusal comes
afterwards, and it is unambiguous:

- `image state-read` reports **`No images on device!`**, even though the upload succeeded.
  The bootloader will not list an image whose signature does not verify.
- Resetting does not start it. The board goes back into update mode instead of running.
- The board is still reachable over the cable, so a wrongly signed image is not a brick.
  Upload a correctly signed one and it recovers.

If an image you expect to work is refused, it is signed with the wrong key. It is not a
cable or port problem.

---

## Troubleshooting

**`SMPBadSequence: Bad sequence N, expected 0`** means a reply from an earlier request was
still in the pipe. `src/utils/usb_dfu_enter.py` normally clears this before it exits, and
warns on stderr when it gave up waiting for the last of them. Either way the board is in
update mode: simply run the command again until it sticks.

**The script printed `bootloader is in serial recovery` and then sat there.** That is the
clean-up described in step 2, and the board is already in update mode. It normally takes
about twenty seconds. If the bootloader drops the script's last request the wait can reach
two minutes before it gives up and warns on stderr, which is slow but harmless.

**The script says the bootloader did not answer.** Check you are on the debug UART using
the shell-prompt test in step 1, and that the firmware is recent enough to enable
`CONFIG_BOOT_SERIAL_WAIT_FOR_DFU`; older builds required a button held during reset.

**Nothing on either port at all.** The board is not powered or the cable is charge-only.

---

## How this was verified

Every step above was run against an AkidaTag board over the USB-C cable, with the debug
probe attached but unused for the round trip:

| Step | Result |
|---|---|
| Enter update mode over the cable | bootloader answered 3.5 s after the reboot command |
| Enter update mode with no bootable image | bootloader waited there on its own |
| List images | `slot=0, version='1.1.1'` |
| Upload a correctly signed image | 100%, accepted |
| List again | `slot=0, version='9.9.9'` |
| Reset and boot it | `App Core Version: 9.9.9+0` |
| Upload an image signed with a different key | uploaded, then refused: not listed, not booted |

The LED behaviour in step 2 was confirmed by reading the GPIO registers while the board
sat in update mode (`P0.28` driven high, `P0.27` low), not by eye.
