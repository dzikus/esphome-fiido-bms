"""Codegen helpers from components/fiido_bms/__init__.py, run on the host.

Needs an interpreter with esphome importable:

    python -m unittest discover -s tests/python
"""

import asyncio
import os
import sys
import unittest
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "components"))

import esphome.config_validation as cv
import esphome.final_validate as fv
import fiido_bms as fb
from esphome.components import ble_client
from esphome.core import CORE
from fiido_bms import switch as fb_switch

ROWS = [("motor", "Power"), ("light", "Light")]


class InjectEntityDefaults(unittest.TestCase):
    def test_does_not_mutate_caller_config(self):
        # A validator used to write into the dict it was handed, which is shared
        # between validation passes.
        original = {"motor": {"icon": "mdi:power"}}
        snapshot = {"motor": dict(original["motor"])}
        fb.inject_entity_defaults(original, ROWS)
        self.assertEqual(original, snapshot)

    def test_absent_key_gets_default_name(self):
        out = fb.inject_entity_defaults({}, ROWS)
        self.assertEqual(out["motor"]["name"], "Power")
        self.assertEqual(out["light"]["name"], "Light")

    def test_explicit_name_survives(self):
        out = fb.inject_entity_defaults({"motor": {"name": "Bike Power"}}, ROWS)
        self.assertEqual(out["motor"]["name"], "Bike Power")

    def test_false_removes_the_entity(self):
        out = fb.inject_entity_defaults({"motor": False}, ROWS)
        self.assertNotIn("motor", out)
        self.assertIn("light", out)

    def test_true_is_the_same_as_absent(self):
        self.assertEqual(
            fb.inject_entity_defaults({"motor": True}, ROWS)["motor"],
            fb.inject_entity_defaults({}, ROWS)["motor"],
        )

    def test_scalar_is_rejected_not_read_as_true(self):
        # 1 == True in Python, so an identity check is the only thing keeping
        # 'motor: 1' from being accepted as an enable flag.
        with self.assertRaises(cv.Invalid):
            fb.inject_entity_defaults({"motor": 1}, ROWS)

    def test_opt_in_key_stays_absent_until_asked_for(self):
        out = fb.inject_entity_defaults({}, ROWS, opt_in={"light"})
        self.assertNotIn("light", out)
        self.assertIn("motor", out)

    def test_opt_in_key_appears_when_requested(self):
        out = fb.inject_entity_defaults({"light": True}, ROWS, opt_in={"light"})
        self.assertEqual(out["light"]["name"], "Light")

    def test_hidden_key_defaults_to_disabled(self):
        out = fb.inject_entity_defaults({}, ROWS, hidden={"light"})
        self.assertTrue(out["light"]["disabled_by_default"])
        self.assertNotIn("disabled_by_default", out["motor"])

    def test_hidden_key_respects_an_explicit_choice(self):
        out = fb.inject_entity_defaults(
            {"light": {"disabled_by_default": False}}, ROWS, hidden={"light"}
        )
        self.assertFalse(out["light"]["disabled_by_default"])

    def test_platform_device_id_reaches_every_entity(self):
        # Without this the second hub fails validation: ESPHome keys its duplicate
        # name check on (device_id, platform, name).
        out = fb.inject_entity_defaults({"device_id": "dev_c11"}, ROWS)
        self.assertEqual(out["motor"]["device_id"], "dev_c11")
        self.assertEqual(out["light"]["device_id"], "dev_c11")

    def test_entity_device_id_wins_over_platform(self):
        out = fb.inject_entity_defaults(
            {"device_id": "dev_c11", "motor": {"device_id": "dev_other"}}, ROWS
        )
        self.assertEqual(out["motor"]["device_id"], "dev_other")


class ApplyEntityPrefix(unittest.TestCase):
    def test_prefixes_the_injected_default(self):
        config = fb.inject_entity_defaults({}, ROWS)
        out = fb.apply_entity_prefix(config, ROWS, "C11")
        self.assertEqual(out["motor"]["name"], "C11 Power")

    def test_leaves_a_name_from_yaml_alone(self):
        config = fb.inject_entity_defaults({"motor": {"name": "Bike Power"}}, ROWS)
        out = fb.apply_entity_prefix(config, ROWS, "C11")
        self.assertEqual(out["motor"]["name"], "Bike Power")

    def test_empty_prefix_changes_nothing(self):
        config = fb.inject_entity_defaults({}, ROWS)
        self.assertIs(fb.apply_entity_prefix(config, ROWS, ""), config)

    def test_does_not_mutate_caller_config(self):
        config = fb.inject_entity_defaults({}, ROWS)
        fb.apply_entity_prefix(config, ROWS, "C11")
        self.assertEqual(config["motor"]["name"], "Power")

    def test_skips_an_entity_that_was_removed(self):
        config = fb.inject_entity_defaults({"motor": False}, ROWS)
        out = fb.apply_entity_prefix(config, ROWS, "C11")
        self.assertNotIn("motor", out)


