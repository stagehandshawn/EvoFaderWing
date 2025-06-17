# EvoFaderWing

**Author:** ShawnR  
**Contact:** [stagehandshawn@gmail.com](mailto:stagehandshawn@gmail.com)

![FaderWing Image](docs/images/faderwing.jpg)

[![Watch the demo video](https://img.youtube.com/vi/fbl81pGS5f4/0.jpg)](https://youtu.be/fbl81pGS5f4)

---

## Overview

OSC control of grandMA3 faders with motorized feedback and backlit faders, 20 encoders, and 40 standard exec buttons.

> **Note:** This is a work in progress (WIP) but is fully functional. This is not a complete guide yet, as I just do not have the time to make one. However, here is a limited Bill of Materials (BOM).

Any questions can be sent to me at [stagehandshawn@gmail.com](mailto:stagehandshawn@gmail.com). I'll help when I can.

There are pinouts in the `images` and/or `docs` folders. These, along with the pictures, should give you an idea of how you'll want to route wires.

I have not coded in years and was never great at it, so I leaned on AI to make this happen. The code could use some work, I'm sure.

Image `00_Connections` gives an idea of the layout.

---

## Required repositories

There are 3 repositories you will need to complete this build. This one is the main controller. The other two are:

- **EvoFaderWing_keyboard_i2c**
- **EvoFaderWing_encoder_i2c**

These run on ATmega328p MCUs and are I2C slaves for getting encoder data from 20 encoders (5 each on 4 ATmega328p's), and 1 ATmega for the keyboard matrix.

There is a Python script and a `tasks.json` for uploading code automatically for when the FaderWing is closed and you cannot get to the bootloader button.

More to come. Thank you for checking out my project!

---

## Bill of Materials (BOM)

You will find most of the BOM here. I will add more when I can. You are probably going to need a decent amount of electronic building gear already, like a soldering iron and solder, wires, and general know-how of Arduino-type projects is a big help.

> **NOTICE:** If you enable serial debug, you will notice jitter in the faders. Serial debug **must be turned off when not debugging.**

### Main components

- **Teensy 4.1 + Ethernet Kit**  
  [Amazon Link](https://www.amazon.com/dp/B08RSCFBNF?ref=ppx_yo2ov_dt_b_fed_asin_title)
- **Teensy 4.1 Breakout Board**  
  [Amazon Link](https://www.amazon.com/dp/B0BYTHZ6T2?ref_=ppx_hzsearch_conn_dt_b_fed_asin_title_4)
- **JST-XH Connector Kit**  
  [Amazon Link](https://www.amazon.com/dp/B09ZTWCZ3K?ref=ppx_yo2ov_dt_b_fed_asin_title)
- **JST-XH Connectors (larger sizes are hard to find, used these and removed wires)**
  [Amazon Link](https://www.amazon.com/dp/B0D3LW9B6J?ref=ppx_yo2ov_dt_b_fed_asin_title)
- **OLED 128x64**  
  [Amazon Link](https://www.amazon.com/dp/B09C5K91H7?ref=ppx_yo2ov_dt_b_fed_asin_title)
- **Encoder Knobs**  
  [Amazon Link](https://www.amazon.com/dp/B0DZTX96NH?ref=ppx_yo2ov_dt_b_fed_asin_title)
- **Power Supply**  
  [Amazon Link](https://www.amazon.com/dp/B01D8FLXJU?ref=ppx_yo2ov_dt_b_fed_asin_title)
  - 1x Female DC barrel connector (don't use cheap plastic ones)
- **LED Diffusion**  
  [Amazon Link](https://www.amazon.com/dp/B07V3QJW1L?ref=ppx_yo2ov_dt_b_fed_asin_title)
- **Addressable LEDs 2.7mm (cut to 20 12-pixel strips)**  
  [Amazon Link](https://www.amazon.com/dp/B0DWWTXJBD?ref=ppx_yo2ov_dt_b_fed_asin_title)
- **ATmega328p 3.3V 8MHz**  
  [Amazon Link](https://www.amazon.com/dp/B08LNBYLBT?ref=ppx_yo2ov_dt_b_fed_asin_title)
- **Motor Drivers**  
  [Amazon Link](https://www.amazon.com/dp/B0CBLTFDZ3?ref=ppx_yo2ov_dt_b_fed_asin_title)
- **Touch Sensor**  
  [Amazon Link](https://www.amazon.com/dp/B0DPFZ6TQV?ref=ppx_yo2ov_dt_b_fed_asin_title)
- **Motor Faders 5pk x2**  
  [Amazon Link](https://www.amazon.com/dp/B01DT827IC?ref=ppx_yo2ov_dt_b_fed_asin_title)
- **Prototype Boards**  
  [Amazon Link](https://www.amazon.com/dp/B07ZYNWJ1S?ref=ppx_yo2ov_dt_b_fed_asin_title)
- **USB-C Breakout Board**  
  [Amazon Link](https://www.amazon.com/dp/B0CB2VFJ54?ref=ppx_yo2ov_dt_b_fed_asin_title)
- **Encoders**  
  652-PEC12R-4025F-S24 from Mouser

### Keyboard

- **Cherry MX Switches**
- **Keycap of your liking**
- **Diodes**  
  [Amazon Link](https://www.amazon.com/dp/B0CKRMK45V?ref=ppx_yo2ov_dt_b_fed_asin_title)
- **Wire**  
  Solid copper wire, 18 or 16 AWG is fine
- **Heatshrink**

### I2C & Power

- **I2C Breakout**  
  DFRobot DFR0759 from Mouser
- **DC to DC Converter**  
  DFR1015 from Mouser

### Wire

- Various sizes: 28 or 30 AWG for I/O. **Use at least 26 AWG PTFE for LEDs**—this will fit in the small space (if you use PTFE insulated wire) and be large enough for the current. **Cover power solder connections of the LEDs or they will short on the faders.** I also did power injection at the other end of the strip; this is what the second 12V connector on the first motor driver board is used for.

### Hardware

- M3 screws, 8 to 12mm, flat or buttonhead
- M3 heatset inserts

### Filament used

[Amazon Link](https://www.amazon.com/dp/B0CSV8XBPV?ref=ppx_yo2ov_dt_b_fed_asin_title)
