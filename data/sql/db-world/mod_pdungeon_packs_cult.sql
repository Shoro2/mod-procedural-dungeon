-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: the Cult of the Damned pack (world database)
--
-- Pack 7 "Cult of the Damned" - Scholomance. Four type 6 risen undead and
-- three type 7 robed cultists, all faction 233 (Scholomance), plus the
-- plague-born horror the cult built, on faction 16 (Monster). Where pack 4
-- "Barrow Dead" is Northrend Scourge, this one is the cult that MADE it: the
-- necromancers' school, its raised soldiery, and the thing in the cellar.
--
-- Pack id 7 is free: SELECT MAX(id) FROM pdungeon_packs = 5 on this box
-- (re-measured 2026-09-09), and the shipped files' own deletes reach only
-- `id BETWEEN 1 AND 3` (mod_pdungeon_packs.sql) and `id IN (4, 5)`
-- (mod_pdungeon_packs_undead_demon.sql), so neither can ever touch pack 7.
-- This file's delete is scoped to id 7 alone in return - it never uses a
-- BETWEEN and it never DROPs, so an operator's own packs survive a re-apply.
--
-- theme 0 = any look, for the same reason all five shipped packs use it:
-- robed cultists and risen dead read fine under every look PDv2 ships, not
-- just one. A pack whose theme is neither 0 nor the live V2.Theme is
-- invisible to the loader and the dungeon fills with the placeholder
-- creature (PDv2PackMgr.cpp:96-121).
--
-- level_min/level_max 80/80 is the BAND SELECTOR, not a description of the
-- creature: a pack enters the pool when its range intersects the player's
-- chosen band [bandMin, bandMin+4]. The seven Scholomance entries are native
-- level 55-61 vanilla stock and are force-levelled to 80 on spawn by
-- OnBeforeCreatureSelectLevel (PDv2Scaling.cpp:215-230); the boss is already
-- native 80/80, so it is the one member the force-level leaves alone. Either
-- way the only band that selects this pack is 76..80.
--
-- ----------------------------------------------------------------------------
-- WHAT THIS PACK WEIGHS AND WHAT IT HITS FOR - BOTH AXES, MEASURED
--
-- The first cut of this pack published health and left damage implied. Both
-- columns are load-bearing and both are below. Health at level 80 is
-- creature_classlevelstats.basehp[exp] x HealthModifier x _GetHealthMod(rank);
-- melee damage is
--
--   swing = (damage_base[exp] {x1.5 for the max} + AttackPower/14 x BaseVariance)
--           x creature_template.DamageModifier
--
-- i.e. Creature::SelectLevel (Creature.cpp:1538-1553) fed into
-- Unit::CalculateMinMaxDamage (StatSystem.cpp:1164-1172). Every
-- Rate.Creature.* key in the deployed C:\wowstuff\dcore\configs\
-- worldserver.conf (lines 2990-3032) is 1, so _GetHealthMod/_GetDamageMod
-- return 1.0 and `rank` 1 (elite) costs and buys nothing beyond the gold
-- dragon on the nameplate. Level-80 class stats used: uc1 basehp0 5342,
-- basehp2 12600, AP 642, dmg_base 47.2377 / dmg_exp2 164.924; uc2 basehp0
-- 4274, basemana 3994, AP 608, dmg_base 44.2013.
--
--   entry  name                    uc exp  hp80   mana80  DmgMod  swing      dps
--   10486  Risen Warrior            1  0   26710       0     3.5  326-409    153
--   10488  Risen Construct          1  0   26710       0     3.5  326-409    184
--   10489  Risen Guard              1  0   16026       0     3.5  326-409    184
--   11551  Necrofiend               1  0   16026       0     3.5  326-409    184
--   10477  Scholomance Necromancer  2  0   12822   11982     3.5  307-384    173
--   10476  Scholomance Necrolyte    2  0   12822   11982     3.5  307-384    173
--   10471  Scholomance Acolyte      2  0   12822   11982     3.5  307-384    173
--   36879  Plagueborn Horror  BOSS  1  2  189000       0     7.5  1581-2199 1260
--
-- (dps = swing midpoint / BaseAttackTime; 10486 swings at 2400 ms, the boss
-- at 1500 ms, everything else at 2000 ms. BaseVariance is 1 on all eight.)
--
-- The trash spread on DamageModifier is 1.00x - all seven carry exactly 3.5,
-- so no member hits several times as hard as the packmate standing next to
-- it. The boss carries 7.5, which is 2.14x its trash and the same figure the
-- shipped stock bosses 25352 Scourge Overlord and 29620 Dreadlord Mal'Ganis
-- carry. Health lands in 12822..26710 for the trash - the same band as the
-- shipped 84263-84290 trash (12600..32000) - and the difficulty dial does the
-- rest. The shipped packs' trash is `rank` 0, so the elite frames on this
-- pack's eight ARE a visible difference: intentional (vanilla dungeon trash
-- is elite), not a defect.
--
-- ----------------------------------------------------------------------------
-- THE BOSS - WHY IT IS 36879 AND NOT 1853 DARKMASTER GANDLING
--
-- The first cut of this pack shipped 1853 Darkmaster Gandling on the strength
-- of his name. Measured, he was the weakest role-2 member in the whole pool
-- on every axis at once, and a boss room drawn from this pack would have been
-- EASIER than the trash room before it:
--
--   1853 Gandling   85480 hp (57% of the 149560 shipped boss floor)
--                   DamageModifier 2.3 - BELOW his own trash's 3.5, so he
--                     swung for 202-252 against their 326-409: 113 dps, i.e.
--                     0.62x a Risen Warrior and 0.12x a shipped stock boss
--                   display 11070 = Character\Human\Male\HumanMale.mdx at
--                     scale 1.50 - the same rig as this pack's own robed
--                     casters AND as the shipped boss 84288 Dralak (display
--                     26271, the same HumanMale.mdx at 1.00)
--                   all four kit rows recycled from his own trash casters
--
-- 36879 Plagueborn Horror replaces him. Every column below was read out of
-- creature_template / creature_classlevelstats / CreatureDisplayInfo.dbc on
-- 2026-09-09, not inherited:
--
--   health          189000 = basehp2 12600 x HealthModifier 15. That is 7.1x
--                   this pack's heaviest trash and 14.7x its lightest, and
--                   126% of the shipped boss floor (149560 on 84288-84290;
--                   252000 on 25352; 327600 on 29620). The shipped precedent
--                   spans 4.7x-26x its own trash, so this sits inside it.
--   damage          DamageModifier 7.5 (= the shipped stock bosses), exp 2
--                   and BaseAttackTime 1500 -> 1581-2199 a swing, 1260 dps.
--                   That is 6.9x this pack's trash and 1.33x the shipped
--                   25352/29620 (945 dps at their 2000 ms). It is 9.4x the
--                   custom 84288-84290, whose BaseVariance is 0 and whose
--                   melee is therefore only 321-481 at 3000 ms (134 dps) -
--                   those three carry their weight in health and kit, not
--                   in swings. Expect this boss room to be a real step up.
--   silhouette      display 23681 -> CreatureDisplayInfo -> CreatureModelData
--                   = Creature\SuperZombie\superzombie.mdx at scale 1.50.
--                   Nothing in this pack and no member of the role-2 pool
--                   draws that model: the pack's own rigs are Skeleton.mdx
--                   (x2), BoneGolem.mdx, CryptFiend.mdx and three robed
--                   humanoid rigs, and the shipped bosses are HumanMale.mdx,
--                   DreadLord.mdx, CryptLord.mdx, Lich.mdx and MalGanis.mdx.
--                   Boss rooms draw from the GLOBAL role-2 pool with no
--                   theming (PDv2PackDraw.cpp:352-354), so that check has to
--                   be made against every pack, and it was.
--   flags           npcflag 0, unit_flags 64 (UNK_6) - never 0x2000000
--                   NOT_SELECTABLE, never the 0x300 immunity pair -
--                   flags_extra 0 (in particular no 0x80000000 HARD_RESET,
--                   the flag that cost Herald Volazj his slot), VehicleId 0,
--                   type 6 UNDEAD, rank 1, no ` (1)` name suffix, and NO
--                   creature_template_addon row at all, so nothing spawns
--                   with a permanent school immunity or an invisibility aura.
--   faction         16, re-measured against FactionTemplate.dbc field 5:
--                   enemyGroupMask 1 -> HOSTILE to players. (This is the
--                   check that disqualified 29112 Gothik the Harvester, whose
--                   faction 2084 has enemyGroupMask 0 and would have spawned
--                   a friendly statue, and 29190 Flesh Behemoth on 2100.)
--   scripting       ScriptName EMPTY and AIName 'SmartAI'. Even the SmartAI
--                   never runs: CreatureAISelector::SelectAI asks
--                   sScriptMgr->GetCreatureAI FIRST (CreatureAISelector.cpp:
--                   78-88) and PDv2's binder is an AllCreatureScript, so it
--                   wins over both a ScriptName and an AIName. Its three
--                   smart_scripts rows are a source of IDENTITY only, and
--                   one of them - 69581 Pustulant Flesh - is the one row in
--                   this pack's 25 that no other member carries.
--   quests / loot   creature_queststarter, creature_questender and
--                   quest_template.RequiredNpcOrGo1..4 all return 0 rows, so
--                   it is no questgiver and no kill-credit target (this is
--                   what disqualified 10505 Instructor Malicia, npcflag 2).
--                   lootid 100000, mingold/maxgold 0, skinloot 0 - see the
--                   economy block: this boss drops NOTHING.
--   speed           speed_run 1.28968 against the trash's 1.14286. It closes
--                   a little faster than its own pack; it is not the 1.71429
--                   of the alternatives that were rejected.
--   CC immunity     CreatureImmunitiesId -93 = MechanicsMask FEAR|HORROR,
--                   SchoolMask 0 - byte-identical to the shipped stock
--                   bosses 25352 and 29620. It can still be stunned, rooted,
--                   snared, silenced and interrupted. Gandling's -346 could
--                   be none of those (it adds STUN, ROOT, SNARE, SILENCE and
--                   INTERRUPT) and nobody had decided that; this one needs
--                   no decision because it matches what already ships.
--
-- Alternatives measured and rejected, so the choice reads as a decision:
-- 26530 Salramm the Fleshcrafter and 26529 Meathook (315000 hp, DamageModifier
-- 7.5, perfectly on theme) both drop 3x 43228 Stone Keeper's Shard and a green
-- at Chance 100 plus a guaranteed group-1 blue, i.e. a currency faucet in an
-- infinitely repeatable dungeon; 16382 Patchwork Terror and 16394 Pallid
-- Horror are faction 1634, enemyGroupMask 0, FRIENDLY; 29190 Flesh Behemoth is
-- faction 2100, friendly; 34127 Boneguard Commander and 32300 Alumeth the
-- Ascended draw HumanMale.mdx, the rig this swap exists to get away from;
-- 25352 Scourge Overlord is already pack 4's boss; 30071 Stitched Colossus
-- runs at speed_run 1.71429 and its own kit is two knock-backs.
--
-- ----------------------------------------------------------------------------
-- WHAT THE THREE CASTERS ACTUALLY DRAW, STATED TO THE LIMIT OF THE DATA
--
-- Each of 10471/10476/10477 publishes FOUR creature_template_model rows, so
-- ONE Necrolyte is one of four robed cultists rather than a fixed model:
--
--   10477  11163, 11154, 11155, 11156
--   10476  11161, 11151, 11152, 11175
--   10471  11157, 11145, 11146, 11173
--
-- Across the three, though, those twelve display ids resolve to only SIX
-- distinct models (HighElfFemale_Mage x2, HighElfFemale_Priest, HumanMale,
-- HumanFemale, GnomeMale/GnomeFemale), and 10477's primary 11163 and 10476's
-- primary 11161 are the SAME model (Creature\HighElf\HighElfFemale_Mage.mdx)
-- differing only in texture. The melee side is thinner still: 10486's display
-- 7847 and 10489's 7848 are both Creature\Skeleton\Skeleton.mdx, at scale
-- 1.50 and 1.30. The variety is real per creature and weaker across the pack;
-- that is the claim the data supports and it is the claim made here.
--
-- Three members - Scholomance Necromancer / Necrolyte / Acolyte - carry a
-- real instance's name into a procedural ruined city. That is new for this
-- data (the live packs are custom 84263-84290 or generic names, and the only
-- place-name in them is "Forgotten Depths High Priest"). It is kept on
-- purpose: the pack IS the school, the names are the only thing in the pack
-- that says so now that the headmaster is gone, and a name is cheaper to
-- change later than a template.
--
-- ----------------------------------------------------------------------------
-- casterSpellId - the RANGE mob's filler, and a FALLBACK only
--
-- pdungeon_member_spells is the truth for every spell a mob casts, including
-- the filler. This column is read only when a role-1 member has no slot-0 row
-- at all, and a role-1 member with casterSpellId 0 AND no member_spells rows
-- is silently DEMOTED to melee at load (PDv2PackMgr.cpp:157-166). All three
-- casters therefore carry both, and the two are deliberately identical:
--
--   10477 -> 69211 Shadow Bolt  30 yd, 2.2 s cast, 1313-1687  ~682 dps
--   10476 -> 60015 Shadow Bolt  40 yd, 3.0 s cast, 1273-1427  ~450 dps
--   10471 -> 47809 Shadow Bolt  30 yd, 3.0 s cast,  694-775   ~245 dps
--
-- The ladder is the point and it is measured, not asserted: the senior
-- caster's filler out-damages the middle rank's, which out-damages the
-- novice's, and all three sit inside the band the shipped fillers already
-- occupy (47809 245 dps and 42842 Frostbolt 278 dps in pack 1; 60015 450 and
-- 69211 682 in packs 4/5). The first cut had 61562 Shadow Bolt on 10476 -
-- 4250-5750 a cast on a 1.5 s cast time, ~3330 dps, seven times the senior
-- caster it was supposed to sit under and six times the highest number
-- anywhere in the live data. It is not in this pack any more, in any slot.
-- Each filler also reaches at least ProceduralDungeon.V2.CastRangeYd (25),
-- which is the other point of the column: a filler that cannot reach is a mob
-- that silently never lands anything (PDv2CreatureAI.cpp:1537). Full
-- per-row identity evidence in mod_pdungeon_member_spells_cult.sql.
--
-- ----------------------------------------------------------------------------
-- THE ECONOMY CHANGE THE OPERATOR SHOULD HEAR ABOUT BEFORE THE FIRST RUN
--
-- Measured against creature_loot_template / reference_loot_template /
-- item_template directly, not the lootid column alone, and NOT filtered to
-- Quality >= 3 - the drops that dominate a repeatable dungeon are below that
-- line. PDv2 multiplies loot by difficulty (lootMult x1.00 -> x3.00).
--
-- The boss contributes NOTHING. 36879's lootid 100000 is a single row that
-- forwards to reference_loot_template 35071, whose eight rows point at item
-- ids 1-8, none of which exists in item_template (measured: SELECT entry FROM
-- item_template WHERE entry BETWEEN 1 AND 8 -> 0 rows). mingold/maxgold 0,
-- skinloot 0, pickpocketloot 0. That is a deliberate part of the boss choice:
-- the previous boss dropped 12843 Corruptor's Scourgestone at 100%, one
-- GUARANTEED level-60 tier-0 helm and one guaranteed group-2 blue per kill
-- (Chance 0 inside groupid 1 and 2 means LootTemplate::LootGroup::Roll always
-- returns one - LootMgr.cpp:1262-1297), 19276 Ace of Portals at 3%, the epics
-- 13937 Headmaster's Charge at 2% and 14514 Pattern: Robe of the Void at 7%,
-- and 2268-2967 copper. None of that is in the pack any more.
--
-- The seven trash members keep their native Scholomance tables, and these are
-- the rates that matter (all QuestRequired = 0, i.e. they drop for everyone):
--
--   12841 Invader's Scourgestone      35%    ALL SEVEN   Argent Dawn token
--   22525 Crypt Fiend Parts           79.1%  11551       AD turn-in, 1-6/kill
--   22526 Bone Fragments              25.8-30.5%  10486/10488/10489, 2-4/kill
--   14047 Runecloth                   24.4-31.6%  six members, 2-4/kill
--   20520 Dark Rune                   20%    10477       mana potion, tradable
--   4585/4337 spider parts            20-32% 11551       vendor trash
--   gold                              387-2890 copper on six; 11551 zero
--
-- So this pack is an Argent Dawn reputation and turn-in-token faucet, and a
-- Runecloth faucet, and that is the largest thing it does to the economy -
-- larger than any single item. Above Quality 3 there is much less: 16722
-- Lightforge Bracers 3.64% on 10486, 16710 Shadowcraft Bracers 3.46% on
-- 10488, 16702 Dreadmist Belt 1.70% on 10477, 18335 Pristine Black Diamond
-- 0.10-0.23%, and a long tail of level-55-61 blues at 0.01-0.06%. No epic
-- drops in this pack at all now. Two 100% items on 10488 (18746 Divination
-- Scryer, 18792 Blessed Arcanite Barding) are QuestRequired = 1 and therefore
-- drop for nobody who has not got the quest. None of the eight is skinnable;
-- the three humanoid casters can be pickpocketed.
--
-- If any of that is unwanted the fix is DATA, not code: drop a member's
-- `weight`, or disable the pack. Editing creature_template.lootid here would
-- retune Scholomance itself.
--
-- ----------------------------------------------------------------------------
-- THIS FILE SHIPS ZERO creature_template ROWS AND ZERO UPDATES TO THEM.
-- Not one of the eight entries falls in 84263-84290, and none of them is
-- referenced by any other pack: SELECT packId, entry FROM
-- pdungeon_pack_members WHERE entry IN (...) returns 0 rows and
-- SELECT COUNT(*) FROM pdungeon_member_spells WHERE entry IN (...) returns 0
-- (re-measured 2026-09-09, including the new boss 36879). No spell_dbc row
-- and no custom id is created here.
--
-- The two CREATE TABLE IF NOT EXISTS blocks below are REPEATED from
-- mod_pdungeon_packs.sql on purpose, and the reason given in the first cut of
-- this file was WRONG and is corrected here. UpdateFetcher::PathCompare
-- (UpdateFetcher.cpp:521-524) orders files by FILENAME, byte-wise, and '.'
-- (0x2E) sorts before '_' (0x5F) - so mod_pdungeon_packs.sql really does run
-- before mod_pdungeon_packs_cult.sql, and mod_pdungeon_member_spells.sql
-- before mod_pdungeon_member_spells_cult.sql. The fresh-database exposure the
-- first cut argued does not exist. The repeat is kept anyway because it is
-- free (IF NOT EXISTS), because it makes each pack file applicable on its own
-- to a database that never had the module, and because the blocks are
-- byte-identical to the canonical ones - but it is defensive practice, not a
-- fix for a real ordering bug.
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

