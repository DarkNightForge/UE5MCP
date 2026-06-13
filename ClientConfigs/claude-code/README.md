# Claude Code client config for UE5MCP

Canonical, pre-written permission + registration config so connecting Claude Code
to the live editor is turnkey — no manual permission setup. These files belong in
the root of the Unreal project your Claude Code session starts from.

## Files

- `.mcp.json` — project-scope MCP registration: runs the zero-dependency stdio
  server `MCPServer/ue5mcp-server.mjs` with Node. Goes in the project root the
  Claude Code session starts from. (First use of a project-scope server requires
  a one-time interactive approval — Claude Code's own safety, by design. Or
  register per-machine instead: `claude mcp add --transport stdio ue5mcp -- node
  <path-to>/ue5mcp-server.mjs` from that directory.)
- `settings.json` — goes in `<project>/.claude/settings.json`. Encodes the
  risk→permission mapping below.

## Risk → permission mapping (the in-session approval model)

| Tier | Tools | Permission behavior |
|---|---|---|
| ReadOnly | `mcp__ue5mcp__get_selection`, `mcp__ue5mcp__find_actors`, `mcp__ue5mcp__preview_actions` | On the `allow` list: never prompt. Observation is free. |
| LowMutation | `mcp__ue5mcp__select_actors`, `mcp__ue5mcp__set_actor_folder`, `mcp__ue5mcp__set_actor_transform`, `mcp__ue5mcp__duplicate_actor_with_offset`, `mcp__ue5mcp__spawn_actor_from_class` | Deliberately UNLISTED: Claude Code's native tool-permission prompt fires on each call — that prompt IS the human approval. The user may consciously session-allowlist a tool. |
| Destructive | `mcp__ue5mcp__delete_actor` | On the `ask` list: ALWAYS prompts. `ask` rules beat session-allows, so it can never be allowlisted. |

Every mutating tool is self-describing (name + arguments fully convey the effect),
so a single permission prompt is meaningful. For composite or non-obvious effects,
the agent should call `preview_actions` first (free) and surface the plugin-generated
typed preview, then make the mutating call — whose prompt is the approval.

When the client supports MCP elicitation (Claude Code does), the server additionally
shows the plugin-generated preview as an in-session confirm on each mutating call.
Control with `UE5MCP_ELICIT` in `.mcp.json`: `auto` (default; on iff the client
advertises elicitation), `1` (force), `0` (off — rely on the permission prompt only).

## Editor-side prerequisite

The plugin enforces its own gate: `Project Settings > Plugins > UE5MCP >
Allow external-session approval` (`bAllowExternalSessionApproval`, default OFF)
must be enabled, plus the bridge (`bEnableBridge` or console `UE5MCP.Bridge.Start`).
Even then the plugin re-validates schema/policy/allowlists, blocks during PIE, runs
every mutation in one undoable transaction, and logs every action to `LogUE5MCP` —
a user-approved call that violates policy is still refused server-side.
