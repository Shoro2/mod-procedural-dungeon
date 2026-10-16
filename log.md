# Change log (newest first)

- 2026-10-16 - fix(PDv2): no release on map 760 reaches the core's graveyard search any more. Zone 5100 has no
  `graveyard_zone` row and the core's fallback is Westfall. Interim per the operator (2026-10-16: "sollen vorerst
  einfach am anfang respawnen"): `OnPlayerCanRepopAtGraveyard` queues the module's own respawn
  (`PDv2InstanceScript::QueueRespawn` -> `RespawnPending`: the entrance, or the last cleared boss hall) for a dead
  player without a pending one, and resurrects a dead player who is leaving the map (logout) so the home teleport
  carries them out alive. The recorded-death path (Round B/C) is unchanged. T0 for the new branch: built and booted
  on the workbench (worldserver `c0130dde`, errors log 84 lines unchanged); a bot cannot test it, because
  `EvictDisconnected` sends a socketless bot session home within a second (bot run 723). Host: MIG-148.
