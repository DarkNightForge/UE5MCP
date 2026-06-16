# UE5MCP — governed AI agents for Unreal Engine 5

**Let an AI agent build, organize, and edit your Unreal level — where every action is approved by you, executed as one undoable step, and written to an audit log. The plugin never runs model-generated code.**

UE5MCP is an Unreal Editor plugin that exposes a small set of **typed, policy-checked tools** over [MCP](https://modelcontextprotocol.io). An agent (Claude Code today; any MCP client) plans and reasons; the plugin validates, previews, executes through native Unreal APIs, and logs. The agent is the brain — the plugin is the trusted hands, and it keeps final refusal authority.

> The design bet: most teams **cannot** let an LLM run arbitrary Python inside their editor against a shared, source-controlled project. UE5MCP makes governance a hard, enforced property instead of a prompt-time suggestion.

---

## Why this is different

Other AI-for-Unreal plugins get their breadth from a single `execute_python_code` tool: the model writes arbitrary Python that runs in-editor, unsandboxed, with approval and undo left to the model's good behavior. That's powerful and fast — and unusable anywhere a bad or hallucinated edit is expensive.

UE5MCP takes the opposite stance:

| | Arbitrary-exec plugins | **UE5MCP** |
|---|---|---|
| What the model emits | free-form code | **typed action plans (data, never code)** |
| Approval | model decides / optional | **enforced; risk-tiered; destructive can't be allowlisted around** |
| Undo | only if the model calls it | **every mutating batch = one `FScopedTransaction`; one Ctrl+Z reverts it** |
| Audit | none | **structured `LogUE5MCP` line per action, executed _or refused_** |
| Allowlists / sandbox | none | **class/mesh allowlists, PIE guards, loopback-only transport, server-side re-validation** |
| Final authority | the model | **the plugin (a user-approved call that violates policy is still refused)** |

We do **not** try to out-breadth the exec-everything approach. We win where breadth-via-exec can't go: studios with shared/source-controlled projects, enterprise, CI, and anyone liability- or review-sensitive.

---

## Status — honest accounting

- **Verified:** builds clean as a drop-in **project plugin** and passes **72/72** headless automation tests on **Unreal Engine 5.7.4** (Linux source build). Live runs with an external Claude Code client building and fully undoing an organized scene through the approval boundary.
- **Not yet verified:** Epic Games Launcher **binary** builds and **Windows**. Treat those as untested until confirmed.
- **Beta, v0.1.** 8 typed actions today (see below). Breadth is the roadmap, not the claim.

---

## Requirements

- Unreal Engine **5.7+** (tested on 5.7.4; a C++-capable install — source build, or a binary install with the platform compiler toolchain).
- **Node.js** (for the zero-dependency MCP server).
- An MCP client. **Claude Code** is wired out of the box (config under `ClientConfigs/claude-code/`).

## Quickstart

```bash
# 1. Drop the plugin into your project
cd /path/to/YourProject/Plugins
git clone https://github.com/DarkNightForge/UE5MCP UE5MCP

# 2. Let Unreal build it (regenerate project files, or accept the editor's
#    "missing modules — rebuild now?" prompt on next launch)
```

Then:

1. **Enable** the plugin (Edit → Plugins → *UE5MCP*) and restart the editor.
2. **Turn on the bridge** in Project Settings → Plugins → UE5MCP:
   - `bEnableBridge` → on (localhost-only HTTP on `127.0.0.1:30110`).
   - `bAllowExternalSessionApproval` → on, to let an external agent session approve mutations inline (default **off** — a conscious per-project opt-in).
3. **Register the MCP server** with your client. For Claude Code, copy the pre-written config from `ClientConfigs/claude-code/` (`.mcp.json` + `settings.json`) into your project, pointing `args` at `Plugins/UE5MCP/MCPServer/ue5mcp-server.mjs`.
4. **Go.** Open your project, start your agent in the project directory, and ask it to build something. Approve each mutation at the native prompt; Ctrl+Z to unwind.

Full walkthrough: [`docs/demo-live.md`](docs/demo-live.md). Architecture & safety internals: [`docs/architecture.md`](docs/architecture.md). Current breadth and next capability gaps: [`docs/capabilities/`](docs/capabilities/).

---

## The tools

Seventeen MCP tools (`mcp__ue5mcp__*`), in three risk tiers:

**Read-only** — `get_selection`, `find_actors`, `read_logs`, `get_package_status`, `get_actor_properties`, `get_actor_components`, `preview_actions`
**Low mutation** — `select_actors`, `set_actor_folder`, `set_actor_label`, `add_actor_tags`, `remove_actor_tags`, `set_actor_property` (allowlisted), `set_actor_transform`, `duplicate_actor_with_offset`, `spawn_actor_from_class` (class + mesh allowlisted)
**Destructive** — `delete_actor`

Mutating tools take a **typed action plan**; the plugin resolves targets, builds a human-readable preview (exact targets, counts, warnings), executes only on approval, wraps the batch in one transaction, and logs the result.

## How approval works

The human gate is your **MCP client's native tool-permission prompt**, inline in the session — no separate in-editor click for external calls. Risk maps to permission behavior (see `ClientConfigs/claude-code/settings.json`):

| Tier | Behavior |
|---|---|
| Read-only | allowlisted → never prompts |
| Low mutation | prompts on each call; you may session-allowlist |
| **Destructive** (`delete_actor`) | on the client `ask` list → **always prompts; cannot be allowlisted around**, even via `--allowedTools` (verified) |

The server enforces the same tiers independently: a destructive plan only executes through the external-approval gate, the bridge re-validates every request server-side, and approval is refused if the world/selection changed after the preview (stale-context refusal).

## How it works

```
Agent / MCP client (Claude Code, …)
   └─ mcp__ue5mcp__<tool>            ← THE HUMAN GATE (native permission prompt)
MCPServer/ue5mcp-server.mjs          (zero-dependency Node stdio; transport only)
   └─ typed plan JSON over 127.0.0.1:30110 (loopback only)
UE5MCP editor plugin
   └─ schema validation → target resolution → policy/allowlists → PIE/world guards
      → preview → approval → native Unreal APIs only
      → one undoable FScopedTransaction → structured LogUE5MCP audit
Unreal Editor / project
```

The MCP server holds **no editor capability** — stdio + loopback HTTP only. Transport hardening: loopback-bind enforced (including per-port listener overrides), `Origin`/`Host` checks, and a required `X-UE5MCP-Client` header to block browser CSRF / DNS-rebinding against the unauthenticated local service.

## What UE5MCP is not

- Not an autonomous agent that edits your project while you watch — every mutation is approved.
- Not a code generator: the host never executes model-written Python or C++.
- Not a chat overlay: the unit of work is a reviewable, typed action plan.

## Roadmap

The open frontier is **breadth without an open `exec`**. The plan: keep the typed-plan JSON as the only thing the model ever speaks, and grow a **capability registry** via (a) reflection-driven codegen for the native-reflectable surface and (b) vetted **parameterized recipes** (typed params bound as data, never string-concatenated) for the Python-only surface — all funneling through the same validate → preview → transaction → log → refusal pipeline. Plus discovery (`list_capabilities`), domain **skills packs**, and a `read_logs` readback loop. Contributions welcome — this is where the project grows.

The living capability map is [`docs/capabilities/`](docs/capabilities/). Update it whenever a tool, risk tier, validation rule, Unreal domain, demo, or verification scope changes.

## License

[MIT](LICENSE) © 2026 Eashan Babber.
