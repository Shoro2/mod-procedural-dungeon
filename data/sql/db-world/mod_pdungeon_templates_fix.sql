-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: kit-prop template restore (world database)
--
-- mod_pdungeon_templates.sql opens with
--   DELETE FROM gameobject_template WHERE entry BETWEEN 910000 AND 910099
-- and re-inserts only 910000-910033. Since then the kit props (910040-910047,
-- shipped by mod_pdungeon_prop_displays.sql) and this round's clutter
-- (910050-910077, mod_pdungeon_decor_clutter.sql) have moved into that range,
-- so a future re-apply of that file - which only happens if its content
-- changes, but has already happened twice to other module files
-- (decor.sql/packs.sql, 2026-08-31) - would wipe both and every PDv2 dungeon
-- would go dark and bare.
--
-- Do not edit mod_pdungeon_templates.sql to fix this: the updater will not
-- re-apply an edited base file to a database that already has it, so a
-- narrowed range delete there would never reach this database. This file is
-- the restore path for 910040-910047 instead. The clutter rows restore
-- themselves the same way, from mod_pdungeon_decor_clutter.sql. Neither file
-- may use a DELETE spanning the full 910000-910099 block that
-- mod_pdungeon_templates.sql claims - each covers only the sub-range it
-- alone owns.
--
-- The real fix - narrowing mod_pdungeon_templates.sql's range delete to
-- 910000-910033 - is recorded in the global queue
-- (share-public docs\World of Warcraft\12-server-todo.md) rather than done
-- here, for the reason above.
--
-- Row values below are copied VERBATIM from the live acore_world database,
-- not from research: `SELECT ... FROM gameobject_template WHERE entry
-- BETWEEN 910040 AND 910049`. 910040-910043 differ from an earlier research
-- draft of this fix (which guessed 'PD Broken Cart Kit'/'Stalagmite A'/
-- 'Stalagmite B'/'PD Cave In' with different displayIds and sizes) - what
-- mod_pdungeon_prop_displays.sql actually shipped and what is actually in
-- the database is 'PD Fountain'/'PD Rock Column'/'PD Stalagmite'/'PD Cave-In'
-- below. 910044-910047 matched the draft. displayId 92040 (PD Fountain) is
-- not a stock Blizzard id; it is the gameobjectdisplayinfo_dbc override row
-- mod_pdungeon_prop_displays.sql ships alongside its own copy of these
-- templates.
-- ----------------------------------------------------------------------------

DELETE FROM `gameobject_template` WHERE `entry` IN (910040, 910041, 910042, 910043, 910044, 910045, 910046, 910047);
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `size`, `Data0`, `Data1`, `ScriptName`) VALUES
(910040, 5, 92040, 'PD Fountain', 1, 0, 0, ''),
(910041, 5, 5073, 'PD Rock Column', 1.7, 0, 0, ''),
(910042, 5, 5073, 'PD Stalagmite', 1, 0, 0, ''),
(910043, 5, 2230, 'PD Cave-In', 0.4, 0, 0, ''),
(910044, 5, 6961, 'PD Broken Cart', 1, 0, 0, ''),
(910045, 5, 251, 'PD Haystack', 1.2, 0, 0, ''),
(910046, 5, 2890, 'PD Sack Pile', 1, 0, 0, ''),
(910047, 5, 150, 'PD Signpost', 1, 0, 0, '');
