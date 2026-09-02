# PDv2 Round A — rollback net and measured baseline

Task 1 of the Round A (`t1b-v28` -> city-look expansion) plan. Written 2026-09-01.
Companion copy (committed): `modules/mod-procedural-dungeon/docs/superpowers/notes/2026-09-01-round-a-baseline.md`
in the `azerothcore-wotlk` checkout, branch `claude/pdv2-expansion-9583c476`. Keep both
copies identical.

## Step 1: the four missing kits' facts

```
> Get-ChildItem 'C:\wowstuff\FL2-Client\Data\FLPD-Kit' -Directory | Select-Object Name,LastWriteTime,@{n='Files';e={(Get-ChildItem $_.FullName -File).Count}} | Sort-Object Name

Name    LastWriteTime       Files
----    -------------       -----
t1b     09.08.2026 19:48:57    59
t1b-v10 30.08.2026 23:32:17   217
t1b-v11 31.08.2026 09:04:47   217
t1b-v12 31.08.2026 09:42:09   217
t1b-v13 31.08.2026 10:37:57   217
t1b-v14 31.08.2026 10:56:15   217
t1b-v15 31.08.2026 14:30:48   217
t1b-v16 31.08.2026 16:20:46   217
t1b-v17 31.08.2026 18:21:51   217
t1b-v18 31.08.2026 18:34:25   217
t1b-v19 31.08.2026 18:46:08   217
t1b-v2  30.08.2026 02:18:59    58
t1b-v20 31.08.2026 19:33:53   217
t1b-v21 31.08.2026 20:09:03   217
t1b-v22 31.08.2026 21:04:08   217
t1b-v23 01.09.2026 00:34:43   217
t1b-v24 01.09.2026 01:28:11   217
t1b-v25 01.09.2026 02:41:57   217
t1b-v26 01.09.2026 03:18:41   217
t1b-v27 01.09.2026 03:57:56   217
t1b-v28 01.09.2026 11:22:29   217
t1b-v3  30.08.2026 03:03:30   109
t1b-v4  30.08.2026 09:22:33   109
t1b-v5  30.08.2026 14:40:25   110
t1b-v6  30.08.2026 16:43:14   217
t1b-v7  30.08.2026 21:33:51   217
t1b-v8  30.08.2026 22:09:44   217
t1b-v9  30.08.2026 23:05:51   217
```

kitVersion confirmed by reading `kit_meta.json` inside each of the four kit dirs directly
(not from memory): v25 = 21, v26 = 22, v27 = 23, v28 = 23. `pdungeon_chunk_meta.sql` sits
at the same kit-relative path in v25 and v26, consistent with the brief's "SQL moved"
claim describing a server-side table move, not a kit-layout move.

`diff -rq t1b-v27 t1b-v28` = **215 differing files** of 217 total in each — confirmed
exactly as the brief states, so v28 is a substantive, non-re-stamp change.

**Disagreements with the brief (measured, not assumed):**
- Directory count is **28**, not the brief's expected 26 (`t1b` + `t1b-v2`..`t1b-v28`).
- v25 build time measured `02:41:57` (brief said `02:41:56` — 1 s, immaterial).
- v26 build time measured `03:18:41` (brief said `03:20:58` — **2 min 17 s off**).
- v27 build time measured `03:57:56` (brief said `03:59:26` — **1 min 30 s off**).
- v28 build time measured `11:22:29` (brief said `11:22:28` — 1 s, immaterial).
- The ladder paragraphs written into `FLStream.ini` (below) use the **measured** v26/v27
  times (`03:18`, `03:57`), not the brief's draft times, because this ladder is the
  rollback record operators will read — it must carry the value that was actually
  measured, not the brief's stale draft.

## Step 2: matching commits, and the v28 gap

