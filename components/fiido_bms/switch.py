import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
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

FiidoMotorSwitch = fiido_bms_ns.class_("FiidoMotorSwitch", switch.Switch, cg.Parented)
FiidoLightSwitch = fiido_bms_ns.class_("FiidoLightSwitch", switch.Switch, cg.Parented)
FiidoAutoshutdownSwitch = fiido_bms_ns.class_(
    "FiidoAutoshutdownSwitch", switch.Switch, cg.Parented, cg.Component
)
FiidoSpeakerSwitch = fiido_bms_ns.class_(
    "FiidoSpeakerSwitch", switch.Switch, cg.Parented
)
FiidoKeySoundSwitch = fiido_bms_ns.class_(
    "FiidoKeySoundSwitch", switch.Switch, cg.Parented
)
FiidoThrottleSwitch = fiido_bms_ns.class_(
    "FiidoThrottleSwitch", switch.Switch, cg.Parented
)
FiidoSlowModeSwitch = fiido_bms_ns.class_(
    "FiidoSlowModeSwitch", switch.Switch, cg.Parented
)
FiidoBleSwitch = fiido_bms_ns.class_(
    "FiidoBleSwitch", switch.Switch, cg.Parented, cg.Component
)
FiidoCruiseSwitch = fiido_bms_ns.class_("FiidoCruiseSwitch", switch.Switch, cg.Parented)
FiidoStartModeSwitch = fiido_bms_ns.class_(
    "FiidoStartModeSwitch", switch.Switch, cg.Parented
)
FiidoInsensitivitySwitch = fiido_bms_ns.class_(
    "FiidoInsensitivitySwitch", switch.Switch, cg.Parented
)
FiidoShowTotalKmSwitch = fiido_bms_ns.class_(
    "FiidoShowTotalKmSwitch", switch.Switch, cg.Parented
)
FiidoAutoScreenOffSwitch = fiido_bms_ns.class_(
    "FiidoAutoScreenOffSwitch", switch.Switch, cg.Parented
)
FiidoRingSwitch = fiido_bms_ns.class_("FiidoRingSwitch", switch.Switch, cg.Parented)
FiidoDoubleSpeedSwitch = fiido_bms_ns.class_(
    "FiidoDoubleSpeedSwitch", switch.Switch, cg.Parented
)
FiidoBikeGuardSwitch = fiido_bms_ns.class_(
    "FiidoBikeGuardSwitch", switch.Switch, cg.Parented
)

CONF_MOTOR = "motor"
CONF_LIGHT = "light"
CONF_AUTO_SHUTDOWN = "auto_shutdown"
CONF_SPEAKER = "speaker"
CONF_KEY_SOUND = "key_sound"
CONF_THROTTLE = "throttle"
CONF_SLOW_MODE_ON_BOOT = "slow_mode_on_boot"
CONF_BLUETOOTH = "bluetooth"
CONF_CRUISE = "cruise"
CONF_START_MODE = "start_mode"
CONF_INSENSITIVITY = "insensitivity"
CONF_SHOW_TOTAL_KM = "show_total_km"
CONF_AUTO_SCREEN_OFF = "auto_screen_off"
CONF_RING = "ring"
CONF_DOUBLE_SPEED = "double_speed"
CONF_BIKE_GUARD = "bike_guard"

# Newer controls ship hidden in the HA registry until validated on hardware.
# bike_guard stays visible (paired with the visible guard_time number).
HIDDEN_SWITCH_KEYS = {
    CONF_CRUISE,
    CONF_START_MODE,
    CONF_INSENSITIVITY,
    CONF_SHOW_TOTAL_KM,
    CONF_AUTO_SCREEN_OFF,
    CONF_RING,
    CONF_DOUBLE_SPEED,
}

