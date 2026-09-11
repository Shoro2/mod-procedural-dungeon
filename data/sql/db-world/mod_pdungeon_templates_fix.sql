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
-- *** SECOND EXCEPTION: 910034 'Pilgrim''s Cache' (Round E / WP8) ***
-- Same reasoning, arrived at from the other end. WP8's plan put this row in
-- mod_pdungeon_event.sql, beside the event host it belongs to and behind an
-- entry-exact DELETE - which is safe for a `creature_template` id outside
-- 910500-910549 (that file's whole existing content) and NOT safe for a
-- `gameobject_template` id inside 910000-910099: mod_pdungeon_event.sql sorts
-- BEFORE mod_pdungeon_templates.sql, whose wide DELETE would wipe 910034 on
-- every fresh database and leave nothing behind to restore it, because that
-- file re-inserts only 910000-910033. That is precisely the landmine the rule
-- above states ("Any new prop entry in 910000-910099 belongs in this file"),
-- and it would have been invisible: the live database re-applies the edited
-- event file and works, a fresh one comes up with a won event that summons
-- nothing. So the TEMPLATE row lives here and its loot rows stay in
-- mod_pdungeon_event.sql - the same split 910068 already has with
-- mod_pdungeon_chromie.sql, and for the same reason.
--
-- The DELETE below names every entry individually rather than a BETWEEN
-- range: this file's rows are not contiguous (910048-910049, 910069,
-- 910087-910099 are unused gaps in the reserved block - SIXTEEN ids), and an
-- explicit list can never claim a gap id some later addition might use for
-- something else. Same discipline as mod_pdungeon_prop_displays.sql. (910067
-- and 910068 were two of those gaps until Round C / C8 took them for the
-- finale below; 910077-910086 were ten more until Round F / F1 took them for
-- the mine props. The sixteen that remain are the forest's budget - F2's
-- design asks for about ten of them.)
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
--
-- Mine props (910077-910086, Round F / F1): the theme-1 dressing - glowing
-- cave crystals, a Dark Iron brazier and the mining clutter (lumber, ore
-- crates, a powder keg, a wheelbarrow, an ore cart). Placed by the theme-1
-- rules in mod_pdungeon_decor_mine.sql and by nothing else; a theme-2 (city)
-- run never sees them. All ten are type 5 GENERIC and size 1.0, for the
-- reasons the clutter paragraph above gives - and 1.0 survives the bbox
-- argument in every one of the ten cases, which is written into each row.
--
-- All ten went through the same three-way check, done on this box on
-- 2026-09-11 and recorded PER ROW below as (1) the DBC model path, (2) the
-- GameObjectModels.dtree bounding box in yards, (3) the count of stock
-- `gameobject_template` rows already using that displayId. Every one of the
-- ten passed all three - no mine id needed the 910066 exception - and the
-- dtree bbox is what settles `size`, because the DBC's own GeoBox columns are
-- all zero on all ten (a known gap of this client's
-- GameObjectDisplayInfo.dbc, not a property of these models).
--
-- The yardstick for "is this too big for a wall foot": the two lights this
-- module already ships at size 1.0 are 910020 'PD Torch' (display 7858,
-- 4.91 x 5.19 x 3.80 yd) and 910021 'PD Brazier' (display 8191, 10.72 x
-- 13.15 x 6.65 yd). The Dark Iron brazier below is 3.33 x 3.33 x 5.28 yd -
-- a SMALLER footprint than either, on an 8.33 yd cell - so size 1.0 needed
-- no shrinking. Blizzard's own six Doodad_DarkIronBrazier rows use 0.67 and
-- its two 'Shadowforge Brazier' rows use 1.0; we take the 1.0 precedent
-- because ours has to read as a light source across a 66 yd room.
--
-- The three crystals are ONE prop at three sizes, not three interchangeable
-- ones: 1.25 / 2.40 / 2.97 yd tall at size 1.0. That ladder is the whole
-- reason there are three rows - a mine wall with three identical crystals is
-- three copies, and with three heights it is a formation. Do not "normalise"
-- their sizes.
-- ----------------------------------------------------------------------------

