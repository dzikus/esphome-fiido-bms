"""Codegen helpers from components/fiido_bms/__init__.py, run on the host.

Needs an interpreter with esphome importable:

    python -m unittest discover -s tests/python
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "components"))

import esphome.config_validation as cv
import esphome.final_validate as fv
import fiido_bms as fb
from esphome.components import ble_client
from esphome.core import CORE

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
