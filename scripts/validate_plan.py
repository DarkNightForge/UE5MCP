#!/usr/bin/env python3
"""Executable spec: validate UE5MCP plan files against docs/specs/action-plan-format.md.

Usage:
    python3 scripts/validate_plan.py PLAN.json [PLAN.json ...]
    python3 scripts/validate_plan.py --expect-invalid PLAN.json [...]

Without --expect-invalid, exits 0 only if every plan is valid.
With --expect-invalid, exits 0 only if every plan is rejected (used to keep the
deliberately-invalid examples failing for the documented reasons). The rule each
rejection violated is printed as its R# prefix.

This mirrors the SCHEMA-LEVEL subset of the C++ validator,
`Source/UE5MCP/Private/UE5MCPPlanValidator.cpp`
(constants from `Source/UE5MCP/Public/UE5MCPPlanValidator.h`). The C++ rules are
R1..R11; this script enforces R1..R11 to the extent they are decidable from the
plan JSON alone:

  * R6 split: empty/forbidden `targets` lists are enforced here. The *live*
    target-resolution check (every target path must resolve to an actor in the
    running editor world) is RUNTIME-ONLY and cannot be checked offline.
  * R11 (spawn allowlists) is enforced here using the DOCUMENTED default
    SpawnClassAllowlist / SpawnMeshAllowlist literals from
    `Source/UE5MCP/Public/UE5MCPSettings.h` (set in UE5MCPSettings.cpp). A
    project may widen these allowlists in Project Settings, so a value rejected
    here might be accepted by a customized live editor (and vice versa) — the
    authoritative check is the running plugin. The static-mesh/class pairing
    constraint (static_mesh only with /Script/Engine.StaticMeshActor) is a pure
    schema rule and is always enforced.

Stdlib only. No external dependencies. The rule numbers (R1..R11) match the spec
table in docs/specs/action-plan-format.md.
"""

import json
import sys

# --- Constants mirrored from Source/UE5MCP/Public/UE5MCPPlanValidator.h --------
SCHEMA_VERSION = 1  # FUE5MCPPlanValidator::SchemaVersion
MAX_TARGETS = 200  # FUE5MCPPlanValidator::MaxTargetsPerAction
MAX_SPAWN_INSTANCES = 25  # FUE5MCPPlanValidator::MaxSpawnInstancesPerAction

# --- Risk strings from UE5MCPToolRegistry.cpp (RiskToString) -------------------
READ_ONLY = "read_only"
LOW_RISK = "low_risk"
DESTRUCTIVE = "destructive"

# --- Spawn allowlists: documented defaults from UE5MCPSettings.cpp -------------
# These are the literal default values; a live project may widen them, so R11
# here is a best-effort schema-level mirror, not the authoritative gate.
SPAWN_CLASS_ALLOWLIST = {
    "/Script/Engine.StaticMeshActor",
    "/Script/Engine.PointLight",
    "/Script/Engine.CameraActor",
}
SPAWN_MESH_ALLOWLIST = {
    "/Engine/BasicShapes/Cube.Cube",
    "/Engine/BasicShapes/Sphere.Sphere",
    "/Engine/BasicShapes/Cylinder.Cylinder",
    "/Engine/BasicShapes/Cone.Cone",
    "/Engine/BasicShapes/Plane.Plane",
}
STATIC_MESH_ACTOR_CLASS = "/Script/Engine.StaticMeshActor"

# --- Tool registry, mirrored from UE5MCPToolRegistry.cpp -----------------------
# tool -> dict(risk, params={key: type}, requires_targets, accepts_targets)
# `type` is the JSON value type the C++ parser accepts for that param:
#   "vec3"  -> array of exactly 3 numbers (bool not a number)
#   str / int / bool -> the corresponding JSON scalar
#   "transforms" -> spawn instance array (special-cased)
# A param being listed does NOT make it required; required params are enforced
# per-tool below (matching the C++ per-action-type checks).
REGISTRY = {
    "get_selection_context": {
        "risk": READ_ONLY,
        "params": {"max_objects": int},
        "requires_targets": False,
        "accepts_targets": False,
    },
    "find_actors": {
        "risk": READ_ONLY,
        "params": {
            "class_path": str,
            "label_contains": str,
            "tag": str,
            "folder_path": str,
            "selected_only": bool,
            "max_results": int,
        },
        "requires_targets": False,
        "accepts_targets": False,
    },
    "select_actors": {
        "risk": LOW_RISK,
        "params": {},
        "requires_targets": True,
        "accepts_targets": True,
    },
    "set_actor_folder": {
        "risk": LOW_RISK,
        "params": {"folder_path": str},
        "requires_targets": True,
        "accepts_targets": True,
    },
    "set_actor_transform": {
        "risk": LOW_RISK,
        "params": {"location": "vec3", "rotation": "vec3", "scale": "vec3"},
        "requires_targets": True,
        "accepts_targets": True,
    },
    "duplicate_actor_with_offset": {
        "risk": LOW_RISK,
        "params": {"offset": "vec3"},
        "requires_targets": True,
        "accepts_targets": True,
    },
    "spawn_actor_from_class": {
        "risk": LOW_RISK,
        "params": {
            "class_path": str,
            "transforms": "transforms",
            "static_mesh": str,
            "label_base": str,
        },
        "requires_targets": False,
        "accepts_targets": False,
    },
    "delete_actor": {
        "risk": DESTRUCTIVE,
        "params": {},
        "requires_targets": True,
        "accepts_targets": True,
    },
}


