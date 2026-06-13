# UE5MCP roadmap: the capability ladder

Capabilities ship one trust level at a time. A capability does not advance because it would demo well; it advances when it passes the gate rubric below. The order is the point: governed growth, not breadth-first.

## Gate rubric

Every action family must answer yes to all of these before it ships at any level:

1. Are the exact targets previewable before execution (count and identity)?
2. Is the action pure typed data — no model-generated code, no scripts, no process launches?
3. Does it execute only through native Unreal Editor APIs?
4. Does an approved batch apply atomically, as one `FScopedTransaction`?
5. Does one `Ctrl+Z` revert the whole batch (verified by hand, not assumed)?
6. Is the worst case bounded (target caps, class/mesh allowlists) and tolerable?
7. Does every outcome — success, failure, refusal — produce a structured `LogUE5MCP` entry?
8. Are the refusal paths (stale context, play mode, invalid plan) tested for it?

## Levels

### L0 — Observe (read-only)
Bounded context: level/world identity, selection, capped loaded-actor summaries with explicit warnings. No approval needed; no transaction opened. Tools: `get_selection`, `find_actors`, `preview_actions`.
*Status: **shipped & verified.***

### L1 — Low-risk visible mutations
Scene organization: move actors into a World Outliner folder; select actors. Everything previewed, approved at the native MCP prompt, applied as one undoable transaction. Tools: `select_actors`, `set_actor_folder`. (Next: rename labels, add/remove tags with count caps.)
*Status: **shipped & verified** (happy path, stale-selection refusal, single-step undo/redo — live and in the automation suite).*

### L2 — Spatial, layout & spawn operations
Bounded transform offsets, duplication with hard count caps, and class+mesh-allowlisted spawn (≤25 instances/action). Higher blast radius, same loop. Tools: `set_actor_transform`, `duplicate_actor_with_offset`, `spawn_actor_from_class`. Destructive `delete_actor` is its own always-prompt tier: undoable, never allowlistable.
*Status: **shipped & verified** — per-actor before/after previews, one undoable batch, no-op / unresolvable-target / play-mode refusals, and the full pend → approve → apply lifecycle, all covered live and by the 56-test suite. Alignment: direction.*

### L3 — Allowlisted property edits
Editing specific, individually allowlisted actor/component properties — never arbitrary property writes.
*Status: direction.*

### L4 — Asset operations
Asset-level changes (rename/move/delete, source-control lifecycle) are explicitly deferred: several lack clean single-step undo semantics, which fails gate 5 until solved.
*Status: deferred by design.*

### L5 — External agent clients (MCP)
Agents connect over MCP (loopback HTTP bridge) and speak the same typed-plan format, terminating at the same validate → preview → approval → transaction → log path inside the plugin. The transport can never be a second executor.
*Status: **shipped & verified** — an external Claude Code client drives the live editor end to end; observation executes immediately, mutations are approved inline at the native permission prompt, the cannot-execute boundary is statically asserted and test-covered, and the loopback surface is CSRF/DNS-rebind hardened. Adapters for other MCP clients (Cursor, Codex): direction.*

## The breadth question

The open frontier is breadth **without** an open `exec`. The plan: the model only ever emits typed-plan JSON; breadth comes from a capability registry built via (a) reflection-driven codegen for the native-reflectable surface and (b) vetted parameterized recipes (typed params bound as data, never string-concatenated) for the Python-only surface — all funneling through the same pipeline above. Plus discovery (`list_capabilities`), domain skills packs, and a `read_logs` readback loop.

## Anti-goals at every level

- No autonomy escalation as a feature. The ladder climbs capability, not unsupervised authority.
- No breadth-first bridge that exposes every editor function to agents at once. **Breadth without gates is the competing failure mode.**
- No unlogged mutation, ever.
