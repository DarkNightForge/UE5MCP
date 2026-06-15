import copy
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "scripts"))

from validate_plan import (  # noqa: E402
    MAX_SPAWN_INSTANCES,
    MAX_TARGETS,
    validate_plan,
)

EXAMPLES = os.path.join(os.path.dirname(__file__), "..", "examples", "plans")


def load_example(name):
    with open(os.path.join(EXAMPLES, name), encoding="utf-8") as fh:
        return json.load(fh)


def valid_mutation_plan():
    # set_actor_folder plan: a low_risk mutation that carries every gate.
    return load_example("organize-selection.json")


def rules(problems):
    return {p.split(":", 1)[0] for p in problems}


class ExampleFileTests(unittest.TestCase):
    def test_read_only_context_is_valid(self):
        self.assertEqual(validate_plan(load_example("read-only-context.json")), [])

    def test_organize_selection_is_valid(self):
        self.assertEqual(validate_plan(load_example("organize-selection.json")), [])

    def test_transform_selection_is_valid(self):
        self.assertEqual(validate_plan(load_example("transform-selection.json")), [])

    def test_spawn_cubes_is_valid(self):
        self.assertEqual(validate_plan(load_example("spawn-cubes.json")), [])

    def test_read_logs_is_valid(self):
        self.assertEqual(validate_plan(load_example("read-logs.json")), [])

    def test_invalid_examples_fail_for_documented_rules(self):
        problems = validate_plan(load_example("invalid-empty-targets.json"))
        self.assertIn("R6", rules(problems))
        problems = validate_plan(load_example("invalid-destructive-no-confirmation.json"))
        self.assertIn("R7", rules(problems))
        problems = validate_plan(load_example("invalid-noop-transform.json"))
        self.assertIn("R9", rules(problems))


