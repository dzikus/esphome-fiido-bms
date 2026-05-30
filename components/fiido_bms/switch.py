import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_DEVICE_ID, ENTITY_CATEGORY_CONFIG

from . import CONF_FIIDO_BMS_ID, FIIDO_BMS_COMPONENT_SCHEMA, fiido_bms_ns

DEPENDENCIES = ["fiido_bms"]
CODEOWNERS = ["@dzikus"]

FiidoMotorSwitch = fiido_bms_ns.class_(
    "FiidoMotorSwitch", switch.Switch, cg.Parented
)
FiidoLightSwitch = fiido_bms_ns.class_(
    "FiidoLightSwitch", switch.Switch, cg.Parented
)
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

CONF_MOTOR = "motor"
CONF_LIGHT = "light"
CONF_AUTO_SHUTDOWN = "auto_shutdown"
CONF_SPEAKER = "speaker"
CONF_KEY_SOUND = "key_sound"
CONF_THROTTLE = "throttle"
CONF_SLOW_MODE_ON_BOOT = "slow_mode_on_boot"
CONF_BLUETOOTH = "bluetooth"

# Each entry: (config_key, cpp_class, hub_setter_name, default_restore_mode, is_component, extra_kwargs, default_name)
# is_component=True means the C++ class inherits Component and needs setup() lifecycle.
# extra_kwargs is forwarded to switch.switch_schema(**extra_kwargs) for icon / entity_category etc.
SWITCHES = [
    (CONF_MOTOR, FiidoMotorSwitch, "set_motor_switch", "RESTORE_DEFAULT_OFF", False,
     {"icon": "mdi:power"}, "Power"),
    (CONF_LIGHT, FiidoLightSwitch, "set_light_switch", "RESTORE_DEFAULT_OFF", False,
     {"icon": "mdi:car-light-high"}, "Light"),
    (CONF_AUTO_SHUTDOWN, FiidoAutoshutdownSwitch, "set_autoshutdown_switch",
     "RESTORE_DEFAULT_ON", True,
     {"icon": "mdi:timer-off-outline", "entity_category": ENTITY_CATEGORY_CONFIG}, "Auto Shutdown"),
    (CONF_SPEAKER, FiidoSpeakerSwitch, "set_speaker_switch",
     "RESTORE_DEFAULT_ON", False,
     {"icon": "mdi:bullhorn", "entity_category": ENTITY_CATEGORY_CONFIG}, "Horn"),
    (CONF_KEY_SOUND, FiidoKeySoundSwitch, "set_key_sound_switch",
     "RESTORE_DEFAULT_ON", False,
     {"icon": "mdi:keyboard", "entity_category": ENTITY_CATEGORY_CONFIG}, "Key Sound"),
    (CONF_THROTTLE, FiidoThrottleSwitch, "set_throttle_switch",
     "RESTORE_DEFAULT_ON", False,
     {"icon": "mdi:speedometer", "entity_category": ENTITY_CATEGORY_CONFIG}, "Throttle"),
    (CONF_SLOW_MODE_ON_BOOT, FiidoSlowModeSwitch, "set_slow_mode_switch",
     "RESTORE_DEFAULT_ON", False,
     {"icon": "mdi:tortoise", "entity_category": ENTITY_CATEGORY_CONFIG}, "Slow Mode on Boot"),
    (CONF_BLUETOOTH, FiidoBleSwitch, "set_ble_switch",
     "RESTORE_DEFAULT_ON", True,
     {"icon": "mdi:bluetooth", "entity_category": ENTITY_CATEGORY_CONFIG}, "Bluetooth"),
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
                for (key, cls, _setter, restore, _is_comp, extra_kwargs, _default_name) in SWITCHES
            },
        }
    ),
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_FIIDO_BMS_ID])
    platform_device_id = config.get(CONF_DEVICE_ID)
    for key, _cls, setter, _restore, is_component, _extra_kwargs, _default_name in SWITCHES:
        sub_config = config[key]
        if platform_device_id is not None and CONF_DEVICE_ID not in sub_config:
            sub_config = {**sub_config, CONF_DEVICE_ID: platform_device_id}
        sw_var = await switch.new_switch(sub_config)
        await cg.register_parented(sw_var, hub)
        if is_component:
            await cg.register_component(sw_var, sub_config)
        cg.add(getattr(hub, setter)(sw_var))
