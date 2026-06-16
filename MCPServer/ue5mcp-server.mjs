#!/usr/bin/env node
// UE5MCP MCP server — a zero-dependency stdio bridge between an MCP client
// (e.g. Claude Code) and the UE5MCP editor plugin's localhost HTTP bridge.
//
// ROLE AND BOUNDARY: this process is a CLIENT/INTEGRATION layer, never a
// privileged executor. Every tool call becomes a typed plan POSTed to the
// plugin's bridge, which re-validates schema/policy/allowlists, blocks during
// PIE, wraps mutations in one undoable transaction, logs every action, and
// retains final refusal authority. Nothing here can bypass that path — this
// file holds no editor capability at all, only HTTP calls to 127.0.0.1.
//
// APPROVAL MODEL (mirrors the plugin's risk tiers via tool naming):
//   read_only   tools (get_selection, find_actors, read_logs,
//               get_package_status, get_actor_properties, get_actor_components,
//               list_capabilities, preview_actions)
//               → safe to allowlist; never mutate.
//   low_risk    tools (select_actors, set_actor_folder, set_actor_label,
//               add_actor_tags, remove_actor_tags, set_actor_property,
//               set_actor_transform, duplicate_actor_with_offset,
//               spawn_actor_from_class, check_out_package)
//               → the MCP client's native tool-permission prompt is the human
//                 approval; users may session-allowlist consciously.
// Mutations are additionally gated by the plugin's package-write policy: a mutation
// that would dirty a package the editor cannot save (read-only / not-checked-out, or
// checked out by someone else) is refused with `package_not_writable` — check the file
// out or make it writable first. Previews surface each target package's writability.
//
//   destructive tools (delete_actor)
//               → keep on the client's "ask" list so they ALWAYS prompt.
// When the client supports MCP elicitation, mutating calls additionally show
// the plugin-generated typed preview as an in-session confirm (set
// UE5MCP_ELICIT=0 to disable, =1 to force, default "auto").
//
// Env: UE5MCP_BRIDGE  (default http://127.0.0.1:30110)
//      UE5MCP_ELICIT  (auto | 1 | 0; default auto)
//      UE5MCP_PENDING_TIMEOUT_MS (poll budget for in-editor-confirm plans)

const BRIDGE = process.env.UE5MCP_BRIDGE ?? 'http://127.0.0.1:30110';
const ELICIT_MODE = (process.env.UE5MCP_ELICIT ?? 'auto').toLowerCase();
const PENDING_TIMEOUT_MS_RAW = Number(process.env.UE5MCP_PENDING_TIMEOUT_MS ?? 120000);
const PENDING_TIMEOUT_MS = Number.isFinite(PENDING_TIMEOUT_MS_RAW) && PENDING_TIMEOUT_MS_RAW > 0
  ? PENDING_TIMEOUT_MS_RAW : 120000;
const SUPPORTED_PROTOCOL_VERSIONS = ['2025-06-18', '2025-03-26', '2024-11-05'];

// Sent on every bridge request: the bridge requires this custom header on POST
// /plan, which a browser cannot set cross-origin without a CORS preflight the
// bridge never grants — so the server-side mutation path can't be driven by CSRF.
const BRIDGE_HEADERS = { 'x-ue5mcp-client': 'ue5mcp-mcp-server', origin: 'null' };

const log = (...args) => console.error('[ue5mcp]', ...args);

// ---------------------------------------------------------------------------
// Tool surface. Names become mcp__ue5mcp__<name> in Claude Code.
// ---------------------------------------------------------------------------

const ACTOR_PATHS_SCHEMA = {
  type: 'array',
  items: { type: 'string' },
  minItems: 1,
  description: 'Full actor object paths as returned by get_selection/find_actors/spawn results (e.g. "/Temp/Untitled_1.Untitled_1:PersistentLevel.StaticMeshActor_0").',
};
const VEC3 = (what) => ({
  type: 'array', items: { type: 'number' }, minItems: 3, maxItems: 3, description: what,
});

