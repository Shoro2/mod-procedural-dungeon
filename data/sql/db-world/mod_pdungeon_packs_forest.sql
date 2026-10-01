-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: the FOREST packs 12-14 (world database), Round F / F2
--
-- The second themed set, and it follows mod_pdungeon_packs_mine.sql exactly:
-- packs 1-8 are "any look" on purpose, packs 9-11 are the three rosters
-- Blizzard itself put in a MINE, and these three are the three it put in a
-- WOOD. A Deviate raptor prowling a lamplit city street is the same wrong
-- picture a Defias pirate was. So they carry `theme = 3`.
--
--   pack 12  Druids of the Fang  Wailing Caverns (map 43)   - the green rot
--   pack 13  Maraudon Grove      Maraudon (map 349)         - the vine deep
--   pack 14  Razorfen Thicket    Razorfen Kraul (map 47)    - the thorn warren
--
-- WHAT `theme = 3` DOES TO THE DRAW (Round F / F1, Task 1)
-- PDv2PackMgr holds every enabled pack of every theme and filters at draw
-- time by the PLAN's theme. With `ProceduralDungeon.V2.Packs.ThemeExclusive`
-- = 1 (the default) and at least one themed pack carrying a usable non-boss
-- member in the run's band, a theme-3 run draws from THESE THREE ONLY and the
-- theme-0 packs sit the run out; every other theme keeps today's behaviour
-- and never sees them. Set the key to 0 and they merely join the pool. That
-- is why all three ship `enabled = 1` and `unlock_dlvl = 0`: a forest run
-- that unlocked nothing would have an empty pool, not a smaller one.
--
-- THE BOSS POOL IS SHARED ACROSS THE THEME, not per pack. One PACK draw
-- themes each room's trash, but the boss pick "always draws from the role-2
-- pool across ALL packs" (src/generator/PDv2PackDraw.cpp) - so theme 3's boss
-- pool is the NINE role-2 rows below, against `GameBossRooms` = 4 at the
-- shipped `V2.DlvlCap`. Round C / C6's no-repeat clause therefore binds on
-- every real forest run (9 distinct bosses > 4 boss rooms) and its fallback
-- stays unreachable, which is the same shape the eight theme-0 bosses have.
--
-- THIS FILE SHIPS ZERO creature_template ROWS AND ZERO UPDATES TO THEM, and
-- zero spell_dbc rows. Every entry below is stock 3.3.5 content shared with
-- the live world; editing its stats, level, faction or ScriptName here would
-- retune Wailing Caverns, Maraudon and Razorfen Kraul for every player on the
-- server. PDv2 does not need to - see the next two paragraphs.
--
-- LEVELS: `level_min`/`level_max` 80/80 is the BAND SELECTOR, not a
-- description of the creature. A pack enters the pool when its range
-- intersects the player's chosen band [bandMin, bandMin+4], and every member
-- is force-levelled on spawn by OnBeforeCreatureSelectLevel regardless of the
-- native 18..48 written in creature_template. Same shape as packs 1-11, so
-- the only band that selects any of them is 76..80.
--
-- SCRIPTS: not one of the 36 entries carries a `ScriptName` (measured, the
-- whole column is empty across the set - packs 9-11 had two). `AIName`
-- 'SmartAI' on all 36 is irrelevant: PDv2CreatureAIBinder is an
-- AllCreatureScript (src/PDv2CreatureAI.cpp:1919-1976) and
-- AllCreatureScript::GetCreatureAI is asked BEFORE the template script and
-- before the AIName factory (azerothcore-wotlk
-- src/server/game/AI/CreatureAISelector.cpp:78-89), so the binder claims
-- every ownerless, non-critter, non-event-host creature on the PD map and all
-- 36 get PDv2MobAI. Nothing here can make
-- ScriptMgr::CheckIfScriptsInDatabaseExist complain, because nothing here
-- names a script at all.
--
-- ----------------------------------------------------------------------------
-- MEASURED ON THIS BOX 2026-09-11 against the running acore_world, joined to
-- creature_classlevelstats at level 80 - hp80/mana80 below is what the module
-- will ACTUALLY hand the creature BEFORE the difficulty multiplier, not the
-- template's own native number.
--
-- All 36 entries exist. None carries difficulty_entry_1/2/3 (no heroic twin
-- can be substituted under the player), none carries npcflag, VehicleId or
-- flags_extra, all are MovementType 0 or 1, none is already in packs 1-11,
-- and every one has a creature_loot_template of its own (5..81 rows) - which
-- is the whole point of 01 §8's "packs are built from creatures that already
-- exist": the native drops stay intact and the dungeon becomes a
-- target-farming mechanism. unit_flags is 0, 0x40 (UNIT_FLAG_UNK_6) or
-- 0x8040 (+ UNIT_FLAG_SWIMMING) - the same harmless set packs 6 and 9 ship.
--
-- unit_class: 1 WARRIOR, 2 PALADIN, 8 MAGE. The trap pack 4's header warns
-- about - creature_classlevelstats.basehp1 = 1 at level 80 for class 8 -
-- CANNOT fire here: basehp1 is the exp-1 column and all 36 entries are
-- `exp = 0`, so every one of them reads basehp0 (class 1: 5342, class 2:
-- 4274, class 8: 3739), times the template's own HealthModifier. Measured,
-- not assumed. mana80 is basemana (class 2: 3994, class 8: 8814) times
-- ManaModifier, which is why two members of the same class differ.
--
-- FACTIONS, re-measured from FactionTemplate.dbc field 5 (0-based 3/4/5 =
-- ourMask/friendMask/enemyMask):
--   270 (Deviate/Fang), 14 (Monster), 16 (Beast - Wild), 90 (Satyr/Dryad),
--   91 (Maraudon Elemental), 834 (Theradrim), 152 (Quilboar),
--   154 (Death's Head)
--   ALL EIGHT: ourMask 0x08, friendMask 0x00, enemyMask 0x01, and not one
--   carries a single enemyFaction entry.
-- enemyMask 0x01 is FACTION_MASK_PLAYER, so all eight are hostile to players
-- and to nobody else; a pack that mixes four of them (pack 13 does) cannot
-- fight itself. 152 and 154 go further and list each other in friendFaction
-- (109/111), which is why the Death's Head cultists stand beside the Kraul
-- quilboar in pack 14 without a word of argument. Mixing factions inside one
-- pack is corpus-normal: pack 4 mixes five, pack 10 mixes five.
--
-- ----------------------------------------------------------------------------
-- *** THE 8911 RULE: NO MEMBER IS IMMUNE TO A SCHOOL OF MAGIC ***
--
-- Round F / F1 dropped 8911 Fireguard Destroyer from pack 11 because its
-- creature_template FIRE immunity survives the PD scaling and a trash mob one
-- spec cannot damage is a bug report, not flavour. The forest rosters are
-- FULL of that case, so every candidate was joined to `creature_immunities`
-- and read out; **all 36 shipped entries measure SchoolMask 0**. The ones
-- that did not are named here so nobody re-adds them by eye:
--
--   -5    11784 Theradrim Guardian, 11783 Theradrim Shardling, 4526 Wind
--         Howler      school=0x8 NATURE           DROPPED
--   -66   13282 Noxxion (a Maraudon wing boss)    school=0x10 FROST  DROPPED
--   -231  12237 Meshlok the Harvester (rare)      school=0x8 NATURE  DROPPED
--   -326  12201 Princess Theradras (the Maraudon
--         END boss)                               school=0x8 NATURE  DROPPED
--
-- Losing Theradras, Noxxion and Meshlok is why pack 13's bosses are
-- Razorlash, Landslide and Rotgrip: they are the three named Maraudon
-- creatures whose immunity row is mechanic-only. Nothing was "fixed" by
-- editing a creature_template row - see the paragraph above about retuning
-- the live world.
--
-- The mechanic-only immunities that DO ship, stated here rather than
-- discovered in a fight:
--   -93   3654 Mutanus, 3669 Cobrahn, 3670 Pythas, 3671 Anacondra,
--         3673 Serpentis
--         SchoolMask 0, mech 0x1000020 FEAR|HORROR
--         -> the mildest row in the module; stun, silence, root and
--            polymorph all still land on every Wailing Caverns boss.
--   -229  4420 Ramtusk, 4421 Charlga, 4422 Agathelos, 12203 Landslide,
--         12258 Razorlash, 13596 Rotgrip
--         SchoolMask 0, mech 0x48966CA6 CHARM|DISORIENTED|FEAR|ROOT|SLEEP|
--         SNARE|FREEZE|KNOCKOUT|POLYMORPH|BANISH|SHACKLE|TURN|DAZE|SAPPED
--         -> the same row 646 Mr. Smite and 3586 Miner Johnson already carry
--            (pack 9): still STUNNABLE and SILENCEABLE, and no wider than
--            what the module already shipped.
-- No member carries -238 or -251, so the forest has no stun-immune and no
-- silence-immune boss at all - it is CC-friendlier than the mine.
--
-- ----------------------------------------------------------------------------
-- ONE SILHOUETTE PER ROW
--
-- creature_template_model was read for all 36: **no two members of the same
-- pack share a CreatureDisplayID.** 4623 Quilguard Champion was the one
-- candidate that failed it - display 6103, the same model as 4442 Razorfen
-- Defender - and was replaced by 4541 Blood of Agamaggan (display 4754, a
-- type-10 blood elemental), for the reason pack 11's header gives about the
-- fifth Anvilrage dwarf: another quilboar in the same armour is a name, not a
-- silhouette. 3840 Druid of the Fang brings four displays of its own
-- (4211/4232/4233/4234), so the one entry never reads as one model.
--
-- RAZORFEN DOWNS (map 129) IS DELIBERATELY NOT HERE, although "Razorfen"
-- names both instances and the design note lists 7355 Tuten'kash and 7358
-- Amnennar the Coldbringer. Its whole roster is UNDEAD (type 6: Withered,
-- Skeletal, Splinterbone, ghouls, Frozen Souls), the module already ships two
-- undead packs - 4 Barrow Dead and 7 Cult of the Damned - and a wall of
-- skeletons under a FOREST theme would read as the crypt those two already
-- are. Pack 14 is therefore the LIVING Kraul: quilboar, their boars, their
-- bats and the Death's Head cult that lives with them. Downs stays available
-- for a crypt or blight theme, where it belongs.
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
-- (a Dryad who roots, a Geomancer who throws stone) are a later round's work
-- and deliberately not faked here.
--
-- The two ids are the ones packs 4-11 already use, for the reason pack 7's
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
-- ids, they are already proven to cast on this map.) Both were re-checked
-- against the MERGED SpellDifficulty store this round, per packs-research
-- §2.5.1: **zero** hits in SpellDifficulty.dbc (581 records) and **zero** in
-- the world table `spelldifficulty_dbc` (604 rows), so neither id is
-- substituted under `GetSpawnMode()` and what the row says is what casts.
--
-- Which caster gets the senior bolt is decided by a stated rule rather than
-- by taste: the pack's HIGHEST NATIVE LEVEL caster takes 69211, ties broken
-- by the larger level-80 mana pool, and - new this round, because pack 13
-- ties on both - a remaining tie by the LOWER entry id, which is a property
-- of the table and not of the row order. Everyone else takes 60015.
--   pack 12  3673 Lord Serpentis  native 20, 11982 mp -> 69211
--            (ties 3671/3669/3670 on level, wins on mana: 11982 vs 3994)
--   pack 13  11793 Celebrian Dryad native 45,  5991 mp -> 69211
--            (ties 12224 Cavern Shambler on level AND on mana; 11793 < 12224)
--   pack 14  4516 Death's Head Adept native 25, 13221 mp -> 69211
--            (ties 4440/4520 on level, wins on mana: mage basemana 8814)
--
-- ROLE 1 GOES TO A MANA CLASS wherever the roster has one (unit_class 2 or
-- 8), which is what packs 9-11 did as well. Wailing Caverns and Razorfen
-- Kraul each offer three; Maraudon's LIVING roster contains exactly two
-- (11793 Celebrian Dryad and 12224 Cavern Shambler - every other candidate
-- with a mana class is a named boss), so pack 13 ships two casters and seven
-- melee rather than promoting a boss into the trash pool. 2 of 9 non-boss
-- members is inside the corpus: pack 11 is 2 of 6.
--
-- ----------------------------------------------------------------------------
-- The CREATE TABLE IF NOT EXISTS blocks below are REDUNDANCY. They are
-- byte-identical to mod_pdungeon_packs_mine.sql's copies, and their DDL is
-- identical to the canonical definitions in mod_pdungeon_packs.sql (that file
-- additionally carries a six-line comment on `theme` which neither themed
-- copy repeats). '.' is 0x2E and '_' is 0x5F, so the canonical file sorts
-- BEFORE this one under the updater's plain
-- std::string filename compare (UpdateFetcher.cpp:521-524) and has already
-- created both tables. Kept for the reason mod_pdungeon_packs_faceless.sql
-- states - the file stays self-contained if the shipped one is ever renamed
-- or split, and IF NOT EXISTS makes them free either way.
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
-- keeps them, and so do the eleven packs this file does not own.
DELETE FROM `pdungeon_pack_members` WHERE `packId` IN (12, 13, 14);
DELETE FROM `pdungeon_packs` WHERE `id` IN (12, 13, 14);

