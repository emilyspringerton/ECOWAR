# ECOWAR — Spherical World + Fixed-Camera Controls (NORTHSTAR)

Registered as `ECOWAR-SPHERECAM-NORTH`. Scoping doc only — explicitly no code this pass, per the
founder's own choice when asked how to sequence this ("Scope it first, no code yet").

## Founder real-time direction

Shared a reference screenshot (Spore's planet/Civilization-stage globe view: a curved terrain
horizon, a "Capture an Opposing City" objective banner, a circular planet minimap bottom-left).
Ask, verbatim shape: "can we have it so that the map is actually a sphere and then the camera is
fixed and you can move the actual map around - like on mobile you can just rotate it like a ball -
on pc you can do that with right click holding that makes it so the map rotates like you could
rotate a sphere in a graphics program and then when you arent holding right click if you scroll to
the edge of the screen like in an RTS the actual camera would pan if you move your mouse to the
edge of the screen or however RTSs usually work flip the paradigm."

Two real, separable asks bundled into one sentence, sized very differently:

1. **Camera control scheme, flipped**: instead of the camera moving over a fixed world (how every
   RTS/MOBA camera, including this one, works today), the world rotates under a fixed camera.
   Right-click-hold-drag = arcball-style rotate the world/sphere. Not holding right-click + mouse
   at the screen edge = the classic RTS edge-pan, but reinterpreted as panning the fixed camera's
   look direction rather than sliding it across a flat plane.
2. **World geometry, made literally spherical**: the map itself becomes a sphere (a planet), not a
   flat bounded plane with a horizon.

These are not the same size of problem. (1) is a real, scoped camera/input change. (2) is a
coordinate-system rewrite every other system this session built (Living Map's hex grid, arena
node/hero positions, aggro/leash ranges) sits on top of. This doc scopes both honestly and
separately, and names the real, load-bearing question that decides how big (2) actually is.

## Real, checked-first finding: today's camera and world, as they actually work

- **The camera is already a real 3D orbit camera**, not 2D — `mat4_orbit_view(focus_x, 0,
  focus_z, cam_yaw, cam_pitch, cam_dist)` (`packages/common/mat4.h`), a real yaw/pitch/distance
  orbit around a focus point, `mat4_perspective(60.0f, aspect, 0.1f, 100.0f)` for real perspective
  projection. Right-click-hold-drag **already** adjusts `cam_yaw`/`cam_pitch` (`apps/arena/src/
  main.c`, the `if (!cam_locked) { cam_yaw += dx * 0.3f; cam_pitch += dy * 0.3f; }` block) — the
  literal input gesture the founder described for "rotate it like a ball" is already wired, just
  rotating the CAMERA around the world today, not the world under the camera.
- **The orbit's focus point is hardcoded to the local player's own hero position**
  (`arena_state.heroes[my_owner].x/.z`), every frame, with no free-roam camera mode at all. A
  `cam_locked` toggle (key `C`, NORTHSTAR §15.1) exists, but it only suppresses the yaw/pitch drag
  input (prevents accidental rotation) — it does NOT decouple the focus point from the hero. There
  is no RTS-style "look anywhere on the map" camera today, no edge-pan, because a MOBA-style
  hero-locked camera never needed one.
- **The world is a flat, bounded float x/z plane** (`ARENA_HALF_EXTENT ≈ 155.34`), with a
  Euclidean distance model used everywhere: hero movement/pathing, `resolve_combat`'s attack-range
  checks, node/creep aggro radii, and — the newest, largest example — this session's own Living
  Map hex grid (`packages/livingmap/hex_grid.h`, axial `(q, r)` coordinates, flat-top hex math,
  `hex_distance` a pure 2D cube-coordinate formula). None of this has any notion of curvature.

## The real, load-bearing question: does "sphere" need to be functionally real, or just visually real?

This is the single biggest cost lever in this whole doc, and it's a real, founder-level decision
this doc does not make:

- **Functionally real**: units actually walk on a curved surface, `hex_distance`/aggro
  ranges/pathing are computed geodesically (great-circle distance, not flat Euclidean), the hex
  grid itself is retiled onto the sphere's surface. This is the "real spherical RTS" reading —
  and a genuinely hard, several-week-class engineering problem (see below).
- **Visually real, functionally flat**: the world stays the exact flat x/z plane every existing
  system (hex grid, movement, combat) already uses underneath, but the RENDERED terrain is a
  curved dome/horizon (a real, common trick — bend vertex Y position by distance-from-camera-center
  in the vertex shader, same family of technique many stylized/low-poly strategy games use to fake
  planet curvature without ever changing gameplay math). The Spore screenshot's own curved horizon
  is consistent with either reading — nothing in it proves units are walking geodesic paths versus
  a flat plane rendered with a curvature bend.

**This doc's own recommendation, not a decision**: start from "visually real, functionally flat."
It gets the actual look in the reference screenshot, needs zero changes to the Living Map hex grid
or any distance/movement/aggro math already shipped this session, and is a real, bounded rendering
task (a vertex-shader-level bend plus the camera work below) instead of a coordinate-system
rewrite. "Functionally real" stays a real, named, later option if flat-with-a-bend turns out to
feel wrong once it's actually on screen — but that's a call best made by looking at the cheap
version first, not by committing to the expensive one blind.

## Real technical challenges, named per approach

### Camera control scheme (real either way, needed regardless of the sphere question)

