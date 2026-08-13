import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_DEVICE_ID, ENTITY_CATEGORY_CONFIG

from . import (
    CONF_FIIDO_BMS_ID,
    FIIDO_BMS_COMPONENT_SCHEMA,
    apply_entity_prefix,
    fiido_bms_ns,
    hub_name_prefix,
    hub_ui_gear_mode_3,
    inject_entity_defaults,
)

DEPENDENCIES = ["fiido_bms"]
CODEOWNERS = ["@dzikus"]

FiidoGearSelect = fiido_bms_ns.class_("FiidoGearSelect", select.Select, cg.Parented)
FiidoModeSelect = fiido_bms_ns.class_("FiidoModeSelect", select.Select, cg.Parented)
FiidoSpeedLimitSelect = fiido_bms_ns.class_(
    "FiidoSpeedLimitSelect", select.Select, cg.Parented
)
FiidoSpeedUnitSelect = fiido_bms_ns.class_(
    "FiidoSpeedUnitSelect", select.Select, cg.Parented
)

CONF_GEAR = "gear"
CONF_MODE = "mode"
CONF_SPEED_LIMIT = "speed_limit"
CONF_SPEED_UNIT = "speed_unit"
CONF_COUNT = "count"

# Bike assist level (gear) names per firmware mode.
#   3-gear: OFF / eco / sport / turbo
#   5-gear: OFF / eco / normal / sport / turbo / turbo+
# Index in the active list = raw byte 0..count for ADDR 0x26 WRITE.
GEAR_NAMES_3 = ["OFF", "eco", "sport", "turbo"]
GEAR_NAMES_5 = ["OFF", "eco", "normal", "sport", "turbo", "turbo+"]
MODE_OPTIONS = ["3", "5"]
SPEED_LIMIT_OPTIONS = ["6 km/h", "25 km/h", "No limit"]
SPEED_UNIT_OPTIONS = ["km/h", "mph"]

GEAR_SCHEMA = select.select_schema(
    FiidoGearSelect,
    icon="mdi:car-shift-pattern",
).extend(
    {
        cv.Optional(CONF_COUNT, default=5): cv.one_of(3, 5, int=True),
    }
)
MODE_SCHEMA = select.select_schema(
    FiidoModeSelect,
    icon="mdi:numeric-3-box-multiple-outline",
    entity_category=ENTITY_CATEGORY_CONFIG,
)
SPEED_LIMIT_SCHEMA = select.select_schema(
    FiidoSpeedLimitSelect,
    icon="mdi:speedometer-slow",
)
SPEED_UNIT_SCHEMA = select.select_schema(
    FiidoSpeedUnitSelect,
    icon="mdi:tape-measure",
    entity_category=ENTITY_CATEGORY_CONFIG,
)


SELECT_DEFAULT_NAMES = {
    CONF_GEAR: "Gear",
    CONF_MODE: "Gear Count",
    CONF_SPEED_LIMIT: "Speed Limit",
    CONF_SPEED_UNIT: "Speed Unit",
}


_DEFAULT_NAMES = list(SELECT_DEFAULT_NAMES.items())


def _inject_defaults(config):
    return inject_entity_defaults(config, _DEFAULT_NAMES)


CONFIG_SCHEMA = cv.All(
    _inject_defaults,
    FIIDO_BMS_COMPONENT_SCHEMA.extend(
        {
            cv.Optional(CONF_DEVICE_ID): cv.sub_device_id,
            cv.Optional(CONF_GEAR): GEAR_SCHEMA,
            cv.Optional(CONF_MODE): MODE_SCHEMA,
            cv.Optional(CONF_SPEED_LIMIT): SPEED_LIMIT_SCHEMA,
            cv.Optional(CONF_SPEED_UNIT): SPEED_UNIT_SCHEMA,
        }
    ),
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_FIIDO_BMS_ID])
    config = apply_entity_prefix(
        config, _DEFAULT_NAMES, hub_name_prefix(config[CONF_FIIDO_BMS_ID])
    )
    ui_gear_3 = hub_ui_gear_mode_3(config[CONF_FIIDO_BMS_ID])

    sub_config = config.get(CONF_GEAR)
    if sub_config is not None:
        sub_config = dict(sub_config)
        count = sub_config.pop(CONF_COUNT)
        if ui_gear_3:
            gear_options = GEAR_NAMES_3
            count = 3
        else:
            gear_options = GEAR_NAMES_5
        sel_var = await select.new_select(sub_config, options=gear_options)
        await cg.register_parented(sel_var, hub)
        cg.add(sel_var.set_names_3(GEAR_NAMES_3))
        cg.add(sel_var.set_names_5(GEAR_NAMES_5))
        cg.add(sel_var.set_gear_count(count))
        # count: 3 is a declaration that the bike is 3-gear, so runtime detection
        # must not raise it back to 5 when the BMS reports a 5 nibble.
        cg.add(sel_var.set_gear_count_pinned(ui_gear_3 or count == 3))
        cg.add(hub.set_gear_select(sel_var))

    sub_config = config.get(CONF_MODE)
    if sub_config is not None and not ui_gear_3:
        sel_var = await select.new_select(sub_config, options=MODE_OPTIONS)
        await cg.register_parented(sel_var, hub)
        cg.add(hub.set_mode_select(sel_var))

    sub_config = config.get(CONF_SPEED_LIMIT)
    if sub_config is not None:
        sel_var = await select.new_select(sub_config, options=SPEED_LIMIT_OPTIONS)
        await cg.register_parented(sel_var, hub)
        cg.add(hub.set_speed_limit_select(sel_var))

    sub_config = config.get(CONF_SPEED_UNIT)
    if sub_config is not None:
        sel_var = await select.new_select(sub_config, options=SPEED_UNIT_OPTIONS)
        await cg.register_parented(sel_var, hub)
        cg.add(hub.set_speed_unit_select(sel_var))
