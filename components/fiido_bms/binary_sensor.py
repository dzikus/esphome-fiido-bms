import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    CONF_DEVICE_ID,
    DEVICE_CLASS_CONNECTIVITY,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from . import (
    CONF_FIIDO_BMS_ID,
    DEV_BINARY_SENSOR_KEYS,
    FIIDO_BMS_COMPONENT_SCHEMA,
    apply_entity_prefix,
    hub_expose_dev,
    hub_name_prefix,
    inject_entity_defaults,
)

DEPENDENCIES = ["fiido_bms"]
CODEOWNERS = ["@dzikus"]

# Exposed binary sensors:
#   connected: BLE link state (observable in esp32 log).
#   brake: ADDR 0x2A bit 5. Not user-verified on physical bike yet.
# (yaml_key, setter_cpp_method, device_class|None, icon|None, entity_category|None, default_name)
BINARY_SENSORS = [
    (
        "connected",
        "set_connected_binary_sensor",
        DEVICE_CLASS_CONNECTIVITY,
        None,
        ENTITY_CATEGORY_DIAGNOSTIC,
        "BLE Connected",
    ),
    ("brake", "set_brake_binary_sensor", None, "mdi:car-brake-alert", None, "Brake"),
    (
        "pas_limit",
        "set_pas_limit_binary_sensor",
        None,
        "mdi:bike-pedal-clipless",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "PAS Limit",
    ),
]


def _bs_schema(device_class, icon, entity_category):
    kwargs = {}
    if device_class is not None:
        kwargs["device_class"] = device_class
    if icon is not None:
        kwargs["icon"] = icon
    if entity_category is not None:
        kwargs["entity_category"] = entity_category
    return binary_sensor.binary_sensor_schema(**kwargs)


_DEFAULT_NAMES = [(key, name) for key, *_row, name in BINARY_SENSORS]


def _inject_defaults(config):
    return inject_entity_defaults(config, _DEFAULT_NAMES, hidden=DEV_BINARY_SENSOR_KEYS)


CONFIG_SCHEMA = cv.All(
    _inject_defaults,
    FIIDO_BMS_COMPONENT_SCHEMA.extend(
        {
            cv.Optional(CONF_DEVICE_ID): cv.sub_device_id,
            **{
                cv.Optional(key): _bs_schema(dc, icon, ec)
                for (key, _setter, dc, icon, ec, _default_name) in BINARY_SENSORS
            },
        }
    ),
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_FIIDO_BMS_ID])
    config = apply_entity_prefix(
        config, _DEFAULT_NAMES, hub_name_prefix(config[CONF_FIIDO_BMS_ID])
    )
    expose_dev = hub_expose_dev(config[CONF_FIIDO_BMS_ID])

    for key, setter, *_row in BINARY_SENSORS:
        if key not in config:
            continue
        if key in DEV_BINARY_SENSOR_KEYS and not expose_dev:
            continue
        bs = await binary_sensor.new_binary_sensor(config[key])
        cg.add(getattr(hub, setter)(bs))
