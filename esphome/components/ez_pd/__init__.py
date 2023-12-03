import esphome.codegen as cg
import esphome.config_validation as cv

from esphome import pins
from esphome.components import i2c
from esphome.const import CONF_ID, CONF_INTERRUPT_PIN, CONF_VOLTAGE, CONF_CURRENT

DEPENDENCIES = ["i2c"]

CONF_I2C_ADDR = 0x08

ez_pd_ns = cg.esphome_ns.namespace("ez_pd")
EZPD = ez_pd_ns.class_("EZPD", cg.Component, i2c.I2CDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(EZPD),
            cv.Required(CONF_INTERRUPT_PIN): cv.All(
                pins.internal_gpio_input_pin_schema
            ),
            cv.Required(CONF_VOLTAGE): cv.voltage,
            cv.Required(CONF_CURRENT): cv.current,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(CONF_I2C_ADDR))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    interrupt_pin = await cg.gpio_pin_expression(config[CONF_INTERRUPT_PIN])
    cg.add(var.set_interrupt_pin(interrupt_pin))
    cg.add(
        var.set_power_requirement(
            1000 * config[CONF_VOLTAGE], 1000 * config[CONF_CURRENT]
        )
    )
