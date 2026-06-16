# Spec: UE5MCP preview and approval flow (v1)

This document specifies the lifecycle of a plan from submission to execution or
refusal in the UE5MCP Unreal Engine 5 editor plugin. The design intent: a
mutation is impossible without a valid, target-resolved plan and an explicit human
approval that re-validates editor context (the `context_fingerprint`) at the
moment of execution. The plugin is the enforcement boundary; the MCP server
(`MCPServer/ue5mcp-server.mjs`) and the in-editor Copilot panel are both clients
of the same `FUE5MCPEditorService` path. See `action-plan-format.md` for the
envelope, the 8-tool registry, and rules R1–R11. See `docs/architecture.md` for
the end-to-end transport narrative.

There is exactly one path to a `FUE5MCPValidatedPlan`:
`FUE5MCPPlanValidator::ValidateAndResolve`. Every transport validates there.

## POST /plan modes

The plugin's localhost bridge (`FUE5MCPBridgeServer`, `127.0.0.1:30110`) accepts a
plan envelope with an optional top-level `mode` selecting how it is handled. Each
mode funnels into a method on `FUE5MCPEditorService`.

| mode | service method | behavior | status codes |
| --- | --- | --- | --- |
| `submit` (default, legacy) | `SubmitPlanRequest` | read-only executes immediately; non-destructive mutations **pend** for the in-editor panel Approve click; destructive plans are refused here | 200 / 400 / 409 (`destructive_requires_external_gate` / `plan_pending`) |
| `preview` | `PreviewPlanRequest` | validate + resolve targets + build the typed preview text; **never executes**, never occupies the approval slot (record status `previewed`) | 200 / 400 |
| `execute_external` | `SubmitExternalPlan` | the human already approved this call inline in the agent session; the service re-validates schema/policy/allowlists, runs the mutation guards, re-checks the context fingerprint, and executes immediately in one transaction | 200, 202 (destructive belt-and-suspenders pending in-editor confirm), 400, 403 (`external_approval_disabled`), 409 (`play_mode_active` / `stale_context` / `plan_pending`) |

`execute_external` is gated by
`UUE5MCPSettings::bAllowExternalSessionApproval` (**default false** — a per-project
conscious opt-in; `Source/UE5MCP/Public/UE5MCPSettings.h`). When disabled, the
service refuses with `external_approval_disabled` before doing anything else.
Every externally-approved plan record carries `approval_mode: external_session`
and a `[external-session approval]` line in `LogUE5MCP` — the audit trail.

## Lifecycle

```
                       ┌─ invalid_plan (R1–R11 failure)
                       │
request ──validate──▶ valid plan
                       │
   read_only ──────────┴──▶ executed  (immediate, no Undo step, no approval)

   low_risk (mode=submit) ──▶ pending_approval ──panel Approve──▶ executed
                                     │
                                     ├─ superseded (panel generates/clears a new plan)
                                     ├─ stale_context (selection/world drifted)
                                     └─ play_mode_active / editor_unavailable

   low_risk (mode=execute_external) ──guards+fingerprint──▶ executed
   destructive (mode=execute_external) ─┬─ executed
                                        └─ (if bRequireInEditorConfirmForDestructive)
                                           pending_approval → 202 → poll /plan/:id
```

Plan record statuses (`UE5MCPJson::PlanStatusToString`): `pending_approval`,
`invalid`, `executed`, `failed`, `refused_stale`, `superseded`, `previewed`.

- **valid → executed (read-only):** observation never needs approval and never
  occupies the pending slot; it executes immediately and creates no Undo step.
- **valid → pending_approval (low_risk, `submit`):** the resolved plan and its
  typed preview are staged for the in-editor panel; the bridge returns the plan id
  and the preview. A human Approves — or doesn't — in the editor. One plan pends
  at a time.
- **valid → executed (low_risk/destructive, `execute_external`):** the in-session
  permission prompt already gated the call; the service re-validates, runs the
  guards and the fingerprint re-check, and executes in one transaction.
- **executed is terminal:** an executed plan is consumed. Re-approving without
  regenerating the preview is refused (`plan_consumed`). A refusal does **not**
  consume the plan — nothing executed, so it stays approvable once the blocker
  clears.
- **Any state → refused:** refusals are results, not errors. They are logged with
  a machine-readable reason code.

## Destructive plans: the external gate

`delete_actor` (the only destructive tool) is double-gated:

1. **Schema (R7):** the plan must set `requires_second_confirmation: true`, or the
   validator rejects it.
2. **Service gate:** destructive plans are **refused on the single-click pending
   path** (`submit`) with `destructive_requires_external_gate`. They execute ONLY
   through `execute_external`, where the human gate is the MCP client's native
   tool-permission prompt — which the client cannot allowlist (it stays on the
   permission "ask" list and always prompts).

Optionally, `bRequireInEditorConfirmForDestructive` (default off) adds a
belt-and-suspenders layer: when on, a destructive `execute_external` plan ALSO
pends for an in-editor Approve click; the bridge returns 202 with the plan id and
the MCP server polls `/plan/:id` to completion (`UE5MCP_PENDING_TIMEOUT_MS`).

## Mutation guards and the stale-context fingerprint

For an `execute_external` mutation the service runs, in order:

1. **External approval enabled?** else `external_approval_disabled` (403).
2. **Re-validate** the plan (R1–R11) — a user-approved call that now violates
   policy is still refused (`invalid_plan`).
3. **Editor mutation blocked?** (`FUE5MCPActionExecutor::IsEditorMutationBlocked`)
   — Play-In-Editor active or no editor world → `play_mode_active` /
   `editor_unavailable` (409). Availability guards are checked **before** the
   stale-context guard: during Play mode the editor can't even report selection
   reliably, so `play_mode_active` is the truthful refusal.
4. **Stale-context fingerprint re-check** (`IsContextStillValid`): the world
   identity (`context_fingerprint.scene`) and the observed selected set must still
   match what the plan was built against. If the selection or world drifted after
   the plan was generated, the promise is void →
   `stale_context` (409, status `refused_stale`); the client must re-read context
   and resubmit. The MCP server re-fetches context immediately before
   `execute_external` (and again after any elicitation wait) so the fingerprint is
   truthful at the moment of execution.

Only after all four pass does `FUE5MCPActionExecutor::ExecuteApprovedPlan` run.

## Execution semantics

- A mutation plan applies as **exactly one** undoable batch wrapped in a single
  `FScopedTransaction`. Read-only plans create no Undo step.
- Every change is captured for Undo before it is applied, so one editor Undo
  reverts the entire approved batch (verified live; redo too).
- Actions execute in plan order. On the first failed action, execution halts;
  completed actions remain applied and the result records exactly how far it got.
  Because the batch is one Undo unit, one Undo still clears a partial application.
- `spawn_actor_from_class` spawns via `UWorld::SpawnActor` with `RF_Transactional`
  (the editor-subsystem placement path runs viewport logic that crashes headless
  and snaps positions).
- Changes participate in the editor's normal save flow — nothing is written behind
  the editor's back.

## Refusal codes

These are the exact `RefusalCode` strings emitted by `FUE5MCPEditorService` /
`FUE5MCPApprovalState` / `FUE5MCPBridgeServer`.

| Code | Meaning | Where |
| --- | --- | --- |
| `invalid_plan` | Plan failed R1–R11 validation | submit / preview / external |
| `destructive_requires_external_gate` | A destructive plan was submitted on the single-click pending path; it must go through `execute_external` | `submit` |
| `external_approval_disabled` | `execute_external` used but `bAllowExternalSessionApproval` is false | `execute_external` (403) |
| `play_mode_active` | Play-In-Editor active; mutations are blocked | `execute_external`, panel approve |
| `editor_unavailable` | No editor or no open world | `execute_external`, panel approve |
| `package_not_writable` | A mutation would dirty a package the editor cannot save — read-only / not-checked-out on disk, or checked out by another user. Check it out / make it writable first, or disable the package-write policy. Gated per-action; new/unsaved + writable packages are unaffected | `execute_external`, panel approve |
| `source_control_checkout_disabled` | `check_out_package` used but `bAllowSourceControlCheckout` is off — the source-control write opt-in | `execute_external`, panel approve |
| `source_control_unavailable` | `check_out_package` but source control is not enabled/available in this editor | `execute_external`, panel approve |
| `package_not_found` / `package_not_source_controlled` / `checkout_failed` | `check_out_package` against a missing package, a package not under source control, or a checkout the provider rejected | `execute_external`, panel approve |
| `stale_context` | World or selection changed after the plan was generated | `execute_external`, panel approve |
| `plan_pending` | Another plan is already pending approval; wait or poll it | submit / external |
| `plan_consumed` | The plan already executed; regenerate the preview to run again | panel approve |
| `superseded` | A pending plan was cleared/replaced by a newer panel plan — humans win, machines wait | approval state |

Every refusal produces a structured result and a human-readable `LogUE5MCP` line.
Silent refusal is a bug by definition.

## External client transport

The MCP server holds **no editor capability**: stdio + loopback HTTP only. It
rewrites each MCP tool call into a typed plan and POSTs it with the appropriate
`mode`. Read-only calls go through `submit` and execute immediately. Mutating
calls go through `execute_external`: the server first POSTs `mode: preview` to get
the plugin-generated typed preview, surfaces it via MCP `elicitation/create` when
the client supports elicitation (the in-session confirm), re-fingerprints context
on accept, then POSTs `mode: execute_external`. Decline is a clean no-op (a
non-error result, so the agent doesn't retry). When elicitation is off
(`UE5MCP_ELICIT=0`), the always-prompting permission gate plus the self-describing
tool names remain the human gate, and the agent may call the read-only
`preview_actions` MCP tool first to surface the effect.

Transport hardening (loopback-only bind, `X-UE5MCP-Client` header requirement,
Origin/Host rejection for CSRF/DNS-rebinding) is documented in
`docs/architecture.md`.

## UI contract

The Copilot panel shows exactly three zones, and nothing executes outside them:

1. **Context header** — live world name, selected count, loaded count (capped,
   with a warning when capped).
2. **Plan preview** — the resolved actions, one row per action, with target labels,
   counts, and the risk badge.
3. **Log** — append-only results: successes, failures, refusals, and an Undo hint
   after each applied batch.

The Approve control is disabled unless a valid plan is pending. Empty selection or
empty required params invalidate the plan rather than producing an approvable
no-op.