const TOOLS = [
  {
    name: 'get_selection',
    risk: 'read_only',
    description: 'Read the current Unreal Editor selection and world context (read-only; never mutates).',
    inputSchema: { type: 'object', properties: {}, additionalProperties: false },
    annotations: { title: 'Get editor selection', readOnlyHint: true },
  },
  {
    name: 'find_actors',
    risk: 'read_only',
    description: 'Search loaded editor-world actors by class/label/tag/folder/selection (read-only, capped results; does not see unloaded World Partition actors).',
    inputSchema: {
      type: 'object',
      properties: {
        class_path: { type: 'string', description: 'Class path filter, e.g. "/Script/Engine.StaticMeshActor".' },
        label_contains: { type: 'string', description: 'Substring match on the actor label.' },
        tag: { type: 'string', description: 'Actor tag to match.' },
        folder_path: { type: 'string', description: 'World Outliner folder to match (includes subfolders).' },
        selected_only: { type: 'boolean', description: 'Restrict to currently selected actors.' },
        max_results: { type: 'number', description: 'Result cap (default 100, max 200).' },
      },
      additionalProperties: false,
    },
    annotations: { title: 'Find actors', readOnlyHint: true },
  },
  {
    name: 'read_logs',
    risk: 'read_only',
    description: 'Read back recent UE5MCP structured log lines from the running editor — tool calls, refusals, and errors, exactly as the plugin recorded them (read-only, capped). Use this after a refusal or an unexpected result to see precisely what the plugin reported, then self-correct instead of guessing.',
    inputSchema: {
      type: 'object',
      properties: {
        max_lines: { type: 'number', description: 'Max most-recent lines to return (default 100, max 512).' },
        contains: { type: 'string', description: 'Case-insensitive substring filter, e.g. "refused", "Rejected", or an action id.' },
      },
      additionalProperties: false,
    },
    annotations: { title: 'Read UE5MCP logs', readOnlyHint: true },
  },
  {
    name: 'get_package_status',
    risk: 'read_only',
    description: 'Report the packages a save/mutation would touch (the dirty set) plus a source-control summary (provider, enabled/available, per-package cached state) — read-only, capped. Per-package source-control state is read from the provider CACHE only; this never starts a Perforce/Git network call. Use it to see blast radius before mutating or saving.',
    inputSchema: {
      type: 'object',
      properties: {
        max_packages: { type: 'number', description: 'Max packages to return (default 100, max 500).' },
        dirty_only: { type: 'boolean', description: 'When true (default) report only dirty/unsaved packages; when false also include other loaded on-disk packages (still capped).' },
      },
      additionalProperties: false,
    },
    annotations: { title: 'Get package / source-control status', readOnlyHint: true },
  },
  {
    name: 'get_actor_properties',
    risk: 'read_only',
    description: 'List the reflected properties of an actor (or one of its components) with current values — read-only, capped. By default returns exactly the ALLOWLISTED, editable properties, i.e. precisely what set_actor_property will accept, so you never have to guess a property name and learn from a refusal. Each row reports name, cpp_type, current_value, editable, differs_from_default, allowlisted, the allowlisted type + range (if any), and the engine\'s own suggested ClampMin/ClampMax range (advisory). Inspects the first target actor; pass a component class to inspect a uniquely-resolved component instead.',
    inputSchema: {
      type: 'object',
      properties: {
        actor_paths: ACTOR_PATHS_SCHEMA,
        component: { type: 'string', description: 'Optional owning component class path to inspect instead of the actor, e.g. "/Script/Engine.PointLightComponent". Refused (ambiguous_component) if the actor has more than one of that class — pass component_name to pick one.' },
        component_name: { type: 'string', description: 'Optional component instance NAME (as reported by get_actor_components) to inspect one specific component when several share a class. Resolves the ambiguous_component case.' },
        editable_only: { type: 'boolean', description: 'When true (default) list only editor-editable properties.' },
        allowlisted_only: { type: 'boolean', description: 'When true (default) list only properties on the plugin PropertyAllowlist — exactly the set_actor_property-writable surface.' },
        max_properties: { type: 'number', description: 'Max properties to return (default 50, max 500).' },
      },
      required: ['actor_paths'],
      additionalProperties: false,
    },
    annotations: { title: 'Get actor properties', readOnlyHint: true },
  },
  {
    name: 'get_actor_components',
    risk: 'read_only',
    description: 'List the components of an actor — read-only, capped. Each row reports name, class_path, creation_method (native / blueprint_template / construction_script / instance), attach_parent (for scene components), component_tags, and editable_instance (whether the editor allows per-instance edits). Use this to discover the exact component name/class to target with get_actor_properties or set_actor_property. Inspects the first target actor.',
    inputSchema: {
      type: 'object',
      properties: {
        actor_paths: ACTOR_PATHS_SCHEMA,
        max_components: { type: 'number', description: 'Max components to return (default 50, max 500).' },
      },
      required: ['actor_paths'],
      additionalProperties: false,
    },
    annotations: { title: 'Get actor components', readOnlyHint: true },
  },
  {
    name: 'list_capabilities',
    risk: 'read_only',
    description: 'Return the plugin\'s LIVE capability + policy snapshot — read-only. Lists every tool (name, risk tier, params, whether it requires/accepts targets) and THIS project\'s configured policy: the spawn class/mesh allowlists, the set_actor_property PropertyAllowlist (class/property/type/range/asset-class/override — i.e. the exact writable surface), the package-write policy switch, external-approval flags, and the plan schema version. Call it first to know exactly what is possible right now instead of guessing tool names or property allowlist entries.',
    inputSchema: { type: 'object', properties: {}, additionalProperties: false },
    annotations: { title: 'List capabilities', readOnlyHint: true },
  },
  {
    name: 'preview_actions',
    risk: 'read_only',
    description: 'Have the UE5MCP plugin validate a typed action list and return its exact effect preview WITHOUT executing anything. Use before a mutating call whose effect is not obvious from its arguments, and surface the preview to the user.',
    inputSchema: {
      type: 'object',
      properties: {
        summary: { type: 'string', description: 'One-line intent of the planned actions.' },
        actions: {
          type: 'array',
          minItems: 1,
          description: 'Typed actions in plugin plan form: {tool, targets?, params?}.',
          items: {
            type: 'object',
            properties: {
              tool: { type: 'string', description: 'A UE5MCP tool name, e.g. set_actor_transform.' },
              targets: { type: 'array', items: { type: 'string' } },
              params: { type: 'object' },
            },
            required: ['tool'],
            additionalProperties: false,
          },
        },
      },
      required: ['actions'],
      additionalProperties: false,
    },
    annotations: { title: 'Preview actions (no execution)', readOnlyHint: true },
  },
  {
    name: 'select_actors',
    risk: 'low_risk',
    description: 'Set the Unreal Editor selection to exactly these actors (selection-state mutation; undoable).',
    inputSchema: {
      type: 'object',
      properties: { actor_paths: ACTOR_PATHS_SCHEMA },
      required: ['actor_paths'],
      additionalProperties: false,
    },
    annotations: { title: 'Select actors' },
  },
  {
    name: 'set_actor_folder',
    risk: 'low_risk',
    description: 'Move these actors into a named World Outliner folder (one undoable transaction).',
    inputSchema: {
      type: 'object',
      properties: {
        actor_paths: ACTOR_PATHS_SCHEMA,
        folder_path: { type: 'string', description: 'Outliner folder path, e.g. "UE5MCP/Organized".' },
      },
      required: ['actor_paths', 'folder_path'],
      additionalProperties: false,
    },
    annotations: { title: 'Set outliner folder' },
  },
  {
    name: 'set_actor_label',
    risk: 'low_risk',
    description: 'Set the World Outliner display label of these actors (one undoable transaction). Labels are display-only and need not be unique.',
    inputSchema: {
      type: 'object',
      properties: {
        actor_paths: ACTOR_PATHS_SCHEMA,
        label: { type: 'string', minLength: 1, description: 'New display label, e.g. "Hero Spawn Point".' },
      },
      required: ['actor_paths', 'label'],
      additionalProperties: false,
    },
    annotations: { title: 'Set actor label' },
  },
  {
    name: 'add_actor_tags',
    risk: 'low_risk',
    description: 'Add one or more actor tags (FName gameplay tags on AActor.Tags) to these actors (one undoable transaction). Idempotent: tags already present are left as-is.',
    inputSchema: {
      type: 'object',
      properties: {
        actor_paths: ACTOR_PATHS_SCHEMA,
        tags: { type: 'array', items: { type: 'string', minLength: 1 }, minItems: 1, description: 'Tags to add, e.g. ["Rock", "Cleanup"].' },
      },
      required: ['actor_paths', 'tags'],
      additionalProperties: false,
    },
    annotations: { title: 'Add actor tags' },
  },
  {
    name: 'remove_actor_tags',
    risk: 'low_risk',
    description: 'Remove one or more actor tags from these actors (one undoable transaction). Idempotent: tags not present are ignored.',
    inputSchema: {
      type: 'object',
      properties: {
        actor_paths: ACTOR_PATHS_SCHEMA,
        tags: { type: 'array', items: { type: 'string', minLength: 1 }, minItems: 1, description: 'Tags to remove, e.g. ["Cleanup"].' },
      },
      required: ['actor_paths', 'tags'],
      additionalProperties: false,
    },
    annotations: { title: 'Remove actor tags' },
  },
  {
    name: 'set_actor_property',
    risk: 'low_risk',
    description: 'Set an ALLOWLISTED property on these actors (or one of their components) to a typed value (one undoable transaction). Only (class, property, type) tuples on the plugin\'s PropertyAllowlist are writable — there is no arbitrary property write; anything else is refused, and the refusal lists what is allowed. Supports dotted struct-member paths (e.g. "PostProcessSettings.BloomIntensity"), and the allowlisted type may be float/int, bool, vector [x,y,z], color [r,g,b,a], name, enum (value passed as its string name, e.g. "Lumens"), or asset (value passed as an asset path, class-constrained). Some entries also flip a paired override flag automatically. The value type must match the allowlisted type.',
    inputSchema: {
      type: 'object',
      properties: {
        actor_paths: ACTOR_PATHS_SCHEMA,
        property: { type: 'string', minLength: 1, description: 'Reflected property name, or dotted struct-member path, e.g. "Intensity", "IntensityUnits", "StaticMesh", or "PostProcessSettings.BloomIntensity".' },
        value: {
          description: 'Value matching the allowlisted type: number (float/int), boolean, [x,y,z] (vector), [r,g,b,a] linear 0..1 (color), or a string for name / enum value name / asset path.',
          type: ['number', 'boolean', 'string', 'array'],
        },
        component: { type: 'string', description: 'Optional owning component class path, e.g. "/Script/Engine.PointLightComponent". Omit to let the plugin resolve the unique matching owner (actor or component) from the allowlist.' },
        component_name: { type: 'string', description: 'Optional component instance NAME (as reported by get_actor_components) to target one specific component when an actor has several of the same class. Instance selector only — it never widens policy; the class is still allowlist-gated. Use it to resolve an "ambiguous owner" refusal.' },
      },
      required: ['actor_paths', 'property', 'value'],
      additionalProperties: false,
    },
    annotations: { title: 'Set allowlisted actor property' },
  },
  {
    name: 'set_actor_transform',
    risk: 'low_risk',
    description: 'Set absolute transform components on these actors; omitted components stay unchanged (one undoable transaction). Rotation is Euler degrees [roll, pitch, yaw].',
    inputSchema: {
      type: 'object',
      properties: {
        actor_paths: ACTOR_PATHS_SCHEMA,
        location: VEC3('Absolute world location [x, y, z].'),
        rotation: VEC3('Absolute rotation as Euler degrees [roll, pitch, yaw].'),
        scale: VEC3('Absolute scale [x, y, z].'),
      },
      required: ['actor_paths'],
      additionalProperties: false,
    },
    annotations: { title: 'Set actor transform' },
  },
  {
    name: 'duplicate_actor_with_offset',
    risk: 'low_risk',
    description: 'Duplicate each of these actors once, placing the copy at source location + offset (one undoable transaction). Returns the new actor paths.',
    inputSchema: {
      type: 'object',
      properties: {
        actor_paths: ACTOR_PATHS_SCHEMA,
        offset: VEC3('World-space offset [x, y, z] applied to every copy.'),
      },
      required: ['actor_paths', 'offset'],
      additionalProperties: false,
    },
    annotations: { title: 'Duplicate actors with offset' },
  },
  {
    name: 'spawn_actor_from_class',
    risk: 'low_risk',
    description: 'Spawn instances of an ALLOWLISTED actor class at the given transforms (one undoable transaction). Default allowlist: /Script/Engine.StaticMeshActor, PointLight, CameraActor. For StaticMeshActor, static_mesh may name an allowlisted basic shape (e.g. /Engine/BasicShapes/Cube.Cube). Returns the new actor paths.',
    inputSchema: {
      type: 'object',
      properties: {
        class_path: { type: 'string', description: 'Allowlisted class path, e.g. "/Script/Engine.StaticMeshActor".' },
        transforms: {
          type: 'array',
          minItems: 1,
          maxItems: 25,
          description: 'One entry per instance to spawn.',
          items: {
            type: 'object',
            properties: {
              location: VEC3('Spawn location [x, y, z].'),
              rotation: VEC3('Optional rotation as Euler degrees [roll, pitch, yaw].'),
              scale: VEC3('Optional scale [x, y, z].'),
            },
            required: ['location'],
            additionalProperties: false,
          },
        },
        static_mesh: { type: 'string', description: 'Optional allowlisted static-mesh asset path (StaticMeshActor only).' },
        label_base: { type: 'string', description: 'Optional label prefix; instances become "<base>_1", "<base>_2", …' },
      },
      required: ['class_path', 'transforms'],
      additionalProperties: false,
    },
    annotations: { title: 'Spawn allowlisted actors' },
  },
  {
    name: 'check_out_package',
    risk: 'low_risk',
    description: 'Check a package out of source control so a mutation blocked by `package_not_writable` can proceed. This is the ONLY tool that issues a real source-control write (a network operation), and it is NOT editor-undoable — revert via source control. Off unless the project enables it; even then the plugin refuses if source control is unavailable, the package is not controlled, or it is checked out by another user. Pass the long package name reported by get_package_status / list_capabilities / the package_not_writable refusal.',
    inputSchema: {
      type: 'object',
      properties: {
        package_name: { type: 'string', minLength: 1, description: 'Long package name to check out, e.g. "/Game/Maps/Demo".' },
      },
      required: ['package_name'],
      additionalProperties: false,
    },
    annotations: { title: 'Check out package (source-control write)' },
  },
  {
    name: 'delete_actor',
    risk: 'destructive',
    description: 'PERMANENTLY delete these actors from the level (DESTRUCTIVE; reversible only via editor Undo). Keep this tool on the permission "ask" list so it always prompts.',
    inputSchema: {
      type: 'object',
      properties: { actor_paths: ACTOR_PATHS_SCHEMA },
      required: ['actor_paths'],
      additionalProperties: false,
    },
    annotations: { title: 'Delete actors (destructive)', destructiveHint: true },
  },
];

