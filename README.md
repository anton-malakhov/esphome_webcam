
## ESPHome components for USB camera

This repository contains [external components](https://esphome.io/components/external_components.html) for [ESPHome](https://esphome.io/) that enable web camera connected via USB OTG port of ESP32 S2/S3 family of microcontrollers.

## Supported video devices
Not every USB video device can work with ESP devices due to their limited capabilities. E.g. only USB1.1 full-speed mode and MJPEG format are supported along with limitations on max bandwidth and max packet size (as requested by the video device). Please refer to the [documentation](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_stream.html#usb-stream-user-guide) for details.

## Power supply
ESP32-S3 DevKitC-1 or similar boards do not provide enough power for USB devices. It must be provided externally or via your own schematics.

## Memory, PSRAM
usb_stream requires a lot of video buffers. Current setting is 256K just for transfer buffers. Thus, the ESP32-S2/S3 device has to have PSRAM connected and enabled, e.g.:
```yaml
psram:
  mode: quad
  speed: 80MHz
```

## ESP-IDF framework mode
The webcam component uses `usb_stream` component of [esp-iot-solution](https://github.com/espressif/esp-iot-solution/) library, which is based on ESP-IDF framework. The following yaml enables `esp-idf` mode for ESP32-S3 DevKitC-1 board along with correct flash mode:
```yaml
esphome:
  platformio_options:
    board_build.flash_mode: dio  # default mode is wrong

esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: esp-idf
```

## Import external components
The following yaml can be used to import components into your ESPHome configuration:
```yaml
external_components:
  - source: github://anton-malakhov/esphome_webcam
```

## Enable webcam
```yaml
usb_webcam:
  name: usb-webcam
```

## Full example YAML
```yaml
esphome:
  name: esp-webcam
  platformio_options:
    board_build.flash_mode: dio

esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: esp-idf

psram:
  mode: quad
  speed: 80MHz

external_components:
  - source: github://anton-malakhov/esphome_webcam

usb_webcam:
  name: usb-webcam
```
