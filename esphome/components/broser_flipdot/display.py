import esphome.codegen as cg
from esphome.components import display, i2c
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_LAMBDA, CONF_NUM_CHIPS, CONF_PAGES

DEPENDENCIES = ["i2c"]

broser_flipdot_ns = cg.esphome_ns.namespace("broser_flipdot")
BroserFlipdot = broser_flipdot_ns.class_(
    "BroserFlipdot", cg.PollingComponent, display.DisplayBuffer, i2c.I2CDevice
)

CONFIG_SCHEMA = cv.All(
    display.FULL_DISPLAY_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(BroserFlipdot),
            cv.Optional(CONF_NUM_CHIPS, default=1): cv.int_range(min=1, max=8),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x03)),
    cv.has_at_most_one_key(CONF_PAGES, CONF_LAMBDA),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await display.register_display(var, config)
    await i2c.register_i2c_device(var, config)

    cg.add(var.set_num_chips(config[CONF_NUM_CHIPS]))

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA], [(display.DisplayRef, "it")], return_type=cg.void
        )
        cg.add(var.set_writer(lambda_))
