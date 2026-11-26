import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC, STATE_CLASS_TOTAL_INCREASING
from esphome.types import ConfigType

from . import CONF_I2C_VOTE_ID, I2CVoteBus

DEPENDENCIES = ["i2c_vote"]

CONF_CORRECTED = "corrected"
CONF_REJECTED = "rejected"

_COUNTER_SCHEMA = sensor.sensor_schema(
    accuracy_decimals=0,
    state_class=STATE_CLASS_TOTAL_INCREASING,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    icon="mdi:counter",
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_I2C_VOTE_ID): cv.use_id(I2CVoteBus),
        cv.Optional(CONF_CORRECTED): _COUNTER_SCHEMA,
        cv.Optional(CONF_REJECTED): _COUNTER_SCHEMA,
    }
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_I2C_VOTE_ID])
    sensors = sensor.sub_sensors(config)
    await sensors(CONF_CORRECTED, hub.set_corrected_sensor)
    await sensors(CONF_REJECTED, hub.set_rejected_sensor)
