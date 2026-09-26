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

**Is the battery included?** No. TBD: which battery the tag takes and how to fit it.

**How do I turn it on?** The rev2 board has a power switch. TBD: which revision ships, and how a
tag without the switch is turned on and off.

## Phone and app

**Which phones work?** Android phones running Android 13 or later, with Bluetooth. An iOS version
of BrainChip Connect is planned.

**Where do I get the app?** Its Google Play listing,
https://play.google.com/store/apps/details?id=com.brainchip.connect, is open for pre-registration.
Google Play tells you when it is released. TBD: the release date.

**Does the phone ask me to pair?** The firmware does not ask for a pairing code, so no pairing
pop-up appears. TBD: whether that changes before launch.

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

**Can it learn a new word?** The firmware can learn new words on the device. TBD: how that is done
from the app; the current guide leaves it out until the app side is settled.

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

**I cannot load a model.** A tag on firmware v1.2.0+0 predates the model transfer the current app
uses. Update the firmware first. TBD: the first release that carries the new transfer.

## Building your own

**Can I build the firmware myself?** Yes. The source is at https://github.com/Brainchip-Inc/AkidaTag
and the [developer guide](developer-guide.md) starts from a fresh clone. Read the caution in its
section 9 first.

**Will the tag accept my firmware?** Not over Bluetooth or USB-C: your build is signed with the
public development key, and a tag from BrainChip trusts the production key. With a debug probe
you can flash the whole image, after which the tag trusts your key instead. See
[Signing, and which firmware a board accepts](developer-guide.md#8-signing-and-which-firmware-a-board-accepts).

**Can I run my own model?** Yes, converted with the Akida tools and loaded like the demo model.
[Models](developer-guide.md#7-models) explains the pipeline. TBD: where developers outside
BrainChip get a model to start from.

## Help

**Where do I ask?** Open an issue at https://github.com/Brainchip-Inc/AkidaTag/issues, or ask on
BrainChip's Discord at https://discord.com/invite/9bmd9g52vn. [Support](support.md) says what to
include.

---

© 2026 BrainChip Holdings Ltd. All rights reserved.