// ---------------------------------------------------------------------------
// Bridge HTTP helpers.
// ---------------------------------------------------------------------------

async function bridgeFetch(path, options = {}, timeoutMs = 30000) {
  let response;
  try {
    response = await fetch(`${BRIDGE}${path}`, {
      ...options,
      headers: { ...BRIDGE_HEADERS, ...(options.headers ?? {}) },
      signal: AbortSignal.timeout(timeoutMs),
    });
  } catch (error) {
    // A timeout is NOT "editor not running": the request may have reached the
    // bridge and (for a write) may still execute on the game thread. Surface that
    // distinctly so the caller can warn against blind retries.
    if (error.name === 'TimeoutError') {
      throw new BridgeTimeoutError(
        `UE5MCP bridge did not respond within ${timeoutMs}ms (the editor game thread may be busy). ` +
        'The request MAY still be applied editor-side.');
    }
    throw new BridgeUnreachableError(
      `UE5MCP bridge is not reachable at ${BRIDGE} (${error.cause?.code ?? error.name}). ` +
      'Is the Unreal Editor running with the UE5MCP bridge enabled (project setting bEnableBridge or console "UE5MCP.Bridge.Start")?');
  }
  const text = await response.text();
  let body = null;
  try { body = JSON.parse(text); } catch { /* non-JSON error body */ }
  return { status: response.status, body, text };
}

