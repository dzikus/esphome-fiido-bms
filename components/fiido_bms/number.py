import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    CONF_DEVICE_ID,
    CONF_DISABLED_BY_DEFAULT,
    ENTITY_CATEGORY_CONFIG,
)

from . import (
    CONF_FIIDO_BMS_ID,
    FIIDO_BMS_COMPONENT_SCHEMA,
    apply_name_prefix,
    fiido_bms_ns,
    hub_name_prefix,
)

DEPENDENCIES = ["fiido_bms"]
CODEOWNERS = ["@dzikus"]

FiidoBrightnessNumber = fiido_bms_ns.class_(
    "FiidoBrightnessNumber", number.Number, cg.Parented
)
FiidoBoostNumber = fiido_bms_ns.class_("FiidoBoostNumber", number.Number, cg.Parented)
FiidoGuardTimeNumber = fiido_bms_ns.class_(
    "FiidoGuardTimeNumber", number.Number, cg.Parented
)

CONF_BRIGHTNESS = "brightness"
CONF_BOOST = "boost"
CONF_GUARD_TIME = "guard_time"

# Newer controls ship hidden in the HA registry until validated on hardware.
# guard_time stays visible (part of the visible bike_guard feature).
HIDDEN_NUMBER_KEYS = {CONF_BRIGHTNESS, CONF_BOOST}

# Real value ranges are unknown, so the controls span the full byte and use BOX
# mode to avoid accidental slider drags. enable_poll_method gates the read-back
# poll so it only runs when the matching number is configured.
# Each entry:
#   (config_key, cpp_class, hub_setter, enable_poll_method, min, max, step, unit,
#    icon, default_name)
NUMBERS = [
    (
        CONF_BRIGHTNESS,
        FiidoBrightnessNumber,
        "set_brightness_number",
        "enable_display_poll",
        0,
        255,
        1,
        None,
        "mdi:brightness-6",
        "Display Brightness",
    ),
    (
        CONF_BOOST,
        FiidoBoostNumber,
        "set_boost_number",
        "enable_boost_poll",
        0,
        255,
        1,
        None,
        "mdi:lightning-bolt",
        "Boost",
    ),
    (
        CONF_GUARD_TIME,
        FiidoGuardTimeNumber,
        "set_guard_time_number",
        "enable_display_poll",
        0,
        255,
        1,
        "s",
        "mdi:timer-lock",
        "Guard Time",
    ),
]


def _number_schema(cls, unit, icon):
    kwargs = {"icon": icon, "entity_category": ENTITY_CATEGORY_CONFIG}
    if unit is not None:
        kwargs["unit_of_measurement"] = unit
    return number.number_schema(cls, **kwargs).extend(
        {
            cv.Optional("mode", default="BOX"): cv.enum(
                number.NUMBER_MODES, upper=True
            ),
        }
    )


def _inject_defaults(config):
    platform_dev = config.get(CONF_DEVICE_ID)
    for key, _cls, *_rest, default_name in NUMBERS:
        sub = config.get(key)
        if sub is None:
            sub = {}
            config[key] = sub
        if not isinstance(sub, dict):
            continue
        sub.setdefault("name", default_name)
        if platform_dev is not None and CONF_DEVICE_ID not in sub:
            sub[CONF_DEVICE_ID] = platform_dev
        if key in HIDDEN_NUMBER_KEYS:
            sub.setdefault(CONF_DISABLED_BY_DEFAULT, True)
    return config


CONFIG_SCHEMA = cv.All(
    _inject_defaults,
    FIIDO_BMS_COMPONENT_SCHEMA.extend(
        {
            cv.Optional(CONF_DEVICE_ID): cv.sub_device_id,
            **{
                cv.Optional(key): _number_schema(cls, unit, icon)
                for (
                    key,
                    cls,
                    _setter,
                    _enable,
                    _min,
                    _max,
                    _step,
                    unit,
                    icon,
                    _default_name,
                ) in NUMBERS
            },
        }
    ),
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_FIIDO_BMS_ID])
    prefix = hub_name_prefix(config[CONF_FIIDO_BMS_ID])
    platform_device_id = config.get(CONF_DEVICE_ID)
    for (
        key,
        _cls,
        setter,
        enable,
        min_value,
        max_value,
        step,
        _unit,
        _icon,
        default_name,
    ) in NUMBERS:
        sub_config = apply_name_prefix(config[key], default_name, prefix)
        if platform_device_id is not None and CONF_DEVICE_ID not in sub_config:
            sub_config = {**sub_config, CONF_DEVICE_ID: platform_device_id}
        num_var = await number.new_number(
            sub_config, min_value=min_value, max_value=max_value, step=step
        )
        await cg.register_parented(num_var, hub)
        cg.add(getattr(hub, setter)(num_var))
        cg.add(getattr(hub, enable)())
