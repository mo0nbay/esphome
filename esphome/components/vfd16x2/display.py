from esphome import pins
import esphome.codegen as cg
from esphome.components import display, spi
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_LAMBDA

CONF_N_RESET_PIN = "n_reset_pin"

DEPENDENCIES = ["spi"]

vfd16x2_ns = cg.esphome_ns.namespace("vfd16x2")
VFD16X2 = vfd16x2_ns.class_("VFD16X2", cg.PollingComponent, spi.SPIDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(VFD16X2),
            cv.Required(CONF_N_RESET_PIN): pins.gpio_output_pin_schema,
        }
    )
    .extend(spi.spi_device_schema())
    .extend(display.BASIC_DISPLAY_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)

    n_reset_pin = await cg.gpio_pin_expression(config[CONF_N_RESET_PIN])
    cg.add(var.set_n_reset_pin(n_reset_pin))

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA],
            [(VFD16X2.operator("ref"), "it")],
            return_type=cg.void,
        )
        cg.add(var.set_writer(lambda_))
