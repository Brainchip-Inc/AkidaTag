# AkidaTag user guide

This guide is for someone who has an AkidaTag with BrainChip's firmware already on it and wants to
run the demos from a phone. It covers installing the BrainChip Connect app, connecting to the tag,
what the LEDs mean, running the keyword spotting demo, updating the firmware, loading a model, and
what to do when something does not work.

It was written from the source code of the firmware on the `main` branch of the AkidaTag
repository, after release v1.2.0+0, and of BrainChip Connect on its `main` branch, release
v1.0.0+0. Where a step could not be confirmed from the code it is marked `TBD`.

If you want to build the firmware or write your own application, read the
[developer guide](developer-guide.md) instead.

## 1. Before you start

You need:

- An AkidaTag.
- Its USB-C cable, for charging.
- An Android phone running Android 13 or later, with Bluetooth. An iOS version of the app is
  planned.
- The BrainChip Connect app. Its Google Play listing,
  https://play.google.com/store/apps/details?id=com.brainchip.connect, is open for
  pre-registration. TBD: the app's release date.

In the box is the AkidaTag in its enclosure, with the latest firmware release and the keyword
spotting model already loaded. The battery is not included. TBD: which battery the tag takes and
how to fit it. TBD: photo of the AkidaTag with the USB-C connector, the LEDs and the button
labelled.

### Charging

The USB-C connector charges the tag. While the app is connected, the applications screen shows
the battery level and, next to it, `Charging` while the tag is on charge, `Warning` for a charging
fault the tag can recover from, such as over-temperature or a timeout, and `Fault` for one it
cannot, such as an over-voltage.

TBD: charging time and battery life. See the [datasheet](hardware/datasheet.md).

### Turning the tag on and off

The rev2 board has a power switch. TBD: which board revision ships, and how a tag without the
switch is turned on and off. The board also has a user button and a DFU button. In this firmware
release the user button does nothing; a reset function for it is planned. TBD: whether the DFU
button is fitted on the retail unit and what it does.

## 2. What the LEDs mean

The tag has a green LED and a red LED.

