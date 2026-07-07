-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: gameobject/creature templates (world database)
-- Reserved id blocks: gameobject_template 910000-910099,
--                     creature_template   910500-910549
-- (registered in share-public docs/06-custom-ids.md)
-- ----------------------------------------------------------------------------

-- Walls 910000-910003 are GAMEOBJECT_TYPE_DESTRUCTIBLE_BUILDING (type 33): their WMO
-- displays (7877/7909/8251/8250) are destructible-building models that the 3.3.5a client
-- renders via the type-33 path, NOT as plain type-0 doors (a type-0 door with these
-- displays spawns collidable-but-invisible). Data0 = intactNumHits set high so the intact
-- model shows and the walls are effectively indestructible.
-- Gate 910010 stays type 0 for now (its open/close uses SetGoState; convert separately).
DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 910000 AND 910099;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `size`, `Data0`, `Data1`, `ScriptName`) VALUES
(910000, 33, 7877, 'PD Wall Long', 1, 9000000, 0, ''),
(910001, 33, 7909, 'PD Wall Long Alt', 1, 9000000, 0, ''),
(910002, 33, 8251, 'PD Wall Short', 1, 9000000, 0, ''),
(910003, 33, 8250, 'PD Wall End', 1, 9000000, 0, ''),
(910010, 0, 7906, 'PD Gate', 1, 0, 0, ''),
(910020, 5, 7858, 'PD Torch', 1, 0, 0, ''),
(910021, 5, 8191, 'PD Brazier', 1, 0, 0, ''),
(910030, 3, 259, 'Shifting Cache', 1, 0, 910030, ''),
(910031, 10, 6691, 'Shrine of Fortune', 1, 0, 0, 'go_pdungeon_shrine'),
(910032, 10, 4711, 'Shifting Exit', 1, 0, 0, 'go_pdungeon_exit'),
(910033, 5, 672, 'PD Entrance Pad', 1, 0, 0, '');

DELETE FROM `gameobject_loot_template` WHERE `Entry` = 910030;
INSERT INTO `gameobject_loot_template` (`Entry`, `Item`, `Reference`, `Chance`, `QuestRequired`, `LootMode`, `GroupId`, `MinCount`, `MaxCount`, `Comment`) VALUES
(910030, 33470, 0, 100, 0, 1, 0, 3, 5, 'PD chest - Frostweave Cloth'),
(910030, 33447, 0, 60, 0, 1, 0, 2, 3, 'PD chest - Runic Healing Potion'),
(910030, 43102, 0, 20, 0, 1, 0, 1, 1, 'PD chest - Frozen Orb');

-- NOTE: creature_template.scale was removed by AzerothCore update 2026_03_22_03
-- (2026-03-22). Per-creature scale now lives in creature_template_model.DisplayScale
-- below (Shifting Overseer 1.35, Warden boss 1.7; all others 1.0).
DELETE FROM `creature_template` WHERE `entry` BETWEEN 910500 AND 910549;
INSERT INTO `creature_template` (`entry`, `name`, `subname`, `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`, `speed_walk`, `speed_run`, `detection_range`, `rank`, `DamageModifier`, `BaseAttackTime`, `RangeAttackTime`, `unit_class`, `unit_flags`, `type`, `type_flags`, `mingold`, `maxgold`, `HealthModifier`, `ManaModifier`, `ArmorModifier`, `RegenHealth`, `MovementType`, `AIName`, `ScriptName`) VALUES
(910500, 'Shifting Horror', '', 80, 80, 2, 14, 0, 1, 1.14286, 20, 0, 1.5, 2000, 2000, 1, 0, 6, 0, 2000, 8000, 2.5, 1, 1, 1, 0, '', 'npc_pdungeon_mob'),
(910501, 'Shifting Geist', '', 80, 80, 2, 14, 0, 1, 1.14286, 20, 0, 1.2, 2000, 2000, 8, 0, 6, 0, 2000, 8000, 1.6, 1.5, 1, 1, 0, '', 'npc_pdungeon_mob'),
(910502, 'Shifting Overseer', '', 81, 81, 2, 14, 0, 1, 1.14286, 25, 1, 3, 2000, 2000, 1, 0, 6, 0, 10000, 30000, 9, 1, 1.2, 1, 0, '', 'npc_pdungeon_mob'),
(910503, 'Warden of the Depths', 'Lord of the Shifting Halls', 82, 82, 2, 14, 0, 1, 1.14286, 30, 3, 5, 2000, 2000, 1, 0, 6, 4, 200000, 500000, 45, 1, 1.5, 1, 0, '', 'npc_pdungeon_boss'),
(910510, 'Keeper of the Shifting Halls', 'Procedural Dungeons', 80, 80, 2, 35, 1, 1, 1.14286, 20, 0, 1, 2000, 2000, 1, 2, 7, 0, 0, 0, 5, 1, 1, 1, 0, '', 'npc_pdungeon_entrance');

DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN 910500 AND 910549;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(910500, 0, 2404, 1, 1, NULL),
(910501, 0, 2405, 1, 1, NULL),
(910502, 0, 10698, 1.35, 1, NULL),
(910503, 0, 16174, 1.7, 1, NULL),
(910510, 0, 3541, 1, 1, NULL);
