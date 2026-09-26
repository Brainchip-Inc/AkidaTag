# AkidaTag user guide

This guide is for someone who has an AkidaTag with BrainChip's firmware already on it and wants to
run the demos from a phone. It covers installing the BrainChip Connect app, connecting to the tag,
running the keyword spotting demo, teaching the tag a new word, updating the firmware, loading a
model, and what to do when something does not work.

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
- The BrainChip Connect app, from TBD: Google Play link.

TBD: what is in the box. TBD: photo of the AkidaTag with the USB-C connector, the LEDs and the
button labelled.

### Charging

The USB-C connector charges the tag. While the app is connected, the applications screen shows
the battery level and, next to it, `Charging` while the tag is on charge, `Warning` for a charging
fault the tag can recover from, such as over-temperature or a timeout, and `Fault` for one it
cannot, such as an over-voltage.

TBD: charging time, battery life, and whether the battery is connected when the tag arrives. See
the [datasheet](hardware/datasheet.md).

### Turning the tag on and off

TBD: how the tag is powered on and off. The board has a user button and a DFU button; in this
firmware release the user button does nothing. TBD: whether the DFU button is fitted on the retail
unit and what it does.

## 2. What the LEDs mean

The tag has a green LED and a red LED.

| What you see                               | What it means                                                                                                   |
| ------------------------------------------ | --------------------------------------------------------------------------------------------------------------- |
| Green blinks slowly, red off               | The tag is running and waiting for a phone to connect.                                                          |
| Green on, red off                          | The app is connected.                                                                                           |
| Red flashes for about half a second        | The tag detected a keyword.                                                                                     |
| Red stays on while you are teaching a word | The tag is listening for you to say the word.                                                                   |
| Green on, red blinks quickly               | A model is being received from the app.                                                                         |
| Green on, red on                           | The model is being written to the tag's flash memory.                                                           |
| Both LEDs blink three times together       | The model was stored.                                                                                           |
| Green off, red on steady                   | A model transfer failed, or the tag is in its update mode over USB. See [Troubleshooting](#10-troubleshooting). |

## 3. Install BrainChip Connect

1. Install BrainChip Connect from TBD: Google Play link.
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
   [Troubleshooting](#10-troubleshooting).
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

The demo needs a model on the tag. TBD: whether the tag arrives with the model already loaded, and
which one. If it does not, load one first: see [Load a model](#8-load-a-model).

1. On the **Select the Application** screen, find the **Keyword Spotting** card. **More
   Information** shows the model name, its input shape, the number of classes and the list of
   keywords.
2. Tap **Run Application**. The badge on the card changes to _Starting_ and then _Active_ once
   the tag has confirmed it is listening.
3. Say one of the keywords. The microphone on the tag is a quiet part, so speak clearly and
   close to it. Each detection appears in the banner on the card with its confidence, and the red
   LED flashes for about half a second.
4. Tap **Application Dashboard** to see more:
   - **Detection** shows the latest keyword.
   - **Microphone**: tap **Start Streaming** to see the sound level the tag hears, which is the
     quickest way to check that the microphone picks you up at all. Tap **Stop Streaming** when
     you are done.
   - **Start Inference** and **Stop Inference** do the same as **Run Application** and **Stop
     Application** on the card.
   - **App Controls** lets you tune how the tag decides that it heard a keyword. The values are
     read from the tag; **Apply** writes them back and **Reset** restores the defaults.
5. Tap **Stop Application** when you are finished. The tag stops listening.

| Setting in the app | Default | What it does                                                                                                    |
| ------------------ | ------- | --------------------------------------------------------------------------------------------------------------- |
| RMS threshold      | 550     | How loud a sound must be before the tag treats it as speech. Lower it in a quiet room, raise it in a noisy one. |
| Debounce time      | 300 ms  | The pause after a detection before the next one can fire.                                                       |
| Smoothing alpha    | 0.70    | How quickly the score follows the model. Higher reacts faster but is noisier.                                   |
| Score threshold    | 0.60    | The score a keyword must reach. Raise it for fewer false detections.                                            |
| Chiming threshold  | 3       | How many consecutive frames must agree before a detection fires. Raise it for fewer false detections.           |
| Speech timeout     | 1300 ms | How long the tag keeps treating the input as speech after the sound drops.                                      |

The defaults are the ones the firmware uses. Values you apply are kept on the tag across
restarts, and are reset to the defaults by a firmware update.

TBD: screenshots of the Keyword Spotting card, a detection, and the dashboard.

## 6. Teach the tag a new word

The keyword spotting model can learn up to three new words on the tag itself. This is called edge
learning. It needs the edge-learning version of the model: with the plain model the tag ignores
the learning controls. TBD: which model the tag arrives with.

Known issue: on firmware v1.2.0+0 turning the Edge Learning switch on restarts the tag instead.
This is fixed on `main` and will be in the next release. Update the firmware first: see
[Update the firmware](#7-update-the-firmware-over-bluetooth).

1. Run the Keyword Spotting application and open the **Application Dashboard**.
2. Turn the **Edge Learning** switch on. The app waits for the tag to confirm before it shows the
   switch as on.
3. **Next Class** chooses which of the three spare slots the new word goes into. The slots are
   reported as `cls_12`, `cls_13` and `cls_14`. Each tap moves to the next slot and wraps around.
4. Tap **Start Learning**. After about a second the app shows _Ready to Speak_ and the red LED
   comes on.
5. Say the new word. The tag captures it, then waits for the next one. Say the word five times in
   all. Each time, the red LED is on while the tag is listening. If it hears nothing for five
   seconds it listens again for the same repetition. A word shorter than about a fifth of a second
   is discarded and asked for again.
6. After the fifth repetition the app shows _Edge Learning is completed_. The tag saves the new
   word and goes back to detecting keywords on its own. The new word is reported by its slot
   name, for example `cls_12`.

The app cannot read the learning state back from the tag, so the **Edge Learning** switch may
still show as on after the tag has finished. TBD: what the app should show after learning
completes; until then, leave the switch alone after _Edge Learning is completed_.

**Delete Class** erases every word the tag has learned. Restart the tag afterwards, from
**Settings > Factory Reset** or by power-cycling it.

TBD: screenshots of the Edge Learning controls and the Ready to Speak prompt.

## 7. Update the firmware over Bluetooth

New firmware is published on the releases page of the AkidaTag repository:
https://github.com/Brainchip-Inc/AkidaTag/releases. Each release attaches a file for updating over
Bluetooth, `akidatag-<version>.signed.bin`, and a package `akidatag-<version>-dfu.zip` that holds
the same image. The app accepts either. The `SHA256SUMS.txt` file lists the checksum of every
file, if you want to verify a download.

TBD: the current release, v1.2.0+0, predates the model transfer protocol that the app on `main`
uses, so a tag on v1.2.0+0 cannot load a model from this app. Update the firmware before loading a
model. TBD: the first release that carries the new protocol.

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

## 8. Load a model

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

## 9. Other things the app shows

- The **i** button in the header opens **Device Information**: the tag's name and hardware
  details.
- **Notifications** lists the events the app has received from the tag.
- **Settings > Factory Reset** restarts the tag. With this firmware it does not erase the model
  or your settings; a restart is all it does. The app disconnects and the tag comes back
  advertising on its own.
- **Settings > Power Mode** has no effect on the tag in this release.

## 10. Troubleshooting

| Problem                                                | What to check                                                                                                                                                                                                 |
| ------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| The app does not find the tag.                         | Bluetooth is on and the app has its Bluetooth permission. The tag's green LED is blinking; if it is solid green, another phone is connected. If no LED is lit, charge the tag.                                |
| Connection timed out.                                  | Move closer to the tag and try again. Restart the tag if it keeps happening.                                                                                                                                  |
| No keywords are detected.                              | The application is _Active_ on the card. Tap **Start Streaming** on the dashboard to see whether the tag hears you at all; speak closer and louder. Lower the RMS threshold in App Controls for a quiet room. |
| Too many false detections.                             | Raise the score threshold or the chiming threshold in App Controls, or raise the RMS threshold in a noisy room.                                                                                               |
| The tag restarts when I turn Edge Learning on.         | This is the known fault in firmware v1.2.0+0. Update to the next release.                                                                                                                                     |
| The learning controls do nothing.                      | The tag needs the edge-learning model. Load it, then try again.                                                                                                                                               |
| Update did not install.                                | The firmware is signed with a key this tag does not accept. Use a release from https://github.com/Brainchip-Inc/AkidaTag/releases.                                                                            |
| Model delivered, but not running.                      | Restart the tag. If it still does not run, the package is not for this tag.                                                                                                                                   |
| Model update failed.                                   | Stay near the tag, keep the app open and send the model again.                                                                                                                                                |
| Green off, red on steady, and the app cannot connect.  | The tag is in its update mode, waiting for firmware over USB. Plug it into a computer and follow [Firmware update over USB-C](firmware-update-over-usb.md), or power-cycle it.                                |
| The app cannot load a model on a tag running v1.2.0+0. | Update the firmware first. See [Update the firmware](#7-update-the-firmware-over-bluetooth).                                                                                                                  |

If none of this helps, TBD: support and community links.

---

TBD: footer
