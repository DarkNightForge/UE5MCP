# UE5MCP capability map

Status: living product/planning document  
Last updated: 2026-06-14  
Source of truth for current tools: `Source/UE5MCP/Private/UE5MCPToolRegistry.cpp`, `docs/specs/action-plan-format.md`, `docs/specs/preview-approval-flow.md`, and `docs/validation-checklist.md`.

This document tracks **breadth**: which parts of Unreal Engine development UE5MCP can currently observe, preview, mutate, refuse, and verify.

It is intentionally broader than the implementation. The point is to make the missing surface area visible so the next useful slice is obvious.

## How to read this

UE5MCP should grow by **governed capability**, not by opening an arbitrary execution hole. Every new area should preserve the core invariant:

> Agent emits typed data → UE5MCP validates → preview shows exact effect → human/client approval gates mutation → plugin revalidates → native Unreal APIs execute → one undoable transaction where applicable → structured result/log/refusal.

### Status vocabulary

| Status | Meaning |
| --- | --- |
| `shipped` | Implemented, documented, and covered by tests/manual verification. |
| `partial` | Some useful support exists, but major workflow gaps remain. |
| `read-only` | UE5MCP can observe/inspect but not mutate. |
| `planned` | Direction is known, no shipped capability yet. |
| `deferred` | Intentionally not built until safety/undo/source-control semantics are solved. |
| `blocked` | Needs engine research, UX decision, or policy model before design can proceed. |
| `none` | No meaningful support yet. |

### Capability levels

| Level | Meaning | Current examples |
| --- | --- | --- |
| L0 Observe | Read context without mutation. | `get_selection`, `find_actors`, `preview_actions` |
| L1 Organize | Low-risk, visible editor organization. | `select_actors`, `set_actor_folder` |
| L2 Spatial | Scene layout, transform, duplication, bounded spawning. | `set_actor_transform`, `duplicate_actor_with_offset`, `spawn_actor_from_class` |
| L3 Properties | Allowlisted actor/component/property edits. | planned |
| L4 Assets | Content Browser/package-level operations. | deferred |
| L5 Runtime/debug | PIE/log/build/test diagnosis and recovery loops. | planned/read-only direction |
| L6 Project/team workflow | source control, CI, packaging, collaboration. | planned/deferred |

## Current shipped surface

Registry tools in v1:

| Tool | Risk | Capability area | Current scope |
| --- | --- | --- | --- |
| `get_selection_context` / MCP `get_selection` | read-only | Editor context | Current world, selected actors, capped loaded-actor context. |
| `find_actors` | read-only | Actor discovery | Finds loaded editor-world actors by class, label, tag, folder, selected-only, capped results. |
| MCP `preview_actions` | read-only | UX / preview | Validates and resolves typed action plans without executing. |
| `select_actors` | low mutation | Selection | Sets editor selection to explicit actor targets. |
| `set_actor_folder` | low mutation | World Outliner organization | Moves target actors into a named folder. |
| `set_actor_transform` | low mutation | Spatial layout | Sets absolute location/rotation/scale components; omitted components unchanged; no-op refused. |
| `duplicate_actor_with_offset` | low mutation | Spatial layout | Duplicates target actors once with a typed offset. |
| `spawn_actor_from_class` | low mutation | Bounded creation | Spawns allowlisted classes/meshes, max 25 instances/action. |
| `delete_actor` | destructive | Scene cleanup | Deletes target actors through destructive gate; undoable by editor transaction. |

Current verification snapshot:

- Public repo Python checks: `58 passed` in the last local verification run.
- UE automation docs claim 56/56 headless tests on UE 5.7.4 Linux source build.
- Windows and Epic Games Launcher binary builds are not yet verified.
- Full tool breadth is still actor/world-editor focused; most asset/Blueprint/team-pipeline workflows are not shipped.

## Unreal development area map

### 1. Project setup, plugin install, and engine compatibility

| Field | Current state |
| --- | --- |
| Status | `partial` |
| Current support | Project-plugin layout, README quickstart, Claude Code client config, Node MCP server. Verified on UE 5.7.4 Linux source build. |
| Missing | Windows verification, Epic Launcher binary verification, packaged release artifact, sample project, compatibility matrix for UE 5.4/5.5/5.6/5.7+, CI build matrix. |
| Next useful slice | Make a boring mainstream install path: packaged plugin + sample project + Windows/binary smoke test. |
| Proof needed | Fresh clone/install on a normal binary Unreal project; document exact version/build result. |

