-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: combat kits for the Cult of the Damned pack
-- (world database)
--
-- 25 rows for the 8 members of pack 7 "Cult of the Damned" (Scholomance) -
-- three per creature, and FOUR for the BOSS (1853 Darkmaster Gandling), which
-- carries a second minDiff 1 row per Round C / C6, exactly like the two Task
-- 11 bosses in mod_pdungeon_member_spells_undead_demon.sql. Column order and
-- cadence rule match the shipped mod_pdungeon_member_spells.sql exactly:
--
--   MELEE + BOSS   position 1  cd 6000-8000 ms   minDiff 1
--                  position 2  cd 8000-10000 ms  minDiff 50
--                  position 3  cd 8000-12000 ms  minDiff 75
--   RANGE          position 1  the slot-0 filler, cd 0, minDiff 1
--                  position 2  cd 8000-10000 ms  minDiff 50
--                  position 3  cd 8000-12000 ms  minDiff 75
--   CC             ALWAYS cd 60000, and NEVER position 1
--
-- with that file's one Round C / C6 exception: a BOSS carries a SECOND row at
-- position 1 at cd 9000 ms, past the 6000-8000 band on purpose, so two base
-- abilities do not double a boss's output. Trash kits are unchanged. Inside a
-- band the heavier ability takes the longer end, which is why every melee
-- opener here sits at 6000 (all four are instant weapon strikes) while the
-- boss's AoE opener sits at 7000.
--
-- ----------------------------------------------------------------------------
-- HOW EVERY ID BELOW WAS VERIFIED
--
-- Against C:\wowstuff\dcore\Data\dbc\Spell.dbc (55100 records - the file the
-- worldserver actually loads, NOT acore_world.spell_dbc, which is an
-- unrelated ~5.5k-row custom FL table and holds none of these ids), plus
-- SpellRange / SpellRadius / SpellCastTimes / SpellDuration, measured
-- 2026-09-09. Per row: the spell IS what the comment claims (Name_Lang_enUS,
-- not merely "some spell exists at that id"); its effects do what the comment
-- claims, read out of Effect_1..3 / EffectAura_1..3 / EffectBasePoints_1..3;
-- its implicit targets can reach a player from where this mob will stand; its
-- CC status is read out of Mechanic / EffectMechanic / EffectAura and never
-- guessed from the name; its cast time does not eat a melee mob's swings; and
-- its power cost is affordable for that creature's real level-80 pool. Every
-- "(its own)" note below was measured too - smart_scripts action_type 11 for
-- that entry - not inherited from a design document.
--
-- ----------------------------------------------------------------------------
-- POWER COST
--
-- The four unit_class 1 members (10486, 10488, 10489, 11551) have basemana 0
-- at level 80, so a FLAT ManaCost on any of their rows would fail
-- Spell::CheckPower with NO_POWER and burn the cooldown anyway
-- (PDv2CreatureAI.cpp:1450-1453 spends the cooldown on the attempt). All
-- twelve of their rows are 0 flat AND 0 percentage - free forever, not merely
-- affordable. A PowerType 1 (rage) spell among them - 48640 Strike, 15572
-- Sunder Armor, 15655 Shield Slam - is NOT a problem while the cost is 0/0:
-- CheckPower compares GetPower(type) against the cost, and 0 < 0 is false.
-- Neither is EquippedItemClass 2 on 15572 or 4 on 15655: Spell::CheckItems
-- returns SPELL_CAST_OK immediately for a non-player caster.
--
-- The three casters and the boss are unit_class 2 with ManaModifier 3, i.e.
-- 3994 x 3 = 11982 mana at level 80 (measured, not assumed). Two rows there
-- are not free, and both are deliberate:
--
--   17615 Mana Burn on 10471   flat 95   -> 126 casts on a 10 s cooldown.
--   48125 Shadow Word: Pain    22 %      -> 2636 mana a cast, about four to
--     on 10477 and 1853                     five casts before the pool is dry.
--
-- The 22 % row is a COOLDOWN row and never a filler, which is the whole
-- distinction the shipped file's power block draws: Creature::Regenerate gives
-- an in-combat creature only Spirit/5 + 17 mana per interval, so a
-- percentage-cost FILLER empties its caster in six to nine casts and then
-- fails silently for the rest of the fight. A dry 48125 costs one row of a
-- long fight while the free filler keeps working. The shipped file already
-- runs 48125 the same way on 30203, and this is the same shape at three times
-- the pool.
--
-- All three fillers (60015 / 61562 / 69211, all "Shadow Bolt") are 0 flat AND
-- 0 percentage and each reaches at least ProceduralDungeon.V2.CastRangeYd
-- (25): 60015 40 yd 3.0 s, 61562 40 yd 1.5 s, 69211 30 yd 2.2 s. Every other
-- role-1 row here reaches 25 yd too (48125 30 yd, 61563 30 yd, 17615 30 yd,
-- 48687 30 yd, 17228 and 54889 self-centred with radius 30 and 25) - the
-- cooldown is spent on the attempt, so a 20 yd spell on a mob holding at 25
-- would throw away a whole rotation slot whenever the player did not oblige.
-- That is why 10476's own 17234 Shadow Shock (20 yd) and 10471's own 17165
-- Mind Flay (20 yd) are not used despite being those creatures' own spells.
--
-- ----------------------------------------------------------------------------
-- CC CLASSIFICATION (aura/mechanic read out of Spell.dbc, not guessed)
--
--  spell  name         why it is CC                              placed
--  15655  Shield Slam  Mechanic 12 STUN, eff1 aura MOD_STUN      10489 @75
--
-- That is the pack's ENTIRE CC budget: one row, cd 60000, minDiff 75, never
-- in slot 0. Two further CC spells that these creatures own are therefore
-- deliberately NOT used - 11428 Knockdown (Mechanic STUN, 10486's own) and
-- 15474 Web Explosion (aura MOD_ROOT, 11551's own). Both are listed here so
-- the omission reads as a decision rather than an oversight. A snare is NOT
-- CC by this project's own definition (mod_pdungeon_member_spells.sql, CC
-- CLASSIFICATION block), which is what lets 15588 Thunderclap sit at cd 12000
-- on 10486 with its MECHANIC_SNARE second effect.
--
-- ----------------------------------------------------------------------------
-- WHAT EACH CREATURE'S OWN ROTATION GAVE, AND WHAT HAD TO BE LEFT OUT
--
-- Measured from smart_scripts (source_type 0, action_type 11) per entry. None
-- of it will RUN on map 760 - PDv2's AllCreatureScript binder wins over both
-- ScriptName and AIName 'SmartAI' (CreatureAISelector.cpp:78-88) - so those
-- rows are a source of IDENTITY, never of behaviour.
--
--   10486  14516 Strike, 16509 Rend, 15588 Thunderclap kept; 11428 Knockdown
--          dropped (CC budget, above).
--   10488  16169 Arcing Smash, 8269 Frenzy kept; 3417 Thrash is its own too
--          and is a free proc-trigger aura, but the third slot went to 22644
--          Blood Leech so the construct has an actual damage step at t75.
--   10489  15572 Sunder Armor and 15655 Shield Slam kept - this creature's
--          whole stock rotation survives, which is why the pack's one CC sits
--          on it.
--   11551  10022 Deadly Poison and 15474 Web Explosion are its only two; the
--          poison is a proc aura on a creature with no weapon procs worth the
--          slot and the explosion is CC, so this kit is built from the shared
--          pool. Its silhouette carries the flavour instead.
--   10477  14887 Shadow Bolt Volley (its own) costs 160 flat and 3.0 s where
--          the free instant 17228 is the same spell name and reaches the same
--          30 yd, so 17228 took the slot. 17616 Corpse Explosion is EXCLUDED:
--          SPELL_EFFECT_FORCE_CAST makes the TARGET cast something, which is
--          out of scope and unaudited. 12020 Call of the Grave is EXCLUDED:
--          5 yd, and this mob holds at 25.
--   10476  12739 Shadow Bolt (its own) costs 90 flat and 3.0 s, so the free
--          1.5 s 61562 fills instead. 17234 Shadow Shock (20 yd, 135 flat)
--          and 17151 Shadow Barrier (450 flat) rejected on range and cost.
--   10471  17615 Mana Burn KEPT - it is the one genuinely different threat in
--          a pack of shadow nukes, it reaches 30 yd, and 95 flat on 11982
--          mana is 126 casts. 17613 Dark Mending EXCLUDED (SPELL_EFFECT_HEAL,
--          no self-heal loops); 17165 Mind Flay rejected (20 yd); 16592
--          Shadowform is a 40 % self-buff aura that would also change its
--          silhouette, and was left out.
--   1853   EXCLUDED from his own four: 17950 Shadow Portal - every effect is
--          a bare scriptless SPELL_EFFECT_DUMMY, so the cast does nothing;
--          18702 Curse of the Darkmaster - effect3 is SPELL_AURA_TRANSFORM
--          with basePoints 1, i.e. it morphs the player into display id 1,
--          and it would additionally spend the pack's single CC budget. His
--          15790 Arcane Missiles is a real spell but a 5 s CHANNEL costing
--          235 flat, and ScriptedAI::DoMeleeAttackIfReady returns early on
--          UNIT_STATE_CASTING, so a boss on a 9 s cadence would spend most of
--          the fight not swinging. 12040 Shadow Shield is a self absorb
--          shield and was left out on purpose: an absorb is a stealth health
--          buff, and this boss's health is already the pack's one open number
--          (see mod_pdungeon_packs_cult.sql).
--
-- The economy this pack brings with it - all eight members carry a native
-- loot table, and Gandling drops two vanilla epics - is argued in full in
-- mod_pdungeon_packs_cult.sql and belongs in the operator document before the
-- first run, not after.
--
-- THIS FILE SHIPS ZERO creature_template AND ZERO spell_dbc ROWS. Every spell
-- id below is a stock 3.3.5a Spell.dbc entry and no custom id is created.
--
-- The CREATE TABLE IF NOT EXISTS block below is REPEATED from
-- mod_pdungeon_member_spells.sql on purpose: the AC updater applies files in
-- FILENAME order (UpdateFetcher.cpp:64-96) and "mod_pdungeon_member_spells_
-- cult.sql" sorts BEFORE "mod_pdungeon_packs.sql" (m < p), so on a fresh
-- database this file would otherwise run before the table it writes to
-- existed. Free on this box, correct on a clean one.
-- ----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS `pdungeon_member_spells` (
  `entry` INT UNSIGNED NOT NULL,
  `spellId` INT UNSIGNED NOT NULL,
  `slot` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `cooldownMs` INT UNSIGNED NOT NULL DEFAULT 8000,
  `minDiff` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`entry`, `spellId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Idempotent re-apply: delete this file's own entries, then insert. Never
-- DROP - an operator who gave a creature of their own a kit keeps it. The
-- delete is an EXPLICIT ENTRY LIST, not a BETWEEN: the entries run from 1853
-- to 11551, and a range delete over that span would wipe an operator's rows
-- for thousands of unrelated templates.
DELETE FROM `pdungeon_member_spells` WHERE `entry` IN
 (1853,10471,10476,10477,10486,10488,10489,11551);

INSERT INTO `pdungeon_member_spells`
  (`entry`, `spellId`, `slot`, `cooldownMs`, `minDiff`, `enabled`) VALUES
  -- ==========================================================================
  -- PACK 7 "Cult of the Damned" - MELEE (role 0)
  -- ==========================================================================
  -- 10486 Risen Warrior  (unit_class 1, 26710 hp) - its whole kit is its own
  (10486, 14516, 1,  6000,  1, 1),  -- Strike            weapon damage, 5 yd        t0
  (10486, 16509, 1, 10000, 50, 1),  -- Rend              5 yd bleed, 21 s           t50
  (10486, 15588, 1, 12000, 75, 1),  -- Thunderclap       self 10 yd AoE + snare     t75
  -- 10488 Risen Construct  (unit_class 1, 26710 hp)
  (10488, 16169, 1,  6000,  1, 1),  -- Arcing Smash      weapon damage, 5 yd        t0
  (10488,  8269, 1, 10000, 50, 1),  -- Frenzy            self damage + haste buff   t50
  (10488, 22644, 1, 12000, 75, 1),  -- Blood Leech       10 yd area leech           t75
  -- 10489 Risen Guard  (unit_class 1, 16026 hp) - carries the pack's only CC
  (10489, 48640, 1,  6000,  1, 1),  -- Strike            weapon % damage, 5 yd      t0
  (10489, 15572, 1, 10000, 50, 1),  -- Sunder Armor      5 yd armour debuff, 30 s   t50
  (10489, 15655, 1, 60000, 75, 1),  -- Shield Slam       5 yd  CC (MOD_STUN, 2 s)   t75
  -- 11551 Necrofiend  (unit_class 1, 16026 hp)
  (11551, 67879, 1,  6000,  1, 1),  -- Claw              weapon % damage, 5 yd      t0
  (11551, 50729, 1, 10000, 50, 1),  -- Carnivorous Bite  damage + bleed, 15 s       t50
  (11551, 69900, 1, 12000, 75, 1),  -- Spirit Burst      self 15 yd area damage     t75
  -- ==========================================================================
  -- PACK 7 "Cult of the Damned" - RANGE (role 1) - filler + two cooldown spells
  -- ==========================================================================
  -- 10477 Scholomance Necromancer  (unit_class 2, 11982 mana)
  (10477, 60015, 0,     0,  1, 1),  -- Shadow Bolt       40 yd single, 0 mana       t0 FILLER
  (10477, 48125, 1, 10000, 50, 1),  -- Shadow Word: Pain R12  30 yd DoT, 22 %       t50
  (10477, 17228, 1, 12000, 75, 1),  -- Shadow Bolt Volley self 30 yd AoE, instant   t75
  -- 10476 Scholomance Necrolyte  (unit_class 2, 11982 mana)
  (10476, 61562, 0,     0,  1, 1),  -- Shadow Bolt       40 yd single, 0 mana       t0 FILLER
  (10476, 61563, 1, 10000, 50, 1),  -- Corruption        30 yd DoT, 12 s, 0 mana    t50
  (10476, 54889, 1, 12000, 75, 1),  -- Shadow Shock      self 25 yd area damage     t75
  -- 10471 Scholomance Acolyte  (unit_class 2, 11982 mana)
  (10471, 69211, 0,     0,  1, 1),  -- Shadow Bolt       30 yd single, 0 mana       t0 FILLER
  (10471, 17615, 1, 10000, 50, 1),  -- Mana Burn         30 yd power burn, 95 flat  t50
  (10471, 48687, 1, 12000, 75, 1),  -- Shadow Bolt Volley 30 yd AoE, 1.5 s cast     t75
  -- ==========================================================================
  -- PACK 7 "Cult of the Damned" - BOSS (role 2)
  -- ==========================================================================
  -- 1853 Darkmaster Gandling  (unit_class 2, rank 1, 85480 hp, 11982 mana)
  ( 1853, 17228, 1,  7000,  1, 1),  -- Shadow Bolt Volley self 30 yd AoE, instant   t0
  -- Round C / C6: a second base ability per boss (operator, 2026-09-08: "2 Basis, dann je eine auf 50 und 75")
  ( 1853, 61562, 1,  9000,  1, 1),  -- Shadow Bolt       40 yd single    (as 10476 filler)  t0 #2
  ( 1853, 48125, 1, 10000, 50, 1),  -- Shadow Word: Pain R12  30 yd DoT, 22 %       t50
  ( 1853, 54889, 1, 12000, 75, 1);  -- Shadow Shock      self 25 yd area damage     t75
