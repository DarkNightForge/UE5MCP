# UE5MCP live demo — external Claude Code builds a scene, you approve in-session

This runbook assumes the plugin is installed and the bridge enabled (see the
top-level README Quickstart). The only human actions in the demo are the
**native Claude Code permission prompts** (and Ctrl+Z at the end). No in-editor
Approve clicks. Paths below use `<UE-ROOT>` (your Unreal install) and
`<YOUR-PROJECT>` (your `.uproject` directory); the commands shown are for a Linux
source build.

## 1. Launch the editor (terminal 1)

```bash
cd <UE-ROOT>
Engine/Binaries/Linux/UnrealEditor <YOUR-PROJECT>/YourProject.uproject
```

The bridge auto-starts on `127.0.0.1:30110` (`bEnableBridge=True`) and
external-session approval is enabled for this project
(`bAllowExternalSessionApproval=True` in `Config/DefaultEditor.ini`). Sanity check
in the editor output log: `LogUE5MCP: Bridge listening on localhost:30110 …`.

Optional: open the UE5 Copilot panel (Tools menu → UE5 Copilot, or **Ctrl+Alt+U**) to
watch the audit log live.

## 2. Start the external agent (terminal 2)

```bash
cd <YOUR-PROJECT>
claude
```

The `ue5mcp` MCP server is already registered for this directory (local scope) and
the permission policy is pre-written in `.claude/settings.json`:
read/preview tools never prompt; every mutation prompts; `delete_actor` always
prompts and can never be allowlisted.

## 3. The demo prompt

> Using your ue5mcp tools, build a small organized scene in the live editor:
> spawn a 3x3 grid of StaticMeshActor cubes (mesh /Engine/BasicShapes/Cube.Cube,
> label_base DemoCube, 300 units apart at z=50), scale them all to [1,1,2], and
> move them into a World Outliner folder named UE5MCP/DemoGrid. Verify with
> find_actors and summarize what you built.

What you'll see:
- `get_selection` / `find_actors` run silently (allowlisted, read-only).
- Each mutating call (`spawn_actor_from_class`, `set_actor_transform`,
  `set_actor_folder`) raises Claude Code's native permission prompt with the full
  arguments — that prompt **is** the approval. With elicitation active you also get
  a confirm dialog showing the plugin-generated preview of the exact effect.
- The cubes appear in the viewport as each batch executes; the panel/output log
  records every action with an `[external-session approval]` audit line.

## 4. The undo proof

Click in the editor viewport and press **Ctrl+Z three times** (folder → scale →
spawn). The scene returns to exactly its prior state — every externally approved
batch is one standard transaction.

(Optional destructive demo: ask it to delete the cubes — `delete_actor` prompts
every time, by policy, and one further Ctrl+Z resurrects them.)

## Troubleshooting

- “bridge is not reachable” → editor not running, or run `UE5MCP.Bridge.Start` in
  the editor console.
- HTTP 403 `external_approval_disabled` → re-enable the setting in Project Settings
  → Plugins → UE5MCP.
- Elicitation dialog misbehaving → set `"UE5MCP_ELICIT": "0"` in
  `<YOUR-PROJECT>/.mcp.json` (permission prompts still gate
  everything).
- Machine rehearsal of the full loop (incl. undo): `UE5MCP.EndToEnd` automation
  test, or rerun: `Engine/Binaries/Linux/UnrealEditor-Cmd .../YourProject.uproject
  -ExecCmds="Automation RunTests UE5MCP;Quit" -unattended -nullrhi -nosplash`.
