import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from esphome.const import CONF_ID

CODEOWNERS = ["@you"]

kelon_ac_ns = cg.esphome_ns.namespace("kelon_ac")
KelonAcClimate = kelon_ac_ns.class_("KelonAcClimate", climate.Climate, cg.Component)
KelonProtocolVariant = kelon_ac_ns.enum("KelonProtocolVariant")

CONF_PROTOCOL = "protocol"

PROTOCOLS = {
    "KELON": KelonProtocolVariant.PROTOCOL_KELON,
    "KELON168": KelonProtocolVariant.PROTOCOL_KELON168,
}

CONFIG_SCHEMA = climate.CLIMATE_SCHEMA.extend(cv.COMPONENT_SCHEMA).extend(
    {
        cv.GenerateID(): cv.declare_id(KelonAcClimate),
        # KELON    = basic 48-bit "ON/OFF 9000-12000" series
        # KELON168 = 168-bit Hisense / DG11R2-01 remote series
        cv.Optional(CONF_PROTOCOL, default="KELON168"): cv.enum(
            PROTOCOLS, upper=True
        ),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    cg.add(var.set_protocol(config[CONF_PROTOCOL]))