-- Idempotent re-apply: delete this file's own pack, then insert. Never DROP -
-- an operator who added packs of their own keeps them, and the id is named
-- explicitly rather than swept with a BETWEEN. The packId 7 delete also
-- removes the previous boss row (1853 Darkmaster Gandling) on a re-apply.
DELETE FROM `pdungeon_pack_members` WHERE `packId` = 7;
DELETE FROM `pdungeon_packs` WHERE `id` = 7;

INSERT INTO `pdungeon_packs`
  (`id`, `name`, `theme`, `level_min`, `level_max`, `unlock_dlvl`, `enabled`) VALUES
  (7, 'Cult of the Damned', 0, 80, 80, 0, 1);

INSERT INTO `pdungeon_pack_members`
  (`packId`, `entry`, `role`, `casterSpellId`, `weight`) VALUES
  -- Pack 7 "Cult of the Damned" - 8 members, 3 range, 1 boss
  -- MELEE (role 0) - the raised dead, type 6 UNDEAD, faction 233, DamageModifier 3.5
  (7, 10486, 0,     0, 100),  -- Risen Warrior           uc1, 26710 hp, 326-409 - raised soldiery
  (7, 10488, 0,     0, 100),  -- Risen Construct         uc1, 26710 hp, 326-409 - flags_extra 2048
  (7, 10489, 0,     0, 100),  -- Risen Guard             uc1, 16026 hp, 326-409 - carries the pack CC
  (7, 11551, 0,     0, 100),  -- Necrofiend              uc1, 16026 hp, 326-409 - drops no gold
  -- RANGE (role 1) - the robed cult, type 7 HUMANOID, faction 233, 4 models each
  (7, 10477, 1, 69211, 100),  -- Scholomance Necromancer uc2, 12822 hp, 11982 mana - senior, 682 dps filler
  (7, 10476, 1, 60015, 100),  -- Scholomance Necrolyte   uc2, 12822 hp, 11982 mana - middle, 450 dps filler
  (7, 10471, 1, 47809, 100),  -- Scholomance Acolyte     uc2, 12822 hp, 11982 mana - novice, 245 dps filler
  -- BOSS (role 2) - type 6 UNDEAD, faction 16, rank 1, npcflag 0, no loot at all
  (7, 36879, 2,     0, 100);  -- Plagueborn Horror       BOSS, 189000 hp, 1581-2199 swing, DmgMod 7.5
