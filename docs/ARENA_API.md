# The Arena API — ECOWAR's real, current PARENA mod-hook surface

Phase 1 of `docs/NORTHSTAR_MAP_EDITOR.md`'s own 6-phase plan (founder, real-time: "arena apis
first we wil build ecowar ontop of them... start from a bare engine and build on top of it with
mod surface"). This doc names and documents a real, already-working pattern — every mod referenced
below already exists and (except where flagged "NOT YET WIRED") already runs live — it does not
introduce a new mechanism. Written by reading the actual `.prn` sources and their real C call
sites in `arena_game.c`, not guessed at.

## The real ABI, as it actually works today

1. A mod is a PARENA module living in `PARENA/stdlib/redgarden/*.prn` (mods inherited from/shared
   with REDGARDEN's own lineage) or `PARENA/stdlib/ecowar/*.prn` (ECOWAR-specific — real,
   already exists: `card_effect_mod.prn`). Module name convention: `redgarden/<name>-mod` or
   `ecowar/<name>-mod`.
2. It exports one or more `on-<event>` functions — plain scalar (`I32`/`Bool`) parameters and
   return type only; no structs, no `Vec`, no `F32` crossing this boundary (see "Real VS0 limits"
   below for why).
3. Two real shapes exist for the function body, both live in production:
   - **Trigger-only** (every mod before `card_effect_mod.prn`): a bare `#target {:c (inline-c
     "...")}` body that calls straight into a hand-written host C function
     (`redgarden_host_<verb>`), which does the real work (buy an item, log an event, apply a
     slow). "Host decides WHEN to call the mod; the mod IS the trigger point" — the mod itself
     contains no real branching logic. Example: `build_template_mod.prn`'s
     `on-apply-build-template-item` is one line, `(inline-c "redgarden_host_buy_build_item(...)")`.
   - **Real decision logic** (`ecowar/card_effect_mod.prn`, ECOWAR's own first real case of
     this): ordinary PARENA control flow (`if`/`or`/arithmetic) computing a real answer — no
     `#target` escape at all for the logic itself. `on-ecowar-resolve-card-magnitude` branches
     over all 16 real card ids (grounded in `TYLER/multiverse_heroes.md`'s own MUNDANE/MYTHIC
     tagging) and returns a real, tier-scaled magnitude. The actual STAT MUTATION that magnitude
     feeds into (`apply_damage`/`arena_apply_slow`/etc — float math, struct state) still stays in
     host C, called by name from `arena_game.c` (`ecowar_resolve_card_effect`) — same "mod does
     what VS0 can, host does the rest" split, just pushed further because this specific decision
     (I32-only tier lookup) genuinely fits within VS0's current real capability.
4. **Build/wiring, all real, all manual today** — no auto-discovery:
   - `parena build stdlib/<redgarden|ecowar>/<name>.prn -o packages/simulation/<name>_mod.c`
     generates the C. The generated `.c` is **committed** into ECOWAR's own repo (matches this
     whole monorepo's own "generated code committed" precedent, e.g. PARENA's own
     `tests/test_json_gen.c`) — not regenerated at build time.
   - The generated file's path is added to `scripts/build.sh`'s own hardcoded `SRCS` list (both
     the client and server build — the same list appears twice; every real mod file so far is in
     both).
   - `arena_game.c` calls the PARENA-emitted function **directly by its mangled C name**
     (hyphens → underscores: `on-tree-passive-strike` → `on_tree_passive_strike`) at the exact
     point the real gameplay event fires — a real, ordinary C function call, not a registered
     callback/dispatch table.

## Real, current mod inventory (verified by reading the actual call sites)

| Mod (`.prn`) | Hook function | Real host C call site | Status |
|---|---|---|---|
| `redgarden/bloodflower_mod.prn` | `on-moon-zenith` | `arena_game.c` map-center timer | **LIVE** |
| `redgarden/tree_passive_mod.prn` | `on-tree-passive-strike` | jungle-tree auto-attack | **LIVE** |
| `redgarden/build_template_mod.prn` | `on-apply-build-template-item` | shop auto-buy sequence | **LIVE** |
| `redgarden/item_curriculum_mod.prn` | `on-generate-counter-item` | counter-item generation | **LIVE** |
| `redgarden/duck_smoke_bomb_mod.prn` | `on-duck-smoke-bomb-cast` | Duck's W ability | **LIVE** |
| `redgarden/abraham_fireball_mod.prn` | `on-abraham-fireball-cast` | Abraham's ground-target ability | **LIVE** |
| `ecowar/card_effect_mod.prn` | `on-ecowar-resolve-card-magnitude` | `ecowar_resolve_card_effect` | **DEFINED, NOT YET CALLED** — `ecowar_resolve_card_effect` itself has zero real callers anywhere in the codebase (confirmed by grep), the same "cards exist, nothing triggers them in a live match" gap this session's own live playtest already found. Closing this — a real input path (client UI + network packet + server dispatch calling `ecowar_resolve_card_effect`) — is Phase 2's own first real task. |
| `redgarden/combat_log_mod.prn` | `on-hero-kill`/`on-item-purchase`/`on-node-capture`/`on-node-uncapture`/`on-king-spawn` | *(none yet)* | **NOT YET WIRED** — real, complete PARENA source (recovered and committed this session, PARENA commit `4709fdd`), but its own host-side C companions (`redgarden_host_log_kill` etc.) don't exist in `arena_game.c` yet, no generated `.c` has been committed into ECOWAR, and nothing calls it. Real, separate follow-up, not attempted here. |

## Real VS0 limits that shape every mod above (why the ABI looks like this)

- No `F32` parameters, no `Vec`/array parameters, no struct-by-value across the `#target`
  boundary — confirmed directly in `card_effect_mod.prn`'s own header comment, matching this
  whole session's own repeated real findings about VS0's current, narrow emitter (no tuple
  returns, no closures, no struct field mutation). This is *why* every mod above is scalar-in/
  scalar-out and why the real stat mutation always stays in C — not a design preference, a real,
  current compiler ceiling.
- A mod can do real PARENA-side branching/arithmetic (`card_effect_mod.prn` proves it) as long as
  the inputs/outputs stay scalar — the "trigger-only vs. real decision logic" split above is a
  spectrum, not two hard categories; expect more mods to move rightward (more real logic) as VS0
  itself grows past today's limits, not a permanent architectural wall.

## What Phase 2 (ECOWAR card/RTS mods) needs from this doc

- The real ABI pattern above — a new card/RTS mod should follow the exact same "module in
  `stdlib/ecowar/`, `on-<event>` export, generate + commit the `.c`, add to `scripts/build.sh`,
  call by name from `arena_game.c`" shape, not invent a new mechanism.
- The one real, confirmed gap to close first: wire a real input path to
  `ecowar_resolve_card_effect` so a player can actually trigger a card in a live match.

## Related

- `docs/NORTHSTAR_MAP_EDITOR.md` — the parent epic this doc is Phase 1 of.
- `PARENA/stdlib/redgarden/*.prn`, `PARENA/stdlib/ecowar/*.prn` — the real mod sources this doc
  documents.
- `PARENA/NORTHSTAR.md` — VS0's own real, current Definition of Done and limits.
