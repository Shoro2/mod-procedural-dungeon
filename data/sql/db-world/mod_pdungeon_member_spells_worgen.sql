-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: combat kits for pack 6 "Shadowfang Pack" (world DB)
--
-- 25 rows for the 8 members of pack 6 (see mod_pdungeon_packs_worgen.sql): three
-- per trash creature, and FOUR for the BOSS 27580 Selas, which carries a second
-- minDiff 1 row since Round C / C6. Column order and cadence rule match the
-- shipped mod_pdungeon_member_spells.sql exactly:
--
--   MELEE + BOSS   position 1  cd 6000-8000 ms   minDiff 1
--                  position 2  cd 8000-10000 ms  minDiff 50
--                  position 3  cd 8000-12000 ms  minDiff 75
--   RANGE          position 1  the slot-0 filler, cd 0, minDiff 1
--                  position 2  cd 8000-10000 ms  minDiff 50
--                  position 3  cd 8000-12000 ms  minDiff 75
--   CC             ALWAYS cd 60000, and NEVER position 1
--
-- with that file's one Round C / C6 exception (operator, 2026-09-08: "2 Basis,
-- dann je eine auf 50 und 75"): a BOSS carries a SECOND row at position 1 at
-- cd 9000 ms, deliberately past the 6000-8000 band so that two base abilities
-- together do not double what one boss put out before. `position` was never a
-- database column - `slot` tells the RANGE filler apart and nothing else - so
-- the boss's two position-1 rows are two rows at minDiff 1 sharing slot 1.
--
-- Inside a band the heavier ability takes the longer end: 6000 a light single
-- weapon strike, 7000 a cleave or a normal single-target ability, 8000+ an AoE,
-- a DoT, a channel or anything with a cast time.
--
-- ROW ORDER IS PRIORITY ORDER. The loader queries
-- `ORDER BY entry, slot, minDiff, spellId` (PDv2PackMgr.cpp:239-245) and the AI
-- casts the FIRST ready row (PDv2CreatureAI.cpp:1436-1455). The boss's two
-- minDiff-1 rows therefore sort 16169 before 48130, i.e. the 7000 ms opener
-- really is the higher-priority row and the 9000 ms one is the follow-up. (The
-- first version of this file had 42397 @9000 sorting ahead of 70191 @7000 and
-- said the opposite in its comment.)
--
-- ----------------------------------------------------------------------------
-- IDENTITY - how every id below was verified (2026-09-09, this box)
--
-- The authority is C:\wowstuff\dcore\Data\dbc\Spell.dbc (55100 records, 234
-- fields, 936-byte records - the file the worldserver actually loads), NOT
-- acore_world.spell_dbc, which is an unrelated custom FL table and holds NONE of
-- these ids (verified: SELECT ID FROM spell_dbc WHERE ID IN (...) -> 0 rows).
-- The 234-field layout comes from the 234-column CREATE TABLE spell_dbc in
-- azerothcore-wotlk/data/sql/base/db_world/spell_dbc.sql, and every effect /
-- aura / mechanic / target label is parsed live out of the core's own
-- SharedDefines.h and SpellAuraDefines.h, so a label here cannot drift from the
-- numbering the running worldserver.exe was built from.
--
-- 24 distinct spell ids, each checked for: identity (the spell IS what the
-- comment claims), effect shape, implicit target, radius, cast time, mechanic /
-- aura CC status, power cost, reach from where the mob will stand, AND - new in
-- this pass - EffectRealPointsPerLevel, because a base-points reading alone
-- misses spells that scale (see the 8599 finding below).
--
--   id     name                dmg @80 (measured)      shape
--   14516  Strike              weapon swing + 247      WEAPON_DAMAGE, single, 5 yd
--   59021  Vicious Bite        6475 - 7525             SCHOOL_DAMAGE, single, 5 yd
--   42395  Lacerating Slash    15615 (1735 x 9 @2 s)   PERIODIC_DAMAGE bleed 18 s, 5 yd
--   48640  Strike              weapon swing x 1.50     WEAPON_PERCENT_DAMAGE, single, 5 yd
--   50729  Carnivorous Bite    5725 - 6105             1710-2090 + bleed 803 x 5, 5 yd
--   59126  Shadow Breath       8788 - 10212            SCHOOL_DAMAGE, CONE_ENEMY_24, 15 yd
--   42746  Cleave              weapon swing x 1.10     WEAPON_PERCENT_DAMAGE, single, 5 yd
--   69900  Spirit Burst        3238 - 3762             SCHOOL_DAMAGE, TARGET_SRC_CASTER r15
--   36965  Rend                11250 (2250 x 5 @3 s)   PERIODIC_DAMAGE bleed 15 s, 5 yd
--   59992  Cleave              weapon swing + 240      WEAPON_DAMAGE, single, 5 yd
--   55249  Whirling Slash      5828 - 6172             2828-3172 + bleed 1000 x 3, SRC_CASTER r5
--   11428  Knockdown           150 - 166 + 2 s stun    MOD_STUN, Mechanic 12 STUN, 5 yd -- THE ONE CC
--   69211  Shadow Bolt         1313 - 1687             SCHOOL_DAMAGE, 30 yd, 2.2 s cast
--   54889  Shadow Shock        2960 - 3440             SCHOOL_DAMAGE, TARGET_SRC_CASTER r25
--   61563  Corruption          9990 - 11610            PERIODIC_DAMAGE 6 x @2 s, 30 yd
--   60015  Shadow Bolt         1273 - 1427             SCHOOL_DAMAGE, 40 yd, 3.0 s cast
--   60016  Corruption          5400 (675 x 8 @3 s)     PERIODIC_DAMAGE 24 s, 30 yd, instant
--   30854  Shadow Word: Pain   9000 (1500 x 6 @3 s)    PERIODIC_DAMAGE 18 s, 30 yd, instant
--   45031  Shadow Bolt Volley  4250 - 5750             SCHOOL_DAMAGE, SINGLE target despite
--                                                      the name (TARGET_UNIT_TARGET_ENEMY,
--                                                      EffectRadiusIndex 0), 40 yd, 1.0 s
--   57464  Shadow Bolt         8483 - 9517             SCHOOL_DAMAGE, 55 yd, 2.0 s cast
--   16169  Arcing Smash        weapon swing + 400      WEAPON_DAMAGE, CONE_ENEMY_24, radius 8
--   48130  Gore                12598 - 13402           6598-7402 + bleed 1000 x 6, 5 yd
--   42397  Rend Flesh          17525 - 18825           5850-7150 + bleed 2335 x 5, 5 yd
--   67860  Impale              17672 - 19828           SCHOOL_DAMAGE, CONE_ENEMY_104, radius 6
--                                                      (effect2 is a scriptless DUMMY - no
--                                                      spell_script_names row - so it does
--                                                      nothing and effect1 carries the spell)
--
-- EVERY one of the 24 is ManaCost 0 AND ManaCostPercentage 0, rangeMin 0,
-- MaxTargetLevel 0, TargetCreatureType 0, EquippedItemClass -1 or 2, and none
-- has a SpellDuration of -1 on a periodic aura. There is no longer a single
-- mana-costing row in this file (the first version carried 48125 Shadow Word:
-- Pain at 0 flat / 22%), which removes the whole CheckPower risk class rather
-- than arguing it away.
--
-- ----------------------------------------------------------------------------
-- REVIEW FIX 3 - two rows had a SpellScript bound in acore_world.spell_script_names
--
-- A global SpellScript runs no matter which AI holds the caster, so PDv2's
-- AllCreatureScript binder does NOT protect a pack from one. Every id considered
-- for this pack was therefore looked up in that table (2787 rows), and two hits
-- came back:
--
--   67879 Claw  -> spell_black_knight_ghoul_claw
--        (boss_black_knight.cpp:450-470). Its OnEffectHitTarget handler calls
--        GetCaster()->GetThreatMgr().ResetAllThreat() and then re-targets a
--        RANDOM player within 30 yd. On a PDv2 mob that is a threat wipe and a
--        forced target switch every time the row fires - a tank can never hold
--        anything that carries it. It was the first version's 3852 opener; it is
--        gone from this file. NOTE FOR A FUTURE CORPUS PASS, NOT FIXED HERE
--        (other files, other owners): 67879 is also live on 84271, 84273, 31528
--        and 31847, and appears in mod_pdungeon_member_spells_cult.sql (11551)
--        and mod_pdungeon_member_spells_faceless.sql (31104).
--
--   41351 Curse of Vitality -> spell_black_temple_curse_of_vitality_aura.
--        Considered as a caster tier-50 row and dropped for this reason before
--        it ever reached a row.
--
-- No id in the 24 above has a spell_script_names row, a spell_linked_spell row,
-- or an acore_world.spell_dbc override. All three tables were queried for the
-- full set.
--
-- ----------------------------------------------------------------------------
-- REVIEW FIX 4 - 8599 Enrage is a Berserk-class buff, not a +10% one
--
-- The first version of this file carried 8599 Enrage on 3854 at tier 50 and on
-- the boss at tier 50, described as "self damage + haste buff" on the strength
-- of its base points. Measured WITH EffectRealPointsPerLevel:
--
--   8599 Enrage  eff1 SPELL_AURA_MOD_DAMAGE_PERCENT_DONE (school mask 1)
--                     basePoints 10, EffectRealPointsPerLevel 1.0, baseLevel 1
--                     -> 10 + (80-1) x 1.0 = +89% PHYSICAL DAMAGE at level 80
--                eff2 SPELL_AURA_MOD_MELEE_HASTE +30%
--                duration 120000 ms, i.e. the rest of the fight
--
-- An 89%-damage / 30%-haste self buff that never falls off is the run-killer
-- class this project excludes by name. It is gone, and so is the whole
-- self-buff category in this pack; the same measurement rejected 8269 Frenzy
-- (+158 flat damage, +60% melee haste), 32714 Enrage (+50% melee haste) and
-- 52071 Killing Rage (+100% melee haste, Selas's own). 7072 Wild Rage, the
-- first version's 3859 tier-50 row, fails the opposite way: MOD_DAMAGE_DONE is
-- a FLAT +25 damage at level 80, i.e. a measured no-op.
--
-- ----------------------------------------------------------------------------
-- REVIEW FIX 5 - the fillers are back inside the shipped band
--
-- A slot-0 filler is cast whenever it is ready, so its output is damage divided
-- by cast time. Measured against every filler the corpus already ships:
--
--   47809 Shadow Bolt R13    694 - 774   / 3.0 s  =  231 - 258 dps   (84263/84281/84287)
--   42842 Frostbolt R16      803 - 865   / 3.0 s  =  268 - 288 dps   (84285)
--   22088 Fireball           765 - 1035  / 2.5 s  =  306 - 414 dps   (30482/18859)
--   60015 Shadow Bolt       1273 - 1427  / 3.0 s  =  424 - 476 dps   (30203/28350/31529/24919)
--   69211 Shadow Bolt       1313 - 1687  / 2.2 s  =  597 - 767 dps   (32284/18870/10471)
--
-- The first version gave 3853 the filler **61562 Shadow Bolt**: 4250-5750 on a
-- 1.5 s cast = 2833-3833 dps, i.e. 3.7x to 5.0x the top of that band and ~10x
-- the pack's own second caster. It is gone. This file's three fillers are 69211
-- (597-767 dps) and 60015 twice (424-476 dps) - both already shipped as fillers
-- on four and three corpus entries respectively, both 0 flat / 0 percent, both
-- reaching well past the deployed V2.CastRangeYd of 25.0 (30 and 40 yd).
--
-- Two casters sharing one filler id is corpus-normal: shipped pack 4 gives 60015
-- to both 28350 and 30203.
--
-- ----------------------------------------------------------------------------
-- REVIEW FIX 6 - every kit now CLIMBS, and no boss row is a trash row
--
-- The first version had five measured no-ops (7122 Blood Tap 20, 22644 Blood
-- Leech 240, 16509 Rend 135, 17228 Shadow Bolt Volley 128-172, 15588
-- Thunderclap 251-259) sitting beside 17500-damage rows, three of them in a
-- difficulty-GATED slot - so unlocking tier 75 made a mob hit for less than its
-- own tier-0 row. All five are gone. Measured per-member curves at difficulty 0
-- (weapon rows quoted for a dm 1.7 uc1 swing of 317-397, dm 4.6 857-1074 for the
-- boss):
--
--   3914  564-644  ->  6475-7525   ->  15615            climbing
--   3854  476-596  ->  5725-6105   ->   8788-10212      climbing
--   3857  349-437  ->  3238-3762   ->  11250            climbing
--   3859  557-637  ->  5828-6172   ->  the CC           the CC is the tier-75 payload
--   3853 1313-1687 ->  2960-3440   ->   9990-11610      climbing
--   3855 1273-1427 ->  5400        ->   9000            climbing
--   2529 1273-1427 ->  4250-5750   ->   8483-9517       climbing
--   27580 1257-1474 / 12598-13402  ->  17525-18825  ->  17672-19828   climbing
--
-- and the boss's four rows (16169, 48130, 42397, 67860) are carried by NO member
-- of this pack, so a player who has cleared four pack-6 trash rooms still meets
-- four new abilities in the boss room. His opener 16169 Arcing Smash is used by
-- no other pack in the module either - the first version's 70191 Cleave @7000
-- was byte-identical to Mal'Ganis's opener row.
--
-- ----------------------------------------------------------------------------
-- CC CLASSIFICATION (aura / mechanic read out of Spell.dbc, not guessed)
--
--  spell  name        why it is CC                                placed
--  11428  Knockdown   Mechanic 12 STUN, aura SPELL_AURA_MOD_STUN  3859 @75
--
-- That is the WHOLE CC budget of pack 6: one row, on the melee member the scale
-- ladder already marks as one of the biggest plain worgen, at cooldownMs 60000
-- and minDiff 75, never in slot 0. SpellDuration is 2000 ms, so a 2 s stun on a
-- 60 s cooldown that cannot chain. No other row in this file carries a CC
-- mechanic or a CC aura - the whole file was re-scanned for the
-- fear/stun/root/sleep/silence/disorient/horror/charm set.
--
-- MECHANIC_BLEED (15) on 42395, 36965, 55249, 50729, 48130 and 42397 is NOT CC -
-- it is a damage-type tag, it is not in the operator's list, and the shipped
-- file's own written doctrine already excludes snare and interrupt on the same
-- grounds.
--
-- Two CC candidates were deliberately NOT taken, because the budget is one:
--   7295 Soul Drain    3914 Rethilgore's OWN spell (smart_scripts) - but its
--        effect2 is SPELL_AURA_MOD_ROOT for 10 s at 40 yd, which would be a
--        SECOND CC and a far heavier one than the 2 s stun.
--   53239 Axe Volley   27580 Selas's OWN opener - and the reason it could never
--        have been used anyway: both of its effects target TARGET_UNIT_CASTER,
--        a PERIODIC_TRIGGER_SPELL on the holder plus SPELL_AURA_MOD_ROOT on the
--        holder. It would root the boss in place for 4 s every time it fired.
--
-- ----------------------------------------------------------------------------
-- POWER COST
--
-- Six of the eight members are unit_class 1 (2529, 3854, 3857, 3859, 3914,
-- 27580). creature_classlevelstats has basemana 0 for class 1 at every level, so
-- Creature::SelectLevel gives them CreateMana 0 and Power(MANA) 0. From the
-- engine, not from a rule of thumb: SpellInfo::CalcPowerCost adds
-- ManaCostPercentage as a percentage OF GetCreateMana() - 0 - so a
-- percentage-only cost resolves to 0, while a FLAT ManaCost is added
-- unconditionally and Spell::CheckPower then fails with NO_POWER.
--
-- Every one of the 24 spell ids in this file is 0 flat AND 0 percent, on every
-- member, filler and cooldown row alike. That is stronger than the rule needs
-- and it is deliberate: Creature::Regenerate gives an in-combat creature only
-- Spirit/5 + 17 mana per interval, so a percentage-cost row empties even a real
-- pool in six to nine casts and then fails silently for the rest of the fight.
-- The two unit_class 2 casters (3853, 3855) have 7988 mana at level 80 and never
-- spend a point of it.
--
-- ----------------------------------------------------------------------------
-- REACH - what "range" means for a PDv2 caster
--
-- UpdateCasterCombat plants a role-1 mob wherever it happens to be INSIDE
-- V2.CastRangeYd - me->IsWithinCombatRange(victim, castRange), not "at exactly
-- 25 yd" (PDv2CreatureAI.cpp:1475-1500) - and the cooldown is spent on the
-- ATTEMPT (PDv2CreatureAI.cpp:1450-1453), so a spell that refuses for range
-- costs the mob a whole rotation slot rather than being retried. Every row on a
-- role-1 member here therefore reaches at least 25 yd: the fillers are 30/40/40
-- yd, and the six cooldown rows are 25 (54889 is TARGET_SRC_CASTER with radius
-- 25), 30, 30, 30, 40 and 55 yd. The self-centred rows that do NOT reach that
-- far - 69900 radius 15 and 55249 radius 5 - and every 5-yd row sit on melee
-- members or on the boss, never on a role-1 row. No row anywhere in this file
-- has a non-zero rangeMin.
--
-- ----------------------------------------------------------------------------
-- THREE NON-PROBLEMS, recorded so the next reader does not re-litigate them
--
--  * EquippedItemClass 2 on 14516 Strike, 48640 Strike and 59992 Cleave is not a
--    creature problem: Spell::CheckItems returns SPELL_CAST_OK immediately for a
--    non-player caster. (It IS a trap for custom PLAYER spells - a different
--    table and a different problem.)
--  * PowerType 1 (rage) on 42746 Cleave, 36965 Rend, 16169 Arcing Smash, 48130
--    Gore and 11428 Knockdown, and PowerType 3 (energy) on 59021 Vicious Bite,
--    are not problems while the cost is 0/0: Spell::CheckPower compares
--    GetPower(type) against the cost, and 0 < 0 is false.
--  * 67860 Impale's effect2 is SPELL_EFFECT_DUMMY. Guard rule R4 only bites when
--    EVERY effect is a scriptless dummy; here effect1 is 17672-19828 real cone
--    damage and the dummy simply does nothing (no spell_script_names row).
--
-- ----------------------------------------------------------------------------
-- EXCLUDED, and why - the spells these creatures own that are NOT used
--
--  67879 Claw            spell_script_names -> threat wipe + random retarget.
--                        See REVIEW FIX 3.
--  53239 Axe Volley      (27580's own)  TARGET_UNIT_CASTER periodic trigger AND
--                        TARGET_UNIT_CASTER MOD_ROOT - it roots the boss.
--  52071 Killing Rage    (27580's own)  +100% melee haste for 6 s. Berserk class.
--   7295 Soul Drain      (3914's own)   10 s MOD_ROOT at 40 yd - a second CC,
--                        and its leech is basePoints 35 (a no-op) besides.
--   7124 Arugal's Gift   (2529's own)   PERIODIC_DAMAGE 175-225 with
--                        EffectAmplitude 60000 over a 300 s duration on a 2.5 s
--                        cast - at most one or two ticks ever land in a fight,
--                        and the cast time eats a melee member's swings.
--   7107 Summon Wolfguard Worg (3854's own)  SPELL_EFFECT_SUMMON_PET.
--   7489 Call Lupine Horror / 7487 Call Bleak Worg  SPELL_EFFECT_SUMMON. A
--        summoned pet is not tracked by PDv2InstanceScript's _roomAlive counter
--        and its despawn path is unaudited.
--   7106 Dark Restore    (3854's own)   SPELL_EFFECT_HEAL - no self-heal loops;
--        ModifyHealReceived is deliberately not hooked (PDv2Scaling.cpp:317-321).
--    970 Shadow Word: Pain R1 (3855's own)  baseLevel 4; the level-80-scale
--        30854 is used instead.
--   8599 Enrage / 8269 Frenzy / 32714 Enrage / 7072 Wild Rage  see REVIEW FIX 4.
--  61562 Shadow Bolt     4250-5750 on a 1.5 s cast - see REVIEW FIX 5.
--  15588 Thunderclap / 22644 Blood Leech / 16509 Rend / 7122 Blood Tap /
--  17228 Shadow Bolt Volley   measured no-ops at level 80 - see REVIEW FIX 6.
--  13338 Curse of Tongues  MOD_CASTING_SPEED_NOT_STACK -50% with a 15 s duration
--        against a 12 s row cooldown, i.e. ~100% uptime from difficulty 75 on
--        every caster and healer in the room, for zero damage. Dropped rather
--        than shipped with an operator note.
--  48125 Shadow Word: Pain  the file's last mana-costing row (0 flat / 22%);
--        30854 is the same effect at 0/0 and 6.5x the damage.
--
-- THIS FILE SHIPS ZERO creature_template AND ZERO spell_dbc ROWS.
--
-- The CREATE TABLE IF NOT EXISTS block below IS load-bearing, unlike the one in
-- the companion packs file: UpdateFetcher::PathCompare compares
-- filename().string() (UpdateFetcher.cpp:521-524), and
-- 'mod_pdungeon_member_spells_worgen.sql' sorts BEFORE 'mod_pdungeon_packs.sql'
-- ('m' < 'p'), which is the only other file that creates this table. On this box
-- the table exists and the block is free; on a database built from scratch it is
-- the difference between applying and failing.
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

-- Idempotent re-apply: delete this file's own entries, then insert. Never DROP -
-- an operator who gave a creature of their own a kit keeps it. The delete is an
-- EXPLICIT ENTRY LIST, not a BETWEEN: a range delete over 2529..27580 would wipe
-- an operator's rows for twenty-five thousand unrelated templates and would
-- reach into the entries the sibling packs authored alongside this one (cult
-- 1853..11551, faceless 29309..31104). The list below is exactly the eight
-- members of pack 6 and nothing else. It also still covers 3852, 3860 and 3927 -
-- the three entries this revision REMOVED from the pack - so an operator who
-- applied the first version does not keep three orphaned kits:
DELETE FROM `pdungeon_member_spells` WHERE `entry` IN
 (2529,3852,3853,3854,3855,3857,3859,3860,3914,3927,27580);

INSERT INTO `pdungeon_member_spells`
  (`entry`, `spellId`, `slot`, `cooldownMs`, `minDiff`, `enabled`) VALUES
  -- ==========================================================================
  -- PACK 6 "Shadowfang Pack" - MELEE (role 0)
  -- ==========================================================================
  -- 3914 Rethilgore  (unit_class 1, dm 1.7, 21368 hp, display 524 scale 1.15)
  (3914, 14516, 1,  6000,  1, 1),  -- Strike            weapon + 247, 5 yd single         t0
  (3914, 59021, 1, 10000, 50, 1),  -- Vicious Bite      6475-7525, 5 yd single            t50
  (3914, 42395, 1, 12000, 75, 1),  -- Lacerating Slash  15615 bleed over 18 s, 5 yd       t75
  -- 3854 Shadowfang Wolfguard  (unit_class 1, dm 1.7, 16026 hp, display 203 scale 1.00)
  (3854, 48640, 1,  6000,  1, 1),  -- Strike            weapon 150%, 5 yd single          t0
  (3854, 50729, 1, 10000, 50, 1),  -- Carnivorous Bite  5725-6105 (hit + bleed), 5 yd     t50
  (3854, 59126, 1, 12000, 75, 1),  -- Shadow Breath     8788-10212, 15 yd cone            t75
  -- 3857 Shadowfang Glutton  (unit_class 1, dm 1.7, 16026 hp, display 202 scale 1.00)
  (3857, 42746, 1,  7000,  1, 1),  -- Cleave            weapon 110%, 5 yd single          t0
  (3857, 69900, 1, 10000, 50, 1),  -- Spirit Burst      3238-3762, self-centred 15 yd     t50
  (3857, 36965, 1, 12000, 75, 1),  -- Rend              11250 bleed over 15 s, 5 yd       t75
  -- 3859 Shadowfang Ragetooth  (unit_class 1, dm 1.7, 16026 hp, display 736 scale 1.15)
  (3859, 59992, 1,  7000,  1, 1),  -- Cleave            weapon + 240, 5 yd single         t0
  (3859, 55249, 1, 10000, 50, 1),  -- Whirling Slash    5828-6172, self-centred 5 yd      t50
  (3859, 11428, 1, 60000, 75, 1),  -- Knockdown         2 s stun + 150-166  CC (STUN)     t75
  -- ==========================================================================
  -- PACK 6 "Shadowfang Pack" - RANGE (role 1) - filler + two cooldown spells
  -- ==========================================================================
  -- 3853 Shadowfang Moonwalker  (unit_class 2, dm 1.7, 12822 hp / 7988 mana, display 729 scale 1.00)
  (3853, 69211, 0,     0,  1, 1),  -- Shadow Bolt       1313-1687, 30 yd, 2.2 s, 0/0      t0 FILLER
  (3853, 54889, 1, 10000, 50, 1),  -- Shadow Shock      2960-3440, self-centred 25 yd     t50
  (3853, 61563, 1, 12000, 75, 1),  -- Corruption        9990-11610 over 12 s, 30 yd       t75
  -- 3855 Shadowfang Darksoul  (unit_class 2, dm 1.7, 12822 hp / 7988 mana, display 657 scale 0.85)
  (3855, 60015, 0,     0,  1, 1),  -- Shadow Bolt       1273-1427, 40 yd, 3.0 s, 0/0      t0 FILLER
  (3855, 60016, 1, 10000, 50, 1),  -- Corruption        5400 over 24 s, 30 yd, instant    t50
  (3855, 30854, 1, 12000, 75, 1),  -- Shadow Word: Pain 9000 over 18 s, 30 yd, instant    t75
  -- 2529 Son of Arugal  (unit_class 1, dm 1.7, 16026 hp, display 1098 scale 1.45)
  (2529, 60015, 0,     0,  1, 1),  -- Shadow Bolt       1273-1427, 40 yd, 3.0 s, 0/0      t0 FILLER
  (2529, 45031, 1, 10000, 50, 1),  -- Shadow Bolt Volley 4250-5750, 40 yd single, 1.0 s   t50
  (2529, 57464, 1, 12000, 75, 1),  -- Shadow Bolt       8483-9517, 55 yd, 2.0 s           t75
  -- ==========================================================================
  -- PACK 6 "Shadowfang Pack" - BOSS (role 2)
  -- ==========================================================================
  -- 27580 Selas  (unit_class 1, dm 4.6, 75600 hp, display 26793 NorthrendWorgen scale 3.00)
  -- None of these four rows is carried by any member of this pack, and 16169 is
  -- carried by no other pack in the module. Rows sort 16169 -> 48130 by spellId,
  -- so the 7000 ms opener is genuinely the higher-priority row.
  (27580, 16169, 1,  7000,  1, 1), -- Arcing Smash      weapon + 400, 8 yd cone           t0
  -- Round C / C6: a second base ability per boss (operator, 2026-09-08: "2 Basis, dann je eine auf 50 und 75")
  (27580, 48130, 1,  9000,  1, 1), -- Gore              12598-13402 (hit + bleed), 5 yd   t0 #2
  (27580, 42397, 1, 10000, 50, 1), -- Rend Flesh        17525-18825 (hit + bleed), 5 yd   t50
  (27580, 67860, 1, 12000, 75, 1); -- Impale            17672-19828, 6 yd cone            t75