class HubOptionLookup(unittest.TestCase):
    # Under MULTI_CONF a platform's to_code can run before the hub fills
    # HUB_CONFIGS, so the lookup must not depend on that order.

    def setUp(self):
        self._config = CORE.config
        self.addCleanup(setattr, CORE, "config", self._config)
        self.addCleanup(fb.HUB_CONFIGS.clear)
        fb.HUB_CONFIGS.clear()

    def test_reads_options_before_the_hub_registered_itself(self):
        CORE.config = {
            "fiido_bms": [
                {"id": "hub_c11", "expose_dev_sensors": True, "name_prefix": "C11"}
            ]
        }
        self.assertTrue(fb.hub_expose_dev("hub_c11"))
        self.assertEqual(fb.hub_name_prefix("hub_c11"), "C11")

    def test_picks_the_matching_hub_out_of_several(self):
        CORE.config = {
            "fiido_bms": [
                {"id": "hub_c11", "ui_gear_mode_3": True},
                {"id": "hub_m1"},
            ]
        }
        self.assertTrue(fb.hub_ui_gear_mode_3("hub_c11"))
        self.assertFalse(fb.hub_ui_gear_mode_3("hub_m1"))

    def test_falls_back_to_registered_config(self):
        CORE.config = {}
        fb.HUB_CONFIGS["hub_c11"] = {"expose_dev_sensors": True}
        self.assertTrue(fb.hub_expose_dev("hub_c11"))

    def test_unknown_hub_yields_defaults(self):
        CORE.config = {}
        self.assertFalse(fb.hub_expose_dev("nope"))
        self.assertEqual(fb.hub_name_prefix("nope"), "")

    def test_name_prefix_is_stripped(self):
        CORE.config = {"fiido_bms": [{"id": "hub_c11", "name_prefix": "  C11  "}]}
        self.assertEqual(fb.hub_name_prefix("hub_c11"), "C11")

    def test_model_reads_back_as_the_yaml_key(self):
        hub = fb.CONFIG_SCHEMA(
            {"id": "hub_air", "ble_client_id": "ble_air", "model": "Air"}
        )
        CORE.config = {"fiido_bms": [hub]}
        self.assertEqual(fb.hub_model("hub_air"), "air")
        self.assertIs(type(fb.hub_model("hub_air")), str)

    def test_model_is_none_when_not_set(self):
        CORE.config = {"fiido_bms": [{"id": "hub_c11"}]}
        self.assertIsNone(fb.hub_model("hub_c11"))


class ModelOption(unittest.TestCase):
    def _hub(self, **options):
        return fb.CONFIG_SCHEMA({"ble_client_id": "ble_test", **options})

    def test_absent_model_stays_absent(self):
        self.assertNotIn("model", self._hub())

    def test_air_rejects_each_gear_option(self):
        for key in ("enforce_gear_mode_3", "ui_gear_mode_3"):
            with self.subTest(key=key):
                with self.assertRaises(cv.Invalid) as caught:
                    self._hub(model="air", **{key: True})
                self.assertEqual(caught.exception.path, [key])

    def test_air_accepts_the_gear_options_left_off(self):
        out = self._hub(model="air", enforce_gear_mode_3=False, ui_gear_mode_3=False)
        self.assertEqual(out["model"], "air")

    def test_gear_options_pass_for_every_other_model(self):
        for model in (None, "c11_pro", "m1_pro_2025"):
            options = {"enforce_gear_mode_3": True, "ui_gear_mode_3": True}
            if model is not None:
                options["model"] = model
            with self.subTest(model=model):
                out = self._hub(**options)
                self.assertTrue(out["enforce_gear_mode_3"])
                self.assertTrue(out["ui_gear_mode_3"])


