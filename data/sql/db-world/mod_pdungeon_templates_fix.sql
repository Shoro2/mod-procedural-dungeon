-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: template restore for the 910040-910099 sub-block
-- (world database)
--
-- mod_pdungeon_templates.sql opens with
--   DELETE FROM gameobject_template WHERE entry BETWEEN 910000 AND 910099
-- and re-inserts only 910000-910033. Since then the kit props (910040-910047,
-- shipped by mod_pdungeon_prop_displays.sql) and this round's clutter
-- (910050-910057/910060-910066/910070-910076, originally shipped by
-- mod_pdungeon_decor_clutter.sql) have moved into that range.
--
-- AzerothCore's updater applies db-world SQL files in filename order, and
-- gates each one independently on its own content hash (see PathCompare,
-- src/server/database/Updater/UpdateFetcher.h:133): a file whose hash is
-- unchanged is skipped, and there is no cascading re-apply of siblings when
-- one file's DELETE happens to remove rows another file inserted. This
-- module's files sort:
--   mod_pdungeon_decor_clutter.sql < mod_pdungeon_prop_displays.sql
--   < mod_pdungeon_templates.sql < mod_pdungeon_templates_fix.sql
-- So if mod_pdungeon_templates.sql's content ever changes and it re-applies
-- (this has already happened twice to other module files - decor.sql/
-- packs.sql, 2026-08-31), its range DELETE would wipe 910040-910099 again,
-- and the only file that sorts AFTER it - this one - is the sole place left
-- that can put those rows back. Every other file that touches this id block
-- sorts before it and would already have run.
--
-- The failure this file exists to prevent, if it were incomplete: the
-- `pdungeon_decor_rules` rows in mod_pdungeon_decor_clutter.sql live in a
-- different table, untouched by the range DELETE, so they would survive
-- and keep firing. Each rule's `goEntry` would then point at a
-- gameobject_template row that no longer exists. Map::SummonGameObject
-- fails silently per spawn attempt in that case - no crash, just a log
-- line - so every PDv2 dungeon would come out bare: walls and torches up,
-- every crate/rubble/bone prop missing, nothing to show for it but log
-- spam.
--
-- Do not edit mod_pdungeon_templates.sql to fix this: the updater will not
-- re-apply an edited base file to a database that already has it, so a
-- narrowed range delete there would never reach this database. This file
-- is the restore path instead, and it owns the WHOLE vulnerable sub-block
-- (910040-910099) rather than splitting it with mod_pdungeon_decor_clutter.sql
-- or mod_pdungeon_prop_displays.sql, because it is the only module file
-- positioned to actually restore anything after a re-apply. Do NOT move any
-- gameobject_template row for this id range into another file - a future
-- addition sorting before mod_pdungeon_templates.sql would silently re-arm
-- this exact landmine. Any new prop entry in 910000-910099 belongs in this
-- file (or, for 910000-910033, in mod_pdungeon_templates.sql's own untouched
-- INSERT list - it never needs restoring because nothing after it can wipe
-- it and then fail to also re-run).
--
-- *** EXCEPTION: 910030 'Shifting Cache' (Round C / C3) ***
-- 910030 is a 910000-910033 id, so by the rule just above it would be
-- fixed where it is declared: mod_pdungeon_templates.sql:26 and
-- mod_pdungeon_phase2.sql:20 (both carry it with the broken Data0 = 0).
-- Both are deliberately left UNTOUCHED, comments included. The updater
-- gates every file on its own content hash, so editing either one at all
-- re-applies it, and a re-applied mod_pdungeon_templates.sql runs its wide
-- DELETE 910000-910099 while THIS file - whose hash would be unchanged -
-- does not re-run to put 910040-910099 back: that is exactly the landmine
-- this file exists to defuse, armed by the very edit meant to fix a chest.
-- Adding the row here instead changes only this file, which is the one
-- place that runs last on every database: after the wide DELETE on a fresh
-- one, and alone on a database that already has everything else. The two
-- declaring copies keep their old values; the row below is the one that
-- survives on both kinds of database, and it is the row to edit from now
-- on. Do NOT "tidy" this by moving 910030 back to its declaring files.
--
-- The DELETE below names every entry individually rather than a BETWEEN
-- range: this file's rows are not contiguous (910048-910049,
-- 910067-910069, 910077-910099 are unused gaps in the reserved block), and
-- an explicit list can never claim a gap id some later addition might use
-- for something else. Same discipline as mod_pdungeon_prop_displays.sql.
--
-- The real fix - narrowing mod_pdungeon_templates.sql's range delete to
-- 910000-910033 - is recorded in the global queue
-- (share-public docs\World of Warcraft\12-server-todo.md) rather than done
-- here, for the reason above.
--
-- Kit props (910040-910047): row values below are copied VERBATIM from the
-- live acore_world database, not from research: `SELECT ... FROM
-- gameobject_template WHERE entry BETWEEN 910040 AND 910049`. 910040-910043
-- differ from an earlier research draft of this fix (which guessed 'PD
-- Broken Cart Kit'/'Stalagmite A'/'Stalagmite B'/'PD Cave In' with different
-- displayIds and sizes) - what mod_pdungeon_prop_displays.sql actually
-- shipped and what is actually in the database is 'PD Fountain'/'PD Rock
-- Column'/'PD Stalagmite'/'PD Cave-In' below. 910044-910047 matched the
-- draft. displayId 92040 (PD Fountain) is not a stock Blizzard id; it is the
-- gameobjectdisplayinfo_dbc override row mod_pdungeon_prop_displays.sql
-- ships alongside its own copy of these templates.
--
-- *** DUPLICATE OWNERSHIP WARNING: kit props 910040-910047 ***
-- The eight rows below are hand-copied from mod_pdungeon_prop_displays.sql,
-- which GENERATES the same eight rows (scripts/57_pd_prop_displays.py in the
-- ForgottenLand2.0 workspace) and must never be hand-edited. Both copies are
-- identical today. The duplication is structurally necessary, not an
-- oversight: this file must own the WHOLE 910040-910099 block (see above)
-- because it is the only module file sorting after
-- mod_pdungeon_templates.sql's range DELETE, and mod_pdungeon_prop_displays.sql
-- cannot be that file - it sorts BEFORE mod_pdungeon_templates.sql, so
-- anything it inserts is wiped by that file's DELETE on any re-apply, the
-- same trap the clutter rows above were moved out of.
--
-- Nothing enforces that the two copies stay in step. If script 57 ever
-- changes these eight rows, THIS FILE MUST CHANGE IN THE SAME COMMIT, or the
-- two copies diverge and which one is live depends on the database's
-- history, not on which file is "correct":
--   - an EXISTING database (already carries this file with its old,
--     unchanged hash, so it will NOT re-apply and re-delete): a regenerated
--     mod_pdungeon_prop_displays.sql's fresh INSERT wins, because nothing
--     that runs after it removes what it just inserted.
--   - a FRESH database (every file applies once, in filename order):
--     mod_pdungeon_templates.sql's range DELETE runs after
--     mod_pdungeon_prop_displays.sql and wipes what it inserted, then THIS
--     file's own copy re-inserts last and wins instead.
-- The only safe state is both copies identical, always. Do NOT edit
-- mod_pdungeon_prop_displays.sql to fix a divergence - it is generated and
-- hand edits there are lost on the next script 57 run.
--
-- Clutter props (910050-910076): broken furniture, crates, rubble and bones
-- for PDv2, moved here verbatim from mod_pdungeon_decor_clutter.sql (that
-- file now keeps only its `pdungeon_decor_rules` rows, which sit in a
-- different, unaffected table - see that file's header). Every entry is
-- type 5 GENERIC: it is the only GameObject class measured to block a
-- player on map 760 (MDDF/MODF doodads do not), which is why decor is
-- GameObjects at all (see mod_pdungeon_decor.sql). `size` is the only scale
-- dial available.
--
-- Every clutter displayId was checked three ways before being shipped:
-- against `GameObjectDisplayInfo.dbc` (the id resolves to the model path
-- the name/comment claims), against `GameObjectModels.dtree` (proof of an
-- actual collision model and its bounding box - a displayId can exist in
-- the DBC and still have nothing to stand on for LoS/pathing), and against
-- stock `gameobject_template` rows (proof Blizzard already used it as
-- indoor dressing, not e.g. a spell-effect-only model).
--
-- 910066 (display 6926, 'PD Rubble Heap') is the one exception and the
-- weakest-verified clutter id: Blizzard used it only as ADT/WMO terrain
-- dressing, so it has no stock gameobject_template row to check against. It
-- passed the other two checks (DBC model KarazahnRockRubble01.m2; a real
-- bounding box in GameObjectModels.dtree), which is the best confirmation
-- available for it.
--
-- Six of the clutter displayIds (130, 293, 7470 among them) are also used
-- by stock quest objects elsewhere in the world DB. That is visual
-- confusion only, never a functional clash: ours are type 5 GENERIC, not
-- the quest GO's type/ScriptName, so ours are never clickable and never
-- satisfy anyone's objective.
-- ----------------------------------------------------------------------------

DELETE FROM `gameobject_template` WHERE `entry` IN (
    910030,
    910040, 910041, 910042, 910043, 910044, 910045, 910046, 910047,
    910050, 910051, 910052, 910053, 910054, 910055, 910056, 910057,
    910058, 910059,
    910060, 910061, 910062, 910063, 910064, 910065, 910066,
    910070, 910071, 910072, 910073, 910074, 910075, 910076
);
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `size`, `Data0`, `Data1`, `ScriptName`) VALUES
-- the loop-room / pocket cache (Round C / C3: lock 57 so the client's
-- Opening cast accepts it). A GAMEOBJECT_TYPE_CHEST is never looted through
-- GameObject::Use() - it has no case 3 - only through the client's Opening
-- cast, and Spell::CheckCast rejects a lockless GO outright:
--   if (!lockId) return SPELL_FAILED_BAD_TARGETS;
-- (azerothcore-wotlk src/server/game/Spells/Spell.cpp:6339-6346). With the
-- shipped Data0 = 0 the cast died there and clicking the cache did nothing
-- at all - no loot window, no error, no log line. Lock 57 is the classic
-- "anyone can open it" treasure lock (Lock.dbc 57: LOCKTYPE_OPEN and
-- LOCKTYPE_TREASURE, skill requirement 0), which is what all 1356 lootable
-- chests in this world DB use, FL's own 800000-800003 included, and what
-- all 15 stock rows sharing displayId 259 (TreasureChest01) use.
(910030, 3, 259, 'Shifting Cache', 1, 57, 910030, ''),
-- kit props
(910040, 5, 92040, 'PD Fountain', 1, 0, 0, ''),
(910041, 5, 5073, 'PD Rock Column', 1.7, 0, 0, ''),
(910042, 5, 5073, 'PD Stalagmite', 1, 0, 0, ''),
(910043, 5, 2230, 'PD Cave-In', 0.4, 0, 0, ''),
(910044, 5, 6961, 'PD Broken Cart', 1, 0, 0, ''),
(910045, 5, 251, 'PD Haystack', 1.2, 0, 0, ''),
(910046, 5, 2890, 'PD Sack Pile', 1, 0, 0, ''),
(910047, 5, 150, 'PD Signpost', 1, 0, 0, ''),
-- clutter: wall feet (things that stand against a wall)
(910050, 5,  288, 'PD Barrel',              1.0,  0, 0, ''),
(910051, 5,  275, 'PD Crate',               1.0,  0, 0, ''),
(910052, 5, 7470, 'PD Plague Barrel',       1.0,  0, 0, ''),
(910053, 5,  130, 'PD Weapon Rack',         1.0,  0, 0, ''),
(910054, 5,  187, 'PD Bookshelf',           1.0,  0, 0, ''),
(910055, 5, 4391, 'PD Alchemy Bench',       0.9,  0, 0, ''),
(910056, 5,  234, 'PD Long Table',          0.8,  0, 0, ''),
(910057, 5, 5511, 'PD Grain Sack',          1.5,  0, 0, ''),
-- clutter: corners (things that wedge into an angle)
(910060, 5, 1868, 'PD Crate Stack',         0.8,  0, 0, ''),
(910061, 5, 1869, 'PD Crate Stack Alt',     0.8,  0, 0, ''),
(910062, 5, 7680, 'PD Broken Crate Stack',  0.75, 0, 0, ''),
(910063, 5, 6036, 'PD Tall Barrel',         1.2,  0, 0, ''),
(910064, 5, 7526, 'PD Broken Keg',          0.6,  0, 0, ''),
(910065, 5, 8480, 'PD Cart Wheel',          1.0,  0, 0, ''),
-- 6926 has no stock gameobject_template precedent - see the header note.
(910066, 5, 6926, 'PD Rubble Heap',         1.0,  0, 0, ''),
-- clutter: scatter (flat things you walk over on open floor)
(910070, 5, 7911, 'PD Rubble Low',          1.0,  0, 0, ''),
(910071, 5, 6736, 'PD Broken Boards',       0.7,  0, 0, ''),
-- Display 9 (BrokenBarrel02), not 8026 (BrokenBarrel01): 8026 has no entry in
-- GameObjectModels.dtree at all, i.e. no collision model, so it would let
-- players walk through it. Do not "fix" this to 8026 - it was checked.
(910072, 5,    9, 'PD Broken Barrel',       1.0,  0, 0, ''),
(910073, 5, 7311, 'PD Skeleton',            1.0,  0, 0, ''),
(910074, 5, 7312, 'PD Skeleton Alt',        1.0,  0, 0, ''),
(910075, 5,  293, 'PD Bone Pile',           1.0,  0, 0, ''),
(910076, 5, 7225, 'PD Coffin',              1.0,  0, 0, ''),
-- Altar of Return: UNSPAWNED since Round C (C5) - the row stays, the script
-- and the spawns are gone. Kept because the id registry's rule is not to prune
-- casually (910040 is unspawned the same way), so nothing may reuse 910058.
-- ScriptName is cleared with the code: a name no C++ script registers is a
-- startup LOG_ERROR ("assigned in the database, but has no code",
-- ScriptMgr::CheckIfScriptsInDatabaseExist), and go_pdungeon_altar was deleted
-- with PDv2Altar.cpp. Round B / B1's original row was type 10 GOOBER so
-- OnGossipHello fired on click, display 7355 Altar01.m2 (a collision model in
-- GameObjectModels.dtree, the display of the stock 'WotLK Light Altar'
-- 190741) - the three-way check this file's header asks for.
(910058, 10, 7355, 'Altar of Return', 1, 0, 0, ''),
-- Round B / B3: the boss-room barrier. type 5 GENERIC because that is the
-- only GameObject class measured to block a player on map 760; opened by
-- Delete(), never by state. Display 7482 Vr_Portcullis.m2 spans 16.3 yd at
-- scale 1 (the lane is 16.67), so size 1.1 overlaps the flanking wall band.
-- Stock precedent for the display, read out of the world DB: 186612/186694
-- 'Giant Portcullis' and 192173/195437 'Doodad_VR_Portcullis01' (all type 0
-- DOOR) - ours is type 5 for the blocking reason above, so it is never
-- clickable and satisfies nobody's objective, the same argument the clutter
-- ids above make. No ScriptName: the barrier is opened by the instance
-- script, and a GENERIC object cannot be used by a player anyway.
(910059, 5, 7482, 'Sealed Portcullis', 1.1, 0, 0, '');

-- Chest data beyond the INSERT's column list: Data3 = consumable (one loot per spawn),
-- Data2 = restock 0. Without Data3 the cache refilled every tick (Round C research 1.3).
UPDATE `gameobject_template` SET `Data2` = 0, `Data3` = 1 WHERE `entry` = 910030;
