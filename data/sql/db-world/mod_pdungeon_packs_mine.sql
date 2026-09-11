-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: the MINE packs 9-11 (world database), Round F / F1
--
-- The first packs in this module that are NOT theme 0. Packs 1-8 are all
-- "any look" on purpose - crypt, demon, worgen and faceless trash reads as
-- well in a dark Gilneas street as underground, and scoping them to a theme
-- once left the city with nothing but the placeholder creature. These three
-- are the opposite case: they are the three stock rosters Blizzard itself put
-- in a MINE, and a Defias pirate patrolling a lamplit city street is exactly
-- the wrong picture. So they carry `theme = 1`.
--
--   pack  9  Defias Miners     Deadmines (map 36)            - the human dig
--   pack 10  Stonevault        Uldaman (map 70)              - the deep dig
--   pack 11  Dark Iron Forge   Blackrock Depths (map 230)    - the hot dig
--
-- WHAT `theme = 1` DOES TO THE DRAW (Round F / F1, Task 1)
-- PDv2PackMgr holds every enabled pack of every theme and filters at draw
-- time by the PLAN's theme. With `ProceduralDungeon.V2.Packs.ThemeExclusive`
-- = 1 (the default) and at least one themed pack carrying a usable non-boss
-- member in the run's band, a theme-1 run draws from THESE THREE ONLY and the
-- theme-0 packs sit the run out; every other theme keeps today's behaviour
-- and never sees them. Set the key to 0 and they merely join the pool. That
-- is why all three ship `enabled = 1` and `unlock_dlvl = 0`: a mine run that
-- unlocked nothing would have an empty pool, not a smaller one.
--
-- THIS FILE SHIPS ZERO creature_template ROWS AND ZERO UPDATES TO THEM, and
-- zero spell_dbc rows. Every entry below is stock 3.3.5 content shared with
-- the live world; editing its stats, level, faction or ScriptName here would
-- retune Deadmines, Uldaman and Blackrock Depths for every player on the
-- server. PDv2 does not need to - see the next two paragraphs.
--
-- LEVELS: `level_min`/`level_max` 80/80 is the BAND SELECTOR, not a
-- description of the creature. A pack enters the pool when its range
-- intersects the player's chosen band [bandMin, bandMin+4], and every member
-- is force-levelled on spawn by OnBeforeCreatureSelectLevel regardless of the
-- native 17..56 written in creature_template. Same shape as packs 1-8, so the
-- only band that selects any of them is 76..80.
--
-- SCRIPTS: two members carry a stock ScriptName - 646 `boss_mr_smite` and
-- 9033 `boss_general_angerforge` - and NEITHER runs on map 760.
-- PDv2CreatureAIBinder is an AllCreatureScript (src/PDv2CreatureAI.cpp:1919-
-- 1976), and AllCreatureScript::GetCreatureAI is asked BEFORE the template
-- script and before the AIName factory (azerothcore-wotlk
-- src/server/game/AI/CreatureAISelector.cpp:78-89). The binder claims every
-- ownerless, non-critter, non-event-host creature on the PD map, so all 32
-- entries get PDv2MobAI and the stock phase scripts stay behind in their own
-- dungeons. The names still resolve to compiled core scripts, so
-- ScriptMgr::CheckIfScriptsInDatabaseExist stays quiet. AIName 'SmartAI' on
-- 30 of the 32 is irrelevant for the same reason.
--
-- ----------------------------------------------------------------------------
-- MEASURED ON THIS BOX 2026-09-11 against the running acore_world, joined to
-- creature_classlevelstats at level 80 - hp80/mana80 below is what the module
-- will ACTUALLY hand the creature BEFORE the difficulty multiplier, not the
-- template's own level-19 number.
--
-- All 32 entries exist. None carries difficulty_entry_1/2/3 (no heroic twin
-- can be substituted under the player), none carries npcflag, VehicleId or
-- flags_extra, all are MovementType 1, and every one has a
-- creature_loot_template of its own (7..33 rows) - which is the whole point
-- of 01 §8's "packs are built from creatures that already exist": the native
-- drops stay intact and the dungeon becomes a target-farming mechanism.
-- unit_flags is 0, 0x40 (UNIT_FLAG_UNK_6) or 0x8040 (+ UNIT_FLAG_SWIMMING, a
-- swim animation nobody will see underground) - the same harmless set pack 6
-- already ships.
--
-- unit_class: 1 WARRIOR, 2 PALADIN, 8 MAGE. The trap pack 4's header warns
-- about - creature_classlevelstats.basehp1 = 1 at level 80 for classes 4 and
-- 8 - CANNOT fire here: basehp1 is the exp-1 column and all 32 entries are
-- `exp = 0`, so every one of them reads basehp0 (class 1: 5342, class 2:
-- 4274, class 8: 3739). Measured, not assumed.
--
-- FACTIONS, re-measured from FactionTemplate.dbc field 5 (0-based 3/4/5 =
-- ourMask/friendMask/enemyMask):
--   17 (Defias), 54 (Dark Iron), 59 (Stonevault), 411 (Shrike Bat),
--   415 (Stone Steward), 49 (Jadespine Basilisk)
--   ALL SIX: ourMask 0x08, friendMask 0x00, enemyMask 0x01
-- enemyMask 0x01 is FACTION_MASK_PLAYER, so all six are hostile to players;
-- friendMask 0x00 means none of them calls assistance for the others; and no
-- row carries a single enemyFaction entry, so a pack that mixes three of them
-- (pack 10 does) cannot fight itself. Mixing factions inside one pack is
-- corpus-normal: pack 4 mixes five, pack 5 mixes three.
--
-- CreatureImmunitiesId - four bosses and one trash member carry one, and this
-- is a step past the shipped corpus, so it is stated here rather than
-- discovered in a fight:
--   -229  646 Mr. Smite, 3586 Miner Johnson
--         mech 0x244B3653 CHARM|DISORIENTED|FEAR|ROOT|SLEEP|SNARE|FREEZE|
--         KNOCKOUT|POLYMORPH|BANISH|SHACKLE|TURN|DAZE|SAPPED
--         -> both are still STUNNABLE and SILENCEABLE.
--   -238  8923 Panzor the Invincible
--         mech 0x24813E53, the -229 set minus BANISH/SHACKLE/TURN PLUS STUN
--         -> stun-immune, silence still lands.
--   -251  9033 General Angerforge
--         mech 0x248B3F5B, the widest of the three: adds SILENCE and DISTRACT
--         -> neither stunnable nor silenceable. The hardest CC target the
--            module ships; a caster group has to out-damage him.
--   -63   8911 Fireguard Destroyer  *** SCHOOL IMMUNITY, READ THIS ***
--         SchoolMask 0x4 (FIRE) + mech 0x2206 DISORIENTED|DISARM|SLEEP|
--         KNOCKOUT. Every other immunity in this module is mechanic-only
--         (pack 4 carries -93, pack 7 -124, both SchoolMask 0). 8911 is the
--         first member that is IMMUNE TO A SCHOOL OF MAGIC: a fire mage or a
--         fire-affix hit does literally nothing to it. It is kept because it
--         is Blackrock's own forge elemental and the pack would be poorer
--         without it - but if the operator reports "my fire spec cannot kill
--         one of the dwarves", this row is the answer, and deleting this one
--         line is the whole fix.
--
-- 4861 Shrike Bat is type_flags 1 (TAMEABLE) and family 24 - a hunter can
-- tame one out of a mine run. Stock behaviour in any dungeon, harmless here
-- (a tamed pet has an owner, so PDv2CreatureAIBinder yields it), stated so it
-- is not filed as a bug.
--
-- ----------------------------------------------------------------------------
-- casterSpellId - the RANGE member's filler, and a FALLBACK only
--
-- pdungeon_member_spells is the truth for every spell a mob casts. This
-- column is read only when a role-1 member has no slot-0 row at all
-- (PDv2CreatureAI.cpp BuildKit), and a role-1 member with casterSpellId 0 AND
-- no member_spells rows is silently DEMOTED to melee at load
-- (PDv2PackMgr.cpp). THIS ROUND SHIPS NO member_spells ROWS FOR THESE EIGHT
-- CASTERS, so the fallback IS their whole ranged kit today; the flavour rows
-- (a Medic who heals, a Flameweaver who burns) are a later round's work and
-- deliberately not faked here.
--
-- The two ids are the ones packs 4-8 already use, for the reason pack 7's
-- header argues at length: a filler must be free FOREVER, not merely
-- affordable, because a mob that runs out of power has no out-of-power
-- fallback and just stands there. 60015 and 69211 are the two free ones in
-- the shipped band (ManaCost 0 flat AND 0 %), and both reach past
-- ProceduralDungeon.V2.CastRangeYd (25.0 in the deployed
-- dcore\configs\modules\mod_procedural_dungeon.conf):
--
--   69211  Shadow Bolt  30 yd  2.2 s  1313-1687  ~682 dps   (7 members use it)
--   60015  Shadow Bolt  40 yd  3.0 s  1273-1427  ~450 dps  (10 members use it)
--
-- (the use counts are live `pdungeon_member_spells` rows - these are not new
-- ids, they are already proven to cast on this map.)
--
-- Which caster gets the senior bolt is decided by a stated rule rather than
-- by taste: the pack's HIGHEST NATIVE LEVEL caster takes 69211, ties broken
-- by the larger level-80 mana pool, and everyone else takes 60015. That gives
-- one senior per pack, the same shape packs 6/7/8 have.
--   pack  9  1732 Squallshaper native 19      -> 69211
--   pack 10  7321 Flameweaver  38-39, 8814 mp -> 69211  (ties 4853 on level,
--                                                        wins on mana)
--   pack 11  8894 Anvilrage Medic 50-51, 17628 mp -> 69211 (ties 8912)
--
-- ----------------------------------------------------------------------------
-- The CREATE TABLE IF NOT EXISTS blocks below are REDUNDANCY, byte-identical
-- to the canonical definitions in mod_pdungeon_packs.sql: '.' is 0x2E and '_'
-- is 0x5F, so that file sorts BEFORE this one under the updater's plain
-- std::string filename compare and has already created both tables. Kept for
-- the reason mod_pdungeon_packs_faceless.sql states - the file stays
-- self-contained if the shipped one is ever renamed or split, and IF NOT
-- EXISTS makes them free either way.
--
-- role: 0 melee, 1 range, 2 boss.
-- ----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS `pdungeon_packs` (
  `id` INT UNSIGNED NOT NULL,
  `name` VARCHAR(64) NOT NULL,
  `theme` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `level_min` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `level_max` TINYINT UNSIGNED NOT NULL DEFAULT 80,
  `unlock_dlvl` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `pdungeon_pack_members` (
  `packId` INT UNSIGNED NOT NULL,
  `entry` INT UNSIGNED NOT NULL,
  `role` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `casterSpellId` INT UNSIGNED NOT NULL DEFAULT 0,
  `weight` SMALLINT UNSIGNED NOT NULL DEFAULT 100,
  PRIMARY KEY (`packId`, `entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Idempotent re-apply: delete this file's own three ids, then insert. Never
-- DROP and never a BETWEEN range - an operator who added packs of their own
-- keeps them, and so do the eight packs this file does not own.
DELETE FROM `pdungeon_pack_members` WHERE `packId` IN (9, 10, 11);
DELETE FROM `pdungeon_packs` WHERE `id` IN (9, 10, 11);

INSERT INTO `pdungeon_packs`
  (`id`, `name`, `theme`, `level_min`, `level_max`, `unlock_dlvl`, `enabled`) VALUES
  ( 9, 'Defias Miners',   1, 80, 80, 0, 1),
  (10, 'Stonevault',      1, 80, 80, 0, 1),
  (11, 'Dark Iron Forge', 1, 80, 80, 0, 1);

INSERT INTO `pdungeon_pack_members`
  (`packId`, `entry`, `role`, `casterSpellId`, `weight`) VALUES
  -- Pack 9 "Defias Miners" - Deadmines (map 36), 12 members: 7 melee, 3 range,
  -- 2 boss. Faction 17 and type 7 HUMANOID throughout, rank 1 on eleven of the
  -- twelve (3586 Miner Johnson is rank 2, a rare). The goblins are the mine's
  -- workforce and the Defias its guards - one roster, two silhouettes, which
  -- is what stops a room reading as seven copies of one model.
  ( 9,  657, 0,     0, 100),  -- Defias Pirate        uc1, 16026 hp, 29 loot rows
  ( 9,  634, 0,     0, 100),  -- Defias Overseer      uc1, 16026 hp, 20 loot rows
  ( 9,  636, 0,     0, 100),  -- Defias Blackguard    uc1, 16026 hp, 14 loot rows
  ( 9, 4417, 0,     0, 100),  -- Defias Taskmaster    uc1, 16026 hp, 21 loot rows
  ( 9,  641, 0,     0, 100),  -- Goblin Woodcarver    uc1, 16026 hp, 26 loot rows
  ( 9, 1731, 0,     0, 100),  -- Goblin Craftsman     uc1, 16026 hp, 15 loot rows
  ( 9, 3947, 0,     0, 100),  -- Goblin Shipbuilder   uc1, 16026 hp, 26 loot rows
  ( 9, 1732, 1, 69211, 100),  -- Defias Squallshaper  uc8, 11217 hp, 17628 mp (RANGE, senior)
  ( 9, 1729, 1, 60015, 100),  -- Defias Evoker        uc8, 11217 hp, 17628 mp (RANGE)
  ( 9, 4418, 1, 60015, 100),  -- Defias Wizard        uc8, 11217 hp, 17628 mp (RANGE)
  ( 9,  646, 2,     0, 100),  -- Mr. Smite            BOSS, uc1, 42736 hp, immu -229
  ( 9, 3586, 2,     0, 100),  -- Miner Johnson        BOSS, uc1, 16026 hp, immu -229, rank 2
  -- Pack 10 "Stonevault" - Uldaman (map 70), 11 members: 7 melee, 3 range,
  -- 1 boss. FIVE factions in one pack (59 Stonevault on six of them, 415 on
  -- the Steward, 411 on the bat, 49 on the basilisk, 54 on the Relic Hunter),
  -- every one of them friendly to nobody and hostile to players only - so
  -- they cannot fight each other; see the header's measurement. The bat and
  -- the basilisk are the cave's own fauna and the reason this pack does not
  -- read as "seven troggs".
  (10, 4855, 0,     0, 100),  -- Stonevault Brawler        uc1, 16026 hp, 33 loot rows
  (10, 4850, 0,     0, 100),  -- Stonevault Cave Lurker    uc1, 16026 hp, 28 loot rows
  (10, 4860, 0,     0, 100),  -- Stone Steward             uc1, 16026 hp, type 4 ELEMENTAL
  (10, 7320, 0,     0, 100),  -- Stonevault Mauler         uc1, 16026 hp, 31 loot rows
  (10, 4861, 0,     0, 100),  -- Shrike Bat                uc1, 16026 hp, type 1 BEAST, tameable
  (10, 4863, 0,     0, 100),  -- Jadespine Basilisk        uc1, 16026 hp, type 1 BEAST
  (10, 4847, 0,     0, 100),  -- Shadowforge Relic Hunter  uc2, 12822 hp, faction 54
  (10, 7321, 1, 69211, 100),  -- Stonevault Flameweaver    uc8, 11217 hp,  8814 mp (RANGE, senior)
  (10, 4852, 1, 60015, 100),  -- Stonevault Oracle         uc2, 12822 hp,  3994 mp (RANGE)
  (10, 4853, 1, 60015, 100),  -- Stonevault Geomancer      uc2, 12822 hp,  3994 mp (RANGE)
  (10, 4857, 2,     0, 100),  -- Stone Keeper              BOSS, uc1, 16026 hp, type 4, no immunities
  -- Pack 11 "Dark Iron Forge" - Blackrock Depths (map 230), 9 members: 5
  -- melee, 2 range, 2 boss. Faction 54 throughout. The smallest of the three
  -- on purpose: the Anvilrage ladder is four near-identical dwarves, and a
  -- fifth would have added a name, not a silhouette.
  (11, 8891, 0,     0, 100),  -- Anvilrage Guardsman         uc1, 16026 hp, 17 loot rows
  (11, 8892, 0,     0, 100),  -- Anvilrage Footman           uc1, 16026 hp, 18 loot rows
  (11, 8893, 0,     0, 100),  -- Anvilrage Soldier           uc1, 16026 hp, 16 loot rows
  (11, 8890, 0,     0, 100),  -- Anvilrage Warden            uc1, 16026 hp,  1 loot row
  (11, 8911, 0,     0, 100),  -- Fireguard Destroyer         uc1, 16026 hp, type 4, immu -63 FIRE-IMMUNE
  (11, 8894, 1, 69211, 100),  -- Anvilrage Medic             uc8, 11217 hp, 17628 mp (RANGE, senior)
  (11, 8912, 1, 60015, 100),  -- Twilight's Hammer Torturer  uc2, 12822 hp,  7988 mp (RANGE)
  (11, 8923, 2,     0, 100),  -- Panzor the Invincible       BOSS, uc1, 26710 hp, immu -238, rank 2
  (11, 9033, 2,     0, 100);  -- General Angerforge          BOSS, uc1, 42736 hp, immu -251
