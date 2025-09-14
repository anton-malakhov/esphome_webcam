# SPDX-License-Identifier: MIT
# This file is derrived from esp32_camera component of ESPHome

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome import pins
from esphome.const import (
    CONF_FREQUENCY,
    CONF_ID,
    CONF_RESOLUTION,
    CONF_TRIGGER_ID,
)
from esphome.core import CORE, TimePeriod
from esphome.components.esp32 import add_idf_sdkconfig_option
from esphome.components.esp32 import add_idf_component
try:
  from esphome.cpp_helpers import setup_entity
except:
  from esphome.core.entity_helpers import setup_entity

DEPENDENCIES = ["esp32", "camera"]

AUTO_LOAD = ["camera", "psram"]

usb_webcam_ns = cg.esphome_ns.namespace("usb_webcam")
USBWebCam = usb_webcam_ns.class_("USBWebCam", cg.PollingComponent, cg.EntityBase)
USBWebCamStreamStartTrigger = usb_webcam_ns.class_(
    "USBWebCamStreamStartTrigger",
    automation.Trigger.template(),
)
USBWebCamStreamStopTrigger = usb_webcam_ns.class_(
    "USBWebCamStreamStopTrigger",
    automation.Trigger.template(),
)
USBWebCamFrameSize = usb_webcam_ns.enum("USBWebCamFrameSize")
FRAME_SIZES = {
    "160X120": USBWebCamFrameSize.USB_WEBCAM_SIZE_160X120,
    "QQVGA": USBWebCamFrameSize.USB_WEBCAM_SIZE_160X120,
    "176X144": USBWebCamFrameSize.USB_WEBCAM_SIZE_176X144,
    "QCIF": USBWebCamFrameSize.USB_WEBCAM_SIZE_176X144,
    "240X176": USBWebCamFrameSize.USB_WEBCAM_SIZE_240X176,
    "HQVGA": USBWebCamFrameSize.USB_WEBCAM_SIZE_240X176,
    "320X240": USBWebCamFrameSize.USB_WEBCAM_SIZE_320X240,
    "QVGA": USBWebCamFrameSize.USB_WEBCAM_SIZE_320X240,
    "400X296": USBWebCamFrameSize.USB_WEBCAM_SIZE_400X296,
    "CIF": USBWebCamFrameSize.USB_WEBCAM_SIZE_400X296,
    "640X480": USBWebCamFrameSize.USB_WEBCAM_SIZE_640X480,
    "VGA": USBWebCamFrameSize.USB_WEBCAM_SIZE_640X480,
    "800X600": USBWebCamFrameSize.USB_WEBCAM_SIZE_800X600,
    "SVGA": USBWebCamFrameSize.USB_WEBCAM_SIZE_800X600,
    "1024X768": USBWebCamFrameSize.USB_WEBCAM_SIZE_1024X768,
    "XGA": USBWebCamFrameSize.USB_WEBCAM_SIZE_1024X768,
    "1280X1024": USBWebCamFrameSize.USB_WEBCAM_SIZE_1280X1024,
    "SXGA": USBWebCamFrameSize.USB_WEBCAM_SIZE_1280X1024,
    "1600X1200": USBWebCamFrameSize.USB_WEBCAM_SIZE_1600X1200,
    "UXGA": USBWebCamFrameSize.USB_WEBCAM_SIZE_1600X1200,
    "1920X1080": USBWebCamFrameSize.USB_WEBCAM_SIZE_1920X1080,
    "FHD": USBWebCamFrameSize.USB_WEBCAM_SIZE_1920X1080,
    "720X1280": USBWebCamFrameSize.USB_WEBCAM_SIZE_720X1280,
    "PHD": USBWebCamFrameSize.USB_WEBCAM_SIZE_720X1280,
    "864X1536": USBWebCamFrameSize.USB_WEBCAM_SIZE_864X1536,
    "P3MP": USBWebCamFrameSize.USB_WEBCAM_SIZE_864X1536,
    "2048X1536": USBWebCamFrameSize.USB_WEBCAM_SIZE_2048X1536,
    "QXGA": USBWebCamFrameSize.USB_WEBCAM_SIZE_2048X1536,
    "2560X1440": USBWebCamFrameSize.USB_WEBCAM_SIZE_2560X1440,
    "QHD": USBWebCamFrameSize.USB_WEBCAM_SIZE_2560X1440,
    "2560X1600": USBWebCamFrameSize.USB_WEBCAM_SIZE_2560X1600,
    "WQXGA": USBWebCamFrameSize.USB_WEBCAM_SIZE_2560X1600,
    "1080X1920": USBWebCamFrameSize.USB_WEBCAM_SIZE_1080X1920,
    "PFHD": USBWebCamFrameSize.USB_WEBCAM_SIZE_1080X1920,
    "2560X1920": USBWebCamFrameSize.USB_WEBCAM_SIZE_2560X1920,
    "QSXGA": USBWebCamFrameSize.USB_WEBCAM_SIZE_2560X1920,
}

# frames
CONF_MAX_FRAMERATE = "max_framerate"
CONF_IDLE_FRAMERATE = "idle_framerate"
CONF_DROP_FRAME_SIZE = "drop_frame_size"

