import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from esphome.const import CONF_ID

# Define the C++ namespace and class
hisense_ac_ns = cg.esphome_ns.namespace("hisense_ac")
HisenseClimate = hisense_ac_ns.class_("HisenseClimate", climate.Climate, cg.Component)

# Extend the standard climate schema
CONFIG_SCHEMA = climate.climate_schema(HisenseClimate).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
