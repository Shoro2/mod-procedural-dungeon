-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: MINE decor rules (world database), Round F / F1
--
-- The first `pdungeon_decor_rules` rows in this module that are not `theme 0`.
-- Rules 1-14 are all "any look" because a torch, a crate and a bone pile read
-- correctly under every theme the kit ships; these nineteen are the opposite
-- case - glowing cave crystals, a Dark Iron brazier and the litter of a
-- working dig belong to the mine and would be absurd in the city street.
--
-- The planner's filter is one line (src/generator/PDv2DecorPlan.cpp, both
-- BuildDecorPlan and BuildCritterPlan): a rule is skipped when
-- `rule.theme != 0 && rule.theme != plan.config.theme`. So these rules are
-- ADDITIVE on a theme-1 run - the shipped theme-0 rules still fire beside
-- them, which is what D4 asks for (torches in a mine are right) - and they are
-- invisible on every other theme.
--
-- Every `goEntry` below resolves to a gameobject_template row shipped by
-- mod_pdungeon_templates_fix.sql (910077-910086, with the three-way display
-- check written into each row). Do NOT add a rule here whose template you
-- shipped in this file: that file is the only one sorting after
-- mod_pdungeon_templates.sql's wide `DELETE ... BETWEEN 910000 AND 910099`,
-- so it is the only place a 9100xx template can survive a re-apply. Full
-- mechanism in that file's header.
--
-- Rule ids start at 15, after the highest id any shipped file owns (14), so
-- mod_pdungeon_decor.sql's `DELETE ... BETWEEN 1 AND 3` and
-- mod_pdungeon_decor_clutter.sql's `BETWEEN 4 AND 14` can never touch them;
-- this file's own DELETE names its nineteen ids and nothing else in return.
--
-- No CREATE TABLE block here, matching mod_pdungeon_decor_clutter.sql: '.' is
-- 0x2E and '_' is 0x5F, so mod_pdungeon_decor.sql sorts before both of us
-- under the updater's plain filename compare and has already created
-- `pdungeon_decor_rules`.
--
-- ----------------------------------------------------------------------------
-- HOW THE PLANNER SPENDS A BLOCK, because every number below follows from it
--
-- Per block the planner collects three INDEPENDENT candidate pools from the
-- walk mask - wall feet, corners, open floor - and then walks the matching
-- rules IN ASCENDING ID ORDER. For each rule it draws `want` uniformly from
-- [minPerBlock, maxPerBlock], computes
--     share = ceil(poolAtStartOfBlock * weight / totalWeightOfThatKind)
-- and places `min(want, share)` candidates, REMOVING each drawn candidate
-- from the pool whether it is used or rejected. Two consequences this file
-- is built around:
--   * weight is a SHARE OF THAT KIND's cells, so a corner rule only ever
--     competes with corner rules; and
--   * a high rule id draws from what the lower ids left. Ordering is not
--     cosmetic here.
--
-- Candidate pools, measured on this box 2026-09-11 over all 122 theme-1
-- chunks in `pdungeon_chunk_meta` by replaying PDv2Classify + CollectWallFeet
-- / CollectCorners / CollectScatter (the replay reproduces the number
-- mod_pdungeon_decor.sql's header already states for corridor_straight - "the
-- four variants yield 30 wall-foot candidates between them" - which is what
-- makes it trustworthy):
--
--   role                wall_foot        corner          scatter
--   room                 9..16 (13.4)    4..6 (4.7)      2..13 (8.5)
--   room_boss           14..16 (15.0)    4..6 (5.0)     10..13 (11.1)
--   room_entrance       14..16 (15.0)    4..6 (5.0)     10..13 (11.1)
--   corridor_straight    7..8  (7.5)     0..2 (1.0)      0..2  (1.0)
--   corridor_corner      6..7  (6.8)     0..1 (0.2)      0..1  (0.2)
--   corridor_t           9..10 (9.5)     0    (0.0)      0..1  (0.5)
--   corridor_cross      12     (12.0)    0    (0.0)      1     (1.0)
--   corridor_dead_end    4     (4.0)     0..1 (0.5)      0     (0.0)
--
-- `roleFilter` is a PREFIX match, so 'room' also matches room_boss and
-- room_entrance and 'corridor' matches all five corridor variants - the same
-- semantics mod_pdungeon_decor.sql documents.
--
-- minSpacingYd follows the shipped convention exactly: 8 for wall feet and
-- corners (one cell, 8.33 yd, rounded down, so two props of the SAME rule
-- never land in touching cells) and 12 for scatter (one and a half cells, so
-- scattered props never clump). The check is per rule, not across rules.
--
-- ----------------------------------------------------------------------------
-- WHY THE BRAZIER TAKES THE FIRST NEW ID
--
-- Rule 15 is the boss-room brazier although the narrative order would put the
-- crystals first. The reason is the draw order above: by the time the six
-- crystal rules have spent their share of a boss room's wall feet, the pool
-- can be empty, and a rule that finds an empty pool places nothing however
-- high its minPerBlock is. Measured over 8000 boss rooms: at id 15 the
-- brazier lands 1.50 times per boss room (exactly its 1..2 average), at the
-- end of the block only 1.10 - it would silently go missing in a quarter of
-- all boss rooms. The mine's boss room must have its light.
--
-- ----------------------------------------------------------------------------
-- WHAT THIS DOES TO THE SHIPPED RULES (measured, 8000 blocks per figure,
-- props actually placed per block, city = theme 2 with rules 1-14 only,
-- mine = theme 1 with rules 1-33)
--
--   rule                          city    mine
--   1  torch, room wall foot      2.01    1.90     <- D4: the torches stay
--   1  torch, boss wall foot      2.00    2.01
--   2  ZulDrak brazier, boss      1.50    1.00
--   4/5 barrel + crate, room      1.00    0.67
--   10/11 crate stacks, corner    1.01    0.67
--   13 rubble, room scatter       1.37    1.31     <- deliberately untouched
--
-- The city furniture thinning by a third while the torches and the rubble
-- stay is the intended trade and the reason for the weights chosen below:
--   * crystals at 120 out-weigh the torch (100) individually, so they take
--     the wall - but 120 is low enough that the torch's own share stays >= 3
--     in any room with 12+ wall feet, i.e. it keeps its full 1..3 almost
--     everywhere and loses at most one prop in the smallest rooms;
--   * the mine clutter at 20 is deliberately FEATHER-light, because at 60 it
--     would push rule 13's share down to 2 and cap the shipped rubble. 20
--     leaves rule 13 uncapped and still fills the mine's floor, because with
--     a scatter pool of 8.5 the POOL, not the weight, is what bounds these
--     rules.
--
-- ----------------------------------------------------------------------------
-- *** BUDGET: THIS RULE SET CAN EXCEED PD_DECOR_MAX_SPOTS AT THE ROOM CAP ***
--
-- `PD_DECOR_MAX_SPOTS` (src/generator/PDv2DecorPlan.h) is 250, and the cut is
-- taken at the END in plan order - so an overflowing layout loses the
-- dressing of its LAST blocks, which is where the boss room usually sits.
--
-- Measured by replaying BuildDecorPlan's placement (pools, share formula,
-- pool consumption, the 3.0 yd anchor clearance and per-rule minSpacing;
-- 3000 layouts per row). The replay was calibrated against two real engine
-- outputs first: `props per layout : 6..51` from the Round A baseline with
-- rules 1-3, and the 84 props of the pinned PD_DECOR_PLAN_PIN layout (19
-- blocks, rules 1-14) - it reproduces both.
--
--   layout                     city (theme 2)      mine (theme 1)
--   5 rooms  +  8 corridors    37..73   (55)       77..130  (104)
--   10 rooms + 15 corridors    79..139  (106)      160..241 (201)
--   15 rooms + 20 corridors    120..189 (155)      238..346 (294)   OVER 250
--
-- The deployed default is `ProceduralDungeon.V2.Rooms = 5`, so ordinary runs
-- are nowhere near the ceiling; `GameRoomsCap` allows 15 from dlvl 12 up, and
-- at 15 rooms essentially every mine layout crosses 250 - as, at 155 mean,
-- the CITY already spends 62 % of a budget whose header calls it "2.5x the
-- measured worst case" (that measurement predates rules 4-14).
--
-- The fix is one line in PDv2DecorPlan.h - 250 -> 450, well clear of the
-- measured mine maximum - and it is NOT made here because this file may only
-- touch SQL. Round F's planner owns that call; until it is made, a 15-room
-- mine run loses the dressing of its last few blocks. Thinning these rules
-- instead was measured and does not work: even cutting every scatter rule to
-- 0..1 and the crystals to 1..2 still crosses 250 on most max-size layouts,
-- because the count is bounded by the candidate pools and not by the rules.
-- ----------------------------------------------------------------------------

