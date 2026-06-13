# UE5MCP MCP Bridge Design — external agents driving the live editor

Date: 2026-06-11
Status: implemented and verified (55/55 headless automation tests; live external-client
runs through Claude Code on this machine).

## What this is

The path from an EXTERNAL Claude Code session to the live Unreal Editor:

```text
Claude Code (external agent session)
    │  mcp__ue5mcp__<tool> call
    │  ← THE HUMAN GATE: Claude Code's native tool-permission prompt
    ▼
MCPServer/ue5mcp-server.mjs        (zero-dependency Node stdio MCP server)
    │  typed plan JSON over HTTP, 127.0.0.1:30110 only
    ▼
FUE5MCPBridgeServer (plugin)       POST /plan {mode}
    ▼
FUE5MCPEditorService               validator → policy → guards → transaction → log
    ▼
FUE5MCPActionExecutor              native Unreal APIs, one FScopedTransaction
```

The MCP server holds **no editor capability**: it is stdio + loopback HTTP only, and
the plugin re-validates everything. A user-approved call that violates policy is
still refused server-side (allowlists, PIE guard, world/context fingerprint, caps).

## POST /plan modes

| mode | behavior | status codes |
|---|---|---|
| `submit` (default, legacy) | read-only executes immediately; mutations pend for the in-editor panel Approve click | 200 / 400 / 409 `plan_pending` |
| `preview` | validate + resolve + build the typed preview; NEVER executes, never occupies the approval slot (`status: previewed`) | 200 / 400 |
| `execute_external` | the human already approved this call inline in the agent session; service re-validates and executes immediately in one transaction | 200, 202 (destructive pending in-editor confirm), 400, 403 `external_approval_disabled`, 409 (`play_mode_active` / `stale_context` / `plan_pending`) |

`execute_external` is gated by `UUE5MCPSettings::bAllowExternalSessionApproval`
(**default false** — per-project conscious opt-in; the demo project sets it in
`Config/DefaultEditor.ini`). Every externally approved plan record carries
`approval_mode: external_session` and a `[external-session approval]` line in
`LogUE5MCP` — the audit trail.

## Risk tiers → Claude Code permission behavior

Implemented exactly as `ClientConfigs/claude-code/settings.json` (installed in the
test project):

| Tier | Tools | Behavior |
|---|---|---|
| ReadOnly | `get_selection`, `find_actors`, `preview_actions` | on `permissions.allow` → never prompt |
| LowMutation | `select_actors`, `set_actor_folder`, `set_actor_transform`, `duplicate_actor_with_offset`, `spawn_actor_from_class` | unlisted → native prompt per call; user may session-allowlist |
| Destructive | `delete_actor` | on `permissions.ask` → ALWAYS prompts; ask beats session-allows and even `--allowedTools` (verified live) |

Server-side mirrors: destructive plans are refused on the single-click pending path
(`destructive_requires_external_gate`) — they execute ONLY through the
external-session gate, optionally double-gated by
`bRequireInEditorConfirmForDestructive` (default off; when on, the external
destructive plan pends for an in-editor Approve on top of the in-session prompt and
the bridge returns 202; the MCP server polls `/plan/:id` to completion).

## Elicitation decision

