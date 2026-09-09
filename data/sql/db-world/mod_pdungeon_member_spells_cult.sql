-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: combat kits for the Cult of the Damned pack
-- (world database)
--
-- 25 rows for the 8 members of pack 7 "Cult of the Damned" (Scholomance) -
-- three per creature, and FOUR for the BOSS (36879 Plagueborn Horror), which
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
-- abilities do not double a boss's output. Inside a band the heavier ability
-- takes the longer end, which is why three of the four melee openers sit at
-- 6000 (instant weapon strikes) while 11551's Cleave and the boss's own
-- opener sit at 7000.
--
-- ----------------------------------------------------------------------------
-- HOW EVERY ID BELOW WAS VERIFIED - INCLUDING THE STEP THE FIRST CUT MISSED
--
-- Against C:\wowstuff\dcore\Data\dbc\Spell.dbc (55100 records - the file the
-- worldserver actually loads, NOT acore_world.spell_dbc, which is an
-- unrelated ~5.5k-row custom FL table and holds none of these ids), plus
-- SpellRange / SpellRadius / SpellCastTimes / SpellDuration, measured
-- 2026-09-09. Per row: the spell IS what the comment claims (Name_Lang_enUS);
-- its effects do what the comment claims, read out of Effect_1..3 /
-- EffectAura_1..3 / EffectBasePoints_1..3 / EffectDieSides_1..3; its implicit
-- targets can reach a player from where this mob will stand; its CC status is
-- read out of Mechanic / EffectMechanic / EffectAura and never guessed from
-- the name; its cast time does not eat a melee mob's swings; its power cost
-- is affordable for that creature's real level-80 pool.
--
-- FOUR STEPS WERE ADDED after the first cut of this file shipped a spell
-- whose behaviour none of the above could see:
--
--   1. acore_world.spell_script_names. A SpellScript is bound to the SPELL,
--      and lives in a different registry from AllCreatureScript - so PDv2's
--      creature binder does NOT displace it. The first cut carried 67879
--      Claw on 11551; 67879 -> spell_black_knight_ghoul_claw, whose
--      OnEffectHitTarget hook calls GetThreatMgr().ResetAllThreat() and then
--      SelectTarget(Random, 0, 30.0f) + AttackStart on every cast
--      (boss_black_knight.cpp:449-469, hooked to
--      SPELL_EFFECT_WEAPON_PERCENT_DAMAGE, which is 67879's effect 1). A mob
--      with that row drops its whole threat table every 6 seconds and cannot
--      be tanked. 67879 is NOT in this pack any more. (It is still live on
--      31528, 31847, 84271 and 84273 - pre-existing, out of scope here, and
--      recorded in .superpowers/sdd/packs-fix-1.md.)
--      Measured for all 23 ids below: ZERO spell_script_names rows.
--   2. acore_world.disables (sourceType 0 = SPELL): 0 rows for all 23.
--   3. acore_world.spell_linked_spell (spell_trigger / spell_effect): 0 rows
--      for all 23, so nothing below silently drags a second spell with it.
--   4. The DAMAGE NUMBER of every row, at level 80, written into the row
--      comment. SpellEffectInfo::CalcValue clamps the caster level into
--      [BaseLevel, MaxLevel] and then scales by (level - SpellLevel) x
--      EffectRealPointsPerLevel, so a raw EffectBasePoints reading is not the
--      number the player takes: 14516 Strike is +247 on the swing and not
--      +10, 15588 Thunderclap's snare is -70% and not -40%. Every value below
--      is the level-80 value, and periodic auras are multiplied out by
--      duration / EffectAuraPeriod.
--
-- Every "(its own)" note was measured too - smart_scripts action_type 11 for
-- that entry - not inherited from a design document.
--
-- ----------------------------------------------------------------------------
-- WHAT EACH ROW ACTUALLY DOES AT LEVEL 80 (the band this pack keeps to)
--
-- Shipped fillers, for scale: 47809 ~245 dps, 42842 Frostbolt ~278 (pack 1),
-- 60015 ~450, 69211 ~682 (packs 4/5). Cooldown rows in the shipped data run
-- from 20 dps (22644 Blood Leech) to ~750 (59018 Bile Vomit). This pack's
-- three fillers are 245 / 450 / 682 and its heaviest single row is the boss's
-- own 69581 at ~750 dps. Nothing here is outside what already ships.
--
--   spell  what it costs the player at level 80             sustained dps
--   14516  weapon swing +247, 5 yd                            ~100 (6 s)
--   16509  bleed 28 x 5 ticks = ~140 over 15 s                  ~14 (10 s)
--   69900  3239-3763, self 15 yd area                          ~292 (12 s)
--   16169  weapon swing +400 in an 8 yd CONE                   ~120 (6 s)
--   70654  self -11% damage taken, 10 s (defensive, no damage)    0 (10 s)
--   22644  241 health leech, 10 yd area                          ~20 (12 s)
--   48640  150% weapon damage, 5 yd                              ~92 (6 s)
--   15572  -948 armour on the target, 30 s (no damage)             0 (10 s)
--   15655  181 damage + 2 s STUN, 5 yd            THE PACK CC      ~3 (60 s)
--   42746  110% weapon damage, 5 yd                              ~58 (7 s)
--   50729  1711-2091 + bleed 803 x 5 = ~5900 over 15 s          ~590 (10 s)
--   60845  2776-3226, self 10 yd area                           ~250 (12 s)
--   69211  1313-1687, 30 yd, 2.2 s cast              FILLER      ~682
--   48125  231 x 6 ticks = ~1386 over 18 s, 30 yd               ~139 (10 s)
--   48687  550-742, 30 yd area, 1.5 s cast                       ~54 (12 s)
--   60015  1274-1428, 40 yd, 3.0 s cast              FILLER      ~450
--   47960  137 x 4 ticks = ~548 over 8 s, 100 yd                 ~55 (10 s)
--   54889  2961-3441, self 25 yd area                           ~267 (12 s)
--   47809  694-774, 30 yd, 3.0 s cast                FILLER      ~245
--   47864  146 x 12 ticks = ~1752 over 24 s, 30 yd              ~175 (10 s)
--   17228  129-173, self 30 yd area, instant                     ~13 (12 s)
--   59992  weapon swing +240, 5 yd                              ~300 (7 s, boss swing)
--   69581  3751-6251 + 2001 x 2 ticks = ~7750-10250, 30 yd      ~750 (12 s)
--
-- Four rows from the first cut were dropped on these numbers, and why:
--
--   61562 Shadow Bolt  (10476 slot-0 filler) 4250-5750 a cast on a 1.5 s cast
--     and cd 0 = ~3330 dps, free and uninterruptible from 40 yd. That is 7.4x
--     the filler it was supposed to sit under and ~5x the highest number
--     anywhere in the live data; two Necrolytes in one room is ~6700 dps at
--     difficulty 0 and ~20000 at difficulty 100. Replaced by 60015 (~450).
--   61563 Corruption   (10476 t50) 1666-1936 PER TICK every 2 s for 12 s =
--     ~10800 a cast, i.e. ~1080 dps from one cooldown row, undodgeable and
--     single-target. Replaced by 47960 Shadowflame (~548 a cast), which the
--     shipped file already runs four times.
--   8269 Frenzy        (10488 t50) +159 flat damage done, +61% MELEE HASTE
--     and +16% scale for 120 s on a 10 s cooldown - permanent uptime, ~2.3x
--     that mob's melee output. A Berserk-class buff. Replaced by 70654 Blood
--     Armor, the defensive self-buff the shipped file uses on 84279 / 30921 /
--     18871. (8599 Enrage was considered and rejected for the same reason:
--     EffectRealPointsPerLevel 1.0 from SpellLevel 1 makes it +90% damage
--     done at level 80, not the +10% its base points suggest.)
--   15588 Thunderclap  (10486 t75) 309 damage, but also -70% movement speed
--     AND -33% melee haste in a 10 yd self-AoE for 10 s on a 12 s cooldown,
--     i.e. ~83% uptime and permanent with two Risen Warriors in the room. A
--     snare is not CC by this project's definition and this was legal - but
--     on a map with no terrain, movement is the whole defensive game, and the
--     precedent it leaned on (42842 Frostbolt) is -40% SINGLE TARGET.
--     Replaced by 69900 Spirit Burst, plain area damage.
--   17615 Mana Burn    (10471 t50) SPELL_EFFECT_POWER_BURN with
--     EffectMultipleValue 0.5: at most 272 mana burned and at most 136
--     damage, and against a warrior, rogue or death knight literally zero -
--     while still returning SPELL_CAST_OK, so CastReadyKitSpell counts it a
--     success and the filler does not get that tick. Replaced by 47864 Curse
--     of Agony R9 (~1752 over 24 s), which is still a "different threat" - a
--     curse - and works on every class.
--
-- ----------------------------------------------------------------------------
-- POWER COST
--
-- The four unit_class 1 melee members (10486, 10488, 10489, 11551) AND the
-- unit_class 1 boss (36879) have basemana 0 at level 80, so a FLAT ManaCost
-- on any of their rows would fail Spell::CheckPower with NO_POWER and burn
-- the cooldown anyway (PDv2CreatureAI.cpp:1450-1453 spends the cooldown on
-- the attempt). All sixteen of their rows are 0 flat AND 0 percentage - free
-- forever, not merely affordable. The PowerType 1 (rage) rows among them -
-- 16509 Rend, 16169 Arcing Smash, 42746 Cleave, 15572 Sunder Armor, 15655
-- Shield Slam - are NOT a problem while the cost is 0/0: CheckPower compares
-- GetPower(type) against the cost, and 0 < 0 is false. (48640 Strike and
-- 59992 Cleave are PowerType 0, not rage - the first cut said otherwise.)
-- Neither is EquippedItemClass 2 on 14516 / 48640 / 59992 / 15572 / 16509 or
-- 4 on 15655: Spell::CheckItems (Spell.cpp:7205-7217) fails only a DISARMED
-- creature on a melee/ranged spell and otherwise returns SPELL_CAST_OK for a
-- non-player caster; nothing here is disarmed.
--
-- The three casters are unit_class 2 with ManaModifier 3, i.e. 3994 x 3 =
-- 11982 mana at level 80 (measured, not assumed). Two rows there are not
-- free, and both are deliberate:
--
--   48125 Shadow Word: Pain on 10477   22 %  -> 2636 a cast, four to five
--                                              casts before the pool is dry.
--   47864 Curse of Agony R9 on 10471   10 %  -> 1198 a cast, ten casts.
--
-- Both are COOLDOWN rows and never fillers, which is the whole distinction
-- the shipped file's power block draws: Creature::Regenerate gives an
-- in-combat creature only Spirit/5 + 17 mana per interval, so a
-- percentage-cost FILLER empties its caster in six to nine casts and then
-- fails silently for the rest of the fight. A dry cooldown row costs one
-- rotation slot while the free filler keeps working - and a failed kit cast
-- falls straight through to the filler in the same tick (UpdateCasterCombat:
-- `if (... && !CastReadyKitSpell()) CastFiller();`). The shipped file already
-- runs 48125 this way on 30203 and 31529.
--
-- All three fillers (69211 / 60015 / 47809, all "Shadow Bolt") are 0 flat AND
-- 0 percentage and each reaches at least ProceduralDungeon.V2.CastRangeYd
-- (25): 69211 30 yd 2.2 s, 60015 40 yd 3.0 s, 47809 30 yd 3.0 s. Every other
-- role-1 row here reaches 25 yd too (48125 30 yd, 48687 30 yd, 47960 100 yd,
-- 47864 30 yd, 17228 and 54889 self-centred with radius 30 and 25) - the
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
-- in slot 0, 2000 ms of stun. Two further CC spells that these creatures own
-- are therefore deliberately NOT used - 11428 Knockdown (Mechanic STUN,
-- 10486's own) and 15474 Web Explosion (aura MOD_ROOT, 11551's own). Both are
-- listed here so the omission reads as a decision rather than an oversight.
-- With 15588 Thunderclap gone (see above) this pack now carries no snare
-- either, so there is nothing in it that touches player movement at all -
-- which is the property that matters most on a map whose floor ends at the
-- platform edge.
--
-- ----------------------------------------------------------------------------
-- WHAT EACH CREATURE'S OWN ROTATION GAVE, AND WHAT HAD TO BE LEFT OUT
--
-- Measured from smart_scripts (source_type 0, action_type 11) per entry. None
-- of it will RUN on map 760 - PDv2's AllCreatureScript binder wins over both
-- ScriptName and AIName 'SmartAI' (CreatureAISelector.cpp:78-88) - so those
-- rows are a source of IDENTITY, never of behaviour. That argument is about
-- CREATURE scripts only; see step 1 above for why it says nothing about a
-- SpellScript.
--
--   10486  14516 Strike and 16509 Rend kept; 15588 Thunderclap dropped on its
--          -70% snare (above), 11428 Knockdown dropped on the CC budget.
--   10488  16169 Arcing Smash kept; 8269 Frenzy dropped on its +61% haste
--          (above); 3417 Thrash is its own too but is a duration -1
--          PROC_TRIGGER_SPELL aura whose proc rate is not measurable from
--          the DBC, so the slot went to 70654 Blood Armor instead.
--   10489  15572 Sunder Armor and 15655 Shield Slam kept - this creature's
--          whole stock rotation survives, which is why the pack's one CC sits
--          on it.
--   11551  10022 Deadly Poison and 15474 Web Explosion are its only two; the
--          poison is a proc aura on a creature with no weapon procs worth the
--          slot and the explosion is CC, so this kit is built from the shared
--          pool. Its silhouette carries the flavour instead.
--   10477  14887 Shadow Bolt Volley (its own) costs 160 flat and 3.0 s where
--          the free 48687 reaches the same 30 yd for four times the damage,
--          so 48687 took the slot. 17616 Corpse Explosion is EXCLUDED:
--          SPELL_EFFECT_FORCE_CAST makes the TARGET cast something, which is
--          out of scope and unaudited. 12020 Call of the Grave is EXCLUDED:
--          5 yd, and this mob holds at 25.
--   10476  12739 Shadow Bolt (its own) costs 90 flat and 3.0 s for 129-173
--          damage, so the free 60015 fills instead. 17234 Shadow Shock
--          (20 yd, 135 flat) and 17151 Shadow Barrier (450 flat) rejected on
--          range and cost.
--   10471  17615 Mana Burn DROPPED (above); 17613 Dark Mending EXCLUDED
--          (SPELL_EFFECT_HEAL, no self-heal loops); 17165 Mind Flay rejected
--          (20 yd); 16592 Shadowform is a 40 % self-buff aura that would also
--          change its silhouette, and was left out.
--   36879  69582 Blight Bomb is EXCLUDED: its effect 2 is
--          SPELL_EFFECT_INSTAKILL, which is banned outright. 70274 Toxic
--          Waste is EXCLUDED: SPELL_AURA_PERIODIC_DAMAGE_PERCENT at 10 per
--          tick is percent-of-max-health damage, i.e. a ground pool that can
--          take a full health bar in one duration regardless of gear. 69581
--          Pustulant Flesh is the survivor and is the one row in this file no
--          other member carries.
--
-- The economy this pack brings with it - the seven trash members keep their
-- native Scholomance tables and the boss drops nothing at all - is argued in
-- full in mod_pdungeon_packs_cult.sql and belongs in the operator document
-- before the first run, not after.
--
-- THIS FILE SHIPS ZERO creature_template AND ZERO spell_dbc ROWS. Every spell
-- id below is a stock 3.3.5a Spell.dbc entry and no custom id is created.
--
-- The CREATE TABLE IF NOT EXISTS block below is REPEATED from
-- mod_pdungeon_member_spells.sql. The first cut justified that with a
-- filename-ordering claim that is WRONG: UpdateFetcher::PathCompare
-- (UpdateFetcher.cpp:521-524) compares filenames byte-wise and '.' (0x2E)
-- sorts before '_' (0x5F), so mod_pdungeon_member_spells.sql runs BEFORE
-- mod_pdungeon_member_spells_cult.sql and the table always exists by the time
-- this file runs. The block is kept because it is free (IF NOT EXISTS) and
-- makes this file applicable on its own to a database that never had the
-- module - defensive practice, not a fix for a real ordering bug.
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
-- to 36879, and a range delete over that span would wipe an operator's rows
-- for tens of thousands of unrelated templates. 1853 Darkmaster Gandling is
-- in the list although he is no longer a member of this pack: the first cut
-- of this file gave him four rows, and a re-apply has to take them back out.
DELETE FROM `pdungeon_member_spells` WHERE `entry` IN
 (1853,10471,10476,10477,10486,10488,10489,11551,36879);

INSERT INTO `pdungeon_member_spells`
  (`entry`, `spellId`, `slot`, `cooldownMs`, `minDiff`, `enabled`) VALUES
  -- ==========================================================================
  -- PACK 7 "Cult of the Damned" - MELEE (role 0)
  -- ==========================================================================
  -- 10486 Risen Warrior  (unit_class 1, 26710 hp, swing 326-409 @2400 ms)
  (10486, 14516, 1,  6000,  1, 1),  -- Strike      (its own) swing +247, 5 yd        t0
  (10486, 16509, 1, 10000, 50, 1),  -- Rend        (its own) bleed 28 x5, 15 s       t50
  (10486, 69900, 1, 12000, 75, 1),  -- Spirit Burst  3239-3763, self 15 yd area      t75
  -- 10488 Risen Construct  (unit_class 1, 26710 hp, swing 326-409 @2000 ms)
  (10488, 16169, 1,  6000,  1, 1),  -- Arcing Smash (its own) swing +400, 8 yd cone  t0
  (10488, 70654, 1, 10000, 50, 1),  -- Blood Armor   self -11% damage taken, 10 s    t50
  (10488, 22644, 1, 12000, 75, 1),  -- Blood Leech   241 leech, 10 yd area           t75
  -- 10489 Risen Guard  (unit_class 1, 16026 hp) - carries the pack's only CC
  (10489, 48640, 1,  6000,  1, 1),  -- Strike        150% weapon damage, 5 yd        t0
  (10489, 15572, 1, 10000, 50, 1),  -- Sunder Armor (its own) -948 armour, 30 s      t50
  (10489, 15655, 1, 60000, 75, 1),  -- Shield Slam (its own) 181 + CC STUN 2 s       t75
  -- 11551 Necrofiend  (unit_class 1, 16026 hp)
  (11551, 42746, 1,  7000,  1, 1),  -- Cleave        110% weapon damage, 5 yd        t0
  (11551, 50729, 1, 10000, 50, 1),  -- Carnivorous Bite  ~5900 with bleed, 15 s      t50
  (11551, 60845, 1, 12000, 75, 1),  -- Shadow Nova   2776-3226, self 10 yd area      t75
  -- ==========================================================================
  -- PACK 7 "Cult of the Damned" - RANGE (role 1) - filler + two cooldown spells
  -- ==========================================================================
  -- 10477 Scholomance Necromancer  (unit_class 2, 11982 mana) - the senior caster
  (10477, 69211, 0,     0,  1, 1),  -- Shadow Bolt   1313-1687, 30 yd, 2.2 s   ~682  t0 FILLER
  (10477, 48125, 1, 10000, 50, 1),  -- Shadow Word: Pain R12  ~1386/18 s, 22 %       t50
  (10477, 48687, 1, 12000, 75, 1),  -- Shadow Bolt Volley  550-742, 30 yd area       t75
  -- 10476 Scholomance Necrolyte  (unit_class 2, 11982 mana) - the middle rank
  (10476, 60015, 0,     0,  1, 1),  -- Shadow Bolt   1274-1428, 40 yd, 3.0 s   ~450  t0 FILLER
  (10476, 47960, 1, 10000, 50, 1),  -- Shadowflame   ~548 over 8 s, 100 yd, free     t50
  (10476, 54889, 1, 12000, 75, 1),  -- Shadow Shock  2961-3441, self 25 yd area      t75
  -- 10471 Scholomance Acolyte  (unit_class 2, 11982 mana) - the novice
  (10471, 47809, 0,     0,  1, 1),  -- Shadow Bolt R13  694-774, 30 yd, 3.0 s ~245   t0 FILLER
  (10471, 47864, 1, 10000, 50, 1),  -- Curse of Agony R9  ~1752 over 24 s, 10 %      t50
  (10471, 17228, 1, 12000, 75, 1),  -- Shadow Bolt Volley  129-173, self 30 yd AoE   t75
  -- ==========================================================================
  -- PACK 7 "Cult of the Damned" - BOSS (role 2)
  -- ==========================================================================
  -- 36879 Plagueborn Horror  (unit_class 1, rank 1, 189000 hp, swing 1581-2199 @1500 ms)
  (36879, 59992, 1,  7000,  1, 1),  -- Cleave        swing +240, 5 yd                t0
  -- Round C / C6: a second base ability per boss (operator, 2026-09-08: "2 Basis, dann je eine auf 50 und 75")
  (36879, 60015, 1,  9000,  1, 1),  -- Shadow Bolt   1274-1428, 40 yd  (as 10476 filler)  t0 #2
  (36879, 54889, 1, 10000, 50, 1),  -- Shadow Shock  2961-3441, self 25 yd  (as 10476)    t50
  (36879, 69581, 1, 12000, 75, 1);  -- Pustulant Flesh (its own) ~7750-10250, 30 yd  t75