-- Idempotent re-apply: delete this file's own nineteen ids, then insert.
-- Never DROP and never a BETWEEN range - an operator who added rules of their
-- own keeps them, and so do the fourteen rules this file does not own.
DELETE FROM `pdungeon_decor_rules` WHERE `id` IN (
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
    28, 29, 30, 31, 32, 33
);
INSERT INTO `pdungeon_decor_rules`
    (`id`,`theme`,`roleFilter`,`goEntry`,`placement`,`minPerBlock`,`maxPerBlock`,`weight`,`minSpacingYd`) VALUES
-- The boss room's light, first in the file for the ordering reason above.
(15, 1, 'room_boss', 910080, 'wall_foot', 1, 2,  60,  8),
-- Wall feet: the crystals. 1..3 each, the same density the shipped torch has,
-- so a mine room carries at least three crystals and usually five or six -
-- they are what lights it. The three ids are one model family at three
-- heights (1.25 / 2.40 / 2.97 yd), which is why all three fire in the same
-- room rather than one being picked per room.
(16, 1, 'room',      910077, 'wall_foot', 1, 3, 120,  8),
(17, 1, 'room',      910078, 'wall_foot', 1, 3, 120,  8),
(18, 1, 'room',      910079, 'wall_foot', 1, 3, 120,  8),
-- Corners: the same three crystals wedged into an angle, 0..1 each. The
-- corner pool is only 4.7 cells and the shipped crate stacks (10/11) draw
-- first, so these land in roughly one room in three each - occasional by
-- design, not a second ring.
(19, 1, 'room',      910077, 'corner',    0, 1, 100,  8),
(20, 1, 'room',      910078, 'corner',    0, 1, 100,  8),
(21, 1, 'room',      910079, 'corner',    0, 1, 100,  8),
-- Scatter, rooms: the litter of a working dig, 0..2 each on open floor. Six
-- ids so a fifteen-room layout does not show the same barrow twelve times.
-- The id order costs the later ones a little (0.84 down to 0.43 placements
-- per room, measured) because the pool is consumed as it goes; that is the
-- planner's shape, not a bug, and the order below is simply ascending entry.
(22, 1, 'room',      910081, 'scatter',   0, 2,  20, 12),
(23, 1, 'room',      910082, 'scatter',   0, 2,  20, 12),
(24, 1, 'room',      910083, 'scatter',   0, 2,  20, 12),
(25, 1, 'room',      910084, 'scatter',   0, 2,  20, 12),
(26, 1, 'room',      910085, 'scatter',   0, 2,  20, 12),
(27, 1, 'room',      910086, 'scatter',   0, 2,  20, 12),
-- Scatter, corridors: 0..1 each, and a corridor's open floor is one cell at
-- most - so at most ONE of these six ever fires in a given corridor and the
-- first rule that wants that cell takes it. That makes the ORDER the choice
-- of what a mine corridor shows, so these six are ordered by what belongs in
-- a haulage tunnel rather than by entry: the ore cart first, then the barrow,
-- the crates, the timber, the keg last.
--
-- Measured (20 000 blocks per role, anchor clearance applied): only
-- corridor_STRAIGHT ever has an open cell clear of its chunk's anchors -
-- 0.50 per block - and corridor_corner / _t / _cross / _dead_end have NONE,
-- their one or two open cells all sitting within 3 yd of a patrol or entry
-- anchor. So the yield is 0.49 props per straight corridor and zero in every
-- other corridor kind, split 28:0.247  29:0.127  30:0.062  31:0.032
-- 32:0.015  33:0.007. The long tail is deliberate and cheap: the keg turns up
-- once every few layouts instead of never, and a rule that finds no pool
-- places nothing.
(28, 1, 'corridor',  910086, 'scatter',   0, 1,  20, 12),
(29, 1, 'corridor',  910085, 'scatter',   0, 1,  20, 12),
(30, 1, 'corridor',  910083, 'scatter',   0, 1,  20, 12),
(31, 1, 'corridor',  910081, 'scatter',   0, 1,  20, 12),
(32, 1, 'corridor',  910082, 'scatter',   0, 1,  20, 12),
(33, 1, 'corridor',  910084, 'scatter',   0, 1,  20, 12);