# Each entry: (config_key, cpp_class, hub_setter_name, default_restore_mode, is_component, extra_kwargs, default_name)
# is_component=True means the C++ class inherits Component and needs setup() lifecycle.
# extra_kwargs is forwarded to switch.switch_schema(**extra_kwargs) for icon / entity_category etc.
SWITCHES = [
    (
        CONF_MOTOR,
        FiidoMotorSwitch,
        "set_motor_switch",
        "RESTORE_DEFAULT_OFF",
        False,
        {"icon": "mdi:power"},
        "Power",
    ),
    (
        CONF_LIGHT,
        FiidoLightSwitch,
        "set_light_switch",
        "RESTORE_DEFAULT_OFF",
        False,
        {"icon": "mdi:car-light-high"},
        "Light",
    ),
    (
        CONF_AUTO_SHUTDOWN,
        FiidoAutoshutdownSwitch,
        "set_autoshutdown_switch",
        "RESTORE_DEFAULT_ON",
        True,
        {"icon": "mdi:timer-off-outline", "entity_category": ENTITY_CATEGORY_CONFIG},
        "Auto Shutdown",
    ),
    (
        CONF_SPEAKER,
        FiidoSpeakerSwitch,
        "set_speaker_switch",
        "RESTORE_DEFAULT_ON",
        False,
        {"icon": "mdi:bullhorn", "entity_category": ENTITY_CATEGORY_CONFIG},
        "Horn",
    ),
    (
        CONF_KEY_SOUND,
        FiidoKeySoundSwitch,
        "set_key_sound_switch",
        "RESTORE_DEFAULT_ON",
        False,
        {"icon": "mdi:keyboard", "entity_category": ENTITY_CATEGORY_CONFIG},
        "Key Sound",
    ),
    (
        CONF_THROTTLE,
        FiidoThrottleSwitch,
        "set_throttle_switch",
        "RESTORE_DEFAULT_ON",
        False,
        {"icon": "mdi:speedometer", "entity_category": ENTITY_CATEGORY_CONFIG},
        "Throttle",
    ),
    (
        CONF_SLOW_MODE_ON_BOOT,
        FiidoSlowModeSwitch,
        "set_slow_mode_switch",
        "RESTORE_DEFAULT_ON",
        False,
        {"icon": "mdi:tortoise", "entity_category": ENTITY_CATEGORY_CONFIG},
        "Slow Mode on Boot",
    ),
    (
        CONF_BLUETOOTH,
        FiidoBleSwitch,
        "set_ble_switch",
        "RESTORE_DEFAULT_ON",
        True,
        {"icon": "mdi:bluetooth", "entity_category": ENTITY_CATEGORY_CONFIG},
        "Bluetooth",
    ),
    (
        CONF_CRUISE,
        FiidoCruiseSwitch,
        "set_cruise_switch",
        "RESTORE_DEFAULT_ON",
        False,
        {"icon": "mdi:car-cruise-control"},
        "Cruise Control",
    ),
    (
        CONF_START_MODE,
        FiidoStartModeSwitch,
        "set_start_mode_switch",
        "RESTORE_DEFAULT_ON",
        False,
        {"icon": "mdi:ray-start-arrow", "entity_category": ENTITY_CATEGORY_CONFIG},
        "Start Mode",
    ),
    (
        CONF_INSENSITIVITY,
        FiidoInsensitivitySwitch,
        "set_insensitivity_switch",
        "RESTORE_DEFAULT_ON",
        False,
        {"icon": "mdi:tune", "entity_category": ENTITY_CATEGORY_CONFIG},
        "Insensitivity",
    ),
    (
        CONF_SHOW_TOTAL_KM,
        FiidoShowTotalKmSwitch,
        "set_show_total_km_switch",
        "RESTORE_DEFAULT_ON",
        False,
        {"icon": "mdi:counter", "entity_category": ENTITY_CATEGORY_CONFIG},
        "Show Total Km",
    ),
    (
        CONF_AUTO_SCREEN_OFF,
        FiidoAutoScreenOffSwitch,
        "set_auto_screen_off_switch",
        "RESTORE_DEFAULT_ON",
        False,
        {"icon": "mdi:monitor-off", "entity_category": ENTITY_CATEGORY_CONFIG},
        "Auto Screen Off",
    ),
    (
        CONF_RING,
        FiidoRingSwitch,
        "set_ring_switch",
        "RESTORE_DEFAULT_OFF",
        False,
        {"icon": "mdi:bell-ring"},
        "Ring",
    ),
    (
        CONF_DOUBLE_SPEED,
        FiidoDoubleSpeedSwitch,
        "set_double_speed_switch",
        "RESTORE_DEFAULT_OFF",
        False,
        {"icon": "mdi:fast-forward", "entity_category": ENTITY_CATEGORY_CONFIG},
        "Double Speed",
    ),
    (
        CONF_BIKE_GUARD,
        FiidoBikeGuardSwitch,
        "set_bike_guard_switch",
        "RESTORE_DEFAULT_OFF",
        False,
        {"icon": "mdi:shield-lock", "entity_category": ENTITY_CATEGORY_CONFIG},
        "Bike Guard",
    ),
]


def _inject_defaults(config):
    platform_dev = config.get(CONF_DEVICE_ID)
    for key, *_, default_name in SWITCHES:
        sub = config.get(key)
        if sub is None:
            sub = {}
            config[key] = sub
        if not isinstance(sub, dict):
            continue
        sub.setdefault("name", default_name)
        if platform_dev is not None and CONF_DEVICE_ID not in sub:
            sub[CONF_DEVICE_ID] = platform_dev
        if key in HIDDEN_SWITCH_KEYS:
            sub.setdefault(CONF_DISABLED_BY_DEFAULT, True)
    return config


CONFIG_SCHEMA = cv.All(
    _inject_defaults,
    FIIDO_BMS_COMPONENT_SCHEMA.extend(
        {
            cv.Optional(CONF_DEVICE_ID): cv.sub_device_id,
            **{
                cv.Optional(key): switch.switch_schema(
                    cls, default_restore_mode=restore, **extra_kwargs
                )
                for (
                    key,
                    cls,
                    _setter,
                    restore,
                    _is_comp,
                    extra_kwargs,
                    _default_name,
                ) in SWITCHES
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
        _restore,
        is_component,
        _extra_kwargs,
        default_name,
    ) in SWITCHES:
        sub_config = apply_name_prefix(config[key], default_name, prefix)
        if platform_device_id is not None and CONF_DEVICE_ID not in sub_config:
            sub_config = {**sub_config, CONF_DEVICE_ID: platform_device_id}
        sw_var = await switch.new_switch(sub_config)
        await cg.register_parented(sw_var, hub)
        if is_component:
            await cg.register_component(sw_var, sub_config)
        cg.add(getattr(hub, setter)(sw_var))
