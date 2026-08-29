import logging

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import ble_client
from esphome.const import (
    CONF_DEVICE_ID,
    CONF_DISABLED_BY_DEFAULT,
    CONF_ID,
    CONF_NAME,
)
from esphome.core import CORE

_LOGGER = logging.getLogger(__name__)

CODEOWNERS = ["@dzikus"]
DEPENDENCIES = ["ble_client"]
AUTO_LOAD = ["sensor", "binary_sensor", "switch", "select", "number", "button"]
MULTI_CONF = True

DOMAIN = "fiido_bms"

CONF_FIIDO_BMS_ID = "fiido_bms_id"
CONF_NAME_PREFIX = "name_prefix"
CONF_STARTUP_DELAY = "startup_delay"
CONF_UPDATE_INTERVAL_ON = "update_interval_on"
CONF_UPDATE_INTERVAL_OFF = "update_interval_off"
CONF_IDLE_DISCONNECT = "idle_disconnect"
CONF_EXPOSE_DEV_SENSORS = "expose_dev_sensors"
CONF_UI_GEAR_MODE_3 = "ui_gear_mode_3"
CONF_ENFORCE_GEAR_MODE_3 = "enforce_gear_mode_3"

# Sensors that yield static, always-zero, or not-yet-validated values on
# C11/M1. When expose_dev_sensors is false (default) they are skipped entirely
# and the polls feeding them can be dropped from the burst rotation. When true
# they are created with disabled_by_default=True so HA hides them until the
# user explicitly opts in per entity.
DEV_SENSOR_KEYS = frozenset(
    {
        # battery group
        "battery_current",
        "battery_current_voltage",
        "battery_hw_version",
        "battery_sw_version",
        "battery_manufacturer",
        # ctrl group (whole poll skippable when expose_dev=false)
        "ctrl_upper_voltage",
        "ctrl_lower_voltage",
        "ctrl_current",
        "ctrl_temperature",
        "ctrl_hw_version",
        "ctrl_sw_version",
        "ctrl_version",
        "ctrl_manufacturer",
        # motor group
        "motor_version",
        "motor_magnetic",
        "motor_wire_count",
        "motor_steel_count",
        "motor_reduction_ratio",
        # energy group
        "crank_rpm",
        "crank_torque",
        "this_take_energy",
        "total_take_energy",
        # stats (backbone poll, no skip; only entity gated)
        "bicycle_gear_start",
        "startup_time",
        # meter group (whole poll skippable when expose_dev=false)
        "meter_hw_version",
        "meter_sw_version",
        "meter_mode_data",
    }
)

DEV_BINARY_SENSOR_KEYS = frozenset(
    {
        "brake",
    }
)

DEV_SWITCH_KEYS = frozenset(
    {
        "start_mode",
        "auto_screen_off",
        "ring",
        "double_speed",
        "cruise",
        "insensitivity",
        "show_total_km",
    }
)

DEV_NUMBER_KEYS = frozenset(
    {
        "brightness",
        "boost",
        "guard_time",
    }
)

DEV_BUTTON_KEYS = frozenset(
    {
        "pair_watch",
    }
)

HUB_CONFIGS = {}


def _hub_conf(hub_id):
    # Must not depend on to_code order: under MULTI_CONF a platform's to_code can
    # run before the hub fills HUB_CONFIGS, which silently dropped dev entities.
    # CORE.config is complete before any to_code runs.
    target = str(hub_id)
    for hub_conf in CORE.config.get(DOMAIN, []):
        if str(hub_conf.get(CONF_ID)) == target:
            return hub_conf
    return HUB_CONFIGS.get(target, {})


def hub_expose_dev(hub_id):
    return bool(_hub_conf(hub_id).get(CONF_EXPOSE_DEV_SENSORS, False))


def hub_ui_gear_mode_3(hub_id):
    return bool(_hub_conf(hub_id).get(CONF_UI_GEAR_MODE_3, False))


def hub_name_prefix(hub_id):
    # Entity names are node-wide: the api key is a hash of the name alone and
    # mqtt derives topic and unique_id from it, so two hubs left on the default
    # names produce colliding entities. name_prefix separates them. Opt-in, so
    # a single-hub setup and every config written before it keep their names.
    return _hub_conf(hub_id).get(CONF_NAME_PREFIX, "").strip()


def inject_entity_defaults(config, rows, hidden=frozenset(), opt_in=frozenset()):
    # Copy before mutating: the validator may run against a shared dict.
    config = dict(config)
    platform_device = config.get(CONF_DEVICE_ID)
    for key, default_name in rows:
        want = config.get(key, ...)
        if want is False or (want is ... and key in opt_in):
            config.pop(key, None)
            continue
        # is, not ==: a stray 'speed: 1' equals True and must not read as one.
        sub = {} if (want is ... or want is None or want is True) else want
        if not isinstance(sub, dict):
            raise cv.Invalid(
                f"'{key}' takes true, false, or the options for one entity. "
                f"To rename it write 'name: {sub}' under it.",
                path=[key],
            )
        sub = dict(sub)
        sub.setdefault(CONF_NAME, default_name)
        if platform_device is not None and CONF_DEVICE_ID not in sub:
            sub[CONF_DEVICE_ID] = platform_device
        if key in hidden:
            sub.setdefault(CONF_DISABLED_BY_DEFAULT, True)
        config[key] = sub
    return config


