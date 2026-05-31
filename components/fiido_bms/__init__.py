import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client
from esphome.const import CONF_ID

CODEOWNERS = ["@dzikus"]
DEPENDENCIES = ["ble_client"]
AUTO_LOAD = ["sensor", "binary_sensor", "switch", "select"]
MULTI_CONF = True

CONF_FIIDO_BMS_ID = "fiido_bms_id"
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

# Filled in by __init__ to_code per hub_id so sensor.py / binary_sensor.py
# to_code can decide whether to create dev entities and which polls to enable.
HUB_CONFIGS = {}

fiido_bms_ns = cg.esphome_ns.namespace("fiido_bms")
FiidoBMSHub = fiido_bms_ns.class_(
    "FiidoBMSHub", ble_client.BLEClientNode, cg.PollingComponent
)

FIIDO_BMS_COMPONENT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_FIIDO_BMS_ID): cv.use_id(FiidoBMSHub),
    }
)

# Platform schemas use cv.sub_device_id (ESPHome 2025.7.0+).
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
    cv.require_esphome_version(2025, 7, 0),
)


_ALL_HUBS = []


async def to_code(config):
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
    # binary_sensor.py) can resolve expose_dev_sensors via fiido_bms_id.
    HUB_CONFIGS[str(config[CONF_ID])] = config