### 2. Editor context and project understanding

| Field | Current state |
| --- | --- |
| Status | `partial` / `read-only` |
| Current support | Current world, selected actors, capped loaded actor summaries, actor path/label/class/tags/folder/transform. |
| Missing | Asset registry context, Blueprint graph summaries, component hierarchy beyond actor-level summaries, map/sublevel/world-partition context, project settings, plugin/module inventory. |
| Next useful slice | Add `list_capabilities` and richer read-only diagnostics before new mutations. |
| Proof needed | Agent can answer “what is in this level/project?” without hallucinating or scanning unbounded surfaces. |

### 3. Actor discovery and selection

| Field | Current state |
| --- | --- |
| Status | `shipped` for loaded actors; `partial` for full projects |
| Current support | `find_actors` filters loaded actors by class path, label substring, tag, folder path, selected-only, capped max results. `select_actors` sets exact selection. |
| Missing | Unloaded World Partition actors, asset references, cross-level search, stable semantic grouping beyond labels/tags/folders. |
| Next useful slice | Add explicit result caps/warnings to user-facing output and consider target-set fingerprints separate from full selection fingerprints. |
| Proof needed | Messy-level cleanup demo: find exact actors, preview count, select/organize only those actors. |

### 4. World Outliner organization

| Field | Current state |
| --- | --- |
| Status | `shipped` |
| Current support | `set_actor_folder` moves exact target actors into a folder in one undoable transaction. |
| Missing | Folder rename/merge/delete, folder conventions, validation against project naming rules, bulk cleanup recipes. |
| Next useful slice | Add labels/tags/folder cleanup recipe support while preserving preview/undo/log. |
| Proof needed | “Clean this messy scene” demo with exact before/after folder counts and one undo. |

### 5. Spatial layout and transforms

| Field | Current state |
| --- | --- |
| Status | `shipped` |
| Current support | `set_actor_transform` for absolute location/rotation/scale components; `duplicate_actor_with_offset`; before/after previews; no-op transform refused. |
| Missing | Relative transforms, align/distribute/snap tools, bounds-aware placement, collision-aware placement, grid/layout recipes, pivot-aware edits. |
| Next useful slice | Add governed layout recipes: align, distribute, grid, circle, stack, snap-to-ground. |
| Proof needed | Agent arranges a recognizable scene layout with before/after preview and clean undo. |

### 6. Bounded actor spawning and deletion

| Field | Current state |
| --- | --- |
| Status | `shipped` for allowlisted primitive/class spawning and actor deletion |
| Current support | `spawn_actor_from_class` with class/mesh allowlists and max 25 instances/action; `delete_actor` destructive tier. |
| Missing | Prefab/Blueprint spawning beyond allowlist, template-driven spawning, source-control/package awareness for spawned assets, broad delete policies. |
| Next useful slice | Strengthen refusal/result UX: non-allowlisted class, over-cap spawn, destructive delete, and undo proof demos. |
| Proof needed | Approved-by-agent but refused-by-plugin scenarios documented and demoed. |

### 7. Actor labels, tags, and metadata

| Field | Current state |
| --- | --- |
| Status | `planned` |
| Current support | Read tags/folders/labels through actor summaries; filter by tag/label/folder. |
| Missing | Set label, add/remove tags, validate naming conventions, metadata recipes. |
| Next useful slice | Add `set_actor_label` and `add/remove_actor_tags` as low-risk visible mutations. |
| Proof needed | Preview exact label/tag changes, reject empty/no-op changes, one undo restores. |

### 8. Components and allowlisted properties

| Field | Current state |
| --- | --- |
| Status | `planned` / `blocked` pending property policy design |
| Current support | Actor transform only; no generic component/property edits. |
| Missing | Component enumeration, component selection, allowlisted property schema, type-safe property writes, before/after preview. |
| Next useful slice | Read-only component summaries, then allowlisted scalar/vector/bool/name property edits for explicit component classes. |
| Proof needed | No arbitrary property write; every property has declared type, constraints, preview, undo, and refusal codes. |

### 9. Blueprint workflows

