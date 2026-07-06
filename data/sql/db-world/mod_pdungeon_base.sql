-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: base map setup (world database)
--
-- Default base map: 37 'Azshara Crater' (PVPZone02). The map exists in every
-- 3.3.5a client but was never player-accessible. The map_dbc override below
-- re-types it SERVER-SIDE to a 5-player dungeon; the client needs NO patch.
-- All original values were extracted from the client Map.dbc; only
-- InstanceType (0 -> 1), PVP (1 -> 0) and MaxPlayers (0 -> 5) are changed.
-- ----------------------------------------------------------------------------

-- Required by MapInstanced::CreateInstance, otherwise instancing ABORTs.
DELETE FROM `instance_template` WHERE `map` = 37;
INSERT INTO `instance_template` (`map`, `parent`, `script`, `allowMount`) VALUES
(37, 0, '', 1);

DELETE FROM `map_dbc` WHERE `ID` = 37;
INSERT INTO `map_dbc` (`ID`, `Directory`, `InstanceType`, `Flags`, `PVP`, `MapName_Lang_enUS`, `MapName_Lang_Mask`, `AreaTableID`, `MapDescription0_Lang_Mask`, `MapDescription1_Lang_Mask`, `LoadingScreenID`, `MinimapIconScale`, `CorpseMapID`, `CorpseX`, `CorpseY`, `TimeOfDayOverride`, `ExpansionID`, `RaidOffset`, `MaxPlayers`) VALUES
(37, 'PVPZone02', 1, 0, 0, 'Azshara Crater', 16712190, 0, 16712190, 16712191, 25, 1, -1, 0, 0, -1, 0, 0, 5);

-- Graveyard inside the crater so ghosts are not sent across the world.
-- Zone 268 = 'Azshara Crater'. Move x/y/z next to your configured
-- ProceduralDungeon.Center.* coordinates.
DELETE FROM `game_graveyard` WHERE `ID` = 910000;
INSERT INTO `game_graveyard` (`ID`, `Map`, `x`, `y`, `z`, `Comment`) VALUES
(910000, 37, 0, 0, 0, 'mod-procedural-dungeon default graveyard');
DELETE FROM `graveyard_zone` WHERE `ID` = 910000;
INSERT INTO `graveyard_zone` (`ID`, `GhostZone`, `Faction`, `Comment`) VALUES
(910000, 268, 0, 'mod-procedural-dungeon');

-- ----------------------------------------------------------------------------
-- Alternative base maps (set ProceduralDungeon.BaseMapId accordingly and run
-- the matching block instead; original values from the client Map.dbc):
--
-- Map 451 'Development Land' (huge flat plains, dev textures):
-- DELETE FROM `instance_template` WHERE `map` = 451;
-- INSERT INTO `instance_template` (`map`, `parent`, `script`, `allowMount`) VALUES (451, 0, '', 1);
-- DELETE FROM `map_dbc` WHERE `ID` = 451;
-- INSERT INTO `map_dbc` (`ID`, `Directory`, `InstanceType`, `Flags`, `PVP`, `MapName_Lang_enUS`, `MapName_Lang_Mask`, `AreaTableID`, `MapDescription0_Lang_Mask`, `MapDescription1_Lang_Mask`, `LoadingScreenID`, `MinimapIconScale`, `CorpseMapID`, `CorpseX`, `CorpseY`, `TimeOfDayOverride`, `ExpansionID`, `RaidOffset`, `MaxPlayers`) VALUES
-- (451, 'development', 1, 15, 0, 'Development Land', 16712190, 0, 16712188, 16712188, 21, 1, 0, 0, 0, -1, 2, 0, 5);
--
-- Map 169 'Emerald Dream' (atmospheric, terrain flatness varies):
-- DELETE FROM `instance_template` WHERE `map` = 169;
-- INSERT INTO `instance_template` (`map`, `parent`, `script`, `allowMount`) VALUES (169, 0, '', 1);
-- DELETE FROM `map_dbc` WHERE `ID` = 169;
-- INSERT INTO `map_dbc` (`ID`, `Directory`, `InstanceType`, `Flags`, `PVP`, `MapName_Lang_enUS`, `MapName_Lang_Mask`, `AreaTableID`, `MapDescription0_Lang_Mask`, `MapDescription1_Lang_Mask`, `LoadingScreenID`, `MinimapIconScale`, `CorpseMapID`, `CorpseX`, `CorpseY`, `TimeOfDayOverride`, `ExpansionID`, `RaidOffset`, `MaxPlayers`) VALUES
-- (169, 'EmeraldDream', 1, 0, 0, 'Emerald Dream', 16712190, 0, 16712188, 16712188, 0, 1, -1, 0, 0, -1, 0, 0, 5);
--
-- Map 44 '<unused> Monastery' is already InstanceType 1 in the client DBC and
-- needs only the instance_template row (no map_dbc override):
-- DELETE FROM `instance_template` WHERE `map` = 44;
-- INSERT INTO `instance_template` (`map`, `parent`, `script`, `allowMount`) VALUES (44, 0, '', 1);
-- ----------------------------------------------------------------------------
