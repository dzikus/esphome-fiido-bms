"""Cross-table invariants in the platform modules.

Every one of these has a silent failure mode on hardware: a sensor whose poll is
never enabled stays empty forever, a hidden key that matches nothing leaves the
entity visible, and a duplicate default name only fails once two hubs exist.

    python -m unittest discover -s tests/python
"""

import os
import re
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
SELECT_KEYS = set(fb_select.SELECT_DEFAULT_NAMES)

PLATFORM_KEYS = {
    "sensor": SENSOR_KEYS,
    "binary_sensor": BINARY_KEYS,
    "switch": SWITCH_KEYS,
    "select": SELECT_KEYS,
    "number": NUMBER_KEYS,
    "button": BUTTON_KEYS,
}


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

    def test_every_poll_enable_emitted_by_codegen_exists_on_the_hub(self):
        # Codegen emits any method name as given; a missing one fails only when
        # the firmware compiles.
        component = os.path.join(
            os.path.dirname(__file__), "..", "..", "components", "fiido_bms"
        )
        called = set()
        for name in sorted(os.listdir(component)):
            if name.endswith(".py"):
                with open(os.path.join(component, name), encoding="utf-8") as handle:
                    called |= set(
                        re.findall(r"\bhub\.(enable_\w+_poll)\(", handle.read())
                    )
        with open(os.path.join(component, "fiido_bms.h"), encoding="utf-8") as handle:
            declared = set(
                re.findall(r"\bvoid\s+(enable_\w+_poll)\s*\(\s*\)", handle.read())
            )
        self.assertIn("enable_speed_limit_poll", called)
        emitted = {
            f"enable_{g}_poll"
            for g in fb_sensor.SENSOR_POLL_GROUP.values()
            if g is not None
        }
        emitted |= {enable for _key, _cls, _setter, enable, *_row in fb_number.NUMBERS}
        emitted |= called
        self.assertEqual(emitted - declared, set())


class GearNames(unittest.TestCase):
    def test_the_two_gear_name_tables_agree(self):
        path = os.path.join(
            os.path.dirname(__file__),
            "..",
            "..",
            "components",
            "fiido_bms",
            "fiido_gear_select.h",
        )
        with open(path, encoding="utf-8") as handle:
            header = handle.read()
        for symbol, expected in (
            ("NAMES_3", fb_select.GEAR_NAMES_3),
            ("NAMES_5", fb_select.GEAR_NAMES_5),
        ):
            line = next(ln for ln in header.splitlines() if symbol + "{" in ln)
            self.assertEqual(re.findall(r'"([^"]*)"', line), expected)


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

    def test_dev_keys_by_platform_names_every_platform(self):
        self.assertEqual(set(fb.DEV_KEYS_BY_PLATFORM), set(PLATFORM_KEYS))


class ModelNames(unittest.TestCase):
    # codegen emits Model::<name> as given; a name missing from the enum fails
    # only when the firmware compiles.

    def setUp(self):
        path = os.path.join(
            os.path.dirname(__file__),
            "..",
            "..",
            "components",
            "fiido_bms",
            "fiido_model.h",
        )
        with open(path, encoding="utf-8") as handle:
            self.header = handle.read()
        body = re.search(
            r"enum\s+class\s+Model\s*:\s*uint8_t\s*\{(.*?)\}", self.header, re.DOTALL
        )
        self.enumerators = re.findall(r"\b[A-Z][A-Z0-9_]*\b", body.group(1))

    def test_models_follow_the_enum_in_order(self):
        self.assertEqual(
            [str(value).rsplit("::", 1)[-1] for value in fb.MODELS.values()],
            self.enumerators,
        )
        self.assertEqual([key.upper() for key in fb.MODELS], self.enumerators)

    def test_used_by_names_the_models_whose_rows_use_the_profile(self):
        # The firmware prints used_by as the model: value to set.
        profiles = dict(
            re.findall(
                r"inline\s+constexpr\s+GattProfile\s+(\w+)\s*\{[^}]*?"
                r"\.used_by\s*=\s*\"([^\"]*)\"",
                self.header,
            )
        )
        rows = re.findall(
            r"\.model\s*=\s*Model::(\w+)\s*,[^{}]*?\.gatt\s*=\s*(\w+)", self.header
        )
        key_of = {
            str(value).rsplit("::", 1)[-1]: key for key, value in fb.MODELS.items()
        }
        self.assertTrue(profiles)
        self.assertEqual(len(rows), len(self.enumerators))
        self.assertLessEqual({gatt for _model, gatt in rows}, set(profiles))
        for profile, used_by in profiles.items():
            with self.subTest(profile=profile):
                named = re.split(r"\s*,\s*|\s+or\s+", used_by.strip())
                using = [key_of[model] for model, gatt in rows if gatt == profile]
                self.assertEqual(sorted(named), sorted(using))


class ModelEntitySets(unittest.TestCase):
    def test_each_set_belongs_to_a_model(self):
        self.assertLessEqual(set(fb.MODEL_ENTITY_SETS), set(fb.MODELS))

    def test_every_key_exists_in_its_platform_rows(self):
        for model, entity_set in fb.MODEL_ENTITY_SETS.items():
            for group in (entity_set["stable"], entity_set["unavailable"]):
                for platform, keys in group.items():
                    with self.subTest(model=model, platform=platform):
                        self.assertEqual(keys - PLATFORM_KEYS[platform], frozenset())

    def test_stable_and_unavailable_do_not_overlap(self):
        for model, entity_set in fb.MODEL_ENTITY_SETS.items():
            for platform in PLATFORM_KEYS:
                with self.subTest(model=model, platform=platform):
                    stable = entity_set["stable"].get(platform, frozenset())
                    unavailable = entity_set["unavailable"].get(platform, frozenset())
                    self.assertEqual(stable & unavailable, frozenset())

    def test_air_keeps_no_stable_switch_beyond_bluetooth_and_auto_shutdown(self):
        stable = fb.MODEL_ENTITY_SETS["air"]["stable"].get("switch", frozenset())
        self.assertLessEqual(stable, {"bluetooth", "auto_shutdown"})

    def test_air_has_no_stable_select_number_or_button(self):
        air = fb.MODEL_ENTITY_SETS["air"]
        for platform in ("select", "number", "button"):
            with self.subTest(platform=platform):
                self.assertEqual(air["stable"].get(platform, frozenset()), frozenset())


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
