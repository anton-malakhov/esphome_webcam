
## ESPHome component for USB camera

## Usage
Copy components files to a ***components*** directory under your homeassistant's esphome directory.<BR>
The following yaml can then be used so ESPHome accesses the component files:
```
external_components:
  - source: components
```

## Example YAML
```
external_components:
  - source: components

esp32_camera:
  name: usb-webcam
```