# stream trigger
CONF_ON_STREAM_START = "on_stream_start"
CONF_ON_STREAM_STOP = "on_stream_stop"

CONFIG_SCHEMA = cv.ENTITY_BASE_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(USBWebCam),
        # image
        cv.Optional(CONF_RESOLUTION, default="640X480"): cv.enum(
            FRAME_SIZES, upper=True
        ),
        # framerates
        cv.Optional(CONF_MAX_FRAMERATE, default="5 fps"): cv.All(
            cv.framerate, cv.Range(min=0, min_included=False, max=60)
        ),
        cv.Optional(CONF_IDLE_FRAMERATE, default="0.1 fps"): cv.All(
            cv.framerate, cv.Range(min=0, max=1)
        ),
        cv.Optional(CONF_DROP_FRAME_SIZE, default="7000"): cv.All(
            cv.int_range(min=0, max=100000)
        ),
        cv.Optional(CONF_ON_STREAM_START): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                    USBWebCamStreamStartTrigger
                ),
            }
        ),
        cv.Optional(CONF_ON_STREAM_STOP): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                    USBWebCamStreamStopTrigger
                ),
            }
        ),
    }
).extend(cv.COMPONENT_SCHEMA)

def _final_validate(config):
    return

FINAL_VALIDATE_SCHEMA = _final_validate

async def to_code(config):
    cg.add_define("USE_CAMERA")
    var = cg.new_Pvariable(config[CONF_ID])
    await setup_entity(var, config, "camera")
    await cg.register_component(var, config)

    cg.add(var.set_max_update_interval(1000 / config[CONF_MAX_FRAMERATE]))
    if config[CONF_IDLE_FRAMERATE] == 0:
        cg.add(var.set_idle_update_interval(0))
    else:
        cg.add(var.set_idle_update_interval(1000 / config[CONF_IDLE_FRAMERATE]))
    cg.add(var.set_drop_size(config[CONF_DROP_FRAME_SIZE]))
    cg.add(var.set_frame_size(config[CONF_RESOLUTION]))

    cg.add_define("USE_USB_WEBCAM")

    assert(CORE.using_esp_idf)
    add_idf_component(
            name="usb_stream",
            ref="v2.0",
            repo="https://github.com/espressif/esp-iot-solution.git",
            path="components/usb/usb_stream",
            ref="v2.0",
            refresh=TimePeriod(days=5)
    )
    # no need in cg.add_library("espressif/esp32-camera", "1.0.0")
    # esp_camera.h and sensor.h are taken from it directly
    for d, v in {
        #"CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT": True,
        "CONFIG_RTCIO_SUPPORT_RTC_GPIO_DESC": True,
        "CONFIG_USB_OTG_SUPPORTED": True,
        "CONFIG_SOC_USB_OTG_SUPPORTED": True,
        "CONFIG_SPIRAM_USE_MALLOC": True, # buffers are big, better let everyone allocate PSRAM
        #
        # USB Stream
        #
        #"CONFIG_USB_STREAM_QUICK_START": True,
        "CONFIG_UVC_GET_DEVICE_DESC": True,
        "CONFIG_UVC_GET_CONFIG_DESC": True,
        "CONFIG_UVC_PRINT_DESC": True,
        "CONFIG_USB_PRE_ALLOC_CTRL_TRANSFER_URB": True,
        "CONFIG_USB_PROC_TASK_PRIORITY": 2,
        #"CONFIG_USB_PROC_TASK_CORE": 1,
        "CONFIG_USB_PROC_TASK_STACK_SIZE": 3072,
        "CONFIG_USB_WAITING_AFTER_CONN_MS": 50,
        "CONFIG_USB_ENUM_FAILED_RETRY": True,
        "CONFIG_USB_ENUM_FAILED_RETRY_COUNT": 10,
        "CONFIG_USB_ENUM_FAILED_RETRY_DELAY_MS": 200,
        #
        # UVC Stream Config
        #
        "CONFIG_SAMPLE_PROC_TASK_PRIORITY": 0,
        #"CONFIG_SAMPLE_PROC_TASK_CORE": 0,
        "CONFIG_SAMPLE_PROC_TASK_STACK_SIZE": 3072,
        "CONFIG_UVC_PRINT_PROBE_RESULT": True,
        "CONFIG_UVC_CHECK_BULK_JPEG_HEADER": True,
        "CONFIG_UVC_DROP_OVERFLOW_FRAME": True,
        "CONFIG_UVC_DROP_NO_EOF_FRAME": True,
        "CONFIG_NUM_BULK_STREAM_URBS": 2,
        "CONFIG_NUM_BULK_BYTES_PER_URB": 2048,
        "CONFIG_NUM_ISOC_UVC_URBS": 3,
        "CONFIG_NUM_PACKETS_PER_URB": 4,
        # end of UVC Stream Config
    }.items():
        add_idf_sdkconfig_option(d, v)

    for conf in config.get(CONF_ON_STREAM_START, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)

    for conf in config.get(CONF_ON_STREAM_STOP, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
