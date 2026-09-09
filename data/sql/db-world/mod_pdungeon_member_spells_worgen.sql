-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: combat kits for pack 6 "Shadowfang Pack" (world DB)
--
-- 25 rows for the 8 members of pack 6 (see mod_pdungeon_packs_worgen.sql):
-- three per trash creature, and FOUR for the BOSS 3927 Wolf Master Nandos,
-- which carries a second minDiff 1 row since Round C / C6. Column order and
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
-- with that file's one Round C / C6 exception (operator, 2026-09-08: "2
-- Basis, dann je eine auf 50 und 75"): a BOSS carries a SECOND row at
-- position 1 at cd 9000 ms, deliberately past the 6000-8000 band so that two
-- base abilities together do not double what one boss put out before. Trash
-- kits are unchanged. `position` was never a database column - `slot` tells
-- the RANGE filler apart and nothing else - so the boss's two position-1 rows
-- are two rows at minDiff 1 sharing slot 1.
--
-- Inside a band the heavier ability takes the longer end: 6000 a light weapon
-- strike, 7000 a normal single-target ability, 8000+ an AoE, a DoT, a channel
-- or anything with a cast time.
--
-- ----------------------------------------------------------------------------
-- IDENTITY - how every id below was verified (2026-09-09, this box)
--
-- The authority is C:\wowstuff\dcore\Data\dbc\Spell.dbc (55100 records - the
-- file the worldserver actually loads), NOT acore_world.spell_dbc, which is
-- an unrelated ~5.5k-row custom FL table and holds NONE of these ids: checking
-- a stock id against it returns "does not exist" for every correct answer.
-- Every effect / aura / mechanic / target name below was read out of the DBC
-- and labelled through the core's own SharedDefines.h and
-- SpellAuraDefines.h, so a label here cannot drift from the numbering the
-- running worldserver.exe was built from.
--
-- 23 distinct spell ids, each checked for: identity (the spell IS what the
-- comment claims, not merely that some spell exists at that id), effect shape,
-- implicit target, radius, cast time, mechanic/aura CC status, power cost, and
-- reach from where the mob will stand. Notable measurements:
--
--   67879 Claw               WEAPON_PERCENT_DAMAGE 150%, single, 5 yd, 0/0
--   16509 Rend               PERIODIC_DAMAGE bleed 15 s, single, 5 yd, 0/0
--   22644 Blood Leech        HEALTH_LEECH, TARGET_SRC_CASTER, radius 10 yd
--   48640 Strike             WEAPON_PERCENT_DAMAGE 150%, single, 5 yd, 0/0
--    8599 Enrage             self MOD_DAMAGE_PERCENT_DONE + MOD_MELEE_HASTE
--   59126 Shadow Breath      SCHOOL_DAMAGE, TARGET_UNIT_CONE_ENEMY_24, 15 yd
--    7122 Blood Tap          HEALTH_LEECH, single, 5 yd, 0/0  (3857's OWN)
--   50729 Carnivorous Bite   SCHOOL_DAMAGE + BLEED aura, single, 5 yd
--   69900 Spirit Burst       SCHOOL_DAMAGE, TARGET_SRC_CASTER, radius 15 yd
--   42397 Rend Flesh         SCHOOL_DAMAGE + BLEED aura, single, 5 yd
--    7072 Wild Rage          self MOD_DAMAGE_DONE + MOD_SCALE  (3859's OWN)
--   11428 Knockdown          MOD_STUN, Mechanic 12 STUN, 5 yd  -- THE ONE CC
--   61562 Shadow Bolt        SCHOOL_DAMAGE, 40 yd, 1.5 s cast, 0/0
--   48125 Shadow Word: Pain  rank 12, PERIODIC_DAMAGE 18 s, 30 yd, 0/22%
--   17228 Shadow Bolt Volley SCHOOL_DAMAGE, TARGET_SRC_CASTER, radius 30 yd,
--                            instant, 0/0
--   60015 Shadow Bolt        SCHOOL_DAMAGE, 40 yd, 3.0 s cast, 0/0
--   47960 Shadowflame        PERIODIC_DAMAGE 8 s, 100 yd, 0/0
--   13338 Curse of Tongues   MOD_CASTING_SPEED_NOT_STACK -50%, 30 yd, 0/0
--   69211 Shadow Bolt        SCHOOL_DAMAGE, 30 yd, 2.2 s cast, 0/0
--   61563 Corruption         PERIODIC_DAMAGE 12 s, 30 yd, 0/0
--   48687 Shadow Bolt Volley SCHOOL_DAMAGE, TARGET_SRC_CASTER, radius 30 yd,
--                            1.5 s cast, 0/0
--   70191 Cleave             WEAPON_PERCENT_DAMAGE 120%, single, 5 yd, 0/0
--   15588 Thunderclap        SCHOOL_DAMAGE, TARGET_SRC_CASTER, radius 10 yd,
--                            + MOD_DECREASE_SPEED snare + MOD_MELEE_HASTE
--
-- One correction to a comment elsewhere in this module, recorded rather than
-- copied: mod_pdungeon_member_spells_undead_demon.sql labels 48687 "100 yd
-- AoE". SpellRange.dbc says 0..30 yd with radius 30. The ROW is right either
-- way (it is a 30 yd self-centred AoE cast by a mob holding at 25 yd), but the
-- comment in THIS file states the measured number.
--
-- ----------------------------------------------------------------------------
-- CC CLASSIFICATION (aura/mechanic read out of Spell.dbc, not guessed)
--
--  spell  name        why it is CC                             placed
--  11428  Knockdown   Mechanic 12 STUN, aura SPELL_AURA_MOD_STUN  3859 @75
--
-- That is the WHOLE CC budget of pack 6: one row, on the melee member the
-- scale ladder already marks as the biggest plain worgen, at cooldownMs 60000
-- and minDiff 75, never in slot 0. No other row in this file carries a CC
-- mechanic or a CC aura.
--
-- Deliberately NOT treated as CC, on the shipped file's own written doctrine:
-- 15588 Thunderclap's MECHANIC_SNARE / SPELL_AURA_MOD_DECREASE_SPEED (a snare
-- is not in the operator's fear/stun/root/sleep/silence/disorient list) and
-- 13338 Curse of Tongues' MOD_CASTING_SPEED_NOT_STACK (a cast-speed debuff is
-- not an incapacitate).
--
-- ----------------------------------------------------------------------------
-- POWER COST
--
-- Five of the eight members are unit_class 1 (3852, 3854, 3857, 3859, 3927).
-- creature_classlevelstats has basemana 0 for class 1 at every level, so
-- Creature::SelectLevel gives them CreateMana 0 and Power(MANA) 0. From the
-- engine, not from a rule of thumb: SpellInfo::CalcPowerCost adds
-- ManaCostPercentage as a percentage OF GetCreateMana() - 0 - so a
-- percentage-only cost resolves to 0, while a FLAT ManaCost is added
-- unconditionally and Spell::CheckPower then fails with NO_POWER. Every one of
-- the 14 spell ids on those five members is 0 flat AND 0 percent, so not one
-- of them needs the argument at all.
--
-- The three unit_class 2 casters (3853, 3855, 3860) have 7988 mana at level
-- 80 (basemana 3994 x ManaModifier 2). All three FILLERS are 0 flat AND 0
-- percent - free forever, not merely affordable - for the reason the shipped
-- header explains at length: Creature::Regenerate gives an in-combat creature
-- only Spirit/5 + 17 mana per interval, so a percentage-cost filler empties
-- even a real pool in six to nine casts and then silently fails CheckPower for
-- the rest of the fight. The single non-zero cost anywhere in this file is
-- 48125 Shadow Word: Pain (0 flat / 22 %) on 3853, a cooldown row at 10 s on a
-- 7988-mana creature: 1757 mana a cast, and it is not the filler.
--
-- ----------------------------------------------------------------------------
-- REACH - what "range" means for a PDv2 caster
--
-- UpdateCasterCombat plants a role-1 mob wherever it happens to be INSIDE
-- V2.CastRangeYd - me->IsWithinCombatRange(victim, castRange), not "at exactly
-- 25 yd" (PDv2CreatureAI.cpp:1475-1500) - and the cooldown is spent on the
-- ATTEMPT (PDv2CreatureAI.cpp:1450-1453), so a spell that refuses for range
-- costs the mob a whole rotation slot rather than being retried. Every row on
-- a role-1 member here therefore reaches at least 25 yd: the three fillers are
-- 40/40/30 yd, and the six cooldown rows are 30, 30, 30, 100, 30 and 30 yd
-- (17228 and 48687 are self-centred with radius 30, which reaches from a 25 yd
-- hold; 69900 radius 15 and 15588 radius 10 would NOT, which is exactly why
-- both sit on melee members instead). No row anywhere in this file has a
-- non-zero rangeMin.
--
-- ----------------------------------------------------------------------------
-- TWO NON-PROBLEMS, recorded so the next reader does not re-litigate them
--
--  * EquippedItemClass 2 on 16509 Rend and 48640 Strike is not a creature
--    problem: Spell::CheckItems returns SPELL_CAST_OK immediately for a
--    non-player caster. (It IS a trap for custom PLAYER spells - a different
--    table and a different problem.)
--  * PowerType 1 (rage) on 67879 Claw, 70191 Cleave and 11428 Knockdown is not
--    a problem while the cost is 0/0: Spell::CheckPower compares
--    GetPower(type) against the cost, and 0 < 0 is false.
--
-- ----------------------------------------------------------------------------
-- EXCLUDED, and why - the spells these creatures own that are NOT used
--
--  7107  Summon Wolfguard Worg (3854's own)  SPELL_EFFECT_SUMMON_PET
--  7489  Call Lupine Horror    (3927's own)  SPELL_EFFECT_SUMMON
--  7487  Call Bleak Worg       (3927's own)  SPELL_EFFECT_SUMMON
--        A summoned pet is not tracked by PDv2InstanceScript's _roomAlive
--        counter and its despawn path is unaudited. 3927's ENTIRE stock
--        rotation is summons, which is why his kit is built from the generic
--        pool instead of from his own spellbook.
--  7106  Dark Restore          (3854's own)  SPELL_EFFECT_HEAL - no self-heal
--        loops; ModifyHealReceived is deliberately not hooked
--        (PDv2Scaling.cpp:317-321).
--   970  Shadow Word: Pain R1  (3855's own)  baseLevel 4. Spell damage is not
--        level-normalised by the module, and a low rank's own level cap would
--        make it a tickle at 80; the level-80 rank 48125 is used instead.
--
-- THIS FILE SHIPS ZERO creature_template AND ZERO spell_dbc ROWS.
--
-- The CREATE TABLE IF NOT EXISTS block below is a DELIBERATE DEVIATION from
-- the mod_pdungeon_member_spells_undead_demon.sql precedent: the AC updater
-- applies files in FILENAME order (UpdateFetcher.cpp:64-96) and
-- 'mod_pdungeon_member_spells_worgen.sql' sorts BEFORE
-- 'mod_pdungeon_packs.sql', the only file that creates this table. On this box
-- the table exists and the block is free; on a database built from scratch it
-- is the difference between applying and failing.
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
-- delete is an EXPLICIT ENTRY LIST, not a BETWEEN: a range delete over
-- 3852..3927 would wipe an operator's rows for seventy-five unrelated
-- templates, and it would reach into entries the sibling packs authored
-- alongside this one.
DELETE FROM `pdungeon_member_spells` WHERE `entry` IN
 (3852,3853,3854,3855,3857,3859,3860,3927);

INSERT INTO `pdungeon_member_spells`
  (`entry`, `spellId`, `slot`, `cooldownMs`, `minDiff`, `enabled`) VALUES
  -- ==========================================================================
  -- PACK 6 "Shadowfang Pack" - MELEE (role 0)
  -- ==========================================================================
  -- 3852 Shadowfang Bloodhowler  (unit_class 1, display 202 scale 1.00)
  (3852, 67879, 1,  7000,  1, 1),  -- Claw              weapon 150%, 5 yd single   t0
  (3852, 16509, 1, 10000, 50, 1),  -- Rend              5 yd bleed, 15 s           t50
  (3852, 22644, 1, 12000, 75, 1),  -- Blood Leech       10 yd area leech           t75
  -- 3854 Shadowfang Wolfguard  (unit_class 1, display 203 scale 1.00)
  (3854, 48640, 1,  6000,  1, 1),  -- Strike            weapon 150%, 5 yd single   t0
  (3854,  8599, 1, 10000, 50, 1),  -- Enrage            self damage + haste buff   t50
  (3854, 59126, 1, 12000, 75, 1),  -- Shadow Breath     15 yd cone damage          t75
  -- 3857 Shadowfang Glutton  (unit_class 1, display 202 scale 1.00)
  (3857,  7122, 1,  6000,  1, 1),  -- Blood Tap  (ITS OWN)  5 yd health leech      t0
  (3857, 50729, 1, 10000, 50, 1),  -- Carnivorous Bite  5 yd damage + bleed        t50
  (3857, 69900, 1, 12000, 75, 1),  -- Spirit Burst      self-centred 15 yd AoE     t75
  -- 3859 Shadowfang Ragetooth  (unit_class 1, display 736 scale 1.15)
  (3859, 42397, 1,  7000,  1, 1),  -- Rend Flesh        5 yd damage + bleed        t0
  (3859,  7072, 1, 10000, 50, 1),  -- Wild Rage  (ITS OWN)  self damage + scale    t50
  (3859, 11428, 1, 60000, 75, 1),  -- Knockdown         5 yd  CC (MECHANIC_STUN)   t75
  -- ==========================================================================
  -- PACK 6 "Shadowfang Pack" - RANGE (role 1) - filler + two cooldown spells
  -- ==========================================================================
  -- 3853 Shadowfang Moonwalker  (unit_class 2, 7988 mana, display 729 scale 1.00)
  (3853, 61562, 0,     0,  1, 1),  -- Shadow Bolt       40 yd single, 1.5 s, 0/0   t0 FILLER
  (3853, 48125, 1, 10000, 50, 1),  -- Shadow Word: Pain 30 yd DoT, 0/22% mana      t50
  (3853, 17228, 1, 12000, 75, 1),  -- Shadow Bolt Volley self-centred 30 yd AoE    t75
  -- 3855 Shadowfang Darksoul  (unit_class 2, 7988 mana, display 657 scale 0.85)
  (3855, 60015, 0,     0,  1, 1),  -- Shadow Bolt       40 yd single, 3.0 s, 0/0   t0 FILLER
  (3855, 47960, 1, 10000, 50, 1),  -- Shadowflame       100 yd single DoT          t50
  (3855, 13338, 1, 12000, 75, 1),  -- Curse of Tongues  30 yd cast-speed -50%      t75
  -- 3860 Shadowfang Tainted One  (unit_class 2, 7988 mana, display 574 scale 1.30)
  (3860, 69211, 0,     0,  1, 1),  -- Shadow Bolt       30 yd single, 2.2 s, 0/0   t0 FILLER
  (3860, 61563, 1, 10000, 50, 1),  -- Corruption        30 yd DoT, 12 s, 0/0       t50
  (3860, 48687, 1, 12000, 75, 1),  -- Shadow Bolt Volley self-centred 30 yd AoE    t75
  -- ==========================================================================
  -- PACK 6 "Shadowfang Pack" - BOSS (role 2)
  -- ==========================================================================
  -- 3927 Wolf Master Nandos  (unit_class 1, rank 1, display 11179 scale 1.50)
  (3927, 70191, 1,  7000,  1, 1),  -- Cleave            weapon 120%, 5 yd single   t0
  -- Round C / C6: a second base ability per boss (operator, 2026-09-08: "2 Basis, dann je eine auf 50 und 75")
  (3927, 42397, 1,  9000,  1, 1),  -- Rend Flesh        5 yd damage + bleed        t0 #2
  (3927,  8599, 1, 10000, 50, 1),  -- Enrage            self damage + haste buff   t50
  (3927, 15588, 1, 12000, 75, 1);  -- Thunderclap       self-centred 10 yd AoE     t75
