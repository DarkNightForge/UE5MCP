# Spec: UE5MCP typed action and plan format (v1)

This is the data contract between any planner (the in-editor Copilot panel, or an
external MCP client such as Claude Code driving the editor through
`MCPServer/ue5mcp-server.mjs`) and the UE5MCP Unreal Engine 5 editor plugin's
tool host. Agent output is **data** conforming to this spec — never code. The
plugin is the enforcement boundary: it re-validates schema, policy, and
allowlists, blocks during Play-In-Editor, wraps every mutation in a single
`FScopedTransaction` (one editor Undo step), logs every action to `LogUE5MCP`,
and retains final refusal authority even after a user-approved call.

The rules in this document are executable. The C++ validator
`Source/UE5MCP/Private/UE5MCPPlanValidator.cpp` enforces R1–R11 at runtime, and
`scripts/validate_plan.py` enforces the schema-decidable subset offline against
the passing and deliberately-failing inputs in `examples/plans/`.

`schema_version` is `1` (`FUE5MCPPlanValidator::SchemaVersion`). Breaking changes
increment it; the host rejects versions it does not know (R1). Constants:
`MaxTargetsPerAction = 200`, `MaxSpawnInstancesPerAction = 25`
(`Source/UE5MCP/Public/UE5MCPPlanValidator.h`).

## Risk vocabulary — enforced, not advisory

The risk strings are exactly `read_only`, `low_risk`, and `destructive`
(`FUE5MCPToolRegistry::RiskToString` / `ParseRisk` in
`Source/UE5MCP/Private/UE5MCPToolRegistry.cpp`).

| Risk | Meaning | Host obligations |
| --- | --- | --- |
| `read_only` | Observes editor/world state; mutates nothing | May execute immediately; never creates an Undo step |
| `low_risk` | Bounded, visible, undoable mutation | Requires `requires_approval` and a `context_fingerprint`; one `FScopedTransaction` |
| `destructive` | Deletes actors from the level | Additionally requires `requires_second_confirmation`; executes only through the external-session gate, never on the single-click pending path |

A client cannot downgrade or upgrade risk: each tool's risk is fixed in the host
registry, and a plan declaring a different risk for a tool is invalid (R4).