class BridgeUnreachableError extends Error {}
class BridgeTimeoutError extends Error {}
class UserDeclinedError extends Error {}

async function getContext() {
  const { status, body, text } = await bridgeFetch('/context', {}, 10000);
  if (status !== 200 || !body) {
    throw new Error(`UE5MCP /context failed (HTTP ${status}): ${text}`);
  }
  return body;
}

function fingerprintFromContext(context) {
  return {
    scene: context.world ?? '',
    selected_object_paths: (context.selected_actors ?? []).map((actor) => actor.path),
  };
}

let actionCounter = 0;

function buildPlan(summary, actions, { mode, secondConfirmation = false } = {}) {
  const plan = {
    schema_version: 1,
    summary,
    requires_approval: actions.some((action) => action.risk !== 'read_only'),
    actions,
  };
  if (mode) plan.mode = mode;
  if (secondConfirmation) plan.requires_second_confirmation = true;
  return plan;
}

function makeAction(tool, risk, targets = [], params = {}) {
  return { id: `${tool}-${++actionCounter}`, tool, risk, targets, params };
}

// Map an MCP preview_actions entry onto a typed plugin action (risk comes from
// the server-side registry; we mirror it here so the plan declares it honestly).
const PLUGIN_TOOL_RISK = {
  get_selection_context: 'read_only',
  find_actors: 'read_only',
  read_logs: 'read_only',
  get_package_status: 'read_only',
  get_actor_properties: 'read_only',
  get_actor_components: 'read_only',
  list_capabilities: 'read_only',
  select_actors: 'low_risk',
  set_actor_folder: 'low_risk',
  set_actor_label: 'low_risk',
  add_actor_tags: 'low_risk',
  remove_actor_tags: 'low_risk',
  set_actor_property: 'low_risk',
  set_actor_transform: 'low_risk',
  duplicate_actor_with_offset: 'low_risk',
  spawn_actor_from_class: 'low_risk',
  check_out_package: 'low_risk',
  delete_actor: 'destructive',
};

