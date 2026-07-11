# Anarchy Markers — Vanilla Marker Bridge (EE Compat)

Makes Anarchy Markers markers visible to mods that only know the **vanilla** marker framework.

Anarchy Markers replaces vanilla player-placed markers with its own system. A mod that reads
`SCR_MapMarkerManagerComponent.GetStaticMarkers()` therefore sees nothing. This addon turns on the
core's `AM_VanillaBridge`, which mixes read-only proxies of our markers into that call.

Written for **EE Transport / Heli Enhanced Frame** (its LZ / LN / CAS markers), but it is not tied to
it: install it next to **any** mod that reads vanilla markers. Its only dependencies are the base
game and Anarchy Markers, so a missing third mod can never break your server.

## Install

Subscribe to it alongside **Anarchy Markers**. Nothing else to do — the defaults expose every marker
a player is already allowed to see on the map.

## Configuration

Server-side, in `$profile:SavingMarkers/SM_VanillaBridge.cfg` (created on first start, next to the
core's `SM_Config.cfg`). The server owns these settings and sends them to clients, so both sides
always expose the same list. Edit and restart.

| Key | Default | Meaning |
|---|---|---|
| `bridgeEnabled` | `true` | Master switch. |
| `markerTextPrefixes` | *(empty)* | Expose only markers whose text starts with one of these, comma separated, case-insensitive. Empty = all. |
| `includeLocalMarkers` | `true` | Also expose the player's own Local-channel markers (only ever to that player). |

**When to narrow `markerTextPrefixes`:** the bridge is a shim — once on, our markers appear to *every*
reader of the vanilla list. If a mod on your server counts, iterates or deletes vanilla markers, it
will meet objects it never created. Narrowing the filter is how you keep it away from them. For EE
alone, `markerTextPrefixes=lz,ln,cas` is exactly enough.

## What it does not do

Writes are not bridged. The proxies are snapshots rebuilt on every call, they are never registered
with the vanilla manager, and they are hidden from the vanilla renderer, so nothing is drawn twice.

New integrations should use **`AM_MarkerAPI`** instead (see `Docs/API.md` in the core) — it is richer
and has no side effects. The bridge exists for mods you cannot change.