| What you see                         | What it means                                                                                                  |
| ------------------------------------ | -------------------------------------------------------------------------------------------------------------- |
| Green blinks slowly, red off         | The tag is running and waiting for a phone to connect.                                                         |
| Green on, red off                    | The app is connected.                                                                                          |
| Red flashes for about half a second  | The tag detected a keyword.                                                                                    |
| Red on, green as before              | The tag is in an edge-learning session, listening for a word to learn.                                         |
| Green on, red blinks quickly         | A model is being received from the app.                                                                        |
| Green on, red on                     | The model is being written to the tag's flash memory.                                                          |
| Both LEDs blink three times together | The model was stored.                                                                                          |
| Green off, red on steady             | A model transfer failed, or the tag is in its update mode over USB. See [Troubleshooting](#9-troubleshooting). |

## 3. Install BrainChip Connect

1. Pre-register for BrainChip Connect on Google Play at
   https://play.google.com/store/apps/details?id=com.brainchip.connect. Google Play tells you when
   the app is released. TBD: the release date.
2. Open it. The first screens introduce the app and ask you to accept its terms and its
   privacy policy.
3. Grant the permissions it asks for. Bluetooth is required to find and connect to the tag.
   Notifications are optional and are used for alerts about model events, firmware updates and
   device status.

TBD: screenshots of the Get Started, Terms, Privacy Policy and Permissions screens.

## 4. Connect to your AkidaTag

1. Make sure the tag is on: the green LED blinks slowly.
2. In the app, open the device list. The app scans for boards that carry a BrainChip Akida
   processor and shows the tag as `AkidaTag`. If the list stays empty, see
   [Troubleshooting](#9-troubleshooting).
3. Tap the tag. A preview screen shows its name and signal strength.
4. Tap **Connect to Device**. The app shows two steps, _Establishing connection_ and _Syncing
   configuration_, and then opens the **Select the Application** screen. The green LED stays on
   while the app is connected.

The firmware on `main` does not ask the phone for a pairing code. TBD: confirm whether your phone
shows a Bluetooth pairing request on first connection, and whether the retail firmware turns
pairing on.

Only one phone can be connected to a tag at a time. If the app cannot connect, check that another
phone is not already connected.

TBD: screenshots of the device list, the preview screen, the connecting screen and the
applications screen.

## 5. Run the keyword spotting demo

The tag ships with one demo, **Keyword Spotting**. It listens through the microphone on the tag
and recognises ten spoken words: `down`, `go`, `left`, `no`, `off`, `on`, `right`, `stop`, `up`
and `yes`. Everything else is reported as silence or unknown and is not shown.

The tag arrives with the keyword spotting model already loaded, so the demo is ready to run. If
the tag has lost its model, load one first: see [Load a model](#7-load-a-model). TBD: the model's
name as the app shows it.

1. On the **Select the Application** screen, find the **Keyword Spotting** card. **More
   Information** shows the model name, its input shape, the number of classes and the list of
   keywords.
2. Tap **Run Application**. The badge on the card changes to _Starting_ and then _Active_ once
   the tag has confirmed it is listening.
3. Say one of the keywords. The microphone on the tag is a quiet part, so speak clearly and
   close to it. Each detection appears in the banner on the card with its confidence, and the red
   LED flashes for about half a second.
4. Tap **Stop Application** when you are finished. The tag stops listening.

TBD: screenshots of the Keyword Spotting card and a detection.

## 6. Update the firmware over Bluetooth

New firmware is published on the releases page of the AkidaTag repository:
https://github.com/Brainchip-Inc/AkidaTag/releases. Each release attaches a file for updating over
Bluetooth, `akidatag-<version>.signed.bin`, and a package `akidatag-<version>-dfu.zip` that holds
the same image. The app accepts either. The `SHA256SUMS.txt` file lists the checksum of every
file, if you want to verify a download.

TBD: the current release, v1.2.0+0, predates the model transfer protocol that the app on `main`
uses, so a tag on v1.2.0+0 cannot load a model from this app. Update the firmware before loading a
model. A release cut from `main` before launch carries the protocol the app uses; TBD: its
version number, which this guide will name.

1. Download the firmware file to your phone.
2. Connect to the tag and open **Settings** from the bar at the bottom.
3. Tap **Firmware Update**, then **Browse Local Firmware**, and pick the file. The app reads the
   version out of the file and shows it. If the file is signed with a different key from the last
   firmware the app installed on this tag, it warns _This file may not install_; a tag from
   BrainChip accepts only BrainChip's release firmware.
4. Tap **Install This Build**. Keep the app open and stay near the tag.
5. The app sends the firmware, asks the tag to restart, and then waits for it to come back. The
   wait can take up to two minutes. Do not power the tag off.
6. The app reports what the tag did:

| The app says                  | What it means                                                                                                                                                       |
| ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Update installed              | The tag is running the new firmware, confirmed after it restarted.                                                                                                  |
| Update did not install        | The tag refused the firmware and restarted on its previous version. It never says why. The usual cause is a file signed with a different key.                       |
| Your AkidaTag did not restart | The firmware was sent but the tag did not restart. It may install the next time the tag is powered off and on.                                                      |
| Update failed                 | The transfer did not complete. The tag is still running its previous firmware.                                                                                      |
| Could not confirm the update  | The firmware was sent but the app could not find the tag again afterwards, or the tag would not say which version it runs. Reconnect to the tag to see its version. |

**Current Build** on the Firmware Update screen shows _No Build Installed_ until you have
installed a firmware through the app. That does not mean the tag has no firmware.

A firmware update leaves the model on the tag in place.

You can also update the tag over its USB-C cable from a computer, with no phone. That path is
described in the developer guide under
[Firmware update over USB-C](firmware-update-over-usb.md).

TBD: screenshots of the Firmware Update screen and the progress dialog.

## 7. Load a model

The model is a `.zip` package attached to the firmware release: TBD: model asset name. It holds
three files, `info.yaml`, `kws_program_info.bin` and `kws_program_data.bin`. Do not unzip it.

TBD: whether a plain keyword spotting package and an edge-learning package are both published,
and which one to pick.

1. Download the package to your phone.
2. Connect to the tag and open **Settings**, then **Model Update**.
3. Tap **Browse Local Model** and pick the `.zip` file.
4. Tap **Install Model**. The app sends the model in blocks and the tag writes each block to its
   flash memory as it arrives: the green LED stays on and the red LED blinks quickly, then both
   are on while a block is written. You can tap **Stop Update** during the transfer; the tag is
   then left without a model until a transfer completes.
5. When the whole model is across, the app shows _Installing on your AkidaTag_. The tag checks
   the file, programs it into the Akida processor and runs a test inference, which takes a few
   seconds. Both LEDs blink three times together when the model is stored.
6. The app reports the result:

| The app says                     | What it means                                                                                                                                                  |
| -------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Model installed                  | The tag has the model and is running it.                                                                                                                       |
| Model delivered, but not running | The model was stored but the tag could not start it. Restart the tag, which makes it try again. If that does not help, this model is not one this tag can run. |
| Model update failed              | The transfer did not complete. If the tag was replacing its model, it has no model until one is sent in full.                                                  |

The tag holds one model at a time. Loading a new one replaces it. The model stays on the tag
across restarts and firmware updates.

**Current Version** on the Model Update screen shows _Not Found_ until you have installed a model
through the app.

TBD: screenshots of the Model Update screen and the progress dialog.

## 8. Other things the app shows

- The **i** button in the header opens **Device Information**: the tag's name and hardware
  details.
- **Notifications** lists the events the app has received from the tag.
- **Settings > Factory Reset** restarts the tag. A full factory reset, one that erases the model
  and your settings, is not in this firmware yet and will come in a later release. Today the app
  disconnects and the tag comes back advertising on its own, with everything still in place.
- **Settings > Power Mode** is where a low-power mode will be switched on to save battery. In
  this release it changes nothing on the tag; the feature will come in a later release.

## 9. Troubleshooting

| Problem                                                | What to check                                                                                                                                                                  |
| ------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| The app does not find the tag.                         | Bluetooth is on and the app has its Bluetooth permission. The tag's green LED is blinking; if it is solid green, another phone is connected. If no LED is lit, charge the tag. |
| Connection timed out.                                  | Move closer to the tag and try again. Restart the tag if it keeps happening.                                                                                                   |
| No keywords are detected.                              | The application is _Active_ on the card. Speak clearly and close to the tag; its microphone is a quiet part.                                                                   |
| Update did not install.                                | The firmware is signed with a key this tag does not accept. Use a release from https://github.com/Brainchip-Inc/AkidaTag/releases.                                             |
| Model delivered, but not running.                      | Restart the tag. If it still does not run, the package is not for this tag.                                                                                                    |
| Model update failed.                                   | Stay near the tag, keep the app open and send the model again.                                                                                                                 |
| Green off, red on steady, and the app cannot connect.  | The tag is in its update mode, waiting for firmware over USB. Plug it into a computer and follow [Firmware update over USB-C](firmware-update-over-usb.md), or power-cycle it. |
| The app cannot load a model on a tag running v1.2.0+0. | Update the firmware first. See [Update the firmware](#6-update-the-firmware-over-bluetooth).                                                                                   |

If none of this helps, the [FAQ](faq.md) answers the common questions, and [Support](support.md)
says where to ask: an issue at https://github.com/Brainchip-Inc/AkidaTag/issues first, or
BrainChip's Discord at https://discord.com/invite/9bmd9g52vn.

---

© 2026 BrainChip Holdings Ltd. All rights reserved.
