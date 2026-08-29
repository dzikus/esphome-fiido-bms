import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_DEVICE_ID,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_DISTANCE,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_SPEED,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_EMPTY,
    UNIT_KILOMETER,
    UNIT_KILOMETER_PER_HOUR,
    UNIT_PERCENT,
    UNIT_SECOND,
    UNIT_VOLT,
    UNIT_WATT,
    UNIT_WATT_HOURS,
)

from . import (
    CONF_FIIDO_BMS_ID,
    DEV_SENSOR_KEYS,
    FIIDO_BMS_COMPONENT_SCHEMA,
    HIDDEN_SENSOR_KEYS,
    apply_entity_prefix,
    hub_expose_dev,
    hub_name_prefix,
    inject_entity_defaults,
)

DEPENDENCIES = ["fiido_bms"]
CODEOWNERS = ["@dzikus"]

UNIT_INCH = "in"
UNIT_AMP_HOUR = "Ah"
UNIT_NEWTON_METER = "Nm"
UNIT_RPM = "rpm"

# (yaml_key, setter_cpp_method, unit, decimals, device_class|None, state_class|None,
#  icon|None, entity_category|None, default_name)
SENSORS = [
    # BATTERY
    (
        "battery_voltage",
        "set_battery_voltage_sensor",
        UNIT_VOLT,
        1,
        DEVICE_CLASS_VOLTAGE,
        STATE_CLASS_MEASUREMENT,
        "mdi:car-battery",
        None,
        "Battery Voltage",
    ),
    (
        "battery_current_voltage",
        "set_battery_current_voltage_sensor",
        UNIT_VOLT,
        1,
        DEVICE_CLASS_VOLTAGE,
        STATE_CLASS_MEASUREMENT,
        "mdi:car-battery",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Battery Current Voltage",
    ),
    (
        "battery_current",
        "set_battery_current_sensor",
        UNIT_AMPERE,
        1,
        DEVICE_CLASS_CURRENT,
        STATE_CLASS_MEASUREMENT,
        "mdi:current-dc",
        None,
        "Battery Current",
    ),
    (
        "battery_capacity",
        "set_battery_capacity_sensor",
        UNIT_AMP_HOUR,
        1,
        None,
        STATE_CLASS_MEASUREMENT,
        "mdi:battery-high",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Battery Capacity",
    ),
    (
        "battery_manufacturer",
        "set_battery_manufacturer_sensor",
        UNIT_EMPTY,
        0,
        None,
        None,
        "mdi:factory",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Battery Manufacturer",
    ),
    (
        "battery_hw_version",
        "set_battery_hw_version_sensor",
        UNIT_EMPTY,
        0,
        None,
        None,
        "mdi:chip",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Battery HW Version",
    ),
    (
        "battery_sw_version",
        "set_battery_sw_version_sensor",
        UNIT_EMPTY,
        0,
        None,
        None,
        "mdi:numeric",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Battery SW Version",
    ),
    # CTRL
    (
        "ctrl_upper_voltage",
        "set_ctrl_upper_voltage_sensor",
        UNIT_VOLT,
        1,
        DEVICE_CLASS_VOLTAGE,
        STATE_CLASS_MEASUREMENT,
        "mdi:car-battery",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Controller Upper Voltage",
    ),
    (
        "ctrl_lower_voltage",
        "set_ctrl_lower_voltage_sensor",
        UNIT_VOLT,
        1,
        DEVICE_CLASS_VOLTAGE,
        STATE_CLASS_MEASUREMENT,
        "mdi:car-battery",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Controller Lower Voltage",
    ),
    (
        "ctrl_current",
        "set_ctrl_current_sensor",
        UNIT_AMPERE,
        1,
        DEVICE_CLASS_CURRENT,
        STATE_CLASS_MEASUREMENT,
        "mdi:current-dc",
        None,
        "Controller Current",
    ),
    (
        "ctrl_temperature",
        "set_ctrl_temperature_sensor",
        UNIT_CELSIUS,
        0,
        DEVICE_CLASS_TEMPERATURE,
        STATE_CLASS_MEASUREMENT,
        None,
        None,
        "Controller Temperature",
    ),
    (
        "ctrl_hw_version",
        "set_ctrl_hw_version_sensor",
        UNIT_EMPTY,
        0,
        None,
        None,
        "mdi:chip",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Controller HW Version",
    ),
    (
        "ctrl_sw_version",
        "set_ctrl_sw_version_sensor",
        UNIT_EMPTY,
        0,
        None,
        None,
        "mdi:numeric",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Controller SW Version",
    ),
    (
        "ctrl_version",
        "set_ctrl_version_sensor",
        UNIT_EMPTY,
        0,
        None,
        None,
        "mdi:numeric",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Controller Version",
    ),
    (
        "ctrl_manufacturer",
        "set_ctrl_manufacturer_sensor",
        UNIT_EMPTY,
        0,
        None,
        None,
        "mdi:factory",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Controller Manufacturer",
    ),
    # MOTOR
    (
        "motor_version",
        "set_motor_version_sensor",
        UNIT_EMPTY,
        0,
        None,
        None,
        "mdi:numeric",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Motor Version",
    ),
    (
        "motor_magnetic",
        "set_motor_magnetic_sensor",
        UNIT_EMPTY,
        0,
        None,
        None,
        "mdi:magnet",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Motor Magnetic",
    ),
    (
        "motor_wire_count",
        "set_motor_wire_count_sensor",
        UNIT_EMPTY,
        0,
        None,
        None,
        "mdi:cable-data",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Motor Wire Count",
    ),
    (
        "motor_steel_count",
        "set_motor_steel_count_sensor",
        UNIT_EMPTY,
        0,
        None,
        None,
        "mdi:counter",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Motor Steel Count",
    ),
    (
        "motor_reduction_ratio",
        "set_motor_reduction_ratio_sensor",
        UNIT_EMPTY,
        1,
        None,
        None,
        "mdi:cog-transfer",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Motor Reduction Ratio",
    ),
    (
        "motor_wheel_diameter",
        "set_motor_wheel_diameter_sensor",
        UNIT_INCH,
        1,
        None,
        None,
        "mdi:tire",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Motor Wheel Diameter",
    ),
    (
        "motor_temperature",
        "set_motor_temperature_sensor",
        UNIT_CELSIUS,
        0,
        DEVICE_CLASS_TEMPERATURE,
        STATE_CLASS_MEASUREMENT,
        None,
        None,
        "Motor Temperature",
    ),
    (
        "motor_capacity",
        "set_motor_capacity_sensor",
        UNIT_WATT,
        0,
        DEVICE_CLASS_POWER,
        None,
        None,
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Motor Capacity",
    ),
    # ENERGY
    (
        "crank_torque",
        "set_crank_torque_sensor",
        UNIT_NEWTON_METER,
        1,
        None,
        STATE_CLASS_MEASUREMENT,
        "mdi:cog",
        None,
        "Crank Torque",
    ),
    (
        "crank_rpm",
        "set_crank_rpm_sensor",
        UNIT_RPM,
        0,
        None,
        STATE_CLASS_MEASUREMENT,
        "mdi:rotate-360",
        None,
        "Crank RPM",
    ),
    (
        "this_take_energy",
        "set_this_take_energy_sensor",
        UNIT_WATT_HOURS,
        1,
        DEVICE_CLASS_ENERGY,
        STATE_CLASS_TOTAL_INCREASING,
        "mdi:lightning-bolt-outline",
        None,
        "Trip Energy",
    ),
    (
        "total_take_energy",
        "set_total_take_energy_sensor",
        UNIT_WATT_HOURS,
        1,
        DEVICE_CLASS_ENERGY,
        STATE_CLASS_TOTAL_INCREASING,
        "mdi:lightning-bolt",
        None,
        "Total Energy",
    ),
    (
        "startup_time",
        "set_startup_time_sensor",
        UNIT_SECOND,
        0,
        DEVICE_CLASS_DURATION,
        STATE_CLASS_TOTAL_INCREASING,
        "mdi:timer-outline",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Uptime",
    ),
    # STATS
    (
        "bicycle_speed",
        "set_bicycle_speed_sensor",
        UNIT_KILOMETER_PER_HOUR,
        1,
        DEVICE_CLASS_SPEED,
        STATE_CLASS_MEASUREMENT,
        "mdi:speedometer",
        None,
        "Speed",
    ),
    (
        "current_kilometers",
        "set_current_kilometers_sensor",
        UNIT_KILOMETER,
        1,
        DEVICE_CLASS_DISTANCE,
        STATE_CLASS_TOTAL_INCREASING,
        "mdi:map-marker-distance",
        None,
        "Trip Distance",
    ),
    (
        "total_kilometers",
        "set_total_kilometers_sensor",
        UNIT_KILOMETER,
        1,
        DEVICE_CLASS_DISTANCE,
        STATE_CLASS_TOTAL_INCREASING,
        "mdi:counter",
        None,
        "Total Distance",
    ),
    (
        "battery_soc",
        "set_battery_soc_sensor",
        UNIT_PERCENT,
        0,
        DEVICE_CLASS_BATTERY,
        STATE_CLASS_MEASUREMENT,
        None,
        None,
        "Battery SOC",
    ),
    (
        "bicycle_gear_start",
        "set_bicycle_gear_start_sensor",
        UNIT_EMPTY,
        0,
        None,
        STATE_CLASS_MEASUREMENT,
        "mdi:bike-pedal",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Gear Start",
    ),
    # METER
    (
        "meter_hw_version",
        "set_meter_hw_version_sensor",
        UNIT_EMPTY,
        0,
        None,
        None,
        "mdi:chip",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Meter HW Version",
    ),
    (
        "meter_sw_version",
        "set_meter_sw_version_sensor",
        UNIT_EMPTY,
        0,
        None,
        None,
        "mdi:numeric",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Meter SW Version",
    ),
    (
        "meter_mode_data",
        "set_meter_mode_data_sensor",
        UNIT_EMPTY,
        0,
        None,
        None,
        "mdi:format-list-numbered",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Meter Mode Data",
    ),
]


