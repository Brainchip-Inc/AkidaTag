# Board Overlay Pin Configuration & Breaking Changes

## Overview

This document describes the pin configuration for two board overlays used with the **nRF5340 SoC**:

- **DK Board** (`nrf5340dk_nrf5340_cpuapp.overlay`) — Development kit overlay used for initial firmware bring-up and testing
- **Spark Board** (`nrf5340_cpuapp_akidatag.overlay`) — Custom production board overlay with updated pin assignments and additional GPIO definitions

---

## 1. SPI Bus Configuration

### SPI2 — AKIDA + AKIDA External Flash

| Signal | DK Board (P) | Spark Board (P) | Change |
|--------|-------------|----------------|--------|
| SCK | P0.08 | P0.08 | No change |
| MOSI | P0.09 | P0.09 | No change |
| MISO | **P1.08** | **P0.10** | Changed |
| CS0 (AKIDA) | P0.11 | P0.11 | No change |
| CS1 (AKIDA Flash) | P0.12 | P0.12 | No change |

> **Note (DK):** P1.08 was used for MISO on the DK board because P0.10 is not available on the DK hardware. The Spark custom board routes MISO correctly to P0.10.

---

### SPI3 — Camera / NRF External Flash

This is a **major structural change**. On the DK board, the camera and NRF flash are on separate SPI buses (SPI3 and SPI4). On the Spark board, both are consolidated onto **SPI3**.

#### DK Board — SPI3 (Camera only)

| Signal | Pin | Notes |
|--------|-----|-------|
| SCK | P0.05 | Alternate pins used — P0.17/P0.13/P0.14 occupied by onboard DK flash |
| MISO | P0.06 | |
| MOSI | P0.07 | |
| CS0 (Camera) | P0.25 | |

#### DK Board — SPI4 (NRF Flash only)

| Signal | Pin | Notes |
|--------|-----|-------|
| SCK | P0.17 | |
| MOSI | P0.13 | |
| MISO | P0.14 | |
| CS0 (ext_flash) | P0.18 | |

#### Spark Board — SPI3 (Camera + NRF Flash combined)

| Signal | Pin | Notes |
|--------|-----|-------|
| SCK | P0.17 | |
| MOSI | P0.13 | |
| MISO | P0.14 | |
| CS0 (Camera) | P0.25 | |
| CS1 (ext_flash) | P0.18 | Flash moved from SPI4 to SPI3 index 1 |

> **Breaking Change:** `spi4` is completely removed on the Spark board. The `ext_flash` node moves from `spi4` (reg=0) to `spi3` (reg=1).

---

### SPI Bus Summary

| Bus | DK Board | Spark Board |
|-----|----------|-------------|
| SPI2 | AKIDA + AKIDA Flash | AKIDA + AKIDA Flash (same) |
| SPI3 | Camera only (alt pins) | Camera + NRF Flash (production pins) |
| SPI4 | NRF Flash | **Removed** |

---

## 2. UART0 Configuration

No changes between boards.

| Signal | DK Board | Spark Board |
|--------|----------|-------------|
| TX | P0.29 | P0.29 |
| RX | P1.04 | P1.04 |
| Baud rate | 115200 | 115200 |

---

## 3. PDM / DMIC Configuration

No changes between boards.

| Signal | DK Board | Spark Board |
|--------|----------|-------------|
| PDM CLK | P1.09 | P1.09 |
| PDM DIN | P1.10 | P1.10 |
| Clock source | ACLK (12.288 MHz) | ACLK (12.288 MHz) |

---

## 4. I2C1 Configuration

No changes between boards.

| Signal | DK Board | Spark Board |
|--------|----------|-------------|
| SCL | P1.03 | P1.03 |
| SDA | P1.02 | P1.02 |
| Speed | 400 kHz (Fast) | 400 kHz (Fast) |
| Device | mysensor @ 0x6A | mysensor @ 0x6A |

---

## 5. GPIO — Control & Enable Pins

The DK board defines only basic LED GPIOs. The Spark board adds full power-enable and control GPIO definitions.

### Power Enable Pins (New on Spark board)

| Alias | Node | Pin | Direction | Active | DK Board | Spark Board |
|-------|------|-----|-----------|--------|----------|-------------|
| `akd-enb` | `akd_enb` | P0.19 | Output | HIGH |  Not defined |  Added |
| `acc-enb` | `acc_enb` | P0.20 | Output | HIGH |  Not defined |  Added |
| `pdm-enb` | `pdm_enb` | P0.21 | Output | HIGH |  Not defined |  Added |
| `akd-0v-enb` | `akd_0v_enb` | P0.22 | Output | HIGH |  Not defined |  Added |
| `cam_enb` | `cam_enb` | P1.15 | Output | HIGH |  Not defined |  Added |

---

### Control GPIOs

| Alias | Node | Pin | DK Board | Spark Board |
|-------|------|-----|----------|-------------|
| `akdsleep` | `akdsleep` | P0.23 |  Defined |  Defined |
| `akdreset` | `akdreset` | P1.13 |  Defined |  Defined |
| `imui` | `imui` | P0.31 |  Defined |  Defined |
| `akd_async` | `akd_async` | P0.03 |  Not defined |  Defined |
| `fg_int` | `fg_int` | P0.30 |  Not defined |  Defined |
| `chgr_sts1` | `chgr_sts1` | P0.23 |  Not defined |  Defined |
| `chgr_sts2` | `chgr_sts2` | P0.24 |  Not defined |  Defined |

---

## 6. User Button

