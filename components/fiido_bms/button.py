import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_DEVICE_ID, ENTITY_CATEGORY_CONFIG

from . import CONF_FIIDO_BMS_ID, FIIDO_BMS_COMPONENT_SCHEMA, fiido_bms_ns

DEPENDENCIES = ["fiido_bms"]
CODEOWNERS = ["@dzikus"]

FiidoPairWatchButton = fiido_bms_ns.class_(
    "FiidoPairWatchButton", button.Button, cg.Parented
)

CONF_PAIR_WATCH = "pair_watch"

# Experimental proximity-unlock pairing. Hidden by default until validated.
# Each entry: (config_key, cpp_class, hub_setter, icon, default_name)
BUTTONS = [
    (
        CONF_PAIR_WATCH,
        FiidoPairWatchButton,
        "set_pair_watch_button",
        "mdi:watch",
        "Pair Watch",
    ),
]


def _inject_defaults(config):
    platform_dev = config.get(CONF_DEVICE_ID)
    for key, _cls, _setter, _icon, default_name in BUTTONS:
        sub = config.get(key)
        if sub is None:
            sub = {}
            config[key] = sub
        if not isinstance(sub, dict):
            continue
        sub.setdefault("name", default_name)
        sub.setdefault("disabled_by_default", True)
        if platform_dev is not None and CONF_DEVICE_ID not in sub:
            sub[CONF_DEVICE_ID] = platform_dev
    return config


CONFIG_SCHEMA = cv.All(
    _inject_defaults,
    FIIDO_BMS_COMPONENT_SCHEMA.extend(
        {
            cv.Optional(CONF_DEVICE_ID): cv.sub_device_id,
            **{
                cv.Optional(key): button.button_schema(
                    cls,
                    icon=icon,
                    entity_category=ENTITY_CATEGORY_CONFIG,
                )
                for (key, cls, _setter, icon, _default_name) in BUTTONS
            },
        }
    ),
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_FIIDO_BMS_ID])
    platform_device_id = config.get(CONF_DEVICE_ID)
    for key, _cls, setter, _icon, _default_name in BUTTONS:
        sub_config = config[key]
        if platform_device_id is not None and CONF_DEVICE_ID not in sub_config:
            sub_config = {**sub_config, CONF_DEVICE_ID: platform_device_id}
        btn_var = await button.new_button(sub_config)
        await cg.register_parented(btn_var, hub)
        cg.add(getattr(hub, setter)(btn_var))