DELETE FROM `gameobject_template` WHERE `entry` IN (
    910030,
    910034,
    910040, 910041, 910042, 910043, 910044, 910045, 910046, 910047,
    910050, 910051, 910052, 910053, 910054, 910055, 910056, 910057,
    910058, 910059,
    910060, 910061, 910062, 910063, 910064, 910065, 910066,
    910067, 910068,
    910070, 910071, 910072, 910073, 910074, 910075, 910076,
    910077, 910078, 910079, 910080, 910081, 910082, 910083, 910084,
    910085, 910086
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
-- 910034 'Pilgrim''s Cache' (Round E / WP8, 2026-09-10): the small chest a WON
-- event room leaves on the pilgrim's own square when he despawns. Same chest
-- contract as 910030 - type 3, lock 57, Data1 = its own loot id, Data2/Data3
-- from the UPDATEs at the end of this file - and deliberately a SEPARATE entry
-- rather than a second spawn of 910030, because PDv2ChestLoot keys its
-- injection on the entry and the operator asked for a visibly smaller reward.
-- groupLootRules (Data15) is left at the column default 0, like every other
-- chest this module ships.
--
-- Its `gameobject_loot_template` rows live in mod_pdungeon_event.sql, beside
-- the host they belong to - exactly the split 910068 already uses with
-- mod_pdungeon_chromie.sql, and safe for the same reason: the loot table's
-- DELETEs in this module are all entry-exact, so only the TEMPLATE half has a
-- wide range to survive and only the template half has to live here.
--
-- displayId 10, checked the three ways this file requires of every id:
--   1. GameObjectDisplayInfo.dbc 10 = World\Generic\ActiveDoodads\Chest01\
--      Chest01.mdx (measured 2026-09-10 in C:\wowstuff\dcore\Data\dbc).
--   2. GameObjectModels.dtree carries Chest01.m2 with a real bounding box,
--      (-0.39, -0.58, 0.01) to (0.40, 0.58, 0.62) - so it has collision to
--      stand on rather than existing only in the DBC.
--   3. Stock precedent: 63 `gameobject_template` rows use displayId 10 at
--      type 3 (72 at any type) - Blizzard's own small wooden chest, e.g. 32
--      'Sunken Chest' and 2039 'Hidden Strongbox'.
-- It is about half the footprint of 259 TreasureChest01 (-0.59/-0.78/0.00 to
-- 0.59/0.77/1.31), which is the whole point: the dungeon's caches stay the big
-- ones and the pilgrim's parting gift reads as the small one. No model facing
-- offset is applied to it anywhere - the quarter turn the finale cache carries
-- was measured for 259 and says nothing about this model.
(910034, 3, 10, 'Pilgrim''s Cache', 1, 57, 910034, ''),
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
(910059, 5, 7482, 'Sealed Portcullis', 1.1, 0, 0, ''),
-- Round C / C8: the finale. Both rows are summoned by the instance script
-- when the last boss dies and are torn down with the run (DespawnAll), so
-- neither is ever a world spawn.
--
-- 910067 'Portal to Azealia': type 10 GOOBER, so a click fires
-- OnGossipHello; display 9041 is the model FL's own three return portals
-- already use (777000 'To Azealia', 222000/223000 'Return To Azealia').
-- Those three carry their teleport in Data2 = an `event_scripts` id; this row
-- deliberately does NOT copy that mechanism. go_pdungeon_azealia_portal
-- (src/PDExitObjects.cpp) teleports in C++, which needs no world-DB script row
-- at all. Nobody is teleported automatically: the click is the player's, so
-- the cache can be looted first (operator decision 2026-09-08).
--
-- Round E / WP10: WHERE it teleports is no longer written in the code either.
-- The hook resolves a `game_tele` row by NAME at click time, from the conf key
-- ProceduralDungeon.V2.Finale.TeleName - `flcapital` (id 20042, map 727,
-- 13356.2 / 12009.1 / -23.39, o 4.50) since the operator asked for the capital
-- on 2026-09-11. C8's `flraidazealia` (id 20040) survives as the compiled-in
-- fallback for a name that resolves to nothing. The row's NAME still says
-- Azealia: it is what the client has cached, so renaming it would cost a
-- ClientCacheVersion bump, and it is a separate decision from where the portal
-- goes.
--
-- 910068 'Chromie''s Cache': type 3 CHEST, display 259 (TreasureChest01) like
-- the pocket cache above but at size 2, so the run's reward reads as the
-- bigger one. Data0 = lock 57 (LOCKTYPE_OPEN/TREASURE, skill 0) for the
-- reason spelled out at 910030: a lockless chest is rejected outright by
-- Spell::CheckCast with SPELL_FAILED_BAD_TARGETS and can never be opened.
-- Data1 = loot 910068, whose rows live in mod_pdungeon_chromie.sql. No
-- ScriptName - a chest is looted through the client's Opening cast, and
-- GameObject::Use() has no CHEST case for a script to hook.
(910067, 10, 9041, 'Portal to Azealia', 1, 0, 0, 'go_pdungeon_azealia_portal'),
(910068, 3, 259, 'Chromie''s Cache', 2, 57, 910068, ''),
-- ----------------------------------------------------------------------------
-- Round F / F1: the mine props (theme 1 only - see the header). Three numbers
-- per row: (1) DBC model path, (2) dtree bbox, (3) stock rows on that display.
--
-- mine: crystals. The mine's LIGHT - they glow, and in a dungeon whose
-- tileset is rock they are the only thing that is not rock. Three rows, three
-- heights, one model family; the size ladder is the point (see the header).
-- Three rows sharing the name 'Cave Crystal' is deliberate and stock-normal:
-- a type 5 GENERIC object is never clickable and shows the player no name at
-- all, the name is for whoever reads the table, and the display id in each
-- comment is the discriminator.
--
-- 910077: (1) World\Dungeon\Cave\PassiveDoodads\Crystals\
--             CaveMineCrystalFormation06.mdx
--         (2) Caveminecrystalformation06.m2, (-0.927, -1.466, -0.087) to
--             (1.151, 1.353, 2.316) = 2.08 x 2.82 x 2.40 yd - the middle rung
--         (3) 3 stock rows: 22246 'Tear of Theradras', 22550 'Draenethyst
--             Crystals', 152622 'Azsharite Formation'
(910077, 5,  219, 'PD Cave Crystal',      1.0, 0, 0, ''),
-- 910078: (1) ...\CaveMineCrystalFormation02.mdx
--         (2) Caveminecrystalformation02.m2, (-0.287, -0.582, -0.232) to
--             (0.663, 0.531, 1.022) = 0.95 x 1.11 x 1.25 yd - the low rung,
--             a knee-high cluster. Stock 2705 'Shards of Myzrael' runs this
--             model at size 3.47, so scaling it is precedented; we keep 1.0
--             because the LADDER is what this row exists for.
--         (3) 2 stock rows: 2705, 178185 'Sapphire of Aku''Mai'
(910078, 5,  244, 'PD Cave Crystal',      1.0, 0, 0, ''),
-- 910079: (1) ...\CaveMineCrystalFormation07.mdx
--         (2) Caveminecrystalformation07.m2, (-1.037, -1.754, -0.111) to
--             (1.405, 1.406, 2.862) = 2.44 x 3.16 x 2.97 yd - the tall rung,
--             taller than a player and still under a third of a cell wide
--         (3) 2 stock rows: 152631 'Azsharite Formation', 175324 'Frostmaul
--             Shards'
(910079, 5, 2592, 'PD Cave Crystal',      1.0, 0, 0, ''),
-- mine: the boss-room light.
-- 910080: (1) WORLD\KHAZMODAN\BLACKROCK\ACTIVEDOODADS\DARKIRONBRAZIER\
--             DARKIRONBRAZIER.MDX
--         (2) Darkironbrazier.m2, (-1.666, -1.663, 0.004) to (1.669, 1.672,
--             5.283) = 3.33 x 3.33 x 5.28 yd. Tall, but a SMALLER footprint
--             than both lights this module already ships at size 1.0 (see the
--             header's yardstick), and 3.33 yd on an 8.33 yd cell leaves the
--             wall foot walkable past it.
--         (3) 14 stock rows, e.g. 174744/174745 'Shadowforge Brazier' at
--             size 1.0 (the precedent taken) and six
--             'Doodad_DarkIronBrazier0n' at 0.67
(910080, 5, 3411, 'PD Dark Iron Brazier', 1.0, 0, 0, ''),
-- mine: the clutter a working dig leaves on the floor. Scattered, never at a
-- wall foot - these are things you walk around, not things that lean.
-- 910081: (1) World\Generic\Human\Passive Doodads\LumberPiles\
--             DeadMineLumberPileSmall.mdx
--         (2) Deadminelumberpilesmall.m2, (-1.673, -0.640, 0.000) to
--             (1.681, 0.654, 0.561) = 3.35 x 1.29 x 0.56 yd - long and flat,
--             ankle height, exactly what `scatter` wants
--         (3) 2 stock rows: 103573 'Cut Woodpile' (0.5), 181687 'Lumber Pile'
-- 910082: (1) ...\DeadMineLumberPileLarge.mdx
--         (2) Deadminelumberpilelarge.m2, (-1.674, -0.914, 0.000) to
--             (1.675, 0.855, 1.668) = 3.35 x 1.77 x 1.67 yd - the same
--             footprint stacked three times as high
--         (3) 1 stock row: 181686 'Lumber Pile'
--         Both stock rows are literally named 'Lumber Pile' on these two
--         displays, which is where these two names come from.
(910081, 5, 1108, 'PD Lumber Pile',       1.0, 0, 0, ''),
(910082, 5, 1109, 'PD Lumber Pile',       1.0, 0, 0, ''),
-- 910083: (1) World\Generic\Human\Passive Doodads\CargoBoxes\
--             DeadMineCargoBoxes.mdx
--         (2) Deadminecargoboxes.m2, (-1.272, -1.147, -0.019) to (1.294,
--             1.120, 1.763) = 2.57 x 2.27 x 1.78 yd
--         (3) 14 stock rows; the exact precedent is 180052 'Deadmine Cargo
--             Boxes', which is type 5 at size 1.0 - the same two values
(910083, 5,   36, 'PD Ore Crates',        1.0, 0, 0, ''),
-- 910084: (1) World\Generic\Human\Passive Doodads\DeadMinePowderKeg\
--             DeadMinePowderKeg.mdx
--         (2) Deadminepowderkeg.m2, (-0.239, -0.255, 0.000) to (0.244,
--             0.304, 0.592) = 0.48 x 0.56 x 0.59 yd - the smallest prop in
--             the module
--         (3) 2 stock rows: 193640/193713 'Doodad_deadminepowderkeg01/02',
--             both type 5 at size 0.74
--         Size 1.0 rather than Blizzard's 0.74 on purpose: at 0.74 the model
--         is 0.36 x 0.41 x 0.44 yd, under half a yard in every axis, which on
--         a 66 yd room floor is a pebble nobody sees. 1.0 leaves it the
--         smallest thing here and still readable as a keg.
(910084, 5,  436, 'PD Powder Keg',        1.0, 0, 0, ''),
-- 910085: (1) WORLD\GENERIC\PASSIVEDOODADS\MISC\WHEELBARROW\
--             CAVEMINEWHEELBARROW01.MDX
--         (2) Caveminewheelbarrow01.m2, (-1.728, -0.710, 0.031) to (1.149,
--             0.640, 1.231) = 2.88 x 1.35 x 1.20 yd
--         (3) 1 stock row: 190859 (type 5, size 1, no name) - the thinnest
--             precedent of the ten, but a real one, and unlike 910066 it is
--             a GameObject row rather than terrain dressing
(910085, 5,  215, 'PD Wheelbarrow',       1.0, 0, 0, ''),
-- 910086: (1) World\Azeroth\Stranglethorn\PassiveDoodads\GemMineCar02\
--             GemMineCar03.mdx  (the folder name is Blizzard's, not a typo)
--         (2) Gemminecar03.m2, (-1.025, -0.769, -0.041) to (1.025, 1.075,
--             2.427) = 2.05 x 1.84 x 2.47 yd - the tallest piece of clutter,
--             and the one silhouette that says "mine" on its own
--         (3) 2 stock rows: 192058 'Ore Cart' (type 3 CHEST, size 1.0) and
--             190767 'Inconspicuous Mine Car' (type 10, 0.65). Ours is type 5
--             GENERIC, so unlike 192058 it is never clickable and satisfies
--             nobody's objective - the same argument the clutter ids make.
(910086, 5, 7997, 'PD Ore Cart',          1.0, 0, 0, '');

-- Chest data beyond the INSERT's column list: Data3 = consumable (one loot per spawn),
-- Data2 = restock 0. Without Data3 the cache refilled every tick (Round C research 1.3).
-- Same two values for the finale cache 910068: it pays out once per run. And the same
-- again for the event cache 910034 (Round E / WP8) - a won event pays its chest once.
UPDATE `gameobject_template` SET `Data2` = 0, `Data3` = 1 WHERE `entry` = 910030;
UPDATE `gameobject_template` SET `Data2` = 0, `Data3` = 1 WHERE `entry` = 910034;
UPDATE `gameobject_template` SET `Data2` = 0, `Data3` = 1 WHERE `entry` = 910068;