```
> git log --since="2026-09-01 02:00" --until="2026-09-01 13:00" --date=iso --pretty=format:"%h %ad %s"

0f27808 2026-09-01 12:06:33 +0200 merge: PDv2 city look, rounds 14-18
6598a8a 2026-09-01 12:02:45 +0200 chore(pdv2): map760 SQL carries the PlagueLands ambience
fc872ae 2026-09-01 03:59:29 +0200 feat(pdv2): arc-laid street cobbles, per-model sinkZ, tower sunk 6 yd (kitVersion 23)
1139876 2026-09-01 03:21:38 +0200 feat(pdv2): corner seats cap the convex terrain noses; centre the small-pad house
c3ebe24 2026-09-01 02:42:37 +0200 feat(pdv2): halve the terrain crest, decouple the facade inset band from it
```

Matches the brief exactly on hashes, timestamps and subjects (including fc872ae's
subject naming kitVersion 23). The two commits after v28's 11:22:29 build
(`6598a8a` at 12:02:45, `0f27808` at 12:06:33) are unrelated work (an SQL commit and a
merge) — neither documents the v28 kit build, confirming the v28 gap: **v28 has no git
commit and no operator report**, exactly as the brief asserts. The brief's own
"75 s after v25" / "40 s after v26" deltas are computed from the brief's (measurably
wrong) v25/v26 kit-build times, not from the measured ones above — an internal
inconsistency in the brief's prose that does not affect any value actually written
into the ladder.

## Step 3: the four ladder paragraphs appended to FLStream.ini

Appended after the existing v23/v24 entry (previously the last, ending at line 321) and
before the `[FLStream]` section. Note: the comment ladder was 321 lines before this edit,
not the 320 the brief states (off by one; immaterial). Written verbatim as follows
(v26/v27 timestamps corrected to measured reality per the disagreement noted in Step 1):

```
; v25 (round 18, 2026-09-01 02:41) - halve the terrain crest 12 -> 6 yd, decouple the
;   facade inset band from it. Commit c3ebe24. patch-9 in force: pre_ambience_20260901
;   (md5 b7eb4a22); kitVersion 21; same worldserver; SQL applies on restart.
;   Rollback: v24.
; v26 (round 19, 2026-09-01 03:18) - corner seats cap the convex terrain noses; the
;   small-pad house is centred. Commit 1139876; kitVersion 22 - the SQL MOVED, so a
;   worldserver restart applies it; same worldserver binary. Rollback: v25.
; v27 (round 20, 2026-09-01 03:57) - arc-laid street cobbles, per-model sinkZ, tower
;   sunk 6 yd. Commit fc872ae; kitVersion 23 - this is the chunk-meta still live in the
;   world DB; same worldserver; SQL applies on restart. Rollback: v26.
; v28 (2026-09-01 11:22) - THE LIVE KIT, and the one gap in this ladder: no git commit
;   and no operator report exist for it. 215 of its 217 files differ from v27, so it is
;   a substantive change, not a re-stamp; its content is only recoverable by diffing the
;   bytes against v27. kitVersion 23 (unchanged, no SQL move); same worldserver.
;   patch-9 in force: the live md5 97fc5cd5, which itself has no pre_* backup.
;   Rollback: v27.
```

`md5 b7eb4a22` (pre_ambience_20260901) and `md5 97fc5cd5` (live patch-9.MPQ) were both
verified with `certutil -hashfile` before writing, not copied from the brief blindly —
see Step 4.

## Step 4: backups taken

**`patch-9.MPQ` round-A backup** (client was confirmed NOT running via `tasklist` both
before this copy and immediately after — see task report for the exact commands/output):

```
copy: C:\wowstuff\FL2-Client\Data\patch-9.MPQ -> C:\wowstuff\FL2-Client\Data\patch-9.MPQ.pre_roundA_20260901
MD5-Hash von C:\wowstuff\FL2-Client\Data\patch-9.MPQ.pre_roundA_20260901:
97fc5cd513789991695b0ad0129ac834
size: 11,433,743 bytes (identical to the live file)
```

Matches the brief's expected `97fc5cd5...` prefix. This is the first `pre_*` backup that
patch-9.MPQ has ever had.