Claude Code 2.1.172 **supports MCP elicitation** (form mode), so the preferred flow
from the goal is implemented: on a mutating call the server first POSTs
`mode=preview`, sends `elicitation/create` with the **plugin-generated** typed
preview, and only on accept re-fingerprints context and POSTs
`mode=execute_external`. Decline = clean no-op (non-error result, so the agent
doesn't retry).

- `UE5MCP_ELICIT=auto` (default): elicit only when the client advertises the
  elicitation capability at initialize.
- `UE5MCP_ELICIT=0`: rely on the permission prompt alone (used for headless runs);
  `=1`: force.
- Fallback when elicitation is off: the self-describing / preview-first pattern —
  every mutating tool's name + arguments fully convey its effect, and for composite
  effects the agent calls the free `preview_actions` tool first and surfaces the
  plugin's preview before the mutating call (whose permission prompt is the gate).

## New typed actions (M2)

| Tool | Risk | Notes |
|---|---|---|
| `set_actor_transform` | low | absolute components; omitted components unchanged |
| `duplicate_actor_with_offset` | low | engine `DuplicateActors` nests its transaction under the plan's outer one |
| `spawn_actor_from_class` | low | class + mesh ALLOWLISTS (`UUE5MCPSettings::SpawnClassAllowlist` / `SpawnMeshAllowlist`), enforced at validator (rule R11) AND re-checked in the executor; ≤25 instances/action; spawns directly via `UWorld::SpawnActor` with `RF_Transactional` (the editor-subsystem path runs viewport placement logic that crashes headless and adjusts positions) |
| `delete_actor` | destructive | `requires_second_confirmation` mandatory (R7); restores fully via one editor Undo |

Validator rule changes: R7 no longer categorically refuses destructive plans (the
refusal moved to the service's pending path); new R11 = spawn class/mesh policy;
tools that take no targets (`get_selection_context`, `find_actors`,
`spawn_actor_from_class`) now refuse non-empty target lists (R6).

## Verification record (2026-06-11, this machine)

- `make UnrealEditor` clean; `validate_first_proof.py` green (extended with
  external-approval, spawn-policy, MCP-server, and permission-config assertions).
- Headless automation: **55/55** (`Automation RunTests UE5MCP`, -nullrhi
  -unattended), including per-action undo tests and
  `UE5MCP.EndToEnd.ExternalSceneBuildThenFullUndo` (spawn grid → transform →
  folder via `execute_external` bodies, then 3 × `GEditor->UndoTransaction()`
  restores the level exactly).
- Live (real sockets, real MCP server, real external `claude -p` client against the
  running editor):
  - read tools returned live structured world data with no prompt;
  - a spawn attempt WITHOUT permission was blocked by Claude Code's permission layer
    and never reached the editor;
  - the M5 scene build ran end to end (9 cubes → scale → `UE5MCP/DemoGrid` folder),
    `LogUE5MCP` carrying the full per-plan audit trail;
  - `delete_actor` was blocked by the `ask` rule even when passed via
    `--allowedTools` (always-prompt proven), then executed correctly from a neutral
    config and destroyed exactly the 9 demo actors.
- Crash found & fixed during this pass: `UEditorActorSubsystem::SpawnActorFromClass`
  → `TryPlacingActorFromObject` → hit-proxy render → SIGFPE under `-nullrhi`;
  replaced with direct `UWorld::SpawnActor` (also semantically better: exact typed
  placement, no viewport snapping).

## Transport hardening (from the 2026-06-11 adversarial review)

An adversarial multi-lens review (5 lenses × 2 refuters each) of this surface found
and we fixed:

- **Loopback guard completeness (was HIGH):** `Start()` now resolves the *effective*
  bind address for the bridge's own port the way the engine does — `DefaultBindAddress`
  then the higher-precedence per-port `[HTTPServer.Listeners] ListenerOverrides` entry —
  and refuses to start unless it is loopback (`localhost`/`127.0.0.x`/`::1`,
  case-insensitive, rejecting `any`/`0.0.0.0`/`::`). The old check read only
  `DefaultBindAddress`, so a per-port `any` override could bind 0.0.0.0 while the guard
  still reported localhost.
- **Browser CSRF / DNS-rebinding (was MEDIUM):** every route now rejects requests
  whose `Origin` is present and non-loopback (and non-`null`), and whose `Host` is
  non-loopback; `POST /plan` additionally requires the custom `X-UE5MCP-Client`
  header. A browser can't set a custom header on a cross-site "simple" request
  without a CORS preflight (which the bridge never grants), so a malicious page in
  the user's browser can't drive `execute_external` mutations. The MCP server sends
  the header on every call.
- **Honest result reporting:** `delete_actor` now reports the true destroyed count
  (derived from the targets, not `DestroyActors`'s any/none bool);
  `set_actor_folder`/`set_actor_transform` report "changed M of N" and only succeed
  when all targets changed (matching spawn/duplicate). The MCP server's
  `formatRecord` now copies `error`/`message`/`refusal_code`, so a refusal never
  serializes to a bare `{}`.
- **Timeout vs unreachable:** a fetch timeout is reported distinctly from a
  connection failure and warns that the write MAY have applied — verify with a read
  before retrying (no idempotency key yet; that's the remaining follow-up).

## Known limits / next steps

- The stale-context fingerprint still compares the full selected set (spec R8); the
  MCP server re-fetches context right before execution (and again after an
  elicitation wait) to keep it truthful. Target-set-only fingerprints remain a
  candidate refinement.
- `select_actors` participates in the editor transaction but selection restore on
  undo follows editor selection-undo semantics.
- Elicitation was verified at the protocol level (capability negotiation + flow
  implemented per spec); the interactive form dialog should be eyeballed once in the
  live demo — if it misbehaves, set `UE5MCP_ELICIT=0` in `.mcp.json` (the permission
  prompt remains the gate).
- Deferred (unchanged): asset rename/move/delete, source-control lifecycle,
  broad scans, anything beyond `delete_actor` in the destructive tier.
