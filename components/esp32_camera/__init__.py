# SPDX-License-Identifier: MIT
# This is replacement for original esp32_camera component
# Registers dummy component for sake of API compatibility
# with ESPHome core expecting esp32_camera.h file

from esphome.const import CONF_ID
import esphome.codegen as cg
import esphome.config_validation as cv

esp32_camera_ns = cg.esphome_ns.namespace("esp32_camera")
dummy = esp32_camera_ns.class_("dummy", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(dummy),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