# Map sensor yaml key to the burst poll that produces its bytes. None means
# the sensor is fed by STATS (backbone, always polled) and needs no opt-in.
SENSOR_POLL_GROUP = {
    "battery_voltage": "battery",
    "battery_current_voltage": "battery",
    "battery_current": "battery",
    "battery_capacity": "battery",
    "battery_manufacturer": "battery",
    "battery_hw_version": "battery",
    "battery_sw_version": "battery",
    "ctrl_upper_voltage": "ctrl",
    "ctrl_lower_voltage": "ctrl",
    "ctrl_current": "ctrl",
    "ctrl_temperature": "ctrl",
    "ctrl_hw_version": "ctrl",
    "ctrl_sw_version": "ctrl",
    "ctrl_version": "ctrl",
    "ctrl_manufacturer": "ctrl",
    "motor_version": "motor",
    "motor_magnetic": "motor",
    "motor_wire_count": "motor",
    "motor_steel_count": "motor",
    "motor_reduction_ratio": "motor",
    "motor_wheel_diameter": "motor",
    "motor_temperature": "motor",
    "motor_capacity": "motor",
    "crank_torque": "energy",
    "crank_rpm": "energy",
    "this_take_energy": "energy",
    "total_take_energy": "energy",
    "startup_time": "energy",
    "meter_hw_version": "meter",
    "meter_sw_version": "meter",
    "meter_mode_data": "meter",
    "bicycle_speed": None,
    "current_kilometers": None,
    "total_kilometers": None,
    "battery_soc": None,
    "bicycle_gear_start": None,
}


