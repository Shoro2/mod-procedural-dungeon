-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: combat kits for the Task 11 packs (world database)
--
-- 72 rows, three per creature, for the 24 members of pack 4 "Barrow Dead"
-- (undead) and pack 5 "Legion Rift" (demons) - see
-- mod_pdungeon_packs_undead_demon.sql. Column order and cadence rule match
-- the shipped mod_pdungeon_member_spells.sql exactly:
--
--   MELEE + BOSS   position 1  cd 6000-8000 ms   minDiff 1
--                  position 2  cd 8000-10000 ms  minDiff 50
--                  position 3  cd 8000-12000 ms  minDiff 75
--   RANGE          position 1  the slot-0 filler, cd 0, minDiff 1
--                  position 2  cd 8000-10000 ms  minDiff 50
--                  position 3  cd 8000-12000 ms  minDiff 75
--   CC             ALWAYS cd 60000, and NEVER position 1
--
-- Every id below was checked against Data\dbc\Spell.dbc (55100 records - the
-- file the worldserver actually loads, NOT acore_world.spell_dbc, which is
-- an unrelated 5542-row custom table and holds none of these ids) with a
-- guard script that verifies, per row: identity (the spell is what the
-- comment claims, not merely that some spell exists at that id), a slot-0
-- filler range >= 25 yd, flat ManaCost 0 on every 0-mana creature, CC at
-- cd 60000 and never in slot 0, no summon/knockback/jump effect, and no
-- (entry, spellId) primary-key duplicate. Run against these 72 rows AND the
-- 74 rows already live in this table: `checked 72 proposed rows, 0 problems`.
-- Full output (including two pre-existing, unrelated findings the same
-- script surfaced on the 74 live rows) is in
-- .superpowers/sdd/task-11-report.md.
--
-- ----------------------------------------------------------------------------
-- POWER COST
--
-- Every unit_class 1 member in these two packs (basemana 0 at level 80, same
-- trap as the shipped file's uc1 mobs) is kept to spells with flat ManaCost
-- 0 - 27 of the 28 distinct spell ids used below are 0 ManaCost AND 0
-- ManaCostPercentage. The lone exception is 700 Sleep (flat ManaCost 60),
-- and it is placed ONLY on the two unit_class 2 casters (30203, 31529),
-- which have nonzero basemana (3994 at level 80) and can afford it; it never
-- appears on a unit_class 1 entry.
--
-- The three slot-0 fillers - 60015, 69211, 22088, all "Shadow Bolt"/
-- "Fireball" variants - were picked for the same reason the shipped file's
-- header explains at length: Creature::Regenerate gives an in-combat
-- creature only Spirit/5 + 17 mana per interval, so a percentage-cost filler
-- (like the shipped set's 47809/47857/42842) empties its caster in six to
-- nine casts and then silently fails CheckPower for the rest of the fight.
-- All three fillers here are 0 ManaCost AND 0 ManaCostPercentage - free
-- forever, not merely affordable - and each reaches at least
-- ProceduralDungeon.V2.CastRangeYd (25): 60015 Shadow Bolt 40 yd, 69211
-- Shadow Bolt 30 yd, 22088 Fireball 100 yd (SpellRange.dbc, measured
-- 2026-09-01).
--
-- ----------------------------------------------------------------------------
-- CC CLASSIFICATION (aura/mechanic read out of Spell.dbc, not guessed)
--
--  spell  name                 why it is CC                        placed
--  5918   Shadowstalker Stab   Mechanic 12 STUN, aura MOD_STUN      31847 @75
--  700    Sleep                Mechanic 10 SLEEP, aura MOD_STUN     30203 @75, 31529 @75
--  42917  Frost Nova R6        EffectMechanic 7 ROOT, aura MOD_ROOT 32284 @50
--  6215   Fear R3              Mechanic 5 FEAR, aura MOD_FEAR       18862 @75, 29620 @75
--
-- All four carry cooldownMs 60000 and sit at minDiff 50 or 75, never in
-- slot 0 - the same rule the shipped file's own CC table enforces.
--
-- ----------------------------------------------------------------------------
-- THE ECONOMY CHANGE THE OPERATOR SHOULD HEAR ABOUT BEFORE THE FIRST RUN
--
-- 9 of these 22 new trash members carry small native creature_loot_template
-- tables (Frostweave Cloth, Fur Clothing Scraps, Scourge Curio, and similar):
-- 29974, 30921, 31847, 30482, 32284, 20427, 18859, 18870, 24919. (Measured
-- 2026-09-01 against acore_world.creature_loot_template directly, not the
-- lootid column alone - the brief this file was built from estimated ten;
-- the true count is nine.) The shipped 84263-84290 all have lootid 0, so
-- PDv2 has never dropped anything but its own materials and the bonus-roll
-- table. Both new bosses (25352, 29620) are deliberately lootid 0. This is a
-- real, player-visible economy change and belongs in the Task 18 operator
-- document before the first run, not after.
--
-- THIS FILE SHIPS ZERO creature_template AND ZERO spell_dbc ROWS.
-- ----------------------------------------------------------------------------

-- Idempotent re-apply: delete this file's own entries, then insert. Never
-- DROP - an operator who gave a creature of their own a kit keeps it. The
-- delete is an explicit entry list, not a BETWEEN: the entries run from
-- 18859 to 32284, and a range delete over that span would wipe an
-- operator's rows for thousands of unrelated templates.
DELETE FROM `pdungeon_member_spells` WHERE `entry` IN
 (18859,18862,18870,18871,19746,20403,20427,23075,24919,25352,28349,28350,
  29620,29974,30203,30482,30921,31278,31438,31528,31529,31721,31847,32284);

INSERT INTO `pdungeon_member_spells`
  (`entry`, `spellId`, `slot`, `cooldownMs`, `minDiff`, `enabled`) VALUES
  -- ==========================================================================
  -- PACK 4 "Barrow Dead" - MELEE (role 0)
  -- ==========================================================================
  -- 31438 Shadow Vault Abomination  (unit_class 1)
  (31438, 70191, 1,  7000,  1, 1),  -- Cleave            weapon damage              t0
  (31438,  8599, 1, 10000, 50, 1),  -- Enrage            self damage buff           t50
  (31438, 59018, 1, 12000, 75, 1),  -- Bile Vomit        15 yd cone                 t75
  -- 31721 Frostbrood Sentry  (unit_class 1)
  (31721, 59126, 1,  8000,  1, 1),  -- Shadow Breath     15 yd cone damage          t0
  (31721, 22644, 1, 10000, 50, 1),  -- Blood Leech       10 yd area leech           t50
  (31721, 69900, 1, 12000, 75, 1),  -- Spirit Burst      15 yd area damage          t75
  -- 29974 Niffelem Forefather  (unit_class 1)
  (29974, 48640, 1,  6000,  1, 1),  -- Strike            weapon damage              t0
  (29974,  8599, 1, 10000, 50, 1),  -- Enrage            self damage buff           t50
  (29974, 54889, 1, 12000, 75, 1),  -- Shadow Shock      25 yd area damage          t75
  -- 28349 Risen Vrykul Berserker  (unit_class 1)
  (28349, 59992, 1,  7000,  1, 1),  -- Cleave            weapon damage              t0
  (28349, 42397, 1,  9000, 50, 1),  -- Rend Flesh        damage + bleed             t50
  (28349, 22644, 1, 12000, 75, 1),  -- Blood Leech       10 yd area leech           t75
  -- 30921 Skeletal Runesmith  (unit_class 1)
  (30921, 48640, 1,  6000,  1, 1),  -- Strike            weapon damage              t0
  (30921, 70654, 1, 10000, 50, 1),  -- Blood Armor       self damage-taken buff     t50
  (30921, 54889, 1, 12000, 75, 1),  -- Shadow Shock      25 yd area damage          t75
  -- 31278 Ravenous Ghoul  (unit_class 1)
  (31278, 48130, 1,  7000,  1, 1),  -- Gore              damage + bleed             t0
  (31278,  8599, 1, 10000, 50, 1),  -- Enrage            self damage buff           t50
  (31278, 22644, 1, 12000, 75, 1),  -- Blood Leech       10 yd area leech           t75
  -- 31847 Scavenging Geist  (unit_class 1)
  (31847, 67879, 1,  6000,  1, 1),  -- Claw              weapon damage              t0
  (31847, 50729, 1, 10000, 50, 1),  -- Carnivorous Bite  damage + bleed             t50
  (31847,  5918, 1, 60000, 75, 1),  -- Shadowstalker Stab 5 yd  CC (MOD_STUN)       t75
  -- ==========================================================================
  -- PACK 4 "Barrow Dead" - RANGE (role 1) - filler + two cooldown spells
  -- ==========================================================================
  -- 30203 Forgotten Depths High Priest  (unit_class 2)
  (30203, 60015, 0,     0,  1, 1),  -- Shadow Bolt       40 yd single, 0 mana       t0 FILLER
  (30203, 48125, 1, 10000, 50, 1),  -- Shadow Word: Pain 30 yd DoT                  t50
  (30203,   700, 1, 60000, 75, 1),  -- Sleep             30 yd  CC (Mechanic SLEEP) t75
  -- 32284 Scourge Soulbinder  (unit_class 2)
  (32284, 69211, 0,     0,  1, 1),  -- Shadow Bolt       30 yd single, 0 mana       t0 FILLER
  (32284, 42917, 1, 60000, 50, 1),  -- Frost Nova R6     10 yd  CC (MOD_ROOT)       t50
  (32284, 54889, 1, 12000, 75, 1),  -- Shadow Shock      25 yd area damage          t75
  -- 30482 Wrathstrike Gargoyle  (unit_class 2)
  (30482, 22088, 0,     0,  1, 1),  -- Fireball          100 yd single, 0 mana      t0 FILLER
  (30482, 47960, 1, 10000, 50, 1),  -- Shadowflame       100 yd single DoT          t50
  (30482, 48687, 1, 12000, 75, 1),  -- Shadow Bolt Volley 100 yd AoE                t75
  -- 28350 Risen Vrykul Magus  (unit_class 1 caster - basemana 0, see the power-cost block)
  (28350, 60015, 0,     0,  1, 1),  -- Shadow Bolt       40 yd single, 0 mana       t0 FILLER
  (28350, 47960, 1, 10000, 50, 1),  -- Shadowflame       100 yd single DoT          t50
  (28350, 71264, 1, 12000, 75, 1),  -- Swarming Shadows  any range, single DoT      t75
  -- ==========================================================================
  -- PACK 4 "Barrow Dead" - BOSS (role 2)
  -- ==========================================================================
  -- 25352 Scourge Overlord  (unit_class 1, rank 1)
  (25352, 59992, 1,  7000,  1, 1),  -- Cleave            weapon damage              t0
  (25352, 54889, 1, 10000, 50, 1),  -- Shadow Shock      25 yd area damage          t50
  (25352, 48687, 1, 12000, 75, 1),  -- Shadow Bolt Volley 100 yd AoE                t75
  -- ==========================================================================
  -- PACK 5 "Legion Rift" - MELEE (role 0)
  -- ==========================================================================
  -- 20427 Veneratus the Many  (unit_class 1)
  (20427, 48640, 1,  6000,  1, 1),  -- Strike            weapon damage              t0
  (20427, 54889, 1, 10000, 50, 1),  -- Shadow Shock      25 yd area damage          t50
  (20427, 69900, 1, 12000, 75, 1),  -- Spirit Burst      15 yd area damage          t75
  -- 20403 Legion Destroyer  (unit_class 1)
  (20403, 59992, 1,  7000,  1, 1),  -- Cleave            weapon damage              t0
  (20403,  8599, 1, 10000, 50, 1),  -- Enrage            self damage buff           t50
  (20403, 59126, 1, 12000, 75, 1),  -- Shadow Breath     15 yd cone damage          t75
  -- 31528 Plagued Felbeast  (unit_class 1)
  (31528, 67879, 1,  6000,  1, 1),  -- Claw              weapon damage              t0
  (31528, 50729, 1, 10000, 50, 1),  -- Carnivorous Bite  damage + bleed             t50
  (31528, 22644, 1, 12000, 75, 1),  -- Blood Leech       10 yd area leech           t75
  -- 23075 Legion Ring Infernal  (unit_class 1)
  (23075, 48640, 1,  7000,  1, 1),  -- Strike            weapon damage              t0
  (23075,  9034, 1, 10000, 50, 1),  -- Immolate          30 yd DoT                  t50
  (23075, 31340, 1, 12000, 75, 1),  -- Rain of Fire      100 yd ground AoE          t75
  -- 19746 Pit Breaker  (unit_class 1)
  (19746, 70191, 1,  7000,  1, 1),  -- Cleave            weapon damage              t0
  (19746,  8599, 1, 10000, 50, 1),  -- Enrage            self damage buff           t50
  (19746, 42397, 1,  9000, 75, 1),  -- Rend Flesh        damage + bleed             t75
  -- 18862 Dread Overlord  (unit_class 1)
  (18862, 48640, 1,  6000,  1, 1),  -- Strike            weapon damage              t0
  (18862,  9034, 1, 10000, 50, 1),  -- Immolate          30 yd DoT                  t50
  (18862,  6215, 1, 60000, 75, 1),  -- Fear R3           20 yd  CC (MOD_FEAR)       t75
  -- 18871 Voidlord  (unit_class 1)
  (18871, 59992, 1,  7000,  1, 1),  -- Cleave            weapon damage              t0
  (18871, 70654, 1, 10000, 50, 1),  -- Blood Armor       self damage-taken buff     t50
  (18871, 69900, 1, 12000, 75, 1),  -- Spirit Burst      15 yd area damage          t75
  -- ==========================================================================
  -- PACK 5 "Legion Rift" - RANGE (role 1) - filler + two cooldown spells
  -- ==========================================================================
  -- 31529 Ravishing Betrayer  (unit_class 2)
  (31529, 60015, 0,     0,  1, 1),  -- Shadow Bolt       40 yd single, 0 mana       t0 FILLER
  (31529, 48125, 1, 10000, 50, 1),  -- Shadow Word: Pain 30 yd DoT                  t50
  (31529,   700, 1, 60000, 75, 1),  -- Sleep             30 yd  CC (Mechanic SLEEP) t75
  -- 18859 Wrath Priestess  (unit_class 2)
  (18859, 22088, 0,     0,  1, 1),  -- Fireball          100 yd single, 0 mana      t0 FILLER
  (18859,  9034, 1, 10000, 50, 1),  -- Immolate          30 yd DoT                  t50
  (18859, 31340, 1, 12000, 75, 1),  -- Rain of Fire      100 yd ground AoE          t75
  -- 18870 Voidshrieker  (unit_class 2, ManaModifier 3 -> 11982 mana)
  (18870, 69211, 0,     0,  1, 1),  -- Shadow Bolt       30 yd single, 0 mana       t0 FILLER
  (18870, 54889, 1, 10000, 50, 1),  -- Shadow Shock      25 yd area damage          t50
  (18870, 47960, 1, 12000, 75, 1),  -- Shadowflame       100 yd single DoT          t75
  -- 24919 Wrath Herald  (unit_class 2)
  (24919, 60015, 0,     0,  1, 1),  -- Shadow Bolt       40 yd single, 0 mana       t0 FILLER
  (24919,  9034, 1, 10000, 50, 1),  -- Immolate          30 yd DoT                  t50
  (24919, 46480, 1, 12000, 75, 1),  -- Fel Lightning     30 yd single damage        t75
  -- ==========================================================================
  -- PACK 5 "Legion Rift" - BOSS (role 2)
  -- ==========================================================================
  -- 29620 Dreadlord Mal'Ganis  (unit_class 1, rank 1)
  (29620, 70191, 1,  7000,  1, 1),  -- Cleave            weapon damage              t0
  (29620,  9034, 1, 10000, 50, 1),  -- Immolate          30 yd DoT                  t50
  (29620,  6215, 1, 60000, 75, 1);  -- Fear R3           20 yd  CC (MOD_FEAR)       t75