**Pipeline-script backups** (`C:\wowstuff\ForgottenLand2.0\scripts`, not a git repo — this
copy is the *only* rollback for these four files):

| file | size (bytes) | backup | verified |
|---|---:|---|---|
| `48_gen_t1_blockkit.py` | 179,040 | `48_gen_t1_blockkit.py.pre_roundA_20260901` | byte-identical (`diff -q`) |
| `51_texture_blockkit.py` | 56,399 | `51_texture_blockkit.py.pre_roundA_20260901` | byte-identical (`diff -q`) |
| `52_punch_kit_holes.py` | 37,472 | `52_punch_kit_holes.py.pre_roundA_20260901` | byte-identical (`diff -q`) |
| `30_build_hot_dbc_patch.py` | 8,888 | `30_build_hot_dbc_patch.py.pre_roundA_20260901` | byte-identical (`diff -q`) |

## Step 5: measured-green baseline

Build commands (MSVC x64 dev prompt via `vcvars64.bat`; see task report for the exact
batch-file invocation used to work around Bash-tool quoting of the vcvars path):

```
cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp
cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdgen.exe tests\ascii_harness.cpp src\generator\PDDungeonGenerator.cpp src\generator\PDGridPath.cpp src\generator\PDWallPlan.cpp src\generator\PDLayout.cpp
```

`pdblock.exe` build produced exactly one warning, `blockplan_harness.cpp(263): warning
C4456` — confirmed as the expected, sole warning per the Global Constraints (no `/WX`
used, and none needed elsewhere). `pdgen.exe` built with zero warnings.

### Before Round A (2026-09-01)

```
=== pdblock --batch 500 ===
  214 walk mask(s) from C:\\wowstuff\\ForgottenLand2.0\\output\\pd_block_kit\\FLStream\\chunks\\t1b\\pdungeon_chunk_meta.sql
  214 kit chunk(s) from C:\\wowstuff\\ForgottenLand2.0\\output\\pd_block_kit\\FLStream\\chunks\\t1b\\kit_meta.json
batch of 500 seeds, 5 rooms + 1 boss each

longest room-to-room path: 169 cells
blocks per layout: 12..41
largest manifest  : 880 bytes (budget 2048)

8407 checks, 0 failure(s)
ALL CHECKS PASS
```

```
=== pdblock --decor-batch 3000 ===
  214 walk mask(s) from C:\\wowstuff\\ForgottenLand2.0\\output\\pd_block_kit\\FLStream\\chunks\\t1b\\pdungeon_chunk_meta.sql
  214 kit chunk(s) from C:\\wowstuff\\ForgottenLand2.0\\output\\pd_block_kit\\FLStream\\chunks\\t1b\\kit_meta.json
decor batch of 3000 seeds x 3 room counts

props per layout : 6..51 (206342 total)

27691 checks, 0 failure(s)
ALL CHECKS PASS
```

```
=== pdblock --roomcap 3000 ===
  214 walk mask(s) from C:\\wowstuff\\ForgottenLand2.0\\output\\pd_block_kit\\FLStream\\chunks\\t1b\\pdungeon_chunk_meta.sql
  214 kit chunk(s) from C:\\wowstuff\\ForgottenLand2.0\\output\\pd_block_kit\\FLStream\\chunks\\t1b\\kit_meta.json
room-cap measurement: 3000 seeds per row, field 8x8, theme 1, manifest budget 1900 B

  rooms  boss  cells   genfail  maxManifest  verdict
      1     1      2         0          389  ok
      2     1      3         0          631  ok
      3     1      4         0          815  ok
      4     1      5         0          817  ok
      5     1      6         0          901  ok
      6     1      7         0         1065  ok
      7     1      8         0         1145  ok
      8     1      9         0         1189  ok
      9     1     10         0         1261  ok
     10     1     11         0         1326  ok
     11     1     12         0         1333  ok
     12     1     13         0         1349  ok
     13     2     15         0         1389  ok
     14     2     16         0         1391  ok
     15     2     17         0         1406  ok

room-cap measurement: 3000 seeds per row, field 8x8, theme 2, manifest budget 1900 B

  rooms  boss  cells   genfail  maxManifest  verdict
      1     1      2         0          406  ok
      2     1      3         0          660  ok
      3     1      4         0          853  ok
      4     1      5         0          855  ok
      5     1      6         0          943  ok
      6     1      7         0         1115  ok
      7     1      8         0         1199  ok
      8     1      9         0         1245  ok
      9     1     10         0         1320  ok
     10     1     11         0         1388  ok
     11     1     12         0         1395  ok
     12     1     13         0         1412  ok
     13     2     15         0         1453  ok
     14     2     16         0         1455  ok
     15     2     17         0         1470  ok

largest room count clean on every seed: 15 (theme 1), 15 (theme 2)
PD_GAME_ROOMS_CAP_MEASURED currently encodes: 15
the encoded cap holds for both themes
```

