# Multi-Agent RL / Bot AI R&D — Research Notes

Source: a long personal research conversation (Gemini), ingested at
`CarePyre/source/gemini-transcript-2026-08-09.md` lines 1-5023 (the CarePyre business plan itself
starts at line 5024 of that same file — this is the material before it). Captured here in full,
in REDGARDEN, because this is where it actually applies: REDGARDEN's arena bot AI already has a
real RL pipeline (`NORTHSTAR.md` §21) this research extends. §25 of that same file turns the
buildable parts of this into a real spec with VS0 code already landed; this document is the
fuller research reference underneath it — every idea from the source conversation, not just the
subset already spec'd.

## 1. Squad coordination without a hive mind

Starting question: how do you get a squad of RL-trained bots to coordinate like a real human
team — imperfectly, with distinct playstyles — instead of every agent converging on one
identical "optimal" policy the moment they share training?

- **The core failure mode**: naive shared-parameter multi-agent training collapses every agent
  into the same behavior. Named in the source conversation as "noisy gestalt" — team cohesion
  that stays noisy/imperfect rather than a perfect single hive-mind.
- **Dynamic persona vectors**: instead of hand-labeling roles (tank/support/carry), let each
  agent carry a learned embedding vector, discovered by training rather than assigned by a
  human — "the features of the game will determine the things that end up mattering."
- **Phased alternating training**: flip between a phase that rewards uniqueness/diversity and a
  phase that rewards being individually strong (win-rate optimal), repeatedly — framed via
  Magic: the Gathering's color-pie archetypes (spike = optimal/competitive, jimmy/johnny/timmy-
  style = distinct personal playstyles) as the intuition for "some bots should be weird and
  interesting but not maximally winning, others should be hyper-optimized, and you want both in
  the population, discovered through alternating training rather than fixed at the start."
- **Combining bots ("peanut butter and jelly")**: once you have distinct, effective bot
  playstyles, combine two of them and keep training the result — described as a recursive
  process (combine, train, combine again).
- **Cross-attention team synergy + comeback mechanic**: dynamically tune a cross-attention
  mechanism across a team so that when one team is winning too decisively, the bots' team
  synergy degrades a bit as a rubber-band comeback mechanic — explicitly randomized ("a random
  chance of synergy decay at different levels"), not a deterministic trigger, so it reads as
  variance rather than a scripted comeback.
- **Stochastic/Markov framing**: the synergy-decay randomness was explicitly connected back to
  Markov-chain / gradient-descent intuition as a teaching example during the conversation — the
  idea that a stochastic state-transition process is the same underlying math whether you're
  describing team cohesion decay or an optimizer's own step.

## 2. Cross-game transfer of game theory

Question: once a squad of bots has learned real team-coordination "intangibles" in one game, can
that transfer to a different game, the way a human who's good at one competitive game often
picks up a new one faster than someone with zero competitive-game background?

- The idea: distill the general-purpose, game-agnostic layer of what was learned (positioning
  discipline, when to commit vs. retreat, resource tempo) separately from the game-specific
  mechanical layer (this specific game's exact controls/kit), so the general layer transfers to a
  new game (needing mechanical fine-tuning, but not re-learning the underlying game theory from
  zero) — and, further out, whether that same distilled "game theory" layer could apply outside
  games entirely, to markets.
- **Fractal commander/soldier hierarchy**: applying the same policy shape recursively — a
  top-level "commander" agent coordinating "commander-soldiers" which each coordinate their own
  "soldiers" — as a way to scale team coordination beyond what a single flat multi-agent policy
  can reasonably represent.
- **Contrastive state encoders**: named as the representation-learning technique that would
  underlie extracting this transferable "what matters" signal from raw game state, separate from
  the state's own game-specific surface form.

## 3. Autocurriculum

Question: instead of a fixed training environment/opponent, can the curriculum itself adapt —
and can that same idea evolve an existing game as people play it, rather than only shaping how a
bot trains inside a game that's already finished?

- **Auto-curriculum engine**: acceptance criteria discussed for what such an engine would need to
  satisfy, built from the squad-coordination + cross-game-transfer ideas above.
- **Applying UED (Unsupervised Environment Design) to an existing game**: rather than a fixed
  training environment, let the environment/opponent selection itself adapt to the current
  policy's weaknesses — explicitly framed as something not done before: using UED to make a game
  *that humans play* emergently evolve as it's played, not just to train an isolated agent
  offline.
- **Procedural mini-games as weight adjustments**: a concrete simplification of the above —
  something like Mario Party's mini-game structure, where the procedurally generated mini-games
  are themselves the visible surface of underlying weight/parameter adjustments, so play and
  training become the same loop.

## 4. Reality/physics-adjacent research thread

A separate thread, more speculative, connected to the squad/curriculum ideas via the framing that
a sufficiently capable system should eventually be able to discover its own better tools for
understanding physical systems, not just game systems:

- **Physics-informed neural networks (PINNs)**: real, established technique (neural networks
  constrained to satisfy known differential equations, used for physics simulation) — the
  question raised was whether a fractal/hierarchical multi-layer version of PINNs could apply
  here, and whether the same "discover better tools recursively" idea that applies to game
  strategy could apply to discovering better numerical methods for solving PDEs.
- **4-state transistor hardware speculation** and provisional-patent-style Python code were
  generated during this thread — noted here as part of the conversation's record, not verified or
  adopted as real engineering; no claim is made that any of this is buildable as described.

## 5. Prompt-engineering technique: "frame break"

Separate from the RL/game research above — a real, reusable prompt-engineering pattern emerged
during the conversation and was applied repeatedly: given a surface-level message or request,
respond by naming the underlying structural/systemic pattern the surface message is an instance
of, one level of abstraction up, rather than answering the surface request directly. Demonstrated
on things like meeting-scheduling emails and technical questions. Real and reusable as a prompting
technique in its own right, independent of the RL material above; whether/where to apply it in
this org's own tooling (if at all) is undecided and not in scope of this document.

## 6. What's already been turned into a real spec

`NORTHSTAR.md` §25 (this repo) takes the squad-coordination and autocurriculum threads above
(sections 1 and 3) and turns them into a real, buildable spec against REDGARDEN's existing RL
pipeline (§21) — role discovery, the noisy-gestalt alternating training schedule, synergy decay
as a live-match mechanic, and an opponent-sampling autocurriculum. VS0 of that spec (the
multi-agent team training environment itself) is real, compiled code as of this document landing
(`apps/arena_training/src/headless.c`'s `sim_init_team`/`sim_step_team`/`sim_get_obs_team`/
`sim_get_done_team`/`sim_get_winner_team`).

The cross-game transfer (section 2), physics/PINN thread (section 4), and frame-break prompting
pattern (section 5) are captured here as research reference only — not yet turned into a spec or
code anywhere.
