# Anarchy Markers × GRS ATAK

Compatibility addon: Anarchy Markers' markers and drawings show up on GRS ATAK's tablet.

Requires **Anarchy Markers** (API v4+) and **GRS ATAK**.

## What it does

**On the tablet's map.** ATAK's map is a real `SCR_MapEntity` (opened in `PLAIN` mode), but it ships
its own map config, and that config has no `SCR_MapMarkersUI` — which is the class Anarchy Markers
renders through. So our markers simply never existed there. `AMGA_MapConfig` splices our component
into their config, exactly the way we already do for the Game Master map. Nothing is replaced, so
other mods that touch the same config are unaffected.

The tablet gets `VIEW` rights (the API's default for `PLAIN`): markers and drawings **render**, but
no drawing panel, no control hints and — the important part — no input listeners, so our hotkeys
never fight ATAK's radial menu, cursor module or its own drawing tools.

**On the in-hand device screen.** That screen is not a map at all: GRS paints a render target by
hand — contours, roads, grid, markers, drawings — projected around the player at its own scale and
heading. Our map layer can't attach to it, so `AMGA_DeviceDisplay` draws our stuff itself, using
GRS's own projection so everything lines up with theirs. Markers go through `AM_MarkerWidgets`, the
same factory the map uses, so they look identical on both. Strokes and fills go on our own
`CanvasWidget` — which means **filled areas survive**, something GRS's drawing renderer cannot do at
all (it's a pool of rotated image strips).

**The other direction already works.** GRS injects its own `GRS_VanillaMapMarkersUI` /
`GRS_VanillaMapDrawingsUI` into the vanilla fullscreen map, so ATAK markers are already visible on
the normal map without us doing anything.

## What it deliberately does NOT do

It does not mirror our markers into GRS's data model. A mirror would mean:

- **Double replication and double storage** — every Anarchy marker becomes a second, GRS-replicated
  marker.
- **A visibility leak** — GRS markers are keyed by faction, so a marker on our **Group** channel
  would become visible to the whole faction.
- **A lossy model** — our fills, sizes and icon set don't fit GRS's palette/SIDC.

Everything here is read through `AM_MarkerAPI` on the client and renders only what that client can
already see. No server traffic, no mirrored data, channel visibility respected for free.

## Files

- `AMGA_MapConfig.c` — splices our map layer into ATAK's map config.
- `AMGA_DeviceDisplay.c` — renders our markers/strokes/fills on the RT device screen.
