# Validation checklist

Claims stay honest by mapping to checks. The plugin's behavior is verified by a headless in-editor automation suite; the typed-plan contract is additionally checked by repository scripts. Anything unchecked is a gap to close, not a footnote to hide.

## Automation suite (in-editor, headless)

`Source/UE5MCP/Private/Tests/` carries a headless automation suite — **67 tests, no display required** — run with:

```bash
<UE-ROOT>/Engine/Binaries/<Platform>/UnrealEditor-Cmd <YOUR-PROJECT>/YourProject.uproject \
  -ExecCmds="Automation RunTests UE5MCP;Quit" -unattended -nullrhi -nosplash \
  -ReportExportPath=/tmp/ue5mcp_report
```

It covers: the executor happy path with single-step undo/redo; every validator format rule; target-resolution round-trips; all tools (observe, find, read logs, package/source-control status, read-only property discovery, select, organize, label, tag add/remove, allowlisted property edit + non-allowlisted refusal + enum/struct-member-path/asset value kinds, transform, duplicate, allowlisted spawn, destructive delete) including bounded queries; the full approval lifecycle (empty selection, stale selection, stale world, play-mode refusal that keeps the plan approvable, plan consumption, supersede-by-panel, external-approval gating); and the MCP/bridge boundary (read-only executes immediately, mutations pend/gate, the transport cannot execute, malformed/unknown requests refused with reasons, loopback-host classification). **Last full run: 67/67 passing on UE 5.7.4 (Linux source build) — verified in-editor, including `get_actor_properties` read-only discovery (`UE5MCP.Tools.GetActorPropertiesListsAllowlistedWithValues`, incl. the `ambiguous_component` refusal), `set_actor_property`'s enum/struct-member+override/asset value kinds (`UE5MCP.Executor.SetPropertyEnumStructAssetKinds`), `get_package_status`, and the label/tag/read_logs tests.**

## Repository scripts (the typed-plan contract)

| # | Check | Command |
| --- | --- | --- |
| A1 | Valid example plans pass the spec | `python3 scripts/validate_plan.py examples/plans/organize-selection.json examples/plans/read-only-context.json examples/plans/read-logs.json examples/plans/package-status.json examples/plans/get-actor-properties.json examples/plans/set-actor-label.json examples/plans/tag-actors.json examples/plans/set-actor-property.json examples/plans/transform-selection.json examples/plans/spawn-cubes.json` |
| A2 | Invalid example plans are rejected for the documented reasons | `python3 scripts/validate_plan.py --expect-invalid examples/plans/invalid-empty-targets.json examples/plans/invalid-destructive-no-confirmation.json examples/plans/invalid-noop-transform.json examples/plans/invalid-empty-tags.json` |
| A3 | Script unit tests pass | `python3 -m unittest discover -s tests` |

## Manual checks (live editor)

Status reflects evidence on UE 5.7.4. "Evidenced" = observed in a real editor session; "(automated)" = also covered by the headless suite.

| # | Check | Status |
| --- | --- | --- |
| M1 | Copilot panel opens (Tools menu / Ctrl+Alt+U) and shows context, preview, log | evidenced |
| M2 | Context header shows correct world name + selected count; loaded count caps with a warning | evidenced (automated at the context layer) |
| M3 | Empty selection produces no approvable plan (reason shown) | evidenced (automated) |
| M4 | Preview shows exact target count and effect before any change | evidenced |
| M5 | Changing selection after preview → approval refused with `stale_context` | evidenced (live + automated) |
| M6 | Switching worlds after preview → approval refused with `stale_context` | evidenced (automated) |
| M7 | Play mode active → mutation refused with `play_mode_active` | evidenced (automated; live click-through still worth one look) |
| M8 | Approved plan applies and persists in a saved level | evidenced |
| M9 | One `Ctrl+Z` reverts the entire batch | evidenced (live + automated) |
| M10 | Redo re-applies the batch; log stays consistent | evidenced (live + automated) |
| M11 | An executed plan is consumed: re-approving without regenerating is refused | evidenced (automated) |
| M12 | Full external MCP loop: an external Claude Code agent observes, submits a typed plan, the human approves inline, the change applies, the agent reads the structured result | **evidenced (live)** — built a 9-cube organized scene through the bridge with a full `LogUE5MCP` audit trail |
| M13 | Destructive `delete_actor` always prompts (cannot be allowlisted, even via `--allowedTools`) and is reverted by one undo | **evidenced (live + automated)** |

## Not yet verified

- Epic Games Launcher **binary** builds and **Windows** (only UE 5.7.4 Linux source build verified so far).
- The MCP elicitation confirm dialog has been verified at the protocol level; eyeball the interactive form once per client.

## Documentation drift check

When a change adds/removes/renames a tool, changes a risk tier, changes validation behavior, adds a demo, or verifies a new engine/platform surface, update the living capability map at [`docs/capabilities/`](capabilities/). The capability map is the planning dashboard for current UE5MCP breadth.