function formatRecord(record) {
  const out = {
    status: record.status,
    plan_id: record.plan_id,
    approval_mode: record.approval_mode,
  };
  // Bridge refusals use a different shape ({error, message}); plan records carry a
  // refusal_code. Copy all of them so a failure never serializes to a bare "{}".
  if (record.error) out.error = record.error;
  if (record.message) out.message = record.message;
  if (record.refusal_code) out.refusal_code = record.refusal_code;
  if (record.problems) out.problems = record.problems;
  if (record.preview) out.preview = record.preview.map((row) => row.preview_text);
  if (record.result) {
    out.executed = record.result.executed;
    out.action_results = record.result.action_results;
  }
  return JSON.stringify(out, null, 2);
}

function textResult(text, isError = false) {
  return { content: [{ type: 'text', text }], isError: isError || undefined };
}

async function pollPendingPlan(planId) {
  const deadline = Date.now() + PENDING_TIMEOUT_MS;
  while (Date.now() < deadline) {
    await new Promise((resolve) => setTimeout(resolve, 1000));
    const { status, body } = await bridgeFetch(`/plan/${planId}`, {}, 10000);
    if (status === 200 && body && body.status !== 'pending_approval') {
      return body;
    }
  }
  return null;
}

// ---------------------------------------------------------------------------
// Elicitation: in-session confirm-with-preview for mutating calls.
// ---------------------------------------------------------------------------

let clientSupportsElicitation = false;

function elicitationEnabled() {
  if (['0', 'off', 'false', 'no'].includes(ELICIT_MODE)) return false;
  if (['1', 'on', 'true', 'yes', 'force'].includes(ELICIT_MODE)) return true;
  return clientSupportsElicitation; // auto
}