```
=== pdgen --batch 500 ===
batch: 500 seeds starting at 1, failures=0
rooms min/max=10/16 avgEstimatedGameObjects=333 maxEstimatedGameObjects=434 avgGenTime=152us
```

All four gates: **PASS, 0 failures.** This is the baseline every later task (2-17) diffs
against, and what Task 18's operator document cites as "Before Round A".

## Dead rollback entry (found during research)

The rollback tables in the operator round documents (`pd_testlauf_runde14.md` through
`runde16.md`, `pd_t2_checkliste_20260830.md`, `pd_t2_checkliste_20260831_alt.md`) each
carry a row of this form (verbatim from `runde16.md` line 137):
`| **Verboten** | ein v5+-Kit unter `FLStream.dll.pre_phase3_20260830` |`

`FLStream.ini` itself carries no such line. Checked directly: `grep -ic verboten
FLStream.ini` = **0**. The DLL is mentioned exactly once, in the v5 ladder paragraph
(line 17), as a general note that the DLL and the `KitDir` flip move together — not as
a standalone prohibition: "NEEDS the Phase-3 FLStream.dll (the value-rewrite composer)
- DLL and KitDir flip move TOGETHER; the pre-Phase-3 DLL is
FLStream.dll.pre_phase3_20260830 and belongs to t1b-v4 and older."

**That file does not exist anywhere on this box.** Confirmed: `C:\wowstuff\FL2-Client\`
contains only the live `FLStream.dll` (built 2026-08-30 14:40, 216,064 bytes) — no
`FLStream.dll.pre_phase3_20260830` or any other `FLStream.dll.pre_*` file.

The source repo `C:\Users\Anwender\Documents\GitHub\fl-stream-client` does have the two
relevant commits:
- `090c7a1` 2026-08-30 13:43:32 +0200 — `feat(hooks): verbose read/seek trace for load-path forensics` (pre-Phase-3)
- `6eba7b7` 2026-08-30 14:42:14 +0200 — `feat(composer): Phase-3 fixed-slot placement value-rewrites`

Oddity worth flagging to the operator: the live `FLStream.dll`'s filesystem timestamp
(14:40) is **before** the Phase-3 commit's timestamp (14:42:14), meaning the deployed
binary may have been built from a working tree that already contained the Phase-3 diff
but was committed two minutes after the build, or (less likely) the deployed DLL
predates Phase-3 entirely. Neither can be told apart from the filesystem alone.

**Decision: the constraint is unenforceable as written, and is not being silently
recreated.** Rebuilding `090c7a1` now would produce a *new*, previously-unverified
binary with no original hash to confirm it against — that would be manufacturing a
"backup" that was never actually taken, which is the same failure mode this task exists
to prevent for the v28 ladder paragraph. Recommendation for the operator/Task 18: either
retire the "Verboten" line from future ladder paragraphs (since it cites a target that
was never captured) or explicitly task someone to build and hash-freeze
`090c7a1`'s DLL output as a deliberate, dated action — not as an unverified side effect
of this baseline task.