| Field | Current state |
| --- | --- |
| Status | `none` for mutation; `planned` for read-only inspection |
| Current support | None beyond actor class paths. |
| Missing | Blueprint asset discovery, compile status, graph/node inspection, safe node edits, variable/function/event inspection, compile/run feedback. |
| Next useful slice | Read-only Blueprint diagnostics: list Blueprints, compile errors, references, high-level graph summary. |
| Proof needed | Agent can explain why a Blueprint is broken without mutating it. Mutation should wait for a typed graph-edit model. |

### 10. C++ and source code workflows

| Field | Current state |
| --- | --- |
| Status | `none` in UE5MCP host; external coding agents can edit repo files separately |
| Current support | UE5MCP does not generate or execute model-authored C++ inside the editor. |
| Missing | Build orchestration, compile error readback, code/navigation integration, safe handoff between coding agent and editor tool host. |
| Next useful slice | Read-only compile/log diagnostics and “open relevant file/symbol” handoff, not code mutation through UE5MCP. |
| Proof needed | Agent reads Unreal build errors/logs and suggests next coding-agent task with citations to files/log lines. |

### 11. Materials and material instances

| Field | Current state |
| --- | --- |
| Status | `none` |
| Current support | Can spawn StaticMeshActor with allowlisted basic shapes; no material editing. |
| Missing | Material instance discovery, parameter listing, scalar/vector/texture parameter edits, assignment to actors/components. |
| Next useful slice | Read material instance parameters; then allowlisted material instance parameter edits. |
| Proof needed | Preview parameter before/after, package dirty reporting, undo/save semantics clear. |

### 12. Assets, Content Browser, and packages

| Field | Current state |
| --- | --- |
| Status | `deferred` |
| Current support | Spawn uses allowlisted class/mesh references; no asset mutation. |
| Missing | Asset rename/move/delete, duplicate assets, redirector handling, package save, dependency/reference previews, asset registry queries. |
| Next useful slice | Read-only asset registry query and dependency/reference preview. Mutation deferred until source-control/package semantics are designed. |
| Proof needed | Asset operation previews must list packages touched, redirectors, references, source-control requirements, and rollback/undo limits. |

### 13. Source control and team pipeline

| Field | Current state |
| --- | --- |
| Status | `deferred` / `planned` |
| Current support | None. Editor changes participate in normal save flow but UE5MCP does not manage checkouts/changelists. |
| Missing | Perforce/Git status, checkout/add/revert, changelist routing, read-only package refusal, dirty package list, conflict handling. |
| Next useful slice | Read-only source-control/package status surfaced in previews. |
| Proof needed | Any mutation that dirties packages reports exact package list and refuses when package state is not writable/checked out as policy requires. |

### 14. World Partition, levels, sublevels, streaming

| Field | Current state |
| --- | --- |
| Status | `partial` for loaded actors; `none` for partition operations |
| Current support | Operates on currently loaded editor world actors only. |
| Missing | Unloaded actor discovery, level streaming context, data layers, world partition cells, level ownership, actor migration between levels. |
| Next useful slice | Read-only world/level/data-layer inventory. |
| Proof needed | Clear warnings when search scope is loaded-only; no silent claim that unloaded actors were searched. |

### 15. Lighting, cameras, and cinematic layout

| Field | Current state |
| --- | --- |
| Status | `partial` through actor spawning/transform only |
| Current support | Can spawn allowlisted `PointLight` and `CameraActor`; transform actors. |
| Missing | Light-specific properties, camera/lens settings, Sequencer integration, shot creation, render preview. |
| Next useful slice | Allowlisted property edits for light intensity/color and camera transform/lens params. |
| Proof needed | Preview before/after values and preserve undo. |

### 16. Sequencer and cinematics

| Field | Current state |
| --- | --- |
| Status | `none` |
| Current support | None. |
| Missing | Level sequences, tracks, bindings, keyframes, camera cuts, playback/render diagnostics. |
| Next useful slice | Read-only sequence inventory and binding summary. |
| Proof needed | Agent can summarize a sequence without modifying it; mutations require a dedicated typed timeline model. |

### 17. Landscape, foliage, PCG, and environment art

| Field | Current state |
| --- | --- |
| Status | `none` |
| Current support | None beyond generic actors if already loaded/targeted. |
| Missing | Landscape edit layers, foliage instances, PCG graphs, environment placement recipes. |
| Next useful slice | Read-only counts/selection summaries for landscape/foliage/PCG actors/assets. |
| Proof needed | No broad/uncapped scans; all environment operations need caps and previews. |

### 18. UI/UMG/CommonUI

