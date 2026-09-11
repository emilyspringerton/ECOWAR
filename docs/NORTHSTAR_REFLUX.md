# REFLUX — Cross-Mod Pub/Sub, Bloodflower Hostiles, Day/Night Lighting (NORTHSTAR)

Registered as `ECOWAR-REFLUX-NORTH`. Scoping + shipped-status doc for BACKLOG.md SECTION 380.

## Founder real-time direction

"add the BLOOD FLOWER and day night rendering - we can take the graphics of the day night
rendering from OG SHANKPIT i love the way the lighting shifts from day to night in shankpit - and
the bloodflower for now just have it spawn a bunch of hostile creeps into the map at exactly the
midpoint (already a mod) have the spwning in hostiles be a mod too with cross mod pub sub
communications (THINK REDUX HOWEVER REDUX WORKS) call the mod itself REFLUX a mod that presents a
new layer of mod interface for cross mod communication - give it its own stdlib in PARENA."

Three real, separate pieces, all shipped this pass:

1. **REFLUX** — a new, real, standalone cross-mod pub/sub primitive, `PARENA/stdlib/reflux/`.
2. **Bloodflower's hostile spawn** — bloodflower_mod.prn now dispatches a real REFLUX action; a
   brand-new, separate mod subscribes and spawns real Living Map creeps.
3. **Day/night dynamic scene lighting** — porting the other real half of SHANKPIT's own
   day/night visuals this codebase had only partially ported before.

## REFLUX — real, checked-first constraint, and what's actually buildable

Same real limit `docs/NORTHSTAR_LIVING_MAP.md`'s own "Mod event model, honestly" section already
established: VS0 (PARENA's current compiler) has no function pointers or closures anywhere — a
true Redux-style callback/subscriber-list dispatch table isn't buildable at the language level
today. What's real and shipped instead, and genuinely does deliver "cross mod pub sub
communications": a single, shared, append-only action log any mod can DISPATCH into, and any
other mod can independently POLL from — the exact "shared log, not push callbacks" shape
`packages/livingmap/living_map_events.h` already proved out for the Living Map specifically,
generalized here into a real, standalone, cross-mod, cross-GAME primitive with zero dependency on
Living Map, towns, or ECOWAR.

- **`packages/simulation/reflux_runtime.h/.c`** (new): the real, generic host runtime — a
  `RefluxAction {action_type, a, b, c}` ring buffer (`REFLUX_LOG_CAPACITY` 256), near-identical
  to `living_map_events.c`'s own proven implementation. Real, honest, arena-scoped for now (one
  global log per match, alongside `arena_state`) since ECOWAR is REFLUX's only real consumer
  today — extracting it into its own standalone package once a second game wants it is a real,
  cheap, later move, not a redesign (this file already has zero `arena_game.h` dependency).
