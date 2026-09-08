# PDv2 Round C / C4 — kit v37: the fog model — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The white 2D triangles disappear: the kit's fog placements use a fog/mist M2 with no static geometry, shipped as kit `t1b-v37` (`KIT_VERSION` 25) with unchanged walk masks, anchors and pins.

**Architecture:** A read-only M2 scan over the client's MPQs finds particle-only fog models; script 48 swaps the model name in its fog placement; the kit chain proves byte-identity twice and that nothing but the fog placements changed; the client kit and the module's chunk-meta SQL move together (kit version only).

**Tech Stack:** Python 3 kit pipeline (`C:\wowstuff\ForgottenLand2.0\scripts\48/51/52/49`, `mpq_stormlib.py`), `pdblock.exe`, `flstream_tests.exe`.

**Spec:** `docs/superpowers/specs/2026-09-08-pdv2-round-c-design.md` §2 C4; §1 "Triangles". Research: `.superpowers/sdd/c-research-bosses-triangles.md` §2 (the `ShadowfangFog01.m2` geometry, the asset audit, the evidence to request).

---

## Global Constraints

The kit laws of `share-public/python_scripts/pdv2-kit/README.md` and the Round B kit rounds (`.superpowers/sdd/progress.md` v31–v36): snapshot `48_gen_t1_blockkit.py` before editing (`.v37_20260908`), `PYTHONUTF8=1` from `C:\wowstuff\ForgottenLand2.0`, patch scripts through the Write tool, two builds (`output5/t1b-v37a`, `t1b-v37b`) byte-identical, the staging flip, 51 → 52 → 49 → DLL 497/0, audits 105/104/103/102/99 unchanged from v36 (same layout), `[6b]` green **without** re-capture (walk masks unchanged), `pdblock --batch 500` on the final staging with no pin moved. The client must be closed for a `KitDir` flip (`tasklist //FI "IMAGENAME eq ForgottenLand.exe"`) — if it runs, stage `t1b-v37` beside v36 and write the flip into runde29 as the operator's first step. The worldserver is never restarted by a task. Module SQL `mod_pdungeon_chunk_meta.sql` is generated, never hand-edited.

---

### Task 1: The M2 scan and the pick

**Files:**
- Create: `C:\wowstuff\ForgottenLand2.0\scripts\58_m2_static_scan.py` (read-only tool; uses `scripts/mpq_stormlib.py` like 57 does)

- [ ] **Step 1:** Enumerate every `*.m2` in the client's MPQs (`C:\wowstuff\FL2-Client\Data\*.MPQ` incl. patches, load order as 57 uses) whose path matches `fog|mist|smoke|dust|haze|steam` (case-insensitive). For each: parse the M2 header (3.3.5a version 264): `nParticleEmitters`, `nRibbonEmitters`, `nViews`, and from the `.skin` file(s) the triangle count (`nTriangles`) and the texture list (`nTextures`, filenames or type). Also parse `ShadowfangFog01.m2` as the control: the tool must report its 4 static triangles.
- [ ] **Step 2:** Print a table sorted by (static triangles asc, particle emitters desc, name); write it to `C:\wowstuff\ForgottenLand2.0\reports\m2_fog_scan_20260908.txt`. Candidates = `nTriangles == 0 && nParticleEmitters > 0` and every referenced texture present in the MPQs. Pick the one whose emitter footprint is closest to `ShadowfangFog01` (bounding box radius from the header); record the pick, its bounds and the runner-up in the report. If the candidate list is empty, the decision is "drop the fog placements" (spec C4.1) — say so and stop after the report.

---

### Task 2: Script 48 swap, two builds, the chain, the audits

- [ ] **Step 1:** Snapshot, then in `48_gen_t1_blockkit.py` replace the fog model path (grep `ShadowfangFog01`) with the pick; `KIT_VERSION = 25`; the closing doc string names v25. No other change.
- [ ] **Step 2:** Build `--out output5/t1b-v37a` and `--out output5/t1b-v37b` (no re-capture flag): both `ALL CHECKS PASS`, `[6b]` green against `baseline_walk_v24.json`, `diff -r` empty, 247 files each. Diff `t1b-v37a` against the current staging: allowed differences = exactly the ADTs carrying fog placements (list them, expect the 105-placement set's chunks), `kit_meta.json` (model names/sha only — masks and anchors byte-equal, verify with a JSON diff that ignores `m2Placements` names and `sha256`), `mod_pdungeon_chunk_meta.sql` (the `SET @KIT := 25` line only; row data identical). Anything else = STOP.
- [ ] **Step 3:** Mirror to the staging (`robocopy … /MIR` → 247), `51 --dry-run` + real, `52`, repeat on `t1b-v37b`, byte-identical 247/247 after 51+52; manifest + `49` PASS; `flstream_tests.exe` 497/0; audits 105/104/103/102/99 equal to the v36 figures (fog is not floor, not facade, not walk); `pdblock --batch 500` on the final staging: no pin moved.
- [ ] **Step 4:** Commit the module SQL alone: `feat(kit): chunk meta v25 - fog model swapped, masks unchanged`; record the 48 diff in `tools/pd_v32_patches/README.md` (v37 paragraph).

---

### Task 3: Deploy, operator document, vault

- [ ] Client: `t1b-v37` beside v36 (`robocopy … /MIR` → 247, byte-identical to the staging); `patch_ini_v37.py` (copy of v36, the v37 paragraph: "Round C/C4: fog model `<pick>` replaces `ShadowfangFog01` (four static white triangles), KIT_VERSION 25, masks/anchors unchanged"); run it only with the client closed, else the flip goes to runde29 §2 as the operator's first step.
- [ ] Vault kit backup: refresh `48_gen_t1_blockkit.py`, add `58_m2_static_scan.py` and the scan report, the v37 README paragraph. runde29 §C4 (no white triangles; if one survives, walk around it — a fog quad faces the camera, a static one does not — and `.gps` it) + the state table (kit v37, `KIT_VERSION` 25, `chunk_meta` kit-version-only change → restart still needed for the bundle). MIG-017 (kit v37 in the launcher payload), queue row, `claude_log.md` END; runbook check 0 errors. One vault commit.

---

## Self-review

- Spec coverage: C4.1 → Tasks 1–2; C4.2 → Tasks 2–3; C4.3 → runde29 note in Task 3.
- Placeholders: the pick, the file lists and the audit figures come from runs; the fallback (drop the fog) is decided by the scan's result, not left open.