| Field | Current state |
| --- | --- |
| Status | `none` |
| Current support | None. |
| Missing | Widget discovery, hierarchy inspection, binding/property inspection, safe edits. |
| Next useful slice | Read-only widget tree summaries. |
| Proof needed | Agent can explain a widget hierarchy and bindings without mutating assets. |

### 19. Enhanced Input and gameplay framework setup

| Field | Current state |
| --- | --- |
| Status | `none` |
| Current support | None. |
| Missing | Input actions/mapping contexts, pawn/controller/game mode setup, gameplay tags, data assets. |
| Next useful slice | Read-only inventory of input/action assets and configured mappings. |
| Proof needed | Avoid mutation until asset/package/source-control story exists. |

### 20. AI, navigation, behavior trees, EQS, StateTree

| Field | Current state |
| --- | --- |
| Status | `none` |
| Current support | None. |
| Missing | NavMesh status, AI controller/pawn setup, behavior tree/blackboard/StateTree inspection and edits. |
| Next useful slice | Read-only diagnostics: navmesh present? AI assets linked? blackboard keys? |
| Proof needed | Agent can diagnose why an AI actor may not move using logs/context, without arbitrary script execution. |

### 21. Physics, collision, and gameplay interaction setup

| Field | Current state |
| --- | --- |
| Status | `none` / `planned` via property edits |
| Current support | Transform only. |
| Missing | Collision presets, simulate physics, mobility, overlap events, constraints. |
| Next useful slice | Read-only collision/physics summaries for selected actors/components. |
| Proof needed | Property mutations must be allowlisted and previewed. |

### 22. Animation, skeletal meshes, Control Rig

| Field | Current state |
| --- | --- |
| Status | `none` |
| Current support | None. |
| Missing | Skeletal mesh/animation blueprint inspection, montage/state machine summaries, Control Rig setup. |
| Next useful slice | Read-only animation asset inventory and broken-reference diagnostics. |
| Proof needed | Mutations deferred until asset/package model exists. |

### 23. Niagara/VFX

| Field | Current state |
| --- | --- |
| Status | `none` |
| Current support | None. |
| Missing | Niagara system discovery, parameter inspection, component placement, simulation diagnostics. |
| Next useful slice | Read-only Niagara system/component summaries. |
| Proof needed | Parameter mutations require typed parameter schema and preview. |

### 24. Audio

| Field | Current state |
| --- | --- |
| Status | `none` |
| Current support | None. |
| Missing | Sound cue/meta sound inspection, attenuation/concurrency settings, audio component placement. |
| Next useful slice | Read-only audio asset/component summaries. |
| Proof needed | Mutations need package/source-control semantics. |

### 25. DataTables, DataAssets, config, gameplay data

| Field | Current state |
| --- | --- |
| Status | `none` |
| Current support | None. |
| Missing | Read rows/fields, schema-aware edits, DataAsset property edits, config diffs. |
| Next useful slice | Read-only schema/row summaries, then allowlisted row edits with diff preview. |
| Proof needed | Typed diffs, package dirty status, source-control policy, validation against row struct. |

### 26. Logs, diagnostics, and performance profiling

| Field | Current state |
| --- | --- |
| Status | `planned`; current internal log exists but no agent readback tool |
| Current support | UE5MCP writes `LogUE5MCP` lines and returns structured results for tool calls. |
| Missing | `read_logs`, output log queries, compile/cook error parsing, performance snapshots, stat commands. |
| Next useful slice | `read_logs` / diagnostics readback tool with caps, filters, and no mutation. |
| Proof needed | Agent can self-correct after refusal/error using structured logs instead of hallucinating. |

### 27. Build, cook, package, and automation

| Field | Current state |
| --- | --- |
| Status | `none` in UE5MCP runtime; docs include manual automation command |
| Current support | Headless automation test command documented; no MCP tool to run builds/cook/package. |
| Missing | Controlled build/cook/package invocation, log parsing, artifact reporting, CI integration. |
| Next useful slice | Read-only parse/report of existing build logs before adding process execution tools. |
| Proof needed | No arbitrary process launch from model-originated requests; any build command needs explicit policy and user approval. |

### 28. Testing and validation inside Unreal

| Field | Current state |
| --- | --- |
| Status | `partial` as internal project tests; no external tool surface |
| Current support | UE automation suite exists in source; validation checklist documents commands. |
| Missing | MCP-exposed test runner, test discovery, structured result ingestion. |
| Next useful slice | Read-only test result parsing and “how to rerun” guidance; test runner later with strict command allowlist. |
| Proof needed | Agent can report actual test results with file/report citations. |