async function confirmWithPreview(planForPreview) {
  // Ask the PLUGIN for the typed preview — the effect description comes from the
  // enforcement boundary, not from this process.
  const { status, body } = await bridgeFetch('/plan', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ ...planForPreview, mode: 'preview' }),
  });
  if (status !== 200 || !body) {
    // Preview refused (invalid plan etc.) — surface it; the execute call would
    // fail identically, so fail fast with the plugin's reasons.
    throw new Error(`UE5MCP preview refused (HTTP ${status}): ${JSON.stringify(body?.problems ?? body)}`);
  }

  const previewLines = (body.preview ?? []).map((row) => row.preview_text).join('\n');
  const response = await sendServerRequest('elicitation/create', {
    message: `UE5MCP will apply in the live Unreal Editor:\n\n${previewLines}\n\nApply now?`,
    requestedSchema: {
      type: 'object',
      properties: {
        confirm: { type: 'boolean', title: 'Apply these editor actions', description: 'true applies; false cancels.' },
      },
      required: ['confirm'],
    },
  });
  if (response?.action !== 'accept' || response?.content?.confirm !== true) {
    throw new UserDeclinedError('User declined the in-session preview confirmation; nothing was executed.');
  }
}

// ---------------------------------------------------------------------------
// Tool handlers.
// ---------------------------------------------------------------------------

async function runReadOnlyPlan(summary, action) {
  const { status, body, text } = await bridgeFetch('/plan', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(buildPlan(summary, [action])),
  });
  if (!body) return textResult(`UE5MCP bridge returned HTTP ${status}: ${text}`, true);
  return textResult(formatRecord(body), status !== 200);
}

async function runMutatingPlan(summary, actions, { secondConfirmation = false } = {}) {
  const context = await getContext();
  const basePlan = {
    ...buildPlan(summary, actions, { secondConfirmation }),
    context_fingerprint: fingerprintFromContext(context),
  };

  if (elicitationEnabled()) {
    await confirmWithPreview(basePlan);
    // Selection may have drifted while the human read the preview; re-fingerprint
    // so the plugin's stale-context guard sees what is true NOW.
    basePlan.context_fingerprint = fingerprintFromContext(await getContext());
  }

  const { status, body, text } = await bridgeFetch('/plan', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ ...basePlan, mode: 'execute_external' }),
  });
  if (!body) return textResult(`UE5MCP bridge returned HTTP ${status}: ${text}`, true);

  if (status === 403) {
    if (body.error === 'external_approval_disabled') {
      return textResult(
        'UE5MCP refused: external-session approval is disabled. Enable "Allow external-session approval" in ' +
        'Project Settings > Plugins > UE5MCP (bAllowExternalSessionApproval) in the running editor, then retry.', true);
    }
    return textResult(formatRecord(body), true);
  }
  if (status === 202 && body.plan_id) {
    // Destructive belt-and-suspenders: an ADDITIONAL in-editor confirm is required.
    const finalRecord = await pollPendingPlan(body.plan_id);
    if (!finalRecord) {
      return textResult(`UE5MCP plan ${body.plan_id} is still awaiting the in-editor confirmation (timed out polling). Ask the user to Approve or Clear it in the UE5 Copilot panel.`, true);
    }
    if (finalRecord.status === 'superseded') {
      // The user cleared/declined the plan in the editor panel — a clean refusal,
      // NOT an error to retry against.
      return textResult(`UE5MCP plan ${body.plan_id} was declined in the editor (the user did not confirm the destructive action). Nothing was changed.`);
    }
    return textResult(formatRecord(finalRecord), finalRecord.status !== 'executed');
  }
  return textResult(formatRecord(body), status !== 200);
}

