# Anarchy Markers × GRS ATAK (DEV)

Puts Anarchy Markers' markers and drawings on the ATAK tablet — both on its **map** and on the
**in-hand device screen** — and hands the tablet's stylus button to our drawing panel.

This is the **DEV** build of GRS ATAK. The BETA build has its own addon (`Anarchy Markers - GRS ATAK`);
they mod the same class names and must never be loaded together.

## What it does

**Map.** ATAK's tablet map is a real `SCR_MapEntity`, but it ships its own `GRS_MapConfig.conf` with no
`SCR_MapMarkersUI` — so our layer is never instantiated there. We splice our component into the config
`SetupMapConfig` already returned, and from then on the tablet renders our markers and drawings with
**our** renderer: real icons, APP-6 symbols, colours, filled areas. No data is mirrored, nothing extra
is replicated, and channel visibility comes for free — a client's store only ever holds what that
client may see.

**Device screen.** That one is not a map at all: it is a render target GRS paints by hand, projected
around the player at its own scale and heading. Our layer cannot attach, so we draw into their RT tree
using their projection, and our stuff lines up with theirs exactly.

**Stylus button.** ATAK's pencil normally opens its colour palette and arms its own canvas capture.
Here it opens **our** drawing panel instead, and their capture never arms — two drawing systems on one
map would mean two cursors, two erasers and two sets of hotkeys fighting each other.

**Layer toggles.** The tablet's own *markers / drawings* switches (Comms app) govern our layers too.
One screen, one set of layers, as far as the player is concerned.

## What it deliberately does not do

**Marker tools stay off on the tablet.** ATAK has its own marker workflow; two competing marker dialogs
on one screen would be a mess. You can look, point and draw — not edit.

**The channel is pinned to Side.** ATAK scopes every marker and drawing it owns to the player's faction
(`GRS_MarkerData.m_iFactionIdx`), so a Group or Everyone switch in our panel would promise the player
an audience the tablet does not actually have. Our channel picker is dropped from the panel there.

## Requirements

Anarchy Markers (API v6+) and GRS ATAK DEV. Everything is read through `AM_MarkerAPI` on the client.