> NOTE: v1 does **not** categorically reject every destructive plan.
> `delete_actor` is a real, implemented tool. It is double-gated (R7 plus the
> service's external-session gate — see `preview-approval-flow.md`), reverts via
> one editor Undo, and was verified live. An earlier draft of this spec wrongly
> claimed destructive plans were rejected outright; that is no longer accurate.

## Tool registry (v1)

These are the 14 tools in `FUE5MCPToolRegistry::GetTools()`
(`Source/UE5MCP/Private/UE5MCPToolRegistry.cpp`). The `tool` field of every plan
action MUST be one of these names. Param keys are exactly those parsed in
`Source/UE5MCP/Private/UE5MCPJson.cpp`.

| Tool | Risk | Targets | Params | Notes |
| --- | --- | --- | --- | --- |
| `get_selection_context` | `read_only` | rejected | `max_objects?: int` | Reads the current editor selection + world context snapshot (capped) |
| `find_actors` | `read_only` | rejected | `class_path?: string`, `label_contains?: string`, `tag?: string`, `folder_path?: string`, `selected_only?: bool`, `max_results?: int` | Searches loaded editor-world actors; deterministic, capped results; does not see unloaded World Partition actors |
| `read_logs` | `read_only` | rejected | `max_lines?: int`, `contains?: string` | Returns recent lines from the plugin's structured `LogUE5MCP` buffer (tool calls, refusals, errors); oldest→newest, capped (default 100, clamped to `[1, FUE5MCPLog::MaxBufferedLines]` = 512), optional case-insensitive substring filter. Mutates nothing — for self-correction after a refusal/error |
| `get_package_status` | `read_only` | rejected | `max_packages?: int`, `dirty_only?: bool` | Reports the packages a save/mutation would touch (the dirty set; default 100, clamped to `[1, 500]`) plus a source-control summary (provider, enabled/available) and per-package cached SC state. `dirty_only` false also includes other loaded on-disk packages (capped). Per-package SC state is read from the provider CACHE only — never starts a source-control network call |
| `select_actors` | `low_risk` | required | — | Sets the editor selection to exactly the targets (undoable selection-state mutation) |
| `set_actor_folder` | `low_risk` | required | `folder_path: string` (required, non-empty) | Moves targets into a named World Outliner folder |
| `set_actor_label` | `low_risk` | required | `label: string` (required, non-empty) | Sets the editor display label of each target; labels are display-only and need not be unique; an empty/whitespace label is a refused no-op |
| `add_actor_tags` | `low_risk` | required | `tags: [string,…]` (required, ≥1 non-empty) | Adds the tags to each target's `AActor.Tags`; idempotent (tags already present are left as-is); an empty list is a refused no-op |
| `remove_actor_tags` | `low_risk` | required | `tags: [string,…]` (required, ≥1 non-empty) | Removes the tags from each target's `AActor.Tags`; idempotent (tags not present are ignored); an empty list is a refused no-op |
| `set_actor_property` | `low_risk` | required | `property: string` (required; may be a dotted struct-member path e.g. `PostProcessSettings.BloomIntensity`), `value` (required; number/bool/`[x,y,z]`/`[r,g,b,a]`/string), `component?: string` (owning component class path) | Writes one **allowlisted** property (R12) on each target or one of its components via reflection. Allowlisted type is one of float/int/bool/vector/color/name/**enum** (value = string value-name, e.g. `"Lumens"`)/**asset** (value = string asset path, class-constrained). Optionally also sets a paired override bool (e.g. `bOverride_BloomIntensity`). Type-safe, previewed before→after, one undoable transaction. Refuses anything not on `PropertyAllowlist` (the refusal lists what is allowed) |
| `set_actor_transform` | `low_risk` | required | `location?: [x,y,z]`, `rotation?: [roll,pitch,yaw]`, `scale?: [x,y,z]` | Sets absolute transform components; omitted components stay unchanged; **≥1 component required** (an empty transform is a refused no-op) |
| `duplicate_actor_with_offset` | `low_risk` | required | `offset: [x,y,z]` (required) | Duplicates each target once at source location + offset; returns the new actor paths |
| `spawn_actor_from_class` | `low_risk` | rejected | `class_path: string` (required), `transforms: [instance,…]` (required, ≥1), `static_mesh?: string`, `label_base?: string` | Spawns ≤25 instances of an **allowlisted** class via `UWorld::SpawnActor` with `RF_Transactional`; returns the new actor paths |
| `delete_actor` | `destructive` | required | — | Permanently deletes the target actors from the level; reversible only via editor Undo |

Notes on params:

- All transform/offset/instance vectors are arrays of **exactly three numbers**
  (`UE5MCPJson.cpp::TryParseVector3` rejects wrong length and non-number
  elements — a bool is not a number). `rotation` is Euler degrees
  `[roll, pitch, yaw]`, the same convention the context pack reports via
  `FQuat::Euler()`.
- A `spawn_actor_from_class` `transforms` entry is an object
  `{ "location": [x,y,z], "rotation"?: [r,p,y], "scale"?: [x,y,z] }`. `location`
  is mandatory per instance; `rotation`/`scale` are optional.
- `find_actors` accepts the folder filter as `folder_path` (the parser also
  accepts the alias `folder`).
- Unknown tools are invalid (R3). Unknown params are invalid (R9). Unknown
  *top-level plan fields* are ignored by the host (the JSON parser ignores
  unknown top-level fields) — only the fields in this spec carry meaning, so what
  the preview shows is what executes.

## MCP server ↔ registry mapping

`MCPServer/ue5mcp-server.mjs` is a zero-dependency Node stdio MCP server: a
**client/integration layer with no editor capability**, only loopback HTTP to the
plugin bridge at `127.0.0.1:30110`. It exposes MCP tools (named
`mcp__ue5mcp__<name>` in Claude Code) and rewrites each call into a typed plan
POSTed to the plugin. Most MCP tool names match a registry tool one-to-one; two
do not:

| MCP tool (`mcp__ue5mcp__…`) | Risk | Maps to | Notes |
| --- | --- | --- | --- |
| `get_selection` | `read_only` | registry `get_selection_context` | **Name differs** — the only renamed tool |
| `find_actors` | `read_only` | registry `find_actors` | 1:1 |
| `read_logs` | `read_only` | registry `read_logs` | 1:1 |
| `get_package_status` | `read_only` | registry `get_package_status` | 1:1 |
| `preview_actions` | `read_only` | **no registry tool** | MCP-server-only; see below |
| `select_actors` | `low_risk` | registry `select_actors` | takes `actor_paths` → plan `targets` |
| `set_actor_folder` | `low_risk` | registry `set_actor_folder` | |
| `set_actor_label` | `low_risk` | registry `set_actor_label` | takes `label` |
| `add_actor_tags` | `low_risk` | registry `add_actor_tags` | takes `tags` (string array) |
| `remove_actor_tags` | `low_risk` | registry `remove_actor_tags` | takes `tags` (string array) |
| `set_actor_property` | `low_risk` | registry `set_actor_property` | takes `property`, `value`, optional `component` |
| `set_actor_transform` | `low_risk` | registry `set_actor_transform` | |
| `duplicate_actor_with_offset` | `low_risk` | registry `duplicate_actor_with_offset` | |
| `spawn_actor_from_class` | `low_risk` | registry `spawn_actor_from_class` | |
| `delete_actor` | `destructive` | registry `delete_actor` | kept on the client permission "ask" list so it always prompts |

`preview_actions` is **not** a plan `tool` and never appears in the `tool` field
of a plan action. It is an MCP-server convenience that wraps an arbitrary typed
action list and POSTs it to the bridge with `mode: preview` — the plugin
validates and resolves it, builds the typed preview text, and returns it WITHOUT
executing anything. See `preview-approval-flow.md` for the `mode` transport.

## Plan envelope

The exact JSON field names parsed by `UE5MCPJson::ParsePlanRequest`:

```json
{
  "schema_version": 1,
  "summary": "Move the selected actors into the World Outliner folder UE5MCP/Organized",
  "requires_approval": true,
  "requires_second_confirmation": false,
  "context_fingerprint": {
    "scene": "/Temp/Untitled_1.Untitled_1",
    "selected_object_paths": [
      "/Temp/Untitled_1.Untitled_1:PersistentLevel.StaticMeshActor_0",
      "/Temp/Untitled_1.Untitled_1:PersistentLevel.StaticMeshActor_1"
    ]
  },
  "actions": [
    {
      "id": "set-actor-folder-01",
      "tool": "set_actor_folder",
      "risk": "low_risk",
      "targets": [
        "/Temp/Untitled_1.Untitled_1:PersistentLevel.StaticMeshActor_0",
        "/Temp/Untitled_1.Untitled_1:PersistentLevel.StaticMeshActor_1"
      ],
      "params": { "folder_path": "UE5MCP/Organized" }
    }
  ]
}
```

- `targets` are full Unreal actor **object paths** (e.g.
  `"/Temp/Untitled_1.Untitled_1:PersistentLevel.StaticMeshActor_0"`), exactly as
  returned by `get_selection_context` / `find_actors` / spawn results, captured at
  plan time. The parser drops empty/non-string targets with an error.
- `context_fingerprint.scene` pins the world identity (the editor world name); the
  optional top-level `mode` field (a transport hint consumed by the bridge, not by
  the validator) selects `submit` / `preview` / `execute_external` — see
  `preview-approval-flow.md`.

## Validation rules (R1–R11)

Every problem string the validator emits is prefixed with its rule number (the
`R#:` prefix), and `scripts/validate_plan.py` prints which rule each rejection
violated.

| Rule | Requirement |
| --- | --- |
| R1 | `schema_version` must equal the `SchemaVersion` constant (currently `1`) |
| R2 | At least one action; every action has a unique, non-empty `id` |
| R3 | Every `tool` exists in the registry |
| R4 | Every action's `risk` equals the registry risk for its tool |
| R5 | If any action mutates (risk ≠ `read_only`), `requires_approval` must be `true` |
| R6 | A requires-targets tool (`select_actors`, `set_actor_folder`, `set_actor_label`, `add_actor_tags`, `remove_actor_tags`, `set_actor_transform`, `duplicate_actor_with_offset`, `delete_actor`) must have a non-empty `targets` list; a requires-targets tool also includes `set_actor_property`; a no-target tool (`get_selection_context`, `find_actors`, `read_logs`, `get_package_status`, `spawn_actor_from_class`) must NOT be given targets; **(live)** every target path must resolve to an actor in the running editor world |
| R7 | If any action is `destructive`, `requires_second_confirmation` must be `true` |
| R8 | Mutation plans must carry a `context_fingerprint` with a non-empty `scene` and a `selected_object_paths` list |
| R9 | Params must be allowlisted for the tool, with required params present and correctly typed: `folder_path` a non-empty string; `set_actor_label` needs a non-empty `label`; `add_actor_tags`/`remove_actor_tags` need a non-empty `tags` array of non-empty strings; `set_actor_property` needs a non-empty `property` and a well-formed `value`; transform/offset/instance vectors arrays of 3 numbers; `set_actor_transform` needs ≥1 of `location`/`rotation`/`scale`; `duplicate_actor_with_offset` needs `offset`; `spawn_actor_from_class` needs `class_path` plus a non-empty `transforms` list (each instance with a `location`) |
| R10 | No action may exceed `MaxTargetsPerAction` (200) targets; no spawn may exceed `MaxSpawnInstancesPerAction` (25) instances |
| R11 | `spawn_actor_from_class` `class_path` must be on `SpawnClassAllowlist` and `static_mesh` (if given) on `SpawnMeshAllowlist`; `static_mesh` is only valid with `class_path` `/Script/Engine.StaticMeshActor` |
| R12 | `set_actor_property` may write only a `(class, property, type)` tuple on `PropertyAllowlist`; `property` may be a dotted struct-member path; `type` is float/int/bool/vector/color/name/enum/asset; the `value` kind must match (number↔float/int, bool↔bool, `[3]`↔vector, `[4]`↔color, string↔name/enum/asset) and, for float/int with a configured range, fall within it. There is no arbitrary property write — the executor re-checks against the live object and additionally refuses missing/ambiguous component owners, missing properties, invalid enum value names (`property_value_invalid_enum`), and assets that fail the entry's `AssetClass` check (`asset_class_not_allowed` / `asset_not_found`) |

### What `scripts/validate_plan.py` enforces (offline subset)

The Python validator enforces the schema-decidable subset of R1–R11. Two pieces
are **runtime-only** and cannot be decided from the plan JSON alone:

- **R6 live target resolution** — whether each target path resolves to a live
  actor in the editor world (`FUE5MCPTargetResolver::ResolveActorPaths`). The
  script enforces the schema part of R6 (empty/forbidden target lists, target
  string shape) but cannot resolve paths offline.
- **R11 allowlists** — the script mirrors the **documented default**
  `SpawnClassAllowlist` / `SpawnMeshAllowlist` from
  `Source/UE5MCP/Public/UE5MCPSettings.h` (set in
  `UE5MCPSettings.cpp`):
  - classes: `/Script/Engine.StaticMeshActor`, `/Script/Engine.PointLight`,
    `/Script/Engine.CameraActor`
  - meshes: `/Engine/BasicShapes/{Cube,Sphere,Cylinder,Cone,Plane}` (e.g.
    `/Engine/BasicShapes/Cube.Cube`)

  A live project may widen these allowlists in Project Settings > Plugins >
  UE5MCP, so a value the script rejects might be accepted by a customized editor
  (and vice versa). The running plugin is authoritative. The static-mesh/class
  pairing constraint is a pure schema rule and is always enforced.
- **R12 property allowlist** — the `(class, property, type, range)` `PropertyAllowlist`
  is project config, so it is enforced RUNTIME-ONLY by the C++ validator + executor.
  The script checks the schema part of `set_actor_property` (a non-empty `property`
  and a well-formed `value`) but not allowlist membership, type match, or range.

## Execution result

The exact shape serialized by `UE5MCPJson::SerializeExecutionResult`:

```json
{
  "executed": true,
  "action_results": [
    {
      "id": "set-actor-folder-01",
      "status": "succeeded",
      "message": "Moved 2 of 2 actors to World Outliner folder 'UE5MCP/Organized'",
      "changed_count": 2
    }
  ],
  "log": [
    "Plan approved with 2 targets in world '/Temp/Untitled_1.Untitled_1'",
    "set-actor-folder-01: moved 2 actors to 'UE5MCP/Organized'",
    "One editor Undo reverts this batch"
  ]
}
```

- `executed` is the overall success bool.
- Each `action_results` entry carries `id`, `status` (`succeeded` or `failed`),
  `message`, and `changed_count`. A refused action additionally carries
  `refusal_code` (a machine-readable reason — see `preview-approval-flow.md`). A
  `find_actors` result additionally carries `found_actors` (an array of actor
  summaries: `path`, `label`, `class_path`, `tags`, `folder`, `selected`,
  `transform`). A `read_logs` result additionally carries `log_lines` (an array
  of the matching structured log-line strings, oldest→newest). A
  `get_package_status` result additionally carries `source_control`
  (`{enabled, available, provider}`), `packages` (an array of
  `{name, filename, dirty, source_control_state}`), and `packages_truncated`.
- `log` is the append-only human-readable log line list, mirroring `LogUE5MCP`.

Every refusal still produces a full result — *what was prevented* is part of the
audit trail. Silent refusal is a bug by definition.

## Compatibility note

The risk vocabulary deliberately mirrors the read-only/destructive annotation
hints in MCP tool annotations (`readOnlyHint`, `destructiveHint`), surfaced in the
MCP server's tool list — with the difference that here the Unreal plugin enforces
the semantics rather than treating them as hints.
