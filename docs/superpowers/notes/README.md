# PDv2 Round A — client-track script diffs

The client-track tasks of Round A (14-17) edit Python pipeline scripts under
`C:\wowstuff\ForgottenLand2.0\scripts` in the operator's workspace, which is
**not a git repository** - nothing there survives a disk failure, and there
is no commit history to point at. Each script carries a `.pre_roundA_20260901`
backup beside it (the rollback path), so a diff against that backup is the
only durable record of what changed and why. This folder is that record.

## What is kept, and why these two files

| File | Script(s) | Task | What it captures |
|---|---|---|---|
| `task-16-script48-floor-texture-per-role.diff` | `48_gen_t1_blockkit.py` | 16 | Per-(theme, role) floor texture selection (`tex_layers_for`, `TEXTURE_SUPERSET`/`THEME_TEXTURE_LAYERS`) |
| `task-17-scripts51-52-floor-doodads.diff` | `51_texture_blockkit.py`, `52_punch_kit_holes.py` | 14, 15, 17 | Ground-effect ids on the floor layer (`build_mcly`) + the MCNK low-quality texture map write (0x40) that makes them render, PLUS the alpha-feather / worn-track-relocation work tasks 14 and 15 made to the same two files |

Both diffs are **cumulative against the `.pre_roundA_20260901` backup**, not
against each other or against the previous task's diff - each is a full
`diff -u <script>.pre_roundA_20260901 <script>` at the point it was taken.
Verified faithful: applying either diff to its `.pre_roundA_20260901` backup
reproduces the live script byte-identically (confirmed during the final
whole-branch review, item 8).

Script 48 is touched by task 16 alone, so its diff needs only one file. Scripts
51 and 52 are touched by tasks 14, 15 AND 17 (14: alpha feather + MCSH; 15:
worn-track relocation; 17: ground-effect ids + the MCNK write) - task 17's
diff, being cumulative against the pre-round baseline rather than against
task 14 or 15's own diff, already carries all three tasks' changes to those
two files in one file. There is nothing left for an intermediate diff to add.

## What is deliberately NOT here

`task-14-scripts.diff`, `task-14-fix-scripts.diff` and `task-15-scripts.diff`
(recorded during the round in the gitignored `.superpowers/sdd/` working
directory) were superseded intermediates: each was cumulative-against-the-
pre-round-backup at ITS point in the round, and task 17's diff above,
being taken later against the same pre-round backup, already contains
everything they captured for scripts 51 and 52. Keeping them here would be
three more copies of a state the two files above already fully describe.
They are not copied forward on purpose - do not "restore" them.