def apply_entity_prefix(config, rows, prefix):
    # Prefix the injected default only. A name set in yaml is used verbatim.
    if not prefix:
        return config
    config = dict(config)
    for key, default_name in rows:
        sub = config.get(key)
        if isinstance(sub, dict) and sub.get(CONF_NAME) == default_name:
            config[key] = {**sub, CONF_NAME: f"{prefix} {default_name}"}
    return config


fiido_bms_ns = cg.esphome_ns.namespace("fiido_bms")
FiidoBMSHub = fiido_bms_ns.class_(
    "FiidoBMSHub", ble_client.BLEClientNode, cg.PollingComponent
)

FIIDO_BMS_COMPONENT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_FIIDO_BMS_ID): cv.use_id(FiidoBMSHub),
    }
)

# Platform schemas use cv.sub_device_id (ESPHome 2025.8.0+).
CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(FiidoBMSHub),
            cv.Optional(
                CONF_STARTUP_DELAY, default="0s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_UPDATE_INTERVAL_ON, default="3s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_UPDATE_INTERVAL_OFF, default="15s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_IDLE_DISCONNECT, default="15min"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_EXPOSE_DEV_SENSORS, default=False): cv.boolean,
            cv.Optional(CONF_NAME_PREFIX): cv.All(cv.string_strict, cv.Length(max=48)),
            cv.Optional(CONF_UI_GEAR_MODE_3, default=False): cv.boolean,
            cv.Optional(CONF_ENFORCE_GEAR_MODE_3, default=False): cv.boolean,
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    # PollingComponent baseline: tick every 1s so adaptive interval gate in
    # update() can flip ON<->OFF cadence without restarting the scheduler.
    # update_interval_on (default 3s) while motor controller is ON, switches
    # to update_interval_off (default 15s) when bit 7 ADDR 0x27 clears.
    # 15s is fast enough to catch physical motor ON in the idle window.
    .extend(cv.polling_component_schema("1s")),
    cv.require_esphome_version(2025, 8, 0),
)


def _warn_on_shared_default_names(config):
    hub_count = len(fv.full_config.get().get(DOMAIN, []))
    if hub_count > 1 and CONF_NAME_PREFIX not in config:
        _LOGGER.warning(
            "fiido_bms hub '%s' has no name_prefix and %d hubs are configured. "
            "Their entities keep the same default names, so they share api "
            "keys and mqtt topics, and only a client that reads device_id can "
            "tell the bikes apart. Set name_prefix per hub to give each bike "
            "its own names, or name_prefix: '' to keep the current ones and "
            "silence this.",
            config[CONF_ID],
            hub_count,
        )
    return config


def _one_hub_per_ble_client(config):
    claimed = {}
    for hub in fv.full_config.get().get(DOMAIN, []):
        ble_id = str(hub[ble_client.CONF_BLE_CLIENT_ID])
        hub_id = str(hub[CONF_ID])
        if ble_id in claimed:
            raise cv.Invalid(
                f"fiido_bms '{hub_id}' and '{claimed[ble_id]}' both use "
                f"ble_client '{ble_id}'. Give each bike its own ble_client."
            )
        claimed[ble_id] = hub_id
    return config


def _final_validate(config):
    _one_hub_per_ble_client(config)
    return _warn_on_shared_default_names(config)


FINAL_VALIDATE_SCHEMA = _final_validate

_ALL_HUBS = []

_RUN_CONFIG_ID = None


def _reset_run_state():
    # The dashboard keeps this module loaded across compiles, so hub_index and
    # total_hubs would climb run to run. CORE.config is rebuilt per run, so its
    # identity marks the boundary.
    global _RUN_CONFIG_ID
    current = id(CORE.config)
    if current != _RUN_CONFIG_ID:
        _RUN_CONFIG_ID = current
        HUB_CONFIGS.clear()
        _ALL_HUBS.clear()


async def to_code(config):
    _reset_run_state()
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    cg.add(var.set_startup_delay(config[CONF_STARTUP_DELAY]))
    cg.add(var.set_update_interval_on_ms(config[CONF_UPDATE_INTERVAL_ON]))
    cg.add(var.set_update_interval_off_ms(config[CONF_UPDATE_INTERVAL_OFF]))
    cg.add(var.set_idle_disconnect_ms(config[CONF_IDLE_DISCONNECT]))
    cg.add(var.set_enforce_gear_mode_3(config[CONF_ENFORCE_GEAR_MODE_3]))
    # Track every hub created so we can broadcast the final count back to all
    # of them. Each call re-emits set_total_hubs(N) for every hub so the value
    # baked into generated code is the final total.
    cg.add(var.set_hub_index(len(_ALL_HUBS)))
    _ALL_HUBS.append(var)
    total = len(_ALL_HUBS)
    for hub in _ALL_HUBS:
        cg.add(hub.set_total_hubs(total))
    # Stash full hub config under its yaml id so platform to_code (sensor.py /
    # binary_sensor.py) can resolve expose_dev_sensors and name_prefix via
    # fiido_bms_id.
    HUB_CONFIGS[str(config[CONF_ID])] = config
