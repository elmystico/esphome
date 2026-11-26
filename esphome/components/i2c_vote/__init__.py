import esphome.codegen as cg
from esphome.components import i2c
import esphome.config_validation as cv
from esphome.const import CONF_I2C_ID, CONF_ID
from esphome.types import ConfigType

CODEOWNERS = ["@elmystico"]
DOMAIN = "i2c_vote"
DEPENDENCIES = ["i2c"]
MULTI_CONF = True

CONF_I2C_VOTE_ID = "i2c_vote_id"

i2c_vote_ns = cg.esphome_ns.namespace("i2c_vote")
I2CVoteBus = i2c_vote_ns.class_("I2CVoteBus", cg.PollingComponent, i2c.I2CBus)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(I2CVoteBus),
        cv.GenerateID(CONF_I2C_ID): cv.use_id(i2c.I2CBus),
    }
).extend(cv.polling_component_schema("60s"))


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_I2C_ID])
    cg.add(var.set_parent(parent))
