# Change log (newest first)

- 2026-10-16 - fix(PDv2): `EvictDisconnected` spares `WorldSession::IsBot()` sessions, so mod-fl-testbots bots can stay
  on map 760 (they have no socket and no client to crash; no production session is a bot). Bot run 725
  `tests/pd_death_respawn.tbs` PASSED 17/0 on the workbench (worldserver `136fade6`): die + release at once, die without
  release, and die + log out at once - the bot logs in at its homebind and no corpse is left on map 760 (the corpse is
  removed by the new logout branch). The release branch for a death the script never recorded stays T0 (no way to
  produce one on purpose).

- 2026-10-16 - fix(PDv2): no release on map 760 reaches the core's graveyard search any more. Zone 5100 has no
  `graveyard_zone` row and the core's fallback is Westfall. Interim per the operator (2026-10-16: "sollen vorerst
  einfach am anfang respawnen"): `OnPlayerCanRepopAtGraveyard` queues the module's own respawn
  (`PDv2InstanceScript::QueueRespawn` -> `RespawnPending`: the entrance, or the last cleared boss hall) for a dead
  player without a pending one, and resurrects a dead player who is leaving the map (logout) so the home teleport
  carries them out alive. The recorded-death path (Round B/C) is unchanged. T0 for the new branch: built and booted
  on the workbench (worldserver `c0130dde`, errors log 84 lines unchanged); a bot cannot test it, because
  `EvictDisconnected` sends a socketless bot session home within a second (bot run 723). Host: MIG-148.