- **Decoupling the orbit focus from the hero.** Today `focus_x/z` is read fresh from
  `arena_state.heroes[my_owner]` every frame — a free camera needs its own persistent
  `cam_focus_x/z` state, moved by input instead of hero position. Real, open question: does the
  camera ever snap back to the hero (e.g., a "center on hero" hotkey, common in real RTSes), or is
  it free-only once unlocked? Not decided here.
- **"Right-click-drag rotates the world, not the camera."** Mathematically, rotating a camera
  around a fixed world and rotating the world under a fixed camera are inverse transforms of the
  same relative motion — visually near-identical for a single self-contained scene with no other
  fixed external reference the player can see. The real, concrete difference that matters: once
  hex-grid town/creep markers, minimap orientation, and (eventually) UI elements referencing
  world-relative direction exist, "which thing is actually rotating" affects how those stay
  correctly oriented — a real implementation decision (rotate a model matrix on the terrain root
  vs. rotate the view matrix), not just semantics.
- **Edge-pan, reinterpreted.** A classic flat-map RTS edge-pan slides the camera position; here,
  with no camera position to slide (it's fixed, per the founder's own framing), "mouse at the
  screen edge" instead needs to mean something else — the two real candidates: (a) it re-centers/
  drifts the world's own rotation slowly toward that edge (a continuous, slower version of the
  right-drag rotate), or (b) it's a genuine camera look-direction pan (tilts where the fixed camera
  is pointed, independent of world rotation) even though the founder's own phrase says "camera is
  fixed" — a real, apparent tension in the ask worth a direct follow-up question before building
  either, not resolved here.
- **Mobile touch-rotate** ("like on mobile you can just rotate it like a ball") — a real, separate
  input binding (single-finger drag, presumably) for whichever platform target that implies; this
  repo has no mobile client today (`apps/arena` is SDL2 desktop), so this is real, future,
  unscoped work, named not guessed at.

### World geometry (only real challenges if "functionally real" is ever chosen)

- **No perfect hex tiling exists on a sphere.** Every real hex-planet game (Civilization VI's
  world map isn't actually a sphere; Spore's own Civ stage's real underlying tiling is not
  publicly documented in detail) that looks hex-tiled and spherical in practice uses a **geodesic/
  Goldberg polyhedron**: hexagons everywhere except exactly 12 pentagons (a real, unavoidable
  topological fact — Euler's formula forces exactly 12 five-sided faces on any hexagon-dominant
  closed polyhedron). `packages/livingmap/hex_grid.h`'s own flat axial `(q, r)` system has no
  analog here — this would be a genuinely new, separate grid data structure, not an extension of
  the existing one.
- **Geodesic distance, not `hex_distance`'s flat cube-coordinate formula.** Every real-distance
  read in this codebase (aggro range, leash range, attack range, hex neighbor-finding) would need
  a spherical-geometry equivalent. Real, nontrivial: even "who is my neighbor" stops being a fixed,
  6-neighbor rule near a pentagon face.
- **Screen-space click → world position** needs real sphere-raycasting (intersect a screen ray
  with a sphere, or with the actual geodesic mesh) instead of a flat-plane line/ray intersection.
- **Hero "up" and facing** stop being a flat, shared world-up vector — a hero standing on the far
  side of the sphere has a different real "up" than one on the near side, affecting movement,
  rendering orientation, and any hero-facing-dependent ability (`facing_rad`, S202-40's Shadow
  Step already depends on this being meaningful).
- **The Living Map's own real starting layout** (`living_map_bridge.c`, founded this session:
  fixed hex coordinates, a real hex-radius-12 flat grid sized to `ARENA_HALF_EXTENT`) would need a
  full redesign for a geodesic grid — a real, second, later rewrite on top of a rewrite, not a
  small follow-up.

## Phased plan (real, not started — no code this pass)

1. **Free camera on the existing flat map** — decouple `cam_focus_x/z` from the hero, add a real
   toggle between "hero-locked" (today's MOBA-style default, unchanged) and "free/map" camera
   modes. Real, scoped, immediately useful for actually surveying the Living Map's spread-out
   towns regardless of any sphere decision.
2. **Resolve the "camera is fixed" vs. "edge-pan tilts the camera" tension** — a real, direct
   founder question (see above), not guessed at, before building either rotate-world or edge-pan
   input handling.
3. **Visual sphere bend** (the recommended cheap path) — a vertex-shader-level terrain curvature
   bend, camera and gameplay math otherwise unchanged. A real, bounded rendering task; the actual
   visual target from the reference screenshot without touching Living Map/combat math at all.
4. **Right-click arcball rotation + reinterpreted edge-pan**, built against whichever resolution
   Phase 2 reached, on top of Phase 3's visual sphere.
5. **Mobile touch-rotate input** — real, separate, later, no mobile client exists yet.
6. **"Functionally real" sphere** (geodesic hex tiling, spherical distance/pathing/raycasting,
   curved-surface hero orientation, a from-scratch Living Map layout on the new grid) — only if
   Phase 3's cheaper visual version is played and found genuinely insufficient. Real, large,
   multi-system rewrite; not started, not estimated further than "large" without a real design
   pass of its own once/if this phase is ever greenlit.

## Related

- `docs/NORTHSTAR_LIVING_MAP.md` — the flat hex-grid Living Map this doc's Phase 6 would need to
  fully redesign if "functionally real" spherical geometry is ever chosen; every other phase here
  leaves it untouched.
- `packages/common/mat4.h` — the real, existing `mat4_orbit_view`/`mat4_perspective` this doc's
  camera work builds on, not replaces.
