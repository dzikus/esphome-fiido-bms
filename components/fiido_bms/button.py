import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_DEVICE_ID, ENTITY_CATEGORY_CONFIG

from . import (
    CONF_FIIDO_BMS_ID,
    DEV_BUTTON_KEYS,
    FIIDO_BMS_COMPONENT_SCHEMA,
    apply_entity_prefix,
    fiido_bms_ns,
    hub_entity_config,
    hub_name_prefix,
    inject_entity_defaults,
)

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


_DEFAULT_NAMES = [(key, name) for key, *_row, name in BUTTONS]


def _inject_defaults(config):
    return inject_entity_defaults(
        config, _DEFAULT_NAMES, hidden=DEV_BUTTON_KEYS, platform="button"
    )


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
    config = apply_entity_prefix(
        config, _DEFAULT_NAMES, hub_name_prefix(config[CONF_FIIDO_BMS_ID])
    )
    for key, _cls, setter, _icon, _default_name in BUTTONS:
        if key not in config:
            continue
        entity = hub_entity_config(
            config[CONF_FIIDO_BMS_ID], "button", key, config[key]
        )
        if entity is None:
            continue
        btn_var = await button.new_button(entity)
        await cg.register_parented(btn_var, hub)
        cg.add(getattr(hub, setter)(btn_var))
