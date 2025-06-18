# EvoFaderWing - [Wiki](https://github.com/stagehandshawn/EvoFaderWing/wiki)

**Author:** ShawnR  
**Contact:** [stagehandshawn@gmail.com](mailto:stagehandshawn@gmail.com)

![FaderWing Image](https://github.com/stagehandshawn/EvoFaderWing/blob/main/docs/images/faderwing.jpg)

[![Watch the demo video](https://img.youtube.com/vi/fbl81pGS5f4/0.jpg)](https://youtu.be/fbl81pGS5f4)

---

## Overview

OSC control of grandMA3 faders with motorized feedback and backlit faders, 20 encoders, and 40 standard exec buttons.

> **Note:** This is a work in progress (WIP) but is fully functional. This is not a complete guide yet, as I just do not have the time to make one. However I have created a Bill of Materials

[Bill Of Materials](https://github.com/stagehandshawn/EvoFaderWing/wiki/Bill-of-materials)

[Wiring Diagrams](https://github.com/stagehandshawn/EvoFaderWing/wiki/Wiring-Diagrams)

Any questions can be sent to me at [stagehandshawn@gmail.com](mailto:stagehandshawn@gmail.com). I'll help when I can.

There are pinouts in the `images` and/or `docs` folders. These, along with the pictures, should give you an idea of how you'll want to route wires.
Image `00_Connections` gives an idea of the layout.

I have not coded in years and was never great at it, so I leaned on AI to make this happen. The code could use some work, I'm sure.

---

## Required repositories

There are 3 repositories you will need to complete this build. This one is the main controller. The other two are:

- **EvoFaderWing_keyboard_i2c**
- **EvoFaderWing_encoder_i2c**

These run on ATmega328p MCUs and are I2C slaves for getting encoder data from 20 encoders (5 each on 4 ATmega328p's), and 1 ATmega for the keyboard matrix.

There is a Python script and a `tasks.json` for uploading code automatically for when the FaderWing is closed and you cannot get to the bootloader button.

## Required Lua

The Lua script `/lua/EvoFaderWingOSC.lua` will poll executors and send updates to the FaderWing using bundled OSC messages, and the FaderWing will send OSC back to the script.

- You will need to create 2 OSC connections
  - The first will be for incoming messages and will be set to recieve.
  - The second will be from outgoing messages and will be set to send.

  - Both will need to be set to a fader range of 100

More to come. Thank you for checking out my project!
