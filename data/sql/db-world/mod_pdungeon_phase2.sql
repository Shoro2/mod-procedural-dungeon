-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: PDv2 Phase 2 - dead-end stubs (world database)
--
-- A dead-end corridor exists to be walked into and rewarded: the instance
-- script places one Shifting Cache on its junction square. The chest template
-- and its loot are re-declared here verbatim from mod_pdungeon_templates.sql
-- (v1's file), because v2 now DEPENDS on them:
--
-- OWNED BY V2 SINCE PHASE 2 - THE V1 CLEANUP MUST KEEP/SKIP ENTRY 910030.
-- When v1's templates are eventually retired, its DELETE over the 910000+
-- range must not take 910030 with it, or every dead-end chest goes missing
-- and the stub becomes a corridor to nothing.
--
-- The DELETEs name the exact entry for the same reason the decor file's do:
-- this file speaks for exactly one id and must not be able to remove others.
-- ----------------------------------------------------------------------------

DELETE FROM `gameobject_template` WHERE `entry` = 910030;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `size`, `Data0`, `Data1`, `ScriptName`) VALUES
(910030, 3, 259, 'Shifting Cache', 1, 0, 910030, '');

DELETE FROM `gameobject_loot_template` WHERE `Entry` = 910030;
INSERT INTO `gameobject_loot_template` (`Entry`, `Item`, `Reference`, `Chance`, `QuestRequired`, `LootMode`, `GroupId`, `MinCount`, `MaxCount`, `Comment`) VALUES
(910030, 33470, 0, 100, 0, 1, 0, 3, 5, 'PD chest - Frostweave Cloth'),
(910030, 33447, 0, 60, 0, 1, 0, 2, 3, 'PD chest - Runic Healing Potion'),
(910030, 43102, 0, 20, 0, 1, 0, 1, 1, 'PD chest - Frozen Orb');
