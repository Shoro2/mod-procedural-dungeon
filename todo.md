# Open items

- (mid) Zone 5100's death handling is an interim (operator 2026-10-16: "vorerst"): the module respawns players at
  the entrance or the last cleared boss hall, and a release never reaches the core. A real `graveyard_zone` row (or a
  hub) is still undecided; the startup line "Table `graveyard_zone` incomplete: Zone 5100" can still appear when the
  core asks for a graveyard outside a death (a GM `.appear` / `.summon`, `SetEntryPoint`).
- (low) Bot tests cannot stay on map 760: `EvictDisconnected` treats the socketless test-bot sessions as gone.
  Exempting `WorldSession::IsBot()` sessions would let mod-fl-testbots cover deaths, barriers and the finale.
- (owed) T2 for the release path: a player dies in a run, presses "Release Spirit" late or logs out dead.
