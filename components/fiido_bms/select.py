import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_DEVICE_ID, ENTITY_CATEGORY_CONFIG

from . import (
    CONF_FIIDO_BMS_ID,
    CONF_UI_GEAR_MODE_3,
    FIIDO_BMS_COMPONENT_SCHEMA,
    HUB_CONFIGS,
    fiido_bms_ns,
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


def _inject_defaults(config):
    platform_dev = config.get(CONF_DEVICE_ID)
    for key, default_name in SELECT_DEFAULT_NAMES.items():
        sub = config.get(key)
        if sub is None:
            sub = {}
            config[key] = sub
        if not isinstance(sub, dict):
            continue
        sub.setdefault("name", default_name)
        if platform_dev is not None and CONF_DEVICE_ID not in sub:
            sub[CONF_DEVICE_ID] = platform_dev
    return config


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
    platform_device_id = config.get(CONF_DEVICE_ID)
    hub_id_str = str(config[CONF_FIIDO_BMS_ID])
    hub_config = HUB_CONFIGS.get(hub_id_str, {})
    ui_gear_3 = hub_config.get(CONF_UI_GEAR_MODE_3, False)
    sub_config = dict(config[CONF_GEAR])
    if platform_device_id is not None and CONF_DEVICE_ID not in sub_config:
        sub_config[CONF_DEVICE_ID] = platform_device_id
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
    cg.add(hub.set_gear_select(sel_var))
    if not ui_gear_3:
        sub_config = dict(config[CONF_MODE])
        if platform_device_id is not None and CONF_DEVICE_ID not in sub_config:
            sub_config[CONF_DEVICE_ID] = platform_device_id
        sel_var = await select.new_select(sub_config, options=MODE_OPTIONS)
        await cg.register_parented(sel_var, hub)
        cg.add(hub.set_mode_select(sel_var))
    sub_config = dict(config[CONF_SPEED_LIMIT])
    if platform_device_id is not None and CONF_DEVICE_ID not in sub_config:
        sub_config[CONF_DEVICE_ID] = platform_device_id
    sel_var = await select.new_select(sub_config, options=SPEED_LIMIT_OPTIONS)
    await cg.register_parented(sel_var, hub)
    cg.add(hub.set_speed_limit_select(sel_var))
    sub_config = dict(config[CONF_SPEED_UNIT])
    if platform_device_id is not None and CONF_DEVICE_ID not in sub_config:
        sub_config[CONF_DEVICE_ID] = platform_device_id
    sel_var = await select.new_select(sub_config, options=SPEED_UNIT_OPTIONS)
    await cg.register_parented(sel_var, hub)
    cg.add(hub.set_speed_unit_select(sel_var))