class RuleTests(unittest.TestCase):
    def mutated(self, **changes):
        plan = valid_mutation_plan()
        plan.update(changes)
        return plan

    def test_r1_bad_schema_version(self):
        self.assertIn("R1", rules(validate_plan(self.mutated(schema_version=99))))

    def test_r1_bool_schema_version_rejected(self):
        self.assertIn("R1", rules(validate_plan(self.mutated(schema_version=True))))

    def test_r2_empty_actions(self):
        self.assertIn("R2", rules(validate_plan(self.mutated(actions=[]))))

    def test_r2_duplicate_ids(self):
        plan = valid_mutation_plan()
        plan["actions"].append(copy.deepcopy(plan["actions"][0]))
        self.assertIn("R2", rules(validate_plan(plan)))

    def test_r2_missing_id(self):
        plan = valid_mutation_plan()
        del plan["actions"][0]["id"]
        self.assertIn("R2", rules(validate_plan(plan)))

    def test_r3_unknown_tool(self):
        plan = valid_mutation_plan()
        plan["actions"][0]["tool"] = "scene.run_script"
        self.assertIn("R3", rules(validate_plan(plan)))

    def test_r3_placeholder_tools_are_unknown(self):
        # The sanitized placeholder names must NOT exist in the real registry.
        for placeholder in (
            "observe.scene_context",
            "scene.find_objects",
            "scene.select_objects",
            "scene.set_object_folder",
            "scene.set_object_transform",
            "scene.delete_objects",
        ):
            plan = valid_mutation_plan()
            plan["actions"][0]["tool"] = placeholder
            self.assertIn("R3", rules(validate_plan(plan)), placeholder)

    def test_r4_risk_mismatch(self):
        plan = valid_mutation_plan()
        plan["actions"][0]["risk"] = "read_only"
        self.assertIn("R4", rules(validate_plan(plan)))

    def test_r5_mutation_without_approval(self):
        self.assertIn("R5", rules(validate_plan(self.mutated(requires_approval=False))))

    def test_r6_empty_targets_on_requires_targets_tool(self):
        plan = valid_mutation_plan()
        plan["actions"][0]["targets"] = []
        self.assertIn("R6", rules(validate_plan(plan)))

    def test_r6_targets_rejected_on_no_target_tool(self):
        # get_selection_context does not accept targets.
        plan = load_example("read-only-context.json")
        plan["actions"][0]["targets"] = ["/Temp/Untitled_1.Untitled_1:PersistentLevel.StaticMeshActor_0"]
        self.assertIn("R6", rules(validate_plan(plan)))

    def test_r6_spawn_rejects_targets(self):
        plan = load_example("spawn-cubes.json")
        plan["actions"][0]["targets"] = ["/Temp/Untitled_1.Untitled_1:PersistentLevel.StaticMeshActor_0"]
        self.assertIn("R6", rules(validate_plan(plan)))

    def test_r7_destructive_needs_second_confirmation(self):
        plan = valid_mutation_plan()
        plan["actions"][0]["tool"] = "delete_actor"
        plan["actions"][0]["risk"] = "destructive"
        plan["actions"][0]["params"] = {}
        self.assertIn("R7", rules(validate_plan(plan)))
        plan["requires_second_confirmation"] = True
        self.assertNotIn("R7", rules(validate_plan(plan)))

    def test_r8_mutation_needs_fingerprint(self):
        plan = valid_mutation_plan()
        del plan["context_fingerprint"]
        self.assertIn("R8", rules(validate_plan(plan)))

    def test_r8_fingerprint_needs_scene(self):
        plan = valid_mutation_plan()
        plan["context_fingerprint"]["scene"] = ""
        self.assertIn("R8", rules(validate_plan(plan)))

    def test_r9_unknown_param(self):
        plan = valid_mutation_plan()
        plan["actions"][0]["params"]["mode"] = "fast"
        self.assertIn("R9", rules(validate_plan(plan)))

    def test_r9_missing_required_folder_path(self):
        plan = valid_mutation_plan()
        plan["actions"][0]["params"] = {}
        self.assertIn("R9", rules(validate_plan(plan)))

    def test_r9_empty_required_string(self):
        plan = valid_mutation_plan()
        plan["actions"][0]["params"]["folder_path"] = ""
        self.assertIn("R9", rules(validate_plan(plan)))

    def test_r9_whitespace_required_string(self):
        plan = valid_mutation_plan()
        plan["actions"][0]["params"]["folder_path"] = "   "
        self.assertIn("R9", rules(validate_plan(plan)))

    def test_r9_bool_rejected_for_int_param(self):
        plan = load_example("read-only-context.json")
        plan["actions"][0]["params"]["max_objects"] = True
        self.assertIn("R9", rules(validate_plan(plan)))

    def test_r10_target_cap(self):
        plan = valid_mutation_plan()
        plan["actions"][0]["targets"] = [
            f"/Temp/Untitled_1.Untitled_1:PersistentLevel.StaticMeshActor_{i}"
            for i in range(MAX_TARGETS + 1)
        ]
        self.assertIn("R10", rules(validate_plan(plan)))

    def test_read_only_plan_needs_no_fingerprint_or_approval(self):
        plan = load_example("read-only-context.json")
        self.assertEqual(validate_plan(plan), [])


class ReadLogsTests(unittest.TestCase):
    def read_logs_plan(self, params):
        return {
            "schema_version": 1,
            "summary": "Read recent UE5MCP log lines.",
            "requires_approval": False,
            "actions": [
                {
                    "id": "read-logs-01",
                    "tool": "read_logs",
                    "risk": "read_only",
                    "targets": [],
                    "params": params,
                }
            ],
        }

    def test_no_params_is_valid(self):
        # read_logs is read-only: it needs neither approval nor a fingerprint.
        self.assertEqual(validate_plan(self.read_logs_plan({})), [])

    def test_filters_are_valid(self):
        self.assertEqual(
            validate_plan(self.read_logs_plan({"max_lines": 50, "contains": "refused"})), []
        )

    def test_unknown_param_rejected(self):
        self.assertIn("R9", rules(validate_plan(self.read_logs_plan({"severity": "error"}))))

    def test_max_lines_must_be_int(self):
        self.assertIn("R9", rules(validate_plan(self.read_logs_plan({"max_lines": 1.5}))))

    def test_max_lines_bool_rejected(self):
        self.assertIn("R9", rules(validate_plan(self.read_logs_plan({"max_lines": True}))))

    def test_contains_must_be_non_empty_string(self):
        self.assertIn("R9", rules(validate_plan(self.read_logs_plan({"contains": ""}))))

    def test_targets_rejected(self):
        plan = self.read_logs_plan({})
        plan["actions"][0]["targets"] = [
            "/Temp/Untitled_1.Untitled_1:PersistentLevel.StaticMeshActor_0"
        ]
        self.assertIn("R6", rules(validate_plan(plan)))