const HANDLERS = {
  async get_selection() {
    return runReadOnlyPlan('Read the current editor selection.',
      makeAction('get_selection_context', 'read_only'));
  },

  async find_actors(args = {}) {
    const params = {};
    for (const key of ['class_path', 'label_contains', 'tag', 'folder_path', 'selected_only', 'max_results']) {
      if (args[key] !== undefined) params[key] = args[key];
    }
    return runReadOnlyPlan('Search loaded editor actors.', makeAction('find_actors', 'read_only', [], params));
  },

  async read_logs(args = {}) {
    const params = {};
    for (const key of ['max_lines', 'contains']) {
      if (args[key] !== undefined) params[key] = args[key];
    }
    return runReadOnlyPlan('Read recent UE5MCP log lines.', makeAction('read_logs', 'read_only', [], params));
  },

  async get_package_status(args = {}) {
    const params = {};
    for (const key of ['max_packages', 'dirty_only']) {
      if (args[key] !== undefined) params[key] = args[key];
    }
    return runReadOnlyPlan('Report dirty packages and source-control status.',
      makeAction('get_package_status', 'read_only', [], params));
  },

  async get_actor_properties(args) {
    const params = {};
    for (const key of ['component', 'component_name', 'editable_only', 'allowlisted_only', 'max_properties']) {
      if (args[key] !== undefined) params[key] = args[key];
    }
    return runReadOnlyPlan('List reflected properties of an actor.',
      makeAction('get_actor_properties', 'read_only', args.actor_paths, params));
  },

  async get_actor_components(args) {
    const params = {};
    for (const key of ['max_components']) {
      if (args[key] !== undefined) params[key] = args[key];
    }
    return runReadOnlyPlan('List the components of an actor.',
      makeAction('get_actor_components', 'read_only', args.actor_paths, params));
  },

  async list_capabilities() {
    return runReadOnlyPlan('Report the live tool registry and configured policy.',
      makeAction('list_capabilities', 'read_only'));
  },

  async preview_actions(args) {
    const actions = args.actions.map((entry) => {
      const risk = PLUGIN_TOOL_RISK[entry.tool] ?? 'low_risk';
      return makeAction(entry.tool, risk, entry.targets ?? [], entry.params ?? {});
    });
    const context = await getContext();
    const plan = {
      ...buildPlan(args.summary ?? 'Preview requested actions.', actions, {
        mode: 'preview',
        secondConfirmation: actions.some((action) => action.risk === 'destructive'),
      }),
      context_fingerprint: fingerprintFromContext(context),
    };
    const { status, body, text } = await bridgeFetch('/plan', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(plan),
    });
    if (!body) return textResult(`UE5MCP bridge returned HTTP ${status}: ${text}`, true);
    return textResult(formatRecord(body), status !== 200);
  },

  async select_actors(args) {
    return runMutatingPlan('Set the editor selection.',
      [makeAction('select_actors', 'low_risk', args.actor_paths)]);
  },

  async set_actor_folder(args) {
    return runMutatingPlan(`Move ${args.actor_paths.length} actor(s) to outliner folder '${args.folder_path}'.`,
      [makeAction('set_actor_folder', 'low_risk', args.actor_paths, { folder_path: args.folder_path })]);
  },

  async set_actor_label(args) {
    return runMutatingPlan(`Set the label of ${args.actor_paths.length} actor(s) to '${args.label}'.`,
      [makeAction('set_actor_label', 'low_risk', args.actor_paths, { label: args.label })]);
  },

  async add_actor_tags(args) {
    return runMutatingPlan(`Add tag(s) [${args.tags}] to ${args.actor_paths.length} actor(s).`,
      [makeAction('add_actor_tags', 'low_risk', args.actor_paths, { tags: args.tags })]);
  },

  async remove_actor_tags(args) {
    return runMutatingPlan(`Remove tag(s) [${args.tags}] from ${args.actor_paths.length} actor(s).`,
      [makeAction('remove_actor_tags', 'low_risk', args.actor_paths, { tags: args.tags })]);
  },

  async set_actor_property(args) {
    const params = { property: args.property, value: args.value };
    if (args.component !== undefined) params.component = args.component;
    if (args.component_name !== undefined) params.component_name = args.component_name;
    return runMutatingPlan(`Set property '${args.property}' on ${args.actor_paths.length} actor(s).`,
      [makeAction('set_actor_property', 'low_risk', args.actor_paths, params)]);
  },

  async set_actor_transform(args) {
    const params = {};
    for (const key of ['location', 'rotation', 'scale']) {
      if (args[key] !== undefined) params[key] = args[key];
    }
    return runMutatingPlan(`Set transform components on ${args.actor_paths.length} actor(s).`,
      [makeAction('set_actor_transform', 'low_risk', args.actor_paths, params)]);
  },

  async duplicate_actor_with_offset(args) {
    return runMutatingPlan(`Duplicate ${args.actor_paths.length} actor(s) with offset [${args.offset}].`,
      [makeAction('duplicate_actor_with_offset', 'low_risk', args.actor_paths, { offset: args.offset })]);
  },

  async spawn_actor_from_class(args) {
    const params = { class_path: args.class_path, transforms: args.transforms };
    if (args.static_mesh !== undefined) params.static_mesh = args.static_mesh;
    if (args.label_base !== undefined) params.label_base = args.label_base;
    return runMutatingPlan(`Spawn ${args.transforms.length} instance(s) of ${args.class_path}.`,
      [makeAction('spawn_actor_from_class', 'low_risk', [], params)]);
  },

  async delete_actor(args) {
    return runMutatingPlan(`PERMANENTLY delete ${args.actor_paths.length} actor(s).`,
      [makeAction('delete_actor', 'destructive', args.actor_paths)],
      { secondConfirmation: true });
  },

  async check_out_package(args) {
    return runMutatingPlan(`Check out package '${args.package_name}' from source control.`,
      [makeAction('check_out_package', 'low_risk', [], { package_name: args.package_name })]);
  },
};

// ---------------------------------------------------------------------------
// JSON-RPC over newline-delimited stdio.
// ---------------------------------------------------------------------------

const send = (message) => process.stdout.write(`${JSON.stringify(message)}\n`);

let serverRequestCounter = 0;
const pendingServerRequests = new Map();