def _is_number(value):
    """JSON number, with bool explicitly excluded (matches EJson::Number)."""
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _is_vec3(value):
    """Array of exactly three numbers — UE5MCPJson.cpp TryParseVector3."""
    return (
        isinstance(value, list)
        and len(value) == 3
        and all(_is_number(component) for component in value)
    )


def _validate_spawn(where, action, params, problems):
    """R9/R10/R11 checks specific to spawn_actor_from_class."""
    class_path = params.get("class_path")
    # R9: class_path required and a non-empty string.
    if not isinstance(class_path, str) or not class_path.strip():
        problems.append(
            f"R9: {where} missing required non-empty param 'class_path'"
        )
        class_path = None

    transforms = params.get("transforms")
    if "transforms" not in params:
        problems.append(
            f"R9: {where} 'transforms' must contain at least one instance with a 'location'"
        )
    elif not isinstance(transforms, list):
        problems.append(
            f"R9: {where} param 'transforms' must be an array of objects"
        )
    else:
        valid_instances = 0
        for i, instance in enumerate(transforms):
            if not isinstance(instance, dict):
                problems.append(
                    f"R9: {where} transforms[{i}] is not an object"
                )
                continue
            # location mandatory per instance; rotation/scale optional vec3.
            if not _is_vec3(instance.get("location")):
                problems.append(
                    f"R9: {where} transforms[{i}] needs 'location' as an array of 3 numbers"
                )
                continue
            ok = True
            for opt in ("rotation", "scale"):
                if opt in instance and not _is_vec3(instance[opt]):
                    problems.append(
                        f"R9: {where} transforms[{i}] '{opt}' must be an array of 3 numbers"
                    )
                    ok = False
            if ok:
                valid_instances += 1
        if valid_instances == 0 and transforms == []:
            problems.append(
                f"R9: {where} 'transforms' must contain at least one instance with a 'location'"
            )
        # R10: instance cap.
        if len(transforms) > MAX_SPAWN_INSTANCES:
            problems.append(
                f"R10: {where} spawns {len(transforms)} instances (max {MAX_SPAWN_INSTANCES})"
            )

    # R11 spawn policy (schema-level mirror of the documented default allowlists).
    static_mesh = params.get("static_mesh")
    if isinstance(class_path, str) and class_path:
        if class_path not in SPAWN_CLASS_ALLOWLIST:
            problems.append(
                f"R11: {where} class {class_path!r} is not on the spawn class allowlist"
            )
    if isinstance(static_mesh, str) and static_mesh.strip():
        mesh = static_mesh.strip()
        if mesh not in SPAWN_MESH_ALLOWLIST:
            problems.append(
                f"R11: {where} static_mesh {mesh!r} is not on the spawn mesh allowlist"
            )
        if class_path != STATIC_MESH_ACTOR_CLASS:
            problems.append(
                f"R11: {where} 'static_mesh' is only valid with class_path '{STATIC_MESH_ACTOR_CLASS}'"
            )


