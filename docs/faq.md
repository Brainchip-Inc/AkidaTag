# AkidaTag FAQ

Short answers to the questions people ask first. The [user guide](user-guide.md) and the
[developer guide](developer-guide.md) have the detail.

## About the tag

**What is AkidaTag?** A small board with a Nordic nRF5340 microcontroller and a BrainChip Akida
AKD1500 neural processor, a microphone, an inertial sensor and Bluetooth Low Energy. The AI model
runs on the board; the phone only shows what the board found.

**What does it do out of the box?** It runs the keyword spotting demo: it recognises ten spoken
words and reports each one to the BrainChip Connect app. The model is loaded before the tag ships.

**Does it need the internet?** No. The tag talks to the phone over Bluetooth, and nothing in the
demo leaves the phone. The app needs the internet only to download firmware and model files.

**Is the battery included?** No. The box holds the board and its enclosure; there is no battery,
camera or USB-C cable in it.

**How do I turn it on?** With the power switch on the board. The green LED blinks slowly once it
is ready.

## Phone and app

**Which phones work?** Android phones running Android 13 or later, with Bluetooth. BrainChip
Connect is coming soon to the iOS App Store.

**Where do I get the app?** Its Google Play listing,
https://play.google.com/store/apps/details?id=com.brainchip.connect, is open for pre-registration.
Google Play tells you when it is released.

**Does the phone ask me to pair?** The firmware does not ask for a pairing code, so no pairing
pop-up appears.

**Can two phones connect at once?** No. One phone at a time. If the tag's green LED is solid,
another phone is already connected.

## Using the demo

**Which words does it know?** `down`, `go`, `left`, `no`, `off`, `on`, `right`, `stop`, `up` and
`yes`. Anything else is reported as silence or unknown and is not shown.

**It does not hear me.** The tag's microphone is a quiet part. Speak clearly and close to it, and
check that the Keyword Spotting card says _Active_.

**What do the LEDs mean?** See [What the LEDs mean](user-guide.md#2-what-the-leds-mean). The short
version: green blinking is ready, green on is connected, a red flash is a detection, red on steady
is a failure.

**Can it learn a new word?** The firmware can learn new words on the device, and every release
attaches `akidatag-kws-edge-learning-model.zip` for that. Running a learning session from the app
is not covered in this guide yet.

**What does Factory Reset do?** Today it restarts the tag and nothing else. A full reset that
erases the model and settings will come in a later release.

**What does Power Mode do?** Nothing in this release. It is where a low-power mode to save
battery will be switched on in a later release.

## Updating

**How do I update the firmware?** From the app, with a file from
https://github.com/Brainchip-Inc/AkidaTag/releases. See
[Update the firmware over Bluetooth](user-guide.md#6-update-the-firmware-over-bluetooth). A
computer and the USB-C cable work too: [Firmware update over USB-C](firmware-update-over-usb.md).

**The update did not install.** The tag only accepts firmware signed with the key it trusts. Use a
file from a BrainChip release. Anything built from the source code is refused over Bluetooth and
USB-C, by design.

**I cannot load a model.** Older firmware predates the model transfer the current app uses.
Update the tag to the latest release, https://github.com/Brainchip-Inc/AkidaTag/releases/latest,
and try again.

## Building your own

**Can I build the firmware myself?** Yes. The source is at https://github.com/Brainchip-Inc/AkidaTag
and the [developer guide](developer-guide.md) starts from a fresh clone. Read the caution in its
section 9 first.

**Will the tag accept my firmware?** Not over Bluetooth or USB-C: your build is signed with the
public development key, and a tag from BrainChip trusts the production key. With a debug probe
you can flash the whole image, after which the tag trusts your key instead. See
[Signing, and which firmware a board accepts](developer-guide.md#8-signing-and-which-firmware-a-board-accepts).

**Can I run my own model?** Yes, converted with the Akida tools and loaded like the demo model.
[Models](developer-guide.md#7-models) explains the pipeline.

## Help

**Where do I ask?** For help, BrainChip's Discord at https://discord.com/invite/9bmd9g52vn. For a
bug, an issue at https://github.com/Brainchip-Inc/AkidaTag/issues. [Support](support.md) says what
to include.

---

© 2026 BrainChip Holdings Ltd. All rights reserved.