class HubEntityConfig(unittest.TestCase):
    CASES = (
        ("sensor", "battery_voltage"),
        ("sensor", "battery_current"),
        ("sensor", "motor_temperature"),
        ("binary_sensor", "brake"),
        ("switch", "throttle"),
        ("switch", "cruise"),
        ("select", "gear"),
        ("select", "speed_limit"),
        ("number", "brightness"),
        ("button", "pair_watch"),
    )

    def setUp(self):
        self._config = CORE.config
        self.addCleanup(setattr, CORE, "config", self._config)
        self.addCleanup(fb.HUB_CONFIGS.clear)
        fb.HUB_CONFIGS.clear()

    def _use(self, expose_dev=False, model=None):
        hub = {"id": "hub_x", "expose_dev_sensors": expose_dev}
        if model is not None:
            hub["model"] = model
        CORE.config = {"fiido_bms": [hub]}

    def test_without_a_model_a_dev_key_needs_expose_dev(self):
        entity = {"name": "Battery Current"}
        self._use(expose_dev=False)
        self.assertIsNone(
            fb.hub_entity_config("hub_x", "sensor", "battery_current", entity)
        )
        self._use(expose_dev=True)
        self.assertIs(
            fb.hub_entity_config("hub_x", "sensor", "battery_current", entity), entity
        )

    def test_without_a_model_a_stable_key_is_the_same_object(self):
        entity = {"name": "Battery Voltage"}
        for expose_dev in (False, True):
            with self.subTest(expose_dev=expose_dev):
                self._use(expose_dev=expose_dev)
                self.assertIs(
                    fb.hub_entity_config("hub_x", "sensor", "battery_voltage", entity),
                    entity,
                )

    def test_reference_models_build_what_a_hub_without_a_model_builds(self):
        for expose_dev in (False, True):
            for platform, key in self.CASES:
                entity = {"name": key}
                self._use(expose_dev=expose_dev)
                expected = fb.hub_entity_config("hub_x", platform, key, entity)
                for model in ("c11_pro", "m1_pro_2025"):
                    with self.subTest(expose_dev=expose_dev, key=key, model=model):
                        self._use(expose_dev=expose_dev, model=model)
                        self.assertIs(
                            fb.hub_entity_config("hub_x", platform, key, entity),
                            expected,
                        )

    def test_air_stable_key_is_the_same_object(self):
        entity = {"name": "Air Battery Voltage"}
        for expose_dev in (False, True):
            with self.subTest(expose_dev=expose_dev):
                self._use(expose_dev=expose_dev, model="air")
                self.assertIs(
                    fb.hub_entity_config("hub_x", "sensor", "battery_voltage", entity),
                    entity,
                )

    def test_air_dev_key_is_left_out_without_expose_dev(self):
        self._use(model="air")
        for key in ("motor", "light", "speaker"):
            with self.subTest(key=key):
                self.assertIsNone(
                    fb.hub_entity_config("hub_x", "switch", key, {"name": key})
                )

    def test_air_dev_key_is_a_hidden_copy_with_expose_dev(self):
        self._use(expose_dev=True, model="air")
        entity = {"name": "Air Power", "disabled_by_default": False}
        out = fb.hub_entity_config("hub_x", "switch", "motor", entity)
        self.assertIsNot(out, entity)
        self.assertEqual(out, {"name": "Air Power", "disabled_by_default": True})
        self.assertEqual(entity, {"name": "Air Power", "disabled_by_default": False})

    def test_air_unavailable_key_is_left_out_even_with_expose_dev(self):
        self._use(expose_dev=True, model="air")
        for platform, key in (
            ("switch", "throttle"),
            ("select", "gear"),
            ("select", "mode"),
            ("select", "speed_unit"),
            ("number", "brightness"),
            ("button", "pair_watch"),
        ):
            with self.subTest(key=key):
                self.assertIsNone(
                    fb.hub_entity_config("hub_x", platform, key, {"name": key})
                )

    def test_an_unknown_platform_is_rejected_for_every_model(self):
        for model in (None, "c11_pro", "air"):
            with self.subTest(model=model):
                self._use(expose_dev=True, model=model)
                with self.assertRaises(KeyError):
                    fb.hub_entity_config("hub_x", "swtich", "motor", {"name": "Power"})


