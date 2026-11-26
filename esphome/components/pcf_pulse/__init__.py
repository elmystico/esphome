"""pcf_pulse: count pulses on PCF8574 inputs, woken by the INT line.

A FreeRTOS task reads the input expanders (read only, nothing is written) after every
falling edge of the shared, open-drain INT line, and counts falling edges (1 -> 0) on
every input. Bit 8*i + n is pin Pn of addresses[i]. Without an interrupt pin the task
polls every `poll_interval` instead.

The built-in `pcf8574` hubs can still use the same chips for input states and outputs:
a read does not change the output latch, and the ESP-IDF I2C driver serialises
transfers coming from both tasks.
"""

from esphome import pins
import esphome.codegen as cg
from esphome.components import i2c
import esphome.config_validation as cv
from esphome.const import CONF_I2C_ID, CONF_ID, CONF_INTERRUPT_PIN
from esphome.types import ConfigType

CODEOWNERS = ["@elmystico"]
DOMAIN = "pcf_pulse"
DEPENDENCIES = ["i2c", "esp32"]

CONF_ADDRESSES = "addresses"
CONF_POLL_INTERVAL = "poll_interval"

pcf_pulse_ns = cg.esphome_ns.namespace("pcf_pulse")
PCFPulse = pcf_pulse_ns.class_("PCFPulse", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(PCFPulse),
        cv.GenerateID(CONF_I2C_ID): cv.use_id(i2c.I2CBus),
        cv.Required(CONF_ADDRESSES): cv.All(
            cv.ensure_list(cv.i2c_address), cv.Length(min=1, max=2)
        ),
        cv.Optional(CONF_INTERRUPT_PIN): pins.internal_gpio_input_pin_schema,
        cv.Optional(
            CONF_POLL_INTERVAL, default="5ms"
        ): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    bus = await cg.get_variable(config[CONF_I2C_ID])
    cg.add(var.set_bus(bus))
    for addr in config[CONF_ADDRESSES]:
        cg.add(var.add_address(addr))
    if (int_pin := config.get(CONF_INTERRUPT_PIN)) is not None:
        pin = await cg.gpio_pin_expression(int_pin)
        cg.add(var.set_interrupt_pin(pin))
    cg.add(var.set_poll_ms(config[CONF_POLL_INTERVAL].total_milliseconds))
