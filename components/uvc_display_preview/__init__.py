import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display
from esphome.components.esp32 import add_idf_component
from esphome.components.lvgl.lvcode import LvglComponent
from esphome.const import CONF_ID
from esphome.core import TimePeriod

DEPENDENCIES = ["esp32", "display", "lvgl", "usb_webcam"]

CONF_DISPLAY_ID = "display_id"
CONF_LVGL_ID = "lvgl_id"
CONF_MAX_FPS = "max_fps"

uvc_display_preview_ns = cg.esphome_ns.namespace("uvc_display_preview")
UvcDisplayPreview = uvc_display_preview_ns.class_("UvcDisplayPreview", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(UvcDisplayPreview),
        cv.Required(CONF_DISPLAY_ID): cv.use_id(display.Display),
        cv.Required(CONF_LVGL_ID): cv.use_id(LvglComponent),
        cv.Optional(CONF_MAX_FPS, default="5 fps"): cv.All(
            cv.framerate, cv.Range(min=0, min_included=False, max=15)
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    disp = await cg.get_variable(config[CONF_DISPLAY_ID])
    lvgl = await cg.get_variable(config[CONF_LVGL_ID])
    cg.add(var.set_display(disp))
    cg.add(var.set_lvgl(lvgl))
    cg.add(var.set_min_frame_interval(int(1000 / config[CONF_MAX_FPS])))

    cg.add_define("USE_UVC_DISPLAY_PREVIEW_ESP_NEW_JPEG")
    cg.add_define("USE_UVC_DISPLAY_PREVIEW_RAW_TAP")
    add_idf_component(
        name="esp_new_jpeg",
        repo="https://github.com/espressif/esp-adf-libs.git",
        ref="4610cd794152ed4a6e4417e92385150a1e32ddeb",
        path="esp_new_jpeg",
        refresh=TimePeriod(days=30),
    )
