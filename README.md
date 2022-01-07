# CO2 Meter with ESP32

## Description

Messasure CO2 particles in to the air.
Warns when you should ventilate the room.

You find an video documentary (in german) on my Youtube channel:
https://www.youtube.com/watch?v=MKK0GAGxzaE

## Features

- Measure CO2 concentration in room
- MQTT support (for Node-Red or Home Assistant)
- easy integration in own WiFi network (Hotspot settings-page)
- OLED display: can used independently thanks to the measured values shown on the display

## Parts

- [ESP32 Development Board \*](https://www.banggood.com/custlink/mKDYaR8Uuc)
- [NDIR CO2 Sensor MH-Z14A \*](https://www.banggood.com/custlink/DmDhaY8k79)
- [0.96 Inch OLED Display 128\*64 SSD1306 \*](https://www.banggood.com/custlink/3DmyLhZMwa)

`* Affiliate Links - help to support my projects`

## Sketch

![sketch](/docs/sketch_bb.png)

## Wiring

### CO2 sensor (MH-Z14A)

| Pin on Sensor | description | Pin on ESP32                     |
| ------------- | ----------- | -------------------------------- |
| 16            | GND         | GND                              |
| 17            | Power (5V)  | VIN (can also used as 5V output) |
| 18            | UART RXD    | GPIO17 (UART 2 TX)               |
| 19            | UART TXD    | GPIO16 (UART 2 RX)               |

### OLED display (SSD1306)

| Pin on Sensor | description  | Pin on ESP32     |
| ------------- | ------------ | ---------------- |
| GND           | GND          | GND              |
| VCC           | Power (3.3V) | 3V3              |
| SCL           |              | GPIO22 (I2C SCL) |
| SDA           |              | GPIO21 (I2C SDA) |

## Setup

- connect all parts to the ESP32
- connect power
- the ESP32 starting an own Wifi access point named "co2-meter-{device ID}"
- connect to this Wifi with your smartphone
- use `waaatering` for Wifi password
- you will redirected to a config page
- insert your Wifi and MQTT broker credentials
- click on "save"
- the ESP32 will restarting and connect to your MQTT broker
- after about 60 seconds should you should see a new topic in your MQTT broker `co2-meter/{device ID}/out/info` with a lot of information in the payload

## MQTT API

The whole module is controllable via MQTT protocol. So it's easy to integrate in existing SmartHome systems (like Home Assistant or Node-Red).

### Incoming commands

Topic: co2-meter/`{device ID}`/in/`{command}`

| command    | description                                                                                | payload              |
| ---------- | ------------------------------------------------------------------------------------------ | -------------------- |
| sleep      | Start deep-sleep for specific duration (in seconds!)                                       | { duration: number } |
| info       | Send info via MQTT topic `co2-meter/out/info` package                                      | -                    |
| display    | turn display on/off                                                                        | { on: boolean }      |
| hard-reset | Reset config with WiFi and MQTT settings and start internal hotspot to reconfigure device. | -                    |

### Outcoming commands

Topic: co2-meter/`{device ID}`/out/`{command}`

| command | description                              | payload                                |
| ------- | ---------------------------------------- | -------------------------------------- |
| info    | status info                              | complex JSON. See "info-state" chapter |
| sleep   | was send if sytem enter deep-sleep       | { duration: number }                   |
| wakeup  | was send if sytem wakeup from deep-sleep | reason "timer"                         |

#### Info state

| field               | description                                             | type    |
| ------------------- | ------------------------------------------------------- | ------- |
| version             | version number of module firmware                       | string  |
| system.deviceId     | Unique ID of the device. Will used also in MQTT topics. | string  |
| system.freeHeap     | free heap memory of CPU                                 | number  |
| network.wifiRssi    | wifi signal strength (RSSI)                             | number  |
| network.wifiQuality | quality of signal strength (value between 0 and 100%)   | number  |
| network.wifiSsid    | SSID of connected wifi                                  | string  |
| network.ip          | ip address of the module                                | string  |
| co2.isPreheating    | TRUE if the sensor is preheated                         | boolean |
| co2.isReady         | ignore                             | boolean |
| co2.temperature     | temperature from sensor (in °C)                         | number  |
| co2.ppm             | CO2 concentration in the air (in ppm)                   | number  |

## Housing

You can create a robust housing with a 3D printer or at the workbench.

I decided to make one out of plywood. It didn't turn out perfect, but it protects the components.

![sketch](/docs/housing.jpg)

## TODOs

- [x] buy sensor and ESP32
- [x] soldering first prototype
- [x] add WifiManager to easy setup
- [x] create sketch
- [x] test firmware
- [x] test OTA updates
- [x] add part list
- [x] add video doc
- [x] create housing (from wood)
