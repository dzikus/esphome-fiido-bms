"""Cross-table invariants in the platform modules.

Every one of these has a silent failure mode on hardware: a sensor whose poll is
never enabled stays empty forever, a hidden key that matches nothing leaves the
entity visible, and a duplicate default name only fails once two hubs exist.

    python -m unittest discover -s tests/python
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "components"))

import fiido_bms as fb
from fiido_bms import binary_sensor as fb_binary_sensor
from fiido_bms import button as fb_button
from fiido_bms import number as fb_number
from fiido_bms import select as fb_select
from fiido_bms import sensor as fb_sensor
from fiido_bms import switch as fb_switch

SENSOR_KEYS = {row[0] for row in fb_sensor.SENSORS}
SWITCH_KEYS = {row[0] for row in fb_switch.SWITCHES}
NUMBER_KEYS = {row[0] for row in fb_number.NUMBERS}
BINARY_KEYS = {row[0] for row in fb_binary_sensor.BINARY_SENSORS}
BUTTON_KEYS = {row[0] for row in fb_button.BUTTONS}


class PollGroups(unittest.TestCase):
    def test_every_sensor_declares_where_its_bytes_come_from(self):
        # A sensor missing from the map never enables its poll. It publishes
        # nothing and the log stays quiet about it.
        self.assertEqual(SENSOR_KEYS - set(fb_sensor.SENSOR_POLL_GROUP), set())

    def test_the_map_has_no_entry_for_a_sensor_that_does_not_exist(self):
        self.assertEqual(set(fb_sensor.SENSOR_POLL_GROUP) - SENSOR_KEYS, set())

    def test_each_group_matches_a_hub_enable_method(self):
        # to_code calls enable_<group>_poll on the hub; an unknown group is an
        # AttributeError at code generation, long after validation passed.
        groups = {g for g in fb_sensor.SENSOR_POLL_GROUP.values() if g is not None}
        self.assertEqual(groups, {"battery", "ctrl", "motor", "energy", "meter"})


class KeySets(unittest.TestCase):
    def test_dev_sensor_keys_all_exist(self):
        self.assertEqual(fb.DEV_SENSOR_KEYS - SENSOR_KEYS, frozenset())

    def test_dev_binary_sensor_keys_all_exist(self):
        self.assertEqual(fb.DEV_BINARY_SENSOR_KEYS - BINARY_KEYS, frozenset())

    def test_dev_switch_keys_all_exist(self):
        self.assertEqual(fb.DEV_SWITCH_KEYS - SWITCH_KEYS, frozenset())

    def test_dev_number_keys_all_exist(self):
        self.assertEqual(fb.DEV_NUMBER_KEYS - NUMBER_KEYS, frozenset())

    def test_dev_button_keys_all_exist(self):
        self.assertEqual(fb.DEV_BUTTON_KEYS - BUTTON_KEYS, frozenset())


class DefaultNames(unittest.TestCase):
    def _assert_unique(self, rows):
        names = [name for _key, name in rows]
        self.assertEqual(len(names), len(set(names)))

    def test_no_platform_repeats_a_default_name(self):
        # ESPHome rejects two entities of one platform sharing a name on the same
        # device, and that failure only shows up at compile time.
        self._assert_unique(fb_sensor._DEFAULT_NAMES)
        self._assert_unique(fb_switch._DEFAULT_NAMES)
        self._assert_unique(fb_number._DEFAULT_NAMES)
        self._assert_unique(fb_binary_sensor._DEFAULT_NAMES)

    def test_every_row_carries_a_name(self):
        for rows in (
            fb_sensor._DEFAULT_NAMES,
            fb_switch._DEFAULT_NAMES,
            fb_number._DEFAULT_NAMES,
            fb_binary_sensor._DEFAULT_NAMES,
        ):
            for key, name in rows:
                self.assertTrue(name, key)


class SwitchRestoreModes(unittest.TestCase):
    def test_only_a_component_switch_asks_to_restore(self):
        # A restored state is read back in setup(), which ESPHome calls only for
        # switches that are also components. RESTORE_* on the others is a promise
        # nothing keeps.
        for (
            key,
            _cls,
            _setter,
            restore,
            is_component,
            _kwargs,
            _name,
        ) in fb_switch.SWITCHES:
            if restore != "DISABLED":
                self.assertTrue(is_component, key)


class SelectOptions(unittest.TestCase):
    def test_speed_limit_options_match_the_firmware_strings(self):
        # resolve_speed_limit_option in fiido_state.cpp returns these literals; a
        # rename here would make every publish from the bike a no-op.
        self.assertEqual(
            fb_select.SPEED_LIMIT_OPTIONS, ["6 km/h", "25 km/h", "No limit"]
        )

    def test_speed_unit_options_match_the_firmware_strings(self):
        self.assertEqual(fb_select.SPEED_UNIT_OPTIONS, ["km/h", "mph"])