INSERT INTO `pdungeon_packs`
  (`id`, `name`, `theme`, `level_min`, `level_max`, `unlock_dlvl`, `enabled`) VALUES
  (12, 'Druids of the Fang', 3, 80, 80, 0, 1),
  (13, 'Maraudon Grove',     3, 80, 80, 0, 1),
  (14, 'Razorfen Thicket',   3, 80, 80, 0, 1);

INSERT INTO `pdungeon_pack_members`
  (`packId`, `entry`, `role`, `casterSpellId`, `weight`) VALUES
  -- Pack 12 "Druids of the Fang" - Wailing Caverns (map 43), 12 members:
  -- 6 melee, 3 range, 3 boss. Faction 270 on all twelve - the one
  -- single-faction pack of the three - and rank 1 throughout.
  -- The roster is deliberately half BEAST and half HUMANOID: six deviate
  -- animals - raptor, raptor, dreadfang, viper, wind serpent, a walking plant
  -- - under three snake-men who cast, which is what stops a room reading as
  -- six copies of one lizard. The four Fang lords split two and two: Serpentis
  -- and Anacondra hold the caster slots they already are in lore, Cobrahn and
  -- Pythas take boss rooms beside Mutanus. That a named mini-boss stands in a
  -- trash slot is corpus-normal in the other direction too - pack 10's boss
  -- 4857 Stone Keeper is ordinary Uldaman trash.
  (12, 3636, 0,     0, 100),  -- Deviate Ravager       uc1, 16026 hp, disp 1747,  12 loot rows
  (12, 3637, 0,     0, 100),  -- Deviate Guardian      uc1, 16026 hp, disp 755,   15 loot rows
  (12, 5056, 0,     0, 100),  -- Deviate Dreadfang     uc1, 16026 hp, disp 3006,  19 loot rows
  (12, 5755, 0,     0, 100),  -- Deviate Viper         uc1, 13355 hp, disp 4312,  24 loot rows
  (12, 5756, 0,     0, 100),  -- Deviate Venomwing     uc1, 16026 hp, disp 2706,  23 loot rows
  (12, 5761, 0,     0, 100),  -- Deviate Shambler      uc2, 12822 hp, disp 1084, type 4 ELEMENTAL
  (12, 3673, 1, 69211, 100),  -- Lord Serpentis        uc2, 25644 hp, 11982 mp (RANGE, senior), immu -93
  (12, 3671, 1, 60015, 100),  -- Lady Anacondra        uc2, 21370 hp,  3994 mp (RANGE), immu -93
  (12, 3840, 1, 60015, 100),  -- Druid of the Fang     uc2, 12822 hp,  7988 mp (RANGE), 4 displays
  (12, 3654, 2,     0, 100),  -- Mutanus the Devourer  BOSS, uc1, 42736 hp, immu -93
  (12, 3669, 2,     0, 100),  -- Lord Cobrahn          BOSS, uc2, 21370 hp, immu -93
  (12, 3670, 2,     0, 100),  -- Lord Pythas           BOSS, uc2, 21370 hp, immu -93
  -- Pack 13 "Maraudon Grove" - Maraudon (map 349), 12 members: 7 melee,
  -- 2 range, 3 boss. FOUR factions (16 Beast - Wild on the plants and
  -- Razorlash, 90 Satyr on the satyrs and the dryad, 834 Theradrim on the
  -- Cavern Shambler, 91 on Landslide, 14 on the Diemetradon and Rotgrip),
  -- every one of them friendly to nobody and hostile to players only - so
  -- they cannot fight each other; see the header's measurement.
  --
  -- This is the ONLY pack in the module built around walking plants: four of
  -- the seven melee are type 4 ELEMENTAL vines and treants, which is what
  -- gives the forest a trash silhouette the mine and the city do not have.
  -- The two satyrs and the Diemetradon are the contrast. Princess Theradras,
  -- Noxxion and Meshlok - the three names a Maraudon pack would obviously
  -- want - are all SCHOOL-IMMUNE and all dropped; see the 8911 rule above.
  (13, 12220, 0,     0, 100),  -- Constrictor Vine          uc1, 16026 hp, type 4, 31 loot rows
  (13, 12219, 0,     0, 100),  -- Barbed Lasher             uc1, 24039 hp, type 4, 27 loot rows
  (13, 13141, 0,     0, 100),  -- Deeprot Stomper           uc1, 16026 hp, type 4, disp 2079
  (13, 13142, 0,     0, 100),  -- Deeprot Tangler           uc1, 16026 hp, type 4, disp 13098
  (13, 11790, 0,     0, 100),  -- Putridus Satyr            uc1, 16026 hp, type 3 DEMON
  (13, 11791, 0,     0, 100),  -- Putridus Trickster        uc1, 16026 hp, type 3 DEMON
  (13, 13323, 0,     0, 100),  -- Subterranean Diemetradon  uc1, 16026 hp, type 1 BEAST
  (13, 11793, 1, 69211, 100),  -- Celebrian Dryad           uc2, 12822 hp, 5991 mp (RANGE, senior)
  (13, 12224, 1, 60015, 100),  -- Cavern Shambler           uc2, 12822 hp, 5991 mp (RANGE), type 4
  (13, 12258, 2,     0, 100),  -- Razorlash                 BOSS, uc1, 34723 hp, type 4, immu -229
  (13, 12203, 2,     0, 100),  -- Landslide                 BOSS, uc1, 37394 hp, type 5 GIANT, immu -229
  (13, 13596, 2,     0, 100),  -- Rotgrip                   BOSS, uc1, 37394 hp, type 1, immu -229
  -- Pack 14 "Razorfen Thicket" - Razorfen Kraul (map 47), 12 members: 6 melee,
  -- 3 range, 3 boss. Faction 152 on the quilboar, 16 on their beasts, 154 on
  -- the Death's Head cult - and 152/154 name each other as FRIENDS, which is
  -- the tightest-knit pack in the module. The warren's own menagerie (a boar,
  -- a bat, a blood elemental) is what keeps six quilboar from reading as one
  -- quilboar; see the silhouette note in the header for why 4623 Quilguard
  -- Champion is not among them.
  (14, 4436, 0,     0, 100),  -- Razorfen Quilguard     uc1, 16026 hp, disp 6108, 14 loot rows
  (14, 4442, 0,     0, 100),  -- Razorfen Defender      uc1, 16026 hp, disp 6103, 21 loot rows
  (14, 6035, 0,     0, 100),  -- Razorfen Stalker       uc1, 16026 hp, disp 6106, 16 loot rows
  (14, 4511, 0,     0, 100),  -- Agam'ar                uc1, 16026 hp, type 1 BEAST, 81 loot rows
  (14, 4538, 0,     0, 100),  -- Kraul Bat              uc1, 16026 hp, type 1 BEAST, disp 1955
  (14, 4541, 0,     0, 100),  -- Blood of Agamaggan     uc1, 16026 hp, type 10, disp 4754
  (14, 4516, 1, 69211, 100),  -- Death's Head Adept     uc8, 14956 hp, 13221 mp (RANGE, senior), faction 154
  (14, 4440, 1, 60015, 100),  -- Razorfen Totemic       uc2, 12822 hp,  3994 mp (RANGE)
  (14, 4520, 1, 60015, 100),  -- Razorfen Geomancer     uc2, 12822 hp,  5991 mp (RANGE)
  (14, 4421, 2,     0, 100),  -- Charlga Razorflank     BOSS, uc2, 42740 hp, 11982 mp, immu -229
  (14, 4420, 2,     0, 100),  -- Overlord Ramtusk       BOSS, uc1, 37394 hp, immu -229
  (14, 4422, 2,     0, 100);  -- Agathelos the Raging   BOSS, uc1, 42736 hp, type 1, immu -229
