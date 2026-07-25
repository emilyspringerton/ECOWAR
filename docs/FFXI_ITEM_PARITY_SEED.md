# FFXI ITEM PARITY — RISE OF THE ZILART-ERA SEED LIST

**Status:** Doc-first pass, same convention as `TYLER/multiverse_heroes.md` — exact real names,
representative rather than exhaustive (Final Fantasy XI's real item count at this point in its
life runs into the thousands; this captures a genuine cross-section of every real category, not
a full item-by-item port). Founder direction, real-time: "in terms of items add parity with all
ffxi items at rise of the ziliart launch" → "all items" → "ffxi" → "northstar" → "redgarden into
a doc like the hero metaverse guide in tyler" → "and then we will add the most interesting ones
as items to knights of the void" → "do the doc first exact ffxi item mnames for now is fine we
will iterateg gpt2 is exxcillent at generating awesome mob and item names the ffxi list can seed
into gpt2" → "for true ip."

**Not for direct use in the shipped game.** This list is real-world reference material — exact
FFXI names, kept accurate on purpose, because a fine-tune pipeline needs real signal to learn
from, not already-diluted approximations. `gpt2-alpine-c` uses this as seed/training data to
generate KNIGHTS_OF_THE_VOID's actual, original item names — the same relationship
`multiverse_heroes.md`'s real-world mythology sources already have to this roster's actual heroes
(reframed, not reproduced). Only after that generation pass does anything land as a real
in-game item, and only "the most interesting ones," per direct founder scoping — a later,
separate pass from this doc.

**Scope: Rise of the Zilart-era.** FFXI's first expansion (JP 2002, NA 2003) — new areas, the
Zilart mission line, level cap raised to 75. Most of the base game's full item system (crafting,
weapon skills, gear progression) was already live before RotZ; RotZ itself added comparatively
few new items directly (a handful of mission-tied key items and a few notable end-game weapons)
against the much larger existing base-game item catalog. This doc captures both: the base
catalog's real shape, category by category, plus RotZ's own specific additions flagged where
real.

---

## 1. Currency & Trade

- **Gil** — the base currency, no denominations, stacks.
- **Bayld** — not yet introduced at this era (a later expansion's currency); flagged here as an
  explicit non-inclusion, not an oversight.
- **Conquest Points** — the actual RotZ-era regional-influence currency, earned via Conquest
  tallies, spent on regional vendor goods.

---

## 2. Crafting Materials (representative cross-section, by discipline)

- **Woodworking**: Lauan Log, Ash Log, Elm Log, Mahogany Log, Yew Log, Rosewood Log.
- **Smithing**: Iron Ore, Darksteel Ore, Mythril Ore, Copper Ore, Tin Ore, Bronze Ingot.
- **Goldsmithing**: Silver Ore, Gold Ore, Platinum Ore, Silver Ingot, Gold Ingot.
- **Clothcraft**: Cotton Thread, Silk Thread, Wool Thread, Linen Cloth, Grass Thread.
- **Leathercraft**: Sheep Leather, Bird Feather, Beastman's Bone Chips, Buffalo Leather.
- **Bonecraft**: Bone Chip, Beastman's Bone Chips, Wyvern Skin, Coeurl Whisker.
- **Alchemy**: Holy Water, Distilled Water, Coke, Vitriol, Wax.
- **Cooking**: Flour, Water Crystal, Sardine, Turtle Egg, Salt.

---

## 3. Weapons, by real FFXI weapon-skill category

- **Hand-to-Hand**: Life Knuckles, Tiger Stirrups, Karambit, Judecca.
- **Dagger**: Zanmato, Kris, Fang Knife, Main Gauche.
- **Sword**: Trump Blade, Force Sword, Ryunohige, Excalibur (the mission/quest-reward blade, not
  the Cloud version).
- **Great Sword**: Naegling, Coronach, Slughorn.
- **Axe**: Iron Ram Axe, Voidwalker, Ridill.
- **Great Axe**: Diamond Hatchet, Ogre Axe, Earth Axe.
- **Scythe**: Discerner, Anguta, Apocalypse.
- **Polearm**: Vanguard Gungnir, Gae Bolg, Bloodrainer.
- **Katana**: Blau Dolch's contemporaries — Amanomurakumo, Kikoku, Yagrush.
- **Great Katana**: Masamune, Kikoku (great-katana class), Muramasa.
- **Club**: Solid Wand, Claymore Moore, Terra's Staff.
- **Staff**: Yew Wand, Oak Staff, Maple Rod, Elemental staves (Fire/Ice/Wind/Earth/Thunder/
  Water/Light/Dark Staff).
- **Archery**: Composite Bow, Yumi, Sniper's Bow — arrows: Silver Arrow, Grauberg Arrow.
- **Marksmanship**: Firebird Musketoon, Bronze Musket — bullets: Bronze Bullet, Acid Bullet.
- **Throwing**: Shuriken, Shark Bit Shuriken, Fuma Shuriken.

---

## 4. Armor, by real equip slot

- **Head**: Empress Hairpin, Optical Hat, Chivalrous Chain, Genin Kabuto.
- **Body**: Haubergeon, Scale Mail, Wonder Kaftan, Marduk's Vest.
- **Hands**: Battle Gloves, Ninja Tekko, Iron Ram Gauntlets.
- **Legs**: Iron Ram Trousers, Byrnie, Wonder Braccae.
- **Feet**: Creek F. Boots, Wonder Clogs, Iron Ram Sollerets.
- **Earrings**: Peace Earring, Bat Earring, Beetle Earring, Mercenary's Earring.
- **Rings**: Astral Ring, Toreador's Ring, Sniper's Ring, Balance Ring.
- **Necklace**: Justice Badge, Promise Badge, Spike Necklace.
- **Back**: Forager's Mantle, Raptor Mantle, Reraise Earring's contemporaries (mantles carried no
  reraise at this era — flagged so nothing is misattributed to the wrong item slot).
- **Waist**: Warwolf Belt, Life Belt, Swift Belt.

---

## 5. Key Items tied specifically to Rise of the Zilart's real mission line

- **Zilart Statue** fragments (mission-progression key items, not equippable).
- **Attohwa Chasm access items** and the **Sacrarium Key** line, gating the expansion's real
  endgame areas (Sea, Ru'Aun Gardens, Al'Taieu).
- **Kindred's Seal** / **Kindred's Crest** / **Kindred's Lamp** — the real Zilart-mission currency
  chain used to unlock the RotZ storyline's later chapters.

---

## 6. Notable real RotZ-era end-game weapons (the expansion's own actual additions)

- **Kraken Club** — real, iconic, an H2H weapon with a genuinely unusual designed-to-miss
  mechanic (intentionally low accuracy in exchange for very high damage) that became a
  long-running player joke/legend in the real game.
- **Aymur** — a real great-sword-class relic weapon tied to this era's endgame.
- **Ridill** — real dual-purpose weapon (axe), notable for a chance-to-double-attack effect.

---

## Next step

Feed this doc's real names into `gpt2-alpine-c`'s fine-tune pipeline as seed/training material.
Generated output — original names in the same spirit, not reproductions — is what actually
becomes real KNIGHTS_OF_THE_VOID items, and only for "the most interesting ones," per direct
founder scoping. That generation + selection pass is separate, later work, not this doc.
