# CO2 Meter with ESP32

## Status

:construction: **Still in development!** :construction:

No final version at the moment! :construction_worker: :building_construction:

## Description

Messasure CO2 particles in to the air.

## Features

- ...

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
| hard-reset | Reset config with WiFi and MQTT settings and start internal hotspot to reconfigure device. | -                    |

### Outcoming commands

Topic: co2-meter/`{device ID}`/out/`{command}`

| command | description                              | payload                                |
| ------- | ---------------------------------------- | -------------------------------------- |
| info    | status info                              | complex JSON. See "info-state" chapter |
| sleep   | was send if sytem enter deep-sleep       | { duration: number }                   |
| wakeup  | was send if sytem wakeup from deep-sleep | reason "timer"                         |

#### Info state

| field               | description                                             | type   |
| ------------------- | ------------------------------------------------------- | ------ |
| version             | version number of module firmware                       | string |
| system.deviceId     | Unique ID of the device. Will used also in MQTT topics. | string |
| system.freeHeap     | free heap memory of CPU                                 | number |
| network.wifiRssi    | wifi signal strength (RSSI)                             | number |
| network.wifiQuality | quality of signal strength (value between 0 and 100%)   | number |
| network.wifiSsid    | SSID of connected wifi                                  | string |
| network.ip          | ip address of the module                                | string |
| co2.isReady         | TRUE if the sensor is preheated                         | number |
| co2.temperature     | ...                                                     | number |
| co2.ppm             | ...                                                     | number |

## Sketch

![sketch](/docs/sketch_bb.png)

## TODOs

- [x] buy sensor and ESP32
- [x] soldering first prototype
- [x] add WifiManager to easy setup
- [ ] create sketch
- [ ] test firmware
- [ ] test OTA updates
- [ ] create housing (from wood)
- [ ] create example flow with [Node-RED](https://nodered.org/)
- [ ] add part list
- [ ] add video doc