- **`PARENA/stdlib/reflux/reflux.prn`** (new, REFLUX's own real stdlib namespace, as asked): 6
  real, thin `#target`/inline-c functions — `reflux-dispatch` (a real trigger call) and 5 real
  scalar accessors (`reflux-log-size`, `reflux-action-type-at`/`-a-at`/`-b-at`/`-c-at`) — every
  one a plain FFI call into `reflux_runtime.c`'s own host functions, same call-by-name convention
  every mod in this repo already uses, applied to a shared log instead of a bespoke per-mod host
  function.
- **Real naming collision found and fixed**: the PARENA-mangled function names `reflux_dispatch`/
  `reflux_log_size` collided with the runtime's own planned public C API names. Renamed the
  runtime's own generic functions to `reflux_log_dispatch`/`reflux_log_length` — the correct fix
  (the PARENA-facing names are the ones other mods actually call), not a workaround.
- Real, honest limitation named up front, not discovered by surprise: dispatching mods never know
  or need to know who's listening; POLLING mods need the HOST to track their own "last seen"
  cursor as persistent state (PARENA itself holds no state across calls) and call the mod's own
  real decision function once per new action — see `ecowar_tick_bloodflower_hostile_spawner`
  (`arena_game.c`) for the first real, working example of this shape.

## Bloodflower's real hostile spawn — REFLUX's own first real dispatcher/subscriber pair

Founder: "for now just have it spawn a bunch of hostile creeps into the map at exactly the
midpoint (already a mod) have the spawning in hostiles be a mod too."

- `PARENA/stdlib/redgarden/bloodflower_mod.prn`'s own `on-moon-zenith` — unchanged in its
  original real behavior (still spawns the claimable Flow pickup at map center) — now ALSO calls
  `reflux_dispatch(REFLUX_ACTION_BLOODFLOWER_TRIGGERED, x, z, 0)` via the same plain inline-c FFI
  escape it already used for `redgarden_host_spawn_bloodflower`.
- **New `PARENA/stdlib/ecowar/bloodflower_hostile_spawner_mod.prn`** — REFLUX's first real
  subscriber. Real PARENA decision logic: `on-bloodflower-hostile-spawner-should-react` (a real
  I32 comparison against the shared action-type constant) and
  `on-bloodflower-hostile-spawner-creep-count` (a real, named, tunable constant — 5 — for exactly
  how big "a bunch" is). **Never imports, includes, or calls anything in bloodflower_mod.prn, and
  vice versa** — the only real connection between them is the shared REFLUX log. Either mod could
  be deleted or a second/third real subscriber added without touching the other.
- **New `living_map_bridge_spawn_hostile_creep_at_map_center`** (`living_map_bridge.h/.c`):
  spawns one real, aggressive `LivingMapCreep` at hex (0,0), owned by Living Map faction 3
  (Corruption) — its first real, live use anywhere in this codebase.
- **New `ecowar_tick_bloodflower_hostile_spawner`** (`arena_game.c`, called once per real
  `arena_update`/`arena_update_teams` tick): the real host-side polling loop, tracking its own
  persistent "last seen" cursor into the shared REFLUX log.
- Live-verified end to end: a real match driven through a full day/night cycle (`arena_update` in
  a loop, `arena_bot_enabled` disabled so the practice bot doesn't end the match early) produces
  both the original Bloodflower pickup AND 5 new real faction-3 creeps at map center, through
  REFLUX alone. 14 new tests (`tests/test_reflux.c`), full suite green (3189 assertions).

## Day/night dynamic scene lighting

Founder: "we can take the graphics of the day night rendering from OG SHANKPIT i love the way the
lighting shifts from day to night in shankpit."

Real, checked-first finding: this codebase had already ported HALF of SHANKPIT's own
`retro_lighting.c` — `arena_daynight_ambient_rgb` (the background clear-color tint) — but the
actual 3D scene light direction `apps/arena`'s shader uses (`uLightDir`) was still a hardcoded,
never-moving constant `(0.4, 0.8, 0.3)`. That's the real gap behind "I love the way the lighting
shifts": the ambient tint alone can't move shadows/highlights across a hero or the terrain.

- **New `arena_daynight_light_dir`** (`arena_game.c`/`.h`): ports the other real half of
  SHANKPIT's own dynamic lighting — `retro_sky_eval_sun_dir`'s real sun-orbit formula (already
  partially reused by `arena_daynight_ambient_rgb`'s own `sun_height`), extended into a full 3D
  direction, plus `retro_lighting.c`'s own `RETRO_LIGHTING_DYNAMIC` "sun by day, the exact
  opposite direction (the moon) by night" convention — keeps a real, moving light source above
  the horizon at every real time of day.
- `apps/arena/src/main.c` now calls this every frame instead of passing the fixed constant to
  `uLightDir`. The shader (`normalize(uLightDir)`, no separate light-color uniform) already reacts
  correctly to a moving direction alone — as the light arcs and dips toward the horizon, the real
  toon-shader band cutoffs (`dot(N, L)`) shift, exactly the "lighting shifts" visual, with zero
  shader changes needed.
- 6 new tests (`tests/test_daynight_lighting.c`): real light-direction math (always above
  horizon, actually changes over time, sun-at-midday, moon-at-midnight — the moon direction is
  exactly the negated sun direction at the analytical zenith, matching SHANKPIT's own real
  formula).
- **Not done, honestly**: no starfield ported (SHANKPIT's own `retro_sky.c` has one,
  `draw_starfield`, real and self-contained — a real, separate, later addition if the direction
  change alone doesn't feel like enough). No sky-dome/gradient rendering (this game's own top-down
  orbit camera has no sky dome to light — `arena_daynight_ambient_rgb`'s own doc comment already
  named this). No visual confirmation possible in this sandbox (no display) — only compiled,
  linked, and confirmed the real underlying math behaves correctly.

## Related

- `docs/NORTHSTAR_LIVING_MAP.md` — the "Mod event model, honestly" section this doc's own REFLUX
  design directly builds on; `packages/livingmap/living_map_events.h` is the proven shape REFLUX
  generalizes.
- `docs/ARENA_API.md` — the real, existing PARENA mod ABI (call-by-name, trigger-only vs. real
  decision logic) every REFLUX-connected mod still follows.
- `PARENA/STDLIB.md` — REFLUX's own new stdlib entry.