class TransformTests(unittest.TestCase):
    def transform_plan(self, params):
        plan = load_example("transform-selection.json")
        plan["actions"][0]["params"] = params
        return plan

    def test_single_component_is_valid(self):
        self.assertEqual(validate_plan(self.transform_plan({"location": [1, 2, 3]})), [])
        self.assertEqual(validate_plan(self.transform_plan({"rotation": [0, 90, 0]})), [])
        self.assertEqual(validate_plan(self.transform_plan({"scale": [2, 2, 2]})), [])

    def test_noop_transform_rejected(self):
        self.assertIn("R9", rules(validate_plan(self.transform_plan({}))))

    def test_vector_must_be_three_numbers(self):
        self.assertIn("R9", rules(validate_plan(self.transform_plan({"location": [1, 2]}))))
        self.assertIn("R9", rules(validate_plan(self.transform_plan({"location": [1, 2, 3, 4]}))))
        self.assertIn("R9", rules(validate_plan(self.transform_plan({"scale": [1, "x", 3]}))))

    def test_bool_component_rejected(self):
        self.assertIn("R9", rules(validate_plan(self.transform_plan({"rotation": [0, True, 0]}))))

    def test_unknown_transform_param_rejected(self):
        plan = self.transform_plan({"location": [1, 2, 3], "pivot": [0, 0, 0]})
        self.assertIn("R9", rules(validate_plan(plan)))


class DuplicateTests(unittest.TestCase):
    def duplicate_plan(self, params):
        plan = load_example("transform-selection.json")
        plan["actions"][0]["id"] = "duplicate-actor-01"
        plan["actions"][0]["tool"] = "duplicate_actor_with_offset"
        plan["actions"][0]["params"] = params
        return plan

    def test_offset_required(self):
        self.assertIn("R9", rules(validate_plan(self.duplicate_plan({}))))

    def test_offset_valid(self):
        self.assertEqual(validate_plan(self.duplicate_plan({"offset": [100, 0, 0]})), [])

    def test_offset_must_be_three_numbers(self):
        self.assertIn("R9", rules(validate_plan(self.duplicate_plan({"offset": [100, 0]}))))


class SpawnTests(unittest.TestCase):
    def spawn_plan(self, params):
        plan = load_example("spawn-cubes.json")
        plan["actions"][0]["params"] = params
        return plan

    def base_params(self, **overrides):
        params = {
            "class_path": "/Script/Engine.StaticMeshActor",
            "static_mesh": "/Engine/BasicShapes/Cube.Cube",
            "transforms": [{"location": [0, 0, 0]}],
        }
        params.update(overrides)
        return params

    def test_baseline_is_valid(self):
        self.assertEqual(validate_plan(self.spawn_plan(self.base_params())), [])

    def test_r9_missing_class_path(self):
        params = self.base_params()
        del params["class_path"]
        self.assertIn("R9", rules(validate_plan(self.spawn_plan(params))))

    def test_r9_empty_transforms(self):
        self.assertIn("R9", rules(validate_plan(self.spawn_plan(self.base_params(transforms=[])))))

    def test_r9_instance_needs_location(self):
        params = self.base_params(transforms=[{"rotation": [0, 0, 0]}])
        self.assertIn("R9", rules(validate_plan(self.spawn_plan(params))))

    def test_r10_too_many_instances(self):
        params = self.base_params(
            transforms=[{"location": [i, 0, 0]} for i in range(MAX_SPAWN_INSTANCES + 1)]
        )
        self.assertIn("R10", rules(validate_plan(self.spawn_plan(params))))

    def test_r11_class_not_allowlisted(self):
        params = self.base_params(class_path="/Script/Engine.SkyAtmosphere")
        del params["static_mesh"]
        self.assertIn("R11", rules(validate_plan(self.spawn_plan(params))))

    def test_r11_mesh_not_allowlisted(self):
        params = self.base_params(static_mesh="/Game/Secret/Bomb.Bomb")
        self.assertIn("R11", rules(validate_plan(self.spawn_plan(params))))

    def test_r11_mesh_only_valid_with_static_mesh_actor(self):
        # PointLight is allowlisted as a class but cannot carry a static_mesh.
        params = self.base_params(class_path="/Script/Engine.PointLight")
        self.assertIn("R11", rules(validate_plan(self.spawn_plan(params))))


if __name__ == "__main__":
    unittest.main()
