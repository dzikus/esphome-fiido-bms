import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    CONF_DEVICE_ID,
    CONF_DISABLED_BY_DEFAULT,
    DEVICE_CLASS_CONNECTIVITY,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from . import (
    CONF_EXPOSE_DEV_SENSORS,
    CONF_FIIDO_BMS_ID,
    DEV_BINARY_SENSOR_KEYS,
    FIIDO_BMS_COMPONENT_SCHEMA,
    HUB_CONFIGS,
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


def _inject_defaults(config):
    platform_dev = config.get(CONF_DEVICE_ID)
    for key, *_, default_name in BINARY_SENSORS:
        sub = config.get(key)
        if sub is None:
            sub = {}
            config[key] = sub
        if not isinstance(sub, dict):
            continue
        sub.setdefault("name", default_name)
        if platform_dev is not None and CONF_DEVICE_ID not in sub:
            sub[CONF_DEVICE_ID] = platform_dev
        if key in DEV_BINARY_SENSOR_KEYS and CONF_DISABLED_BY_DEFAULT not in sub:
            sub[CONF_DISABLED_BY_DEFAULT] = True
    return config


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
    platform_device_id = config.get(CONF_DEVICE_ID)
    hub_id_str = str(config[CONF_FIIDO_BMS_ID])
    hub_config = HUB_CONFIGS.get(hub_id_str, {})
    expose_dev = hub_config.get(CONF_EXPOSE_DEV_SENSORS, False)

    for key, setter, *_ in BINARY_SENSORS:
        if key in DEV_BINARY_SENSOR_KEYS and not expose_dev:
            continue
        sub_config = config[key]
        if platform_device_id is not None and CONF_DEVICE_ID not in sub_config:
            sub_config = {**sub_config, CONF_DEVICE_ID: platform_device_id}
        bs = await binary_sensor.new_binary_sensor(sub_config)
        cg.add(getattr(hub, setter)(bs))