class KeysTheModelNeverBuilds(unittest.TestCase):
    ROWS = (("gear", "Gear"), ("speed_limit", "Speed Limit"))

    def setUp(self):
        self.addCleanup(setattr, CORE, "raw_config", CORE.raw_config)
        self.air = {"id": "hub_air", "model": "Air"}

    def _warnings(self, hubs, block, platform="select"):
        CORE.raw_config = {"fiido_bms": hubs}
        with self.assertLogs(fb._LOGGER, level="WARNING") as logs:
            fb._LOGGER.warning("start")
            fb.inject_entity_defaults(block, self.ROWS, platform=platform)
        return logs.output[1:]

    def test_a_key_written_under_the_air_hub_is_named(self):
        block = {
            "fiido_bms_id": "hub_air",
            "gear": {"name": "Gear"},
            "speed_limit": None,
        }
        warnings = self._warnings([self.air, {"id": "hub_c11"}], block)
        self.assertEqual(len(warnings), 1)
        self.assertIn("select 'gear'", warnings[0])

    def test_keys_only_the_defaults_add_are_not_named(self):
        self.assertEqual(self._warnings([self.air], {"fiido_bms_id": "hub_air"}), [])

    def test_a_key_set_to_false_is_not_named(self):
        self.assertEqual(self._warnings([self.air], {"gear": False}), [])

    def test_a_block_of_another_hub_is_not_named(self):
        hubs = [self.air, {"id": "hub_c11"}]
        self.assertEqual(
            self._warnings(hubs, {"fiido_bms_id": "hub_c11", "gear": {}}), []
        )
        self.assertEqual(self._warnings(hubs, {"gear": {}}), [])

    def test_a_lone_hub_owns_a_block_without_fiido_bms_id(self):
        warnings = self._warnings([self.air], {"gear": {}})
        self.assertEqual(len(warnings), 1)
        self.assertIn("model: air", warnings[0])

    def test_a_hub_without_a_model_stays_quiet(self):
        self.assertEqual(self._warnings([{"id": "hub_c11"}], {"gear": {}}), [])


class AutoShutdownSwitch(unittest.TestCase):
    def setUp(self):
        self.addCleanup(setattr, CORE, "config", CORE.config)
        CORE.config = {"fiido_bms": [{"id": "hub_x"}]}

    def _generate(self, config):
        hub = mock.MagicMock()
        added = []
        with (
            mock.patch.object(
                fb_switch.cg, "get_variable", mock.AsyncMock(return_value=hub)
            ),
            mock.patch.object(fb_switch.cg, "add", added.append),
            mock.patch.object(fb_switch.cg, "register_parented", mock.AsyncMock()),
            mock.patch.object(fb_switch.cg, "register_component", mock.AsyncMock()),
            mock.patch.object(fb_switch.switch, "new_switch", mock.AsyncMock()),
        ):
            asyncio.run(fb_switch.to_code({"fiido_bms_id": "hub_x", **config}))
        return hub, added

    def test_false_turns_the_mechanism_off(self):
        hub, added = self._generate({})
        hub.set_auto_shutdown_enabled.assert_called_once_with(False)
        self.assertIn(hub.set_auto_shutdown_enabled.return_value, added)

    def test_a_built_switch_leaves_the_mechanism_to_the_switch(self):
        hub, _added = self._generate({"auto_shutdown": {"name": "Auto Shutdown"}})
        hub.set_auto_shutdown_enabled.assert_not_called()
        hub.set_autoshutdown_switch.assert_called_once()


class RunStateReset(unittest.TestCase):
    # The dashboard keeps the module loaded between compiles, so hub_index would
    # climb from run to run, and hub_index is what spreads the poll bursts apart.

    def setUp(self):
        self._config = CORE.config
        self.addCleanup(setattr, CORE, "config", self._config)
        self.addCleanup(fb.HUB_CONFIGS.clear)
        self.addCleanup(fb._ALL_HUBS.clear)

    def test_new_config_object_clears_state(self):
        CORE.config = {"run": 1}
        fb._reset_run_state()
        fb.HUB_CONFIGS["hub_c11"] = {}
        fb._ALL_HUBS.append("hub_c11")
        CORE.config = {"run": 2}
        fb._reset_run_state()
        self.assertEqual(fb.HUB_CONFIGS, {})
        self.assertEqual(fb._ALL_HUBS, [])

    def test_same_config_object_keeps_state(self):
        CORE.config = {"run": 1}
        fb._reset_run_state()
        fb.HUB_CONFIGS["hub_c11"] = {}
        fb._ALL_HUBS.append("hub_c11")
        fb._reset_run_state()
        self.assertEqual(len(fb._ALL_HUBS), 1)


class OneHubPerBleClient(unittest.TestCase):
    # Two hubs on one ble_client would poll the same bike twice, silently.

    def _run(self, hubs):
        token = fv.full_config.set({"fiido_bms": hubs})
        try:
            return fb._one_hub_per_ble_client(hubs[0])
        finally:
            fv.full_config.reset(token)

    def test_distinct_clients_pass(self):
        key = ble_client.CONF_BLE_CLIENT_ID
        self._run([{"id": "hub_c11", key: "ble_c11"}, {"id": "hub_m1", key: "ble_m1"}])

    def test_shared_client_is_rejected(self):
        key = ble_client.CONF_BLE_CLIENT_ID
        with self.assertRaises(cv.Invalid):
            self._run(
                [{"id": "hub_c11", key: "ble_c11"}, {"id": "hub_m1", key: "ble_c11"}]
            )
