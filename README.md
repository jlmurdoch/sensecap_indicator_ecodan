# SenseCAP Indicator for Mitsubishi Ecodan

### Introduction
This is a Christmas '25 project where a [Seeed Studio SenseCAP Indicator D1Pro](https://wiki.seeedstudio.com/Sensor/SenseCAP/SenseCAP_Indicator/Get_started_with_SenseCAP_Indicator/) was reprogrammed to work as a climate control interface and perform indoor climate monitoring.

![Photo of a SenseCAP Indicator D1Pro with an Climate Control UI](/assets/sensecap_ecodan_ui.jpg)

The project targets a Mitsubishi Ecodan ASHP (packetised 868MHz FSK) using hte LoRa SX1262 radio, inconjunction with the climate sensors supplied with the D1Pro. Both microcontrollers ([EspressIf ESP32-S3](https://www.espressif.com/en/products/socs/esp32-s3) & [Raspberry Pi RP2040](https://www.raspberrypi.com/products/rp2040/)) were utilised, hence the two codebases in this repository.

### Software

Rather than unifying the codebase under a single framework like Arduino IDE or Platform IO, the author used the microcontroller manufacturers SDKs with their requisite Visual Studio Code Extensions and used [profiles](https://code.visualstudio.com/docs/configure/profiles) to contain each SDK, such as the [pico-vscode profile](https://github.com/raspberrypi/pico-vscode?tab=readme-ov-file#vs-code-profiles):
- [Espressif IoT Development Framework (ESP-IDF)](https://github.com/espressif/esp-idf)
  - VS Code Extension: [ESP-IDF](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension) (v1.11.0)
    - Ensure "Set Espressif Device Target" is `esp32s3`
- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
  - VS Code Extension: [Raspberry Pi Pico](https://github.com/raspberrypi/pico-vscode) (v0.19.0)
    1. Click **Import Project**
    2. Click **Show Advanced Options**
    3. Ninja Version: Use system version *Author used [1.13.1](https://github.com/ninja-build/ninja/releases)*
    4. CMake Version: Use system version *Author used [3.31.5](https://cmake.org/download/)*
    5. On **Location**, click **Change**, then **Select** the `sensecap_indicator_ecodan_rp2040` folder
    6. Click **Import**
    7. Ensure the *Board* setting is `seeed_xiao_rp2040`

#### Libraries

Besides the libraries integrated into the SDKs, the following were used:
- [ESP Component: ESP IO Expander Chip TCA9539 and TCA9555](https://components.espressif.com/components/espressif/esp_io_expander_tca95xx_16bit/versions/2.0.1/readme)
- [ESP Component: ESP LCD ST7701(S)](https://components.espressif.com/components/espressif/esp_lcd_st7701/versions/2.0.2/readme)
- [ESP Component: ESP LCD Touch FT5x06 Controller](https://components.espressif.com/components/espressif/esp_lcd_touch_ft5x06/versions/1.1.0/readme)
- [ESP Component: LVGL - Light and Versatile Graphics Library](https://components.espressif.com/components/lvgl/lvgl/versions/9.4.0/readme)
- [GitHub: Sensirion VOC Index](https://github.com/Sensirion/embedded-sgp/tree/master/sgp40_voc_index) 

#### Other software / notes

- All the RP2040-based I2C sensor work is raw I2C work, to avoid future dependency hell.
- The ST7701S LCD Panel needs a custom initialisation process. 
- The author modified their own [ESP32 SX1262 example](https://github.com/jlmurdoch/sx1262_esp32_fsk_example) to deal with nuances of an IO Expander to use the interrupt and busy pins. 
- Used basic UART functions of the SDKs. [COBS](https://github.com/cmcqueen/cobs-c) ([ESP32 Variant](https://components.espressif.com/components/espp/cobs/versions/1.0.33/readme)) may be used in the future.
- LVGL is used for the display UI, with a few free [Font Awesome](https://fontawesome.com) icons imported via the [LVGL Font Converter](https://lvgl.io/tools/fontconverter) for additional heating symbols. 

### Hardware

As stated in the introduction, the SenseCAP Indicator D1Pro kit has two microcontrollers inside it: a Raspberry Pi RP2040 and an EspressIf ESP32-S3. Both are used in radically different ways, but communicate over a UART connection. The hardware - device alignments are detailed below. If more detail is needed on individual pin functions, see the source code.

#### RP2040 IO Configuration

| Device                   | Interface     | GPIO
|--------------------------|---------------|----------
| SGP40 tVOC Sensor        | I2C[^1] (0x59)| 18 (+V), 20 (SDA), 21 (SCL)
| SCD41 CO2 Sensor         | I2C[^1] (0x62)| 18 (+V), 20 (SDA), 21 (SCL)
| Micro SD Card Reader     | SPI           | 10 (SCK), 11 (MOSI), 12 (MISO), 13 (NSS)
| MLT-8530 Buzzer          | GPIO (PWM)    | 19
| UART to ESP32            | GPIO (UART)   | 16 (TX), 17 (RX)
| USB-C-based GPIO         | SPI           | 0 (MISO), 1 (NSS), 2 (SCK), 3 (MOSI)
| USB-C-based GPIO         | I2C           | 14 (SDA), 15 (SCL)
| USB-C-based GPIO         | GPIO (UART)   | 8 (TX), 9 (RX)
| Grove Port A             | GPIO (ADC)    | 26, 27
| Grove Port B[^2]         | I2C[^1]       | 18 (+V), 20 (SDA), 21 (SCL)

[^1]: GPIO 18 needs to be pulled up for the I2C bus to be powered on.
[^2]: The D1Pro comes with an AHT20 Temperature & Humidity sensor that can be attached to Grove Port B over I2C. The AHT20 I2C address is 0x38.

#### ESP32-S3 IO Configuration

The ESP32-S3 is the main conduit to the outside word, with WiFi and LCD Touchscreen:

| ESP32-S3               | Interface         | GPIO
|------------------------|-------------------|-----------------
| WiFi/Bluetooth         | N/A               | N/A
| Button                 | GPIO              | 38
| UART to RP2040         | GPIO (UART)       | 19 (TX), 20 (RX)
| PCA9535PWR IO Expander | I2C (0x20)        | 39 (SDA), 40 (SCL), 42 (INT)
| FT6336U Touch Interface| I2C[^3] (0x48)    | 39 (SDA), 40 (SCL)
| LCD Backlight          | GPIO (PWM)        | 45
| ST7701S 480x480 LCD    | SPI[^3] , GPIO[^3]| 0-18,21 (RGB IO), 41 (SCK), 48 (MOSI)
| SX1262 LoRa Radio      | SPI[^3] , GPIO[^3]| 41 (SCK), 47 (MISO), 48 (MOSI)

[^1]: The IO Expander needs to be utilised to work with these devices effectively.

Due to large amounts of the ESP32-S3 GPIO being used for the RGB on the LCD, there's an IO Expander to handle even more GPIO activity:

| IO Expander Device | Port | Pin | Function | Direction
|--------------------|------|-----|----------|----------
| Interrupt**        | N/A  | INT | Interrupt| Output
| SX1262 LoRa Radio  | 0    | P00 | SPI NSS  | Output
| SX1262 LoRa Radio  | 0    | P01 | Reset    | Output
| SX1262 LoRa Radio  | 0    | P02 | BUSY     | Input
| SX1262 LoRa Radio  | 0    | P03 | DIO1     | Input
| ST7701S LCD        | 0    | P04 | SPI NSS  | Output
| ST7701S LCD        | 0    | P05 | Reset    | Output
| FT6336U Touch      | 0    | P06 | Interrupt| Input
| FT6336U Touch      | 0    | P07 | Reset    | Output
| RP2040             | 1    | P10 | Reset    | Output
| SX1262 LoRa Radio  | 1    | P13 | TXCO     | Input

** The IO Expander has a single interrupt assigned to ESP32-S3 GPIO 42 to detect attached pin levels going up/down, which can be used for BUSY, DIO1, INT and TXCO inputs. The IO expander does not elaborate on the exact pin that caused the interrupt. This pin is mislabelled in the high-level schematic as it is in fact wired to ESP-S3 GPIO 42 (not 45, the LCD backlight).

### TODO

- Immediate: Further LVGL button integration
- Immediate: LVGL Interface pop-ups
- Immediate: LVGL ASHP duration information
- Probably: LVGL charts
- Probably: LVGL Ecodan radio auto-config
- Unsure: Buzzer - State change notification? Button beeps?
- Unsure: Micro SD Card slot - Data logging?
- Unsure: Do more with WiFi, such as exposing metrics