def _sensor_schema(unit, decimals, device_class, state_class, icon, entity_category):
    kwargs = {"unit_of_measurement": unit, "accuracy_decimals": decimals}
    if device_class is not None:
        kwargs["device_class"] = device_class
    if state_class is not None:
        kwargs["state_class"] = state_class
    if icon is not None:
        kwargs["icon"] = icon
    if entity_category is not None:
        kwargs["entity_category"] = entity_category
    return sensor.sensor_schema(**kwargs)


_DEFAULT_NAMES = [(key, name) for key, *_row, name in SENSORS]


def _inject_defaults(config):
    return inject_entity_defaults(
        config, _DEFAULT_NAMES, hidden=DEV_SENSOR_KEYS | HIDDEN_SENSOR_KEYS
    )


CONFIG_SCHEMA = cv.All(
    _inject_defaults,
    FIIDO_BMS_COMPONENT_SCHEMA.extend(
        {
            cv.Optional(CONF_DEVICE_ID): cv.sub_device_id,
            **{
                cv.Optional(key): _sensor_schema(unit, dec, dc, sc, icon, ec)
                for (
                    key,
                    _setter,
                    unit,
                    dec,
                    dc,
                    sc,
                    icon,
                    ec,
                    _default_name,
                ) in SENSORS
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

    pollers_used = set()
    for key, setter, *_row in SENSORS:
        if key not in config:
            continue
        if key in DEV_SENSOR_KEYS and not expose_dev:
            continue
        sens = await sensor.new_sensor(config[key])
        cg.add(getattr(hub, setter)(sens))
        group = SENSOR_POLL_GROUP.get(key)
        if group is not None:
            pollers_used.add(group)

    for group in pollers_used:
        cg.add(getattr(hub, f"enable_{group}_poll")())