def validate_plan(plan):
    """Return a list of problems; an empty list means the plan is valid."""
    problems = []
    if not isinstance(plan, dict):
        return ["R1: plan must be a JSON object"]

    # R1: schema_version must equal the SchemaVersion constant.
    version = plan.get("schema_version")
    if type(version) is not int or version != SCHEMA_VERSION:
        problems.append(
            f"R1: schema_version must be {SCHEMA_VERSION}, got {plan.get('schema_version')!r}"
        )

    # R2: non-empty actions list.
    actions = plan.get("actions")
    if not isinstance(actions, list) or not actions:
        problems.append("R2: plan must contain a non-empty actions list")
        return problems

    seen_ids = set()
    has_mutation = False
    has_destructive = False

    for index, action in enumerate(actions):
        where = f"actions[{index}]"
        if not isinstance(action, dict):
            problems.append(f"R2: {where} must be an object")
            continue

        # R2: unique, non-empty id.
        action_id = action.get("id")
        if not isinstance(action_id, str) or not action_id:
            problems.append(f"R2: {where} needs a non-empty id")
        elif action_id in seen_ids:
            problems.append(f"R2: duplicate action id {action_id!r}")
        else:
            seen_ids.add(action_id)

        # R3: known tool.
        tool = action.get("tool")
        descriptor = REGISTRY.get(tool)
        if descriptor is None:
            problems.append(f"R3: {where} uses unknown tool {tool!r}")
            continue
        risk = descriptor["risk"]

        # R4: declared risk == registry risk.
        if action.get("risk") != risk:
            problems.append(
                f"R4: {where} declares risk {action.get('risk')!r} but {tool} is {risk!r}"
            )

        if risk != READ_ONLY:
            has_mutation = True
        if risk == DESTRUCTIVE:
            has_destructive = True

        # --- targets (R6 schema subset, R10 cap) -----------------------------
        targets = action.get("targets", [])
        if not isinstance(targets, list) or any(
            not isinstance(t, str) or not t for t in targets
        ):
            problems.append(
                f"R6: {where} targets must be a list of non-empty strings"
            )
            targets = []
        else:
            if descriptor["requires_targets"] and not targets:
                problems.append(
                    f"R6: {where} is a mutation with an empty targets list"
                )
            if not descriptor["accepts_targets"] and targets:
                problems.append(
                    f"R6: {where} tool {tool} does not accept targets"
                )
            if len(targets) > MAX_TARGETS:
                problems.append(
                    f"R10: {where} has {len(targets)} targets (max {MAX_TARGETS})"
                )

        # --- params (R9 unknown/required/typed, R10/R11 spawn) ---------------
        params = action.get("params", {})
        if not isinstance(params, dict):
            problems.append(f"R9: {where} params must be an object")
            params = {}

        param_spec = descriptor["params"]
        for key in params:
            if key not in param_spec:
                problems.append(f"R9: {where} has unknown param {key!r} for {tool}")
                continue
            expected = param_spec[key]
            if expected == "vec3":
                if not _is_vec3(params[key]):
                    problems.append(
                        f"R9: {where} param {key!r} must be an array of 3 numbers"
                    )
            elif expected == "transforms":
                pass  # handled in _validate_spawn
            elif expected is str:
                value = params[key]
                if not isinstance(value, str) or not value.strip():
                    problems.append(
                        f"R9: {where} param {key!r} must be a non-empty string"
                    )
            elif expected is int:
                value = params[key]
                if not isinstance(value, int) or isinstance(value, bool):
                    problems.append(
                        f"R9: {where} param {key!r} must be an integer"
                    )
            elif expected is bool:
                if not isinstance(params[key], bool):
                    problems.append(
                        f"R9: {where} param {key!r} must be a boolean"
                    )

        # Per-tool required-param + no-op checks (mirror the C++ per-type rules).
        if tool == "set_actor_folder":
            folder = params.get("folder_path")
            if not isinstance(folder, str) or not folder.strip():
                problems.append(
                    f"R9: {where} missing required non-empty param 'folder_path'"
                )
        elif tool == "set_actor_transform":
            if not any(k in params for k in ("location", "rotation", "scale")):
                problems.append(
                    f"R9: {where} set_actor_transform needs at least one of "
                    "'location', 'rotation', or 'scale'"
                )
        elif tool == "duplicate_actor_with_offset":
            if "offset" not in params:
                problems.append(
                    f"R9: {where} missing required param 'offset' (array of 3 numbers)"
                )
        elif tool == "spawn_actor_from_class":
            _validate_spawn(where, action, params, problems)

    # R5: any mutation => requires_approval true.
    if has_mutation and plan.get("requires_approval") is not True:
        problems.append(
            "R5: plan contains mutations but requires_approval is not true"
        )

    # R7: any destructive => requires_second_confirmation true.
    if has_destructive and plan.get("requires_second_confirmation") is not True:
        problems.append(
            "R7: plan contains destructive actions but requires_second_confirmation is not true"
        )

    # R8: mutation plans need a context_fingerprint with scene + selected_object_paths.
    if has_mutation:
        fingerprint = plan.get("context_fingerprint")
        if (
            not isinstance(fingerprint, dict)
            or not isinstance(fingerprint.get("scene"), str)
            or not fingerprint.get("scene")
            or not isinstance(fingerprint.get("selected_object_paths"), list)
        ):
            problems.append(
                "R8: mutation plans need a context_fingerprint with scene and selected_object_paths"
            )

    return problems


def main(argv):
    args = argv[1:]
    expect_invalid = False
    if args and args[0] == "--expect-invalid":
        expect_invalid = True
        args = args[1:]
    if not args:
        print(__doc__)
        return 2

    failures = 0
    for path in args:
        try:
            with open(path, encoding="utf-8") as fh:
                plan = json.load(fh)
            problems = validate_plan(plan)
        except (OSError, json.JSONDecodeError) as exc:
            problems = [f"unreadable plan: {exc}"]

        valid = not problems
        if valid == expect_invalid:
            failures += 1
        verdict = "valid" if valid else "invalid"
        marker = "ok" if valid != expect_invalid else "UNEXPECTED"
        print(f"{marker}: {path} is {verdict}")
        for problem in problems:
            print(f"    {problem}")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