### 29. Collaboration, permissions, and UX approval

| Field | Current state |
| --- | --- |
| Status | `partial` |
| Current support | Risk tiers, Claude Code config, native client approval path, optional in-editor destructive confirm, plugin final refusal authority. |
| Missing | Multi-client configs, durable approval records, identity/session metadata, team policy profiles, role-based permissions. |
| Next useful slice | Document/refine tool metadata for client approval: risk tier, approval policy, preview availability, undoability, idempotency, affected surface. |
| Proof needed | Same risk semantics demonstrated in at least two MCP clients or with documented fallback behavior. |

### 30. Documentation and capability governance

| Field | Current state |
| --- | --- |
| Status | `partial` |
| Current support | README, architecture, roadmap, action-plan spec, approval-flow spec, validation checklist, this capability map. |
| Missing | Automated drift checks tying registry/tools to this map, per-domain pages as breadth grows, release notes. |
| Next useful slice | Add capability-map update to PR/agent instructions and validation checklist. |
| Proof needed | Every new tool/domain change updates this map in the same commit or explicitly records why not. |

## Recommended next capability slices

Ranked by leverage and fit with the governed model:

1. **`read_logs` / diagnostics readback** (`L0`) — helps agents self-correct without increasing mutation risk.
2. **Actor labels and tags** (`L1`) — high-usefulness, low-risk, obvious scene-cleanup demos.
3. **Source-control/package status readback** (`L0/L6`) — required before serious asset/package mutation.
4. **Allowlisted property edits** (`L3`) — gateway to lights/cameras/material instances/components without open exec.
5. **Read-only Blueprint/material/asset inspection** (`L0`) — expands project understanding before risky mutation.
6. **Capability registry / `list_capabilities`** (`L0`) — lets agents know exactly what is possible now and prevents hallucinated tools.

## Demo coverage matrix

| Demo | Shows | Current readiness |
| --- | --- | --- |
| 3x3 cube grid → scale → folder → undo | spawn, transform, organize, native approval, undo | ready / existing |
| Messy level cleanup | find, select, folder, preview target count | ready with current tools |
| Non-allowlisted spawn refusal | plugin final refusal after client approval | ready if allowlist refusal is surfaced clearly |
| Stale context refusal | preview bound to editor state | ready / covered by tests |
| PIE/SIE mutation refusal | editor-state guardrails | ready / covered by tests |
| Over-cap spawn refusal | blast-radius limit | ready if result UX is clear |
| Actor labels/tags cleanup | low-risk metadata breadth | needs new tools |
| Read logs and self-correct | refusal/error recovery loop | needs `read_logs` tool |
| Source-control-safe package mutation | studio adoption proof | needs design + implementation |

## Rules: when to update this document

Update this capability map in the **same change** whenever any of the following happen:

1. **A tool is added, removed, renamed, or changes risk tier** in `FUE5MCPToolRegistry` or the MCP server tool list.
2. **A validation rule changes** in `FUE5MCPPlanValidator`, `scripts/validate_plan.py`, or the action-plan spec.
3. **A capability graduates status** (`none` → `read-only`, `planned` → `partial`, `partial` → `shipped`, etc.).
4. **A capability is intentionally deferred or blocked** because of undo/source-control/security concerns.
5. **A new Unreal domain becomes relevant** through competitive research, user requests, or demos.
6. **Verification scope changes**, especially platform/engine support, test count, CI coverage, or live demo evidence.
7. **The product positioning changes** in a way that alters which domains matter most.
8. **A demo is added or retired** because it no longer reflects the actual product surface.

Every update should include:

- status change
- evidence/source file or command
- missing gaps
- next useful slice
- proof needed before calling it shipped

## Scaling rule for future pages

Keep this page as the top-level dashboard. When any domain grows beyond a few rows of status, split it into a child page and link it here. Suggested future child pages:

- `docs/capabilities/actor-and-scene.md`
- `docs/capabilities/assets-and-packages.md`
- `docs/capabilities/blueprints.md`
- `docs/capabilities/source-control.md`
- `docs/capabilities/logs-and-diagnostics.md`
- `docs/capabilities/client-approval-ux.md`

Child pages must preserve the same fields: current support, missing surface, next slice, proof needed, and update triggers.
