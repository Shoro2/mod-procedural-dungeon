-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: the finale's NPC and the reward cache's loot
-- (world database) - Round C / C8
--
-- Chromie (creature_template 910550) is summoned by the instance script when
-- the last boss dies: she speaks three lines four seconds apart, the reward
-- cache stands beside her and the portal to Azealia opens after the third
-- line (the _finale state machine in src/PDv2InstanceScript.cpp).
--
-- Reserved id block: creature_template 910550-910599, the PDv2 sub-block
-- registered in share-public docs/06-custom-ids.md. Chromie is the first
-- entry authored in it. (v1's mobs live in 910500-910549, a different block
-- with a different DELETE - see below.)
--
-- WHY A NEW FILE and not a row in mod_pdungeon_templates.sql: the updater
-- gates every db-world file on its own content hash, so editing that file at
-- all re-applies it - and its opening
--   DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 910000 AND 910099
-- would then wipe the whole 910040-910099 sub-block on any database where
-- mod_pdungeon_templates_fix.sql (the only file that restores it) is
-- unchanged and therefore does NOT re-run. That landmine is documented at
-- length in mod_pdungeon_templates_fix.sql's header; this file exists so the
-- finale never arms it.
--
-- FILENAME ORDER: the updater applies db-world files sorted by name, so
--   mod_pdungeon_chromie.sql < mod_pdungeon_templates.sql
-- and this file runs BEFORE that file's range DELETEs. That is safe because
-- none of them reaches these rows: `creature_template` and
-- `creature_template_model` are deleted there only for 910500-910549, and
-- `gameobject_loot_template` only for `Entry` = 910030. Nothing inserted here
-- lies in any of those ranges. Keep it that way - a creature id inside
-- 910500-910549, or a loot Entry of 910030, added to this file would be
-- silently deleted again on every fresh database.
--
-- `creature_template` carries no modelid column in this core; the model lives
-- in `creature_template_model` (the same shape mod_pdungeon_templates.sql
-- uses for 910500-910510, and the table its header's note about the removed
-- `scale` column points at).
-- ----------------------------------------------------------------------------

-- Column list copied verbatim from mod_pdungeon_templates.sql's own custom
-- creature insert (910510 'Keeper of the Shifting Halls'); every column the
-- design does not name keeps that row's value.
--   faction 35     - friendly to everything, hostile to nobody
--   npcflag 0      - no gossip and no click at all: she is spoken through by
--                    the instance script, which holds her GUID
--   unit_flags 514 - UNIT_FLAG_NON_ATTACKABLE (2) | UNIT_FLAG_IMMUNE_TO_PC
--                    (512): she cannot be targeted or pulled by a player
--   ScriptName ''  - deliberate. There is no npc_pdungeon_chromie script: the
--                    three lines are timed by PDv2InstanceScript's _finale.
--                    A ScriptName no C++ script registers is a startup
--                    LOG_ERROR ("assigned in the database, but has no code",
--                    ScriptMgr::CheckIfScriptsInDatabaseExist).
DELETE FROM `creature_template` WHERE `entry` = 910550;
INSERT INTO `creature_template` (`entry`, `name`, `subname`, `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`, `speed_walk`, `speed_run`, `detection_range`, `rank`, `DamageModifier`, `BaseAttackTime`, `RangeAttackTime`, `unit_class`, `unit_flags`, `type`, `type_flags`, `mingold`, `maxgold`, `HealthModifier`, `ManaModifier`, `ArmorModifier`, `RegenHealth`, `MovementType`, `AIName`, `ScriptName`) VALUES
(910550, 'Chromie', 'Timewalker', 80, 80, 2, 35, 0, 1, 1.14286, 20, 0, 1, 2000, 2000, 1, 514, 7, 0, 0, 0, 5, 1, 1, 1, 0, '', '');

-- Display 10008 is the gnome Chromie that every stock humanoid-form entry
-- uses (10667, 26527, 27915, 30997, 86002); 27856's 24877 is the bronze
-- dragon form, which is not the one the finale wants.
DELETE FROM `creature_template_model` WHERE `CreatureID` = 910550;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(910550, 0, 10008, 1, 1, NULL);

-- Loot for "Chromie's Cache" - gameobject_template 910068, whose Data1 points
-- at this Entry. The GO row itself lives in mod_pdungeon_templates_fix.sql
-- (that file owns the whole 910040-910099 sub-block; see its header).
--
-- FILLER, not the reward. Round E's real payout for this cache is injected
-- at RUNTIME - 25 % filler beside the Round-E gear/currency/bonus injection
-- at GO_ACTIVATED (src/PDv2ChestLoot.cpp), which adds the rolled ICC gear,
-- the T4/T5 currencies and the L5 legacy-rare bonus hits to go->loot after
-- Player::SendLoot has filled these template rows (Player.cpp:7893) and
-- before the loot packet goes out (Player.cpp:8200-8205). Five GUARANTEED
-- mat stacks on top of that would drown it, so the Chance below is 25 and
-- not the C8.1 placeholder's 100: on average a bit over one mat stack per
-- cache instead of five. The Min/MaxCount ladder is unchanged.
--
-- The cache is Data3 = 1 (consumable), so this pays out once per spawned
-- chest, not once per restock tick.
DELETE FROM `gameobject_loot_template` WHERE `Entry` = 910068;
INSERT INTO `gameobject_loot_template` (`Entry`, `Item`, `Reference`, `Chance`, `QuestRequired`, `LootMode`, `GroupId`, `MinCount`, `MaxCount`, `Comment`) VALUES
(910068, 920100, 0, 25, 0, 1, 0, 3, 5, 'PD finale cache - Forgotten Shard (filler)'),
(910068, 920101, 0, 25, 0, 1, 0, 2, 4, 'PD finale cache - Forgotten Sliver (filler)'),
(910068, 920102, 0, 25, 0, 1, 0, 1, 2, 'PD finale cache - Forgotten Fragment (filler)'),
(910068, 920103, 0, 25, 0, 1, 0, 1, 1, 'PD finale cache - Forgotten Core (filler)'),
(910068, 920104, 0, 25, 0, 1, 0, 1, 1, 'PD finale cache - Forgotten Relic (filler)');