function sendServerRequest(method, params) {
  const id = `ue5mcp-${++serverRequestCounter}`;
  return new Promise((resolve, reject) => {
    pendingServerRequests.set(id, { resolve, reject });
    send({ jsonrpc: '2.0', id, method, params });
    // The human may take their time on an elicitation; cap at 10 minutes.
    setTimeout(() => {
      if (pendingServerRequests.delete(id)) {
        reject(new UserDeclinedError('Timed out waiting for the in-session confirmation.'));
      }
    }, 600000).unref();
  });
}

async function handleRequest(message) {
  const { id, method, params } = message;
  try {
    switch (method) {
      case 'initialize': {
        clientSupportsElicitation = Boolean(params?.capabilities?.elicitation);
        const requested = params?.protocolVersion;
        const protocolVersion = SUPPORTED_PROTOCOL_VERSIONS.includes(requested)
          ? requested : SUPPORTED_PROTOCOL_VERSIONS[0];
        log(`initialize: client=${params?.clientInfo?.name ?? 'unknown'} protocol=${protocolVersion} elicitation=${clientSupportsElicitation}`);
        return send({
          jsonrpc: '2.0', id,
          result: {
            protocolVersion,
            capabilities: { tools: {} },
            serverInfo: { name: 'ue5mcp', version: '0.6.0' },
            instructions:
              'UE5MCP drives a LIVE Unreal Editor through typed, policy-checked, undoable tool calls. ' +
              'Read tools (get_selection, find_actors, read_logs, get_package_status, preview_actions) are safe and free. Mutating tools are ' +
              'approved by the user via the tool-permission prompt — make every call self-describing, and for ' +
              'non-obvious effects call preview_actions first and surface the preview. All mutations are ' +
              'reversible with editor Undo (one step per call). Actor targets must be exact paths from ' +
              'get_selection/find_actors/spawn results. After a refusal or surprising result, call read_logs ' +
              "to read the plugin's own structured reasons before retrying.",
          },
        });
      }
      case 'tools/list':
        return send({
          jsonrpc: '2.0', id,
          result: {
            tools: TOOLS.map(({ name, description, inputSchema, annotations }) => ({
              name, description, inputSchema, annotations,
            })),
          },
        });
      case 'tools/call': {
        const handler = HANDLERS[params?.name];
        if (!handler) {
          return send({ jsonrpc: '2.0', id, error: { code: -32602, message: `Unknown tool: ${params?.name}` } });
        }
        let result;
        try {
          result = await handler(params?.arguments ?? {});
        } catch (error) {
          if (error instanceof UserDeclinedError) {
            result = textResult(error.message);
          } else if (error instanceof BridgeTimeoutError) {
            // Do NOT present a timeout as a clean failure: a write may have applied.
            result = textResult(
              `${error.message} Do NOT blindly retry a mutation — first call get_selection or find_actors ` +
              'to check whether it already took effect, then decide.', true);
          } else if (error instanceof BridgeUnreachableError) {
            result = textResult(error.message, true);
          } else {
            result = textResult(`UE5MCP tool failed: ${error.message}`, true);
          }
        }
        return send({ jsonrpc: '2.0', id, result });
      }
      case 'ping':
        return send({ jsonrpc: '2.0', id, result: {} });
      default:
        return send({ jsonrpc: '2.0', id, error: { code: -32601, message: `Method not found: ${method}` } });
    }
  } catch (error) {
    log(`request ${method} failed:`, error);
    return send({ jsonrpc: '2.0', id, error: { code: -32603, message: String(error?.message ?? error) } });
  }
}

function handleLine(line) {
  let message;
  try {
    message = JSON.parse(line);
  } catch {
    log('dropped non-JSON line');
    return;
  }
  if (message.method !== undefined && message.id !== undefined) {
    void handleRequest(message);
  } else if (message.method !== undefined) {
    // Notifications (initialized, cancelled, …) need no reply.
  } else if (message.id !== undefined) {
    // Response to one of OUR requests (elicitation).
    const pending = pendingServerRequests.get(message.id);
    if (pending) {
      pendingServerRequests.delete(message.id);
      if (message.error) {
        pending.reject(new UserDeclinedError(`Client rejected the request: ${message.error.message}`));
      } else {
        pending.resolve(message.result);
      }
    }
  }
}

let stdinBuffer = '';
process.stdin.setEncoding('utf8');
process.stdin.on('data', (chunk) => {
  stdinBuffer += chunk;
  let newlineIndex;
  while ((newlineIndex = stdinBuffer.indexOf('\n')) >= 0) {
    const line = stdinBuffer.slice(0, newlineIndex).trim();
    stdinBuffer = stdinBuffer.slice(newlineIndex + 1);
    if (line) handleLine(line);
  }
});
process.stdin.on('end', () => process.exit(0));

log(`ready (bridge=${BRIDGE}, elicit=${ELICIT_MODE})`);