| Property | DK Board | Spark Board |
|----------|----------|-------------|
| Node | Not defined | `user_btn` |
| Pin | — | P0.26 |
| Flags | — | `GPIO_PULL_UP \| GPIO_ACTIVE_LOW` |
| Interrupt | — | `GPIO_INT_EDGE_TO_ACTIVE` |
| Alias | — | `user-button` |

> **Breaking Change:** The DK board has no `user_btn` node.

---

## 7. DFU Button

| Property | DK Board | Spark Board |
|----------|----------|-------------|
| Node | Not defined | `dfu_button` |
| Pin | — | P1.01 |
| Flags | — | `GPIO_PULL_UP \| GPIO_ACTIVE_LOW` |
| Alias | — | `mcuboot-button0` |

---

## 8. LED Configuration

| LED | Pin | DK Board | Spark Board |
|-----|-----|----------|-------------|
| `led_red` | P0.28 |  Defined |  Defined |
| `led_green` | P0.27 |  Defined |  Defined |

Both boards define the same LED pins. Default DK board LEDs (`led0`–`led3`) are disabled on both overlays.

---

## 9. External Flash Node Location Change

| Property | DK Board | Spark Board |
|----------|----------|-------------|
| Node label | `ext_flash` | `ext_flash` |
| Parent bus | `spi4` | `spi3` |
| `reg` (CS index) | `<0>` | `<1>` |
| CS GPIO | P0.18 on SPI4 | P0.18 on SPI3 (CS index 1) |
| `compatible` | `jedec,spi-nor` | `jedec,spi-nor` |
| `size` | 64 Mbit (8 MB) | 64 Mbit (8 MB) |
| `jedec-id` | `[c2 28 17]` | `[c2 28 17]` |
| `spi-max-frequency` | 16 MHz | 16 MHz |

---

## 10.ADC Configuration

| Channel | Rail       | Pin     | Gain      | Reference       | Resolution |
|---------|------------|---------|-----------|-----------------|------------|
| CH0     | 1V8_AKD    | P0.04   | 1/3       | Internal (0.6V) | 12-bit     |
| CH1     | 0V8_AKD    | P0.05   | 1/3       | Internal (0.6V) | 12-bit     |

---

## 11. Disabled Peripherals (Both Boards)

Both overlays disable the following DK-specific peripherals to avoid conflicts:

- `qspi` — onboard DK QSPI flash
- `led0` – `led3` — default DK LEDs
- `button0` – `button3` — default DK buttons
- `gpio_fwd` — GPIO forwarding
- `pwm0` — PWM peripheral
- Default `buttons` and `leds` nodes

---

## 12. Full Pin Map Reference

| Function | Signal | DK Board Pin | Spark Board Pin | Changed |
|----------|--------|-------------|----------------|---------|
| SPI2 SCK | AKIDA/Flash CLK | P0.08 | P0.08 | — |
| SPI2 MOSI | AKIDA/Flash MOSI | P0.09 | P0.09 | — |
| SPI2 MISO | AKIDA/Flash MISO | **P1.08** | **P0.10** | Yes |
| SPI2 CS0 | AKIDA CS | P0.11 | P0.11 | — |
| SPI2 CS1 | AKIDA Flash CS | P0.12 | P0.12 | — |
| SPI3 SCK | Camera CLK | P0.05 | **P0.17** | Yes |
| SPI3 MOSI | Camera MOSI | P0.07 | **P0.13** | Yes |
| SPI3 MISO | Camera MISO | P0.06 | **P0.14** | Yes |
| SPI3 CS0 | Camera CS | P0.25 | P0.25 | — |
| SPI3 CS1 | NRF Flash CS | — (SPI4) | **P0.18** | Added |
| SPI4 SCK | NRF Flash CLK | P0.17 | **Removed** | Yes |
| SPI4 MOSI | NRF Flash MOSI | P0.13 | **Removed** | Yes |
| SPI4 MISO | NRF Flash MISO | P0.14 | **Removed** | Yes |
| SPI4 CS0 | NRF Flash CS | P0.18 | **Removed** | Yes |
| UART TX | Debug UART | P0.29 | P0.29 | — |
| UART RX | Debug UART | P1.04 | P1.04 | — |
| PDM CLK | Microphone CLK | P1.09 | P1.09 | — |
| PDM DIN | Microphone DATA | P1.10 | P1.10 | — |
| I2C SCL | IMU/Sensor | P1.03 | P1.03 | — |
| I2C SDA | IMU/Sensor | P1.02 | P1.02 | — |
| GPIO | user_btn | — | P0.26 | Added |
| GPIO | dfu_button | — | P1.01 | Added |
| GPIO | AKD Sleep | P0.23 | P0.23 | — |
| GPIO | AKD Reset | P1.13 | P1.13 | — |
| GPIO | akd_async | — | P0.03 | Added |
| GPIO | IMU Interrupt | P0.31 | P0.31 | — |
| GPIO | AKD Enable | — | **P0.19** | Added |
| GPIO | ACC Enable | — | **P0.20** | Added |
| GPIO | PDM Enable | — | **P0.21** | Added |
| GPIO | AKD 0V Enable | — | **P0.22** | Added |
| GPIO | User Button | — | **P0.26** | Added |
| GPIO | LED Red | P0.28 | P0.28 | — |
| GPIO | LED Green | P0.27 | P0.27 | — |
| GPIO | cam_enb | — | P1.15 | Added |
| GPIO | fg_int | — | P0.30 | Added |
| GPIO | chgr_sts1 | — | P0.23 | Added |
| GPIO | chgr_sts2 | — | P0.24 | Added |
| ADC  | CH0 | — | P0.04 | Added |
| ADC  | CH1 | — | P0.05 | Added |

---
