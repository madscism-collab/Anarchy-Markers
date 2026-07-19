# Anarchy Markers — Modding API

Public integration surface for other mods. Two entry points:

| Class | What for |
|---|---|
| `AM_MarkerAPI` | Read/write markers and drawings, subscribe to events. |
| `AM_MapFeatures` | Control which Anarchy Markers features attach to a map screen (tablets, terminals, custom map UIs). |
| `AM_VanillaBridge` | Opt-in read-only bridge that exposes our markers to mods which only read the vanilla marker list. |

**Setup:** add `Anarchy Markers` to `Dependencies` in your `addon.gproj` and call the static methods directly. Do not touch internal classes (`SM_MapMarkerStore`, `SM_MarkerNet`, ...) — only what is documented here is kept stable.

`AM_MarkerAPI.API_VERSION` (currently `9`) is bumped whenever behavior changes. Existing signatures are only extended, never broken.

---

## Quick start — the three common integrations

**1. Place a marker from script** (a mission event, an admin tool):

```c
// Server side — you get the created object back:
SM_MapMarkerData m = AM_MarkerAPI.NewCivilianMarker(pos, 14, 0xFFFF4040, "Crash site");
AM_MarkerAPI.ServerPlaceMarker(m);

// Client side — fire and forget; the marker comes back via GetOnMarkerAdded:
AM_MarkerAPI.RequestPlaceMarker(m);
```

**2. Your tablet/terminal screen IS a map** (a real `SCR_MapEntity` with your own config) — splice our layer in and say what it may do:

```c
modded class SCR_MapEntity
{
    override MapConfiguration SetupMapConfig(EMapEntityMode mode, ResourceName path, Widget root)
    {
        MapConfiguration cfg = super.SetupMapConfig(mode, path, root);
        if (cfg && IsMyTabletConfig(cfg))   // check for YOUR components — mode alone is not enough
        {
            AM_MarkerAPI.AttachToMapConfig(cfg);                                  // idempotent splice
            AM_MapFeatures.SetForMode(mode, AM_MapFeatures.VIEW | AM_EMapFeature.DRAWING_TOOLS);
        }
        return cfg;
    }
}
```

**3. Your screen is NOT a map** (a hand-painted render target, a minimap) — draw our data yourself; the API hands you render-ready geometry. The whole drawing layer is this loop:

```c
array<SM_MapDrawingData> all = {};
AM_MarkerAPI.GetAllDrawings(all);
foreach (SM_MapDrawingData d : all)
{
    AM_DrawingRenderData rd = AM_MarkerAPI.GetDrawingRenderData(d);   // cached — call every tick
    if (!rd) continue;
    if (rd.m_iMaxX < viewMinX || rd.m_iMinX > viewMaxX
     || rd.m_iMaxZ < viewMinZ || rd.m_iMinZ > viewMaxZ) continue;     // shape-aware AABB culling

    if (rd.m_bFill)
    {
        if (!rd.m_aFillIndices) continue;            // untriangulable outline — skip, never Polygon
        TriMeshDrawCommand mesh = new TriMeshDrawCommand();
        mesh.m_iColor   = rd.m_iColor;
        mesh.m_Vertices = ProjectToMyScreen(rd.m_aFillPts);   // your projection, your units
        mesh.m_Indices  = rd.m_aFillIndices;                  // shared cache — do not mutate
        cmds.Insert(mesh);
    }
    else
    {
        foreach (SM_ShapeLine l : rd.m_aLines)       // 1 line for freehand, N for shapes/grids
        {
            LineDrawCommand line = new LineDrawCommand();
            line.m_iColor   = rd.m_iColor;
            line.m_fWidth   = l.m_fWidthMeters * myMetresToPixels;
            line.m_Vertices = ProjectToMyScreen(l.m_aPts);
            cmds.Insert(line);
        }
    }
}
myCanvas.SetDrawCommands(cmds);
```

Markers on a custom surface go through `AM_MarkerWidgets` the same way — see section 1. For a complete, shipping reference of both patterns read the *Anarchy Markers x GRS-ATAK* compat mod (`AMGA_MapConfig.c` for pattern 2, `AMGA_DeviceDisplay.c` / `AMGA_Minimap.c` for pattern 3).

> [!IMPORTANT]
> Everything you read from the API (`GetAll*`, events) is **live objects — read-only by contract**. Mutating them desyncs you from the server; change things through `Request*`/`Server*`, copy with `SM_Clone()`.

---

## 1. Map feature flags (`AM_MapFeatures`)

Anarchy Markers hooks the vanilla map framework, so by default it appears on **every** map the game opens — including map screens created by other mods (GPS tablets, base terminals, mission boards).

Since API v2 each map open resolves a **feature mask** first:

| `EMapEntityMode` of the screen | Default features |
|---|---|
| `FULLSCREEN` (player's map), `EDITOR` (Game Master) | `FULL` — everything |
| everything else (`PLAIN`, `SPAWNSCREEN`, station terminals, unknown/custom) | `VIEW` — markers + drawings render; **no input is captured**, no panel, no hints, no pointer |

So a tablet mod that does nothing gets a clean read-only overlay: players see the team's markers and drawings, and none of our hotkeys or panels interfere with the tablet's own UI.

### Flags

```c
enum AM_EMapFeature
{
    MARKERS       = 1,   // render markers (+ hover tooltip)
    DRAWINGS      = 2,   // render drawings (strokes/fills)
    POINTER       = 4,   // "point a finger" (hold LMB/A on an empty spot; nearby teammates see it)
    MARKER_TOOLS  = 8,   // marker dialog, place/move/edit/delete, Del/double-click/gamepad actions
    DRAWING_TOOLS = 16,  // the drawing panel (pencil/eraser/fill) and its input
    TEMPLATES     = 32,  // the Templates tab in the drawing panel (API v8)
}

AM_MapFeatures.NONE  // 0
AM_MapFeatures.VIEW  // MARKERS | DRAWINGS
AM_MapFeatures.FULL  // everything, incl. TEMPLATES
```

`TEMPLATES` is deliberately **not** part of `DRAWING_TOOLS`: saving and stamping a template drives the fullscreen map's own input, so it does not work on a tablet-style screen. A host that wants the tab anyway opts in explicitly — `AM_MapFeatures.VIEW | AM_EMapFeature.DRAWING_TOOLS | AM_EMapFeature.TEMPLATES`. The player's fullscreen map and the GM editor (`FULL`) get it by default.

### Choosing features for your map screen

```c
// One-shot: affects only the NEXT map open. Call right before you open your map.
// Example — a tablet with markers, drawings and the finger pointer:
AM_MapFeatures.SetNextOpen(AM_MapFeatures.VIEW | AM_EMapFeature.POINTER);
// ... open your map screen ...

// Example — a tablet that also offers the full drawing panel:
AM_MapFeatures.SetNextOpen(AM_MapFeatures.VIEW | AM_EMapFeature.DRAWING_TOOLS);

// Persistent: every future open with this EMapEntityMode uses the mask.
AM_MapFeatures.SetForMode(EMapEntityMode.PLAIN, AM_MapFeatures.NONE); // fully opt out
AM_MapFeatures.ClearForMode(EMapEntityMode.PLAIN);                    // back to defaults
```

Notes:

- The one-shot mask (`SetNextOpen`) wins over per-mode overrides; per-mode overrides win over the defaults.
- Server config is still enforced on top: `allowDrawing=false` removes the canvas/panel everywhere, `pointerAllowed=false` disables the pointer, disabled visibility channels stay disabled, and all marker/drawing writes go through the same server-side permission and rate-limit checks.
- `MARKER_TOOLS` implies the full marker edit dialog. It is designed for the fullscreen map; on very small custom screens prefer `VIEW` (+`POINTER`) and add your own compact UI on top of `AM_MarkerAPI` write calls if needed.

### Your screen ships its own map config (`AttachToMapConfig`, API v9)

A device whose map uses its **own** `MapConfiguration` (not the vanilla gadget config) never lists our UI component, so our layer doesn't exist there at all — no markers, no drawings, nothing to feature-flag. The fix is one call from your `SetupMapConfig` override:

```c
AM_MarkerAPI.AttachToMapConfig(cfg);   // splices our layer into cfg.Components; idempotent
AM_MapFeatures.SetForMode(mode, ...);  // then say what it may DO on your screen
```

See the Quick start (pattern 2) for the full override.

> [!IMPORTANT]
> Identify your config by **your own components** being in `cfg.Components`, never by map mode — other mods open `PLAIN` maps too, and attaching to theirs is not yours to do. The call is idempotent because the engine caches built configs and re-issues them on every re-open.

### Render policy for always-on screens (API v3)

The fullscreen map is open for seconds at a time, so by default we track **every** marker on the map. A tablet or terminal that stays on screen permanently cannot afford that. A render policy caps it:

```c
// Only the next map open (pairs with SetNextOpen):
AM_MapFeatures.SetNextOpen(AM_MapFeatures.VIEW);
AM_MapFeatures.SetRenderPolicy(1500, 2000);   // radius 1.5 km, refresh the visible set every 2 s

// Or persistently for a mode:
AM_MapFeatures.SetRenderPolicyForMode(EMapEntityMode.PLAIN, 1500, 2000);
AM_MapFeatures.ClearRenderPolicyForMode(EMapEntityMode.PLAIN);
```

What the two numbers mean:

- **`radiusMeters`** — markers and drawings render only within this distance of the view centre. Marker widgets outside the radius are not merely hidden — they don't exist, so a map with 500 markers costs your screen only the nearby handful. Fills are cut at the radius edge; strokes that straddle it draw in full. `0` = unlimited.
- **`membershipIntervalMs`** — how often the visible **set** may be recomputed (markers entering or leaving the radius as the view follows the player). Clamped to ≥ 250 ms; 1000–3000 is a good range for a vehicle tablet. **Positions are exact every frame regardless** — the interval only delays markers popping in/out at the radius edge, so nothing ever slides against the terrain.

Notes:

- No policy (the default, and always the case for `FULLSCREEN`/`EDITOR` unless you insist) = exact current behavior.
- Pick the radius from your screen size and typical zoom: there is no point rendering what the widget can't show. For a 400 px wide tablet at a 1:25k-style zoom, 1000–2000 m is plenty.
- A marker placed far away appears on the screen within one interval once the player gets close — that is the trade, not a bug.

### Live layer switches and a pinned channel (API v5)

The feature mask is resolved once, when the map opens. Two things are not:

```c
// Your screen has its own "show markers / show drawings" toggles — push them every frame:
AM_MapFeatures.SetLayerVisible(showMarkers, showDrawings);

// Your screen owns the audience: pin the channel and take our channel picker off the panel.
AM_MapFeatures.SetForcedVisibilityForMode(EMapEntityMode.PLAIN, SM_EMarkerVisibility.FACTION);
AM_MapFeatures.SetForcedVisibilityNextOpen(SM_EMarkerVisibility.FACTION);   // or just the next open
```

- **`SetLayerVisible`** gates *rendering only* — nothing is destroyed, so flipping a toggle back is instant. It is global (one map is open at a time) and our layer resets it to visible on every map open, so a toggle you left off cannot follow the player onto the fullscreen map.
- **`SetForcedVisibilityForMode`** pins every marker and drawing created on that screen to one channel and **removes our channel picker from the drawing panel**. Use it when your screen has its own audience model: an ATAK-style tablet scopes everything to the player's faction, so leaving a Group/Everyone switch in our panel would promise the player an audience your screen doesn't actually have. Resolved per open, exactly like the feature mask, so it cannot leak onto the normal map.

### Your toolbar owns our panel (API v6)

If your screen already has a tool column, our drawing panel should not just appear on top of it — your button should be the way in:

```c
// Our panel starts hidden on your screen:
AM_MapFeatures.SetDrawPanelHiddenForMode(EMapEntityMode.PLAIN, true);

// Your button toggles it (the map layer is a modded SCR_MapMarkersUI):
SCR_MapMarkersUI layer = SCR_MapMarkersUI.Cast(m_MapEntity.GetMapUIComponent(SCR_MapMarkersUI));
if (layer)
{
    layer.AM_ToggleDrawPanel();          // also AM_SetDrawPanelShown(bool) / AM_IsDrawPanelShown()
}

// And shove our control hints clear of your chrome (they anchor bottom-left):
AM_MapFeatures.SetHintNudgeForMode(EMapEntityMode.PLAIN, 90, 0);   // +x = right, +y = down
```

Hiding the panel also disarms the active tool — a hidden panel must never keep drawing. Registering your button as a vanilla `SCR_MapToolEntry` is worth it: the tool menu is already gamepad-navigable, so console and PC get the same entry point with no second code path.

### Drawing markers on your own surface (`AM_MarkerWidgets`, API v4)

Everything above assumes your screen is a `SCR_MapEntity`. Some devices aren't: a render-target tablet may paint its own mini-map by hand — centred on the player, its own scale, maybe rotated to heading — and there is no map entity for our layer to attach to. `AM_MarkerWidgets` exists for that case. It owns what a marker **looks like**, and knows nothing about how you got to your screen coordinates:

```c
// Once, when a marker appears (subscribe to AM_MarkerAPI.GetOnMarkerAdded/Changed/Removed):
SM_MarkerVisual vis = AM_MarkerWidgets.Create(data, myFrameWidget);

// Every frame, with coordinates YOU projected however your screen works:
AM_MarkerWidgets.Place(vis, screenX, screenY, sizePx);
AM_MarkerWidgets.SetLabelsVisible(vis, inRange);

// When it goes away:
vis.Destroy();
```

- `Create(data, parent)` builds the civilian icon or the APP-6 military symbol, plus the label and timestamp, and dresses them with the marker's data. `parent` must be a frame widget (the widgets position themselves with `FrameSlot`). Returns `null` if the widgets couldn't be made.
- `Apply(vis)` re-dresses an existing visual after its data changed. If `m_iKind` flipped (civilian ↔ military) call `RebuildMain(vis, parent)` first — that swaps the main widget.
- `Place(vis, sx, sy, sizePx)` positions the marker's **centre** at `sx`/`sy` (DPI-unscaled layout units) at `sizePx` across. It hides labels once their font would go sub-pixel, but never shows them again — **label visibility is yours**, via `SetLabelsVisible`, so your own culling isn't fought every frame.
- `SizeFactor(m_iSize)` gives the marker's size multiplier; `SizePx(data, factor)` is the map's own `BASE_SIZE * SizeFactor * factor` if you want to match it. On a small screen you'll usually want a fixed base pixel size times `SizeFactor`, clamped — a 1000% marker would otherwise swallow a tablet.

Because the map layer uses this exact factory, a marker looks identical on your device and on the map, and it keeps looking identical when we change the look.

### Drawing the drawings on your own surface (`GetDrawingRenderData`, API v9)

For drawings the equivalent of the widget factory is one call:

```c
AM_DrawingRenderData rd = AM_MarkerAPI.GetDrawingRenderData(d);
```

| Field | Meaning |
|---|---|
| `m_aLines` | Polylines to stroke (`SM_ShapeLine`: world-metre `x,z` pairs + width in **metres** each). One line for a freehand stroke; a parametric shape arrives already expanded — a grid with all its cell lines and labels. |
| `m_aFillPts` / `m_aFillIndices` | For a fill: the outline and its triangulation, ready for `TriMeshDrawCommand`. `m_aFillIndices == null` means the outline couldn't be triangulated — skip it. |
| `m_iMinX/MaxX/MinZ/MaxZ` | World AABB for culling, **shape-aware** (a circle's box is centre±radius, not the box of its 2 parameter points). |
| `m_iColor`, `m_bFill` | ARGB (opacity baked in) and the fill flag. |

Project the points however your screen works and emit canvas commands — the full loop is in the Quick start above.

> [!TIP]
> Call it every tick, freely: results are cached per drawing id and evicted on the removal event (which also fires for every entry on bulk clears), so it is a map lookup after the first sight of each drawing. This works because drawings are **immutable** — an edit arrives as remove + add.

> [!WARNING]
> Do not render `m_aPoints` raw. For parametric shapes (`m_iShape != 0`) those are two *parameter* points — you would draw a diagonal line where the player sees a circle. `GetDrawingRenderData` is precisely the guard against that class of bug (we hit it ourselves).

---

## 2. Markers (`AM_MarkerAPI`)

Coordinates are world meters. In a `vector`, `[0]` = X and `[2]` = Z (same as entity `GetOrigin()`).

### Reading

```c
int  AM_MarkerAPI.GetMarkerCount();
void AM_MarkerAPI.GetAllMarkers(out array<SM_MapMarkerData> outMarkers);
SM_MapMarkerData AM_MarkerAPI.GetMarkerById(int id);
void AM_MarkerAPI.GetMarkersByOwner(int playerId, out array<SM_MapMarkerData> outMarkers);
void AM_MarkerAPI.GetMarkersInRadius(vector centerWorld, float radiusMeters, out array<SM_MapMarkerData> outMarkers);
vector AM_MarkerAPI.GetMarkerWorldPos(SM_MapMarkerData m);
```

- Reads work on **both sides**: the server sees everything; a client sees only what it is allowed to (its faction/group channels + Global + its own).
- Returned objects are **live references — treat them as read-only.** Mutating them directly desyncs the client from the server. To change a marker, go through `RequestEditMarker`/`RequestMoveMarker`. Need a safe copy? `data.SM_Clone()`.
- **Readiness:** stores fill up asynchronously (server: after persistence load ~1 s into the mission; client: after the first sync following spawn). An empty result early in the session may just mean "not synced yet" — prefer subscribing to events over polling once at init.

### Events

```c
AM_MarkerAPI.GetOnMarkerAdded().Insert(cb);    // void cb(SM_MapMarkerData data)
AM_MarkerAPI.GetOnMarkerChanged().Insert(cb);  // void cb(SM_MapMarkerData data)
AM_MarkerAPI.GetOnMarkerRemoved().Insert(cb);  // void cb(int markerId)
```

Events fire on whichever side you subscribed on: on the server for every authoritative change, on a client whenever its replicated mirror changes (including the initial sync flood after spawn/faction change). Use `AM_MarkerAPI.IsServer()` if you need to tell them apart.

### Writing

```c
// Works on both sides. On a client it goes through the server (id/owner/channel are
// assigned there; limits and permissions apply). Result arrives via OnMarkerAdded.
bool AM_MarkerAPI.RequestPlaceMarker(SM_MapMarkerData data);
bool AM_MarkerAPI.RequestEditMarker(int id, SM_MapMarkerData data);  // id/owner immutable
bool AM_MarkerAPI.RequestMoveMarker(int id, vector world);
bool AM_MarkerAPI.RequestRemoveMarker(int id);

// Server only. Return the created object (with assigned m_iId) or null.
SM_MapMarkerData AM_MarkerAPI.ServerPlaceMarker(SM_MapMarkerData data);
bool AM_MarkerAPI.ServerRemoveMarker(int id);
```

Convenient constructors:

```c
SM_MapMarkerData AM_MarkerAPI.NewCivilianMarker(vector world, int iconEntry, int argb, string text,
    SM_EMarkerVisibility visibility = SM_EMarkerVisibility.FACTION);

// identity = EMilitarySymbolIdentity, dimension = EMilitarySymbolDimension,
// symbolFlags = EMilitarySymbolIcon bitmask (0 = frame only)
SM_MapMarkerData AM_MarkerAPI.NewMilitaryMarker(vector world, int identity, int dimension,
    int symbolFlags, int argb, string text, SM_EMarkerVisibility visibility = SM_EMarkerVisibility.FACTION);
```

### Visibility channels

```c
enum SM_EMarkerVisibility
{
    PERSONAL = 0,  // "Local" — author only (see the Local section below)
    GROUP    = 1,  // the author's group
    FACTION  = 2,  // "Side" — the author's faction
    ALL      = 3,  // "Global" — everyone
}
```

Visibility can only be **widened** by edits (Local < Group < Side < Global) — the server rejects narrowing.

### `SM_MapMarkerData` fields (read)

| Field | Meaning |
|---|---|
| `m_iId` | Unique id. Server markers ≥ 1; **client-side Local markers ≤ -2**; -1 = unassigned. |
| `m_iOwnerId` | Author playerId; -1 = placed by the server/API. |
| `m_iPosX`, `m_iPosY` | World X / Z, meters. |
| `m_iKind` | `SM_EMarkerKind.CIVILIAN` (icon) or `MILITARY` (APP-6 symbol). |
| `m_iIconEntry` | Civilian: icon index in the vanilla PLACED_CUSTOM catalog. |
| `m_iIdentity`, `m_iDimension`, `m_iSymbolFlags` | Military: APP-6 symbol description. |
| `m_iColor` | ARGB. |
| `m_iRotation` | Degrees 0..359. |
| `m_iSize` | Percent of base size (25..1000; default 200). |
| `m_iVisibility`, `m_iChannel` | Channel; `m_iChannel` is the faction index / group id for FACTION/GROUP (server-assigned). |
| `m_iGmLocked` | 1 = locked by the Game Master (players can't move/edit/delete). |
| `m_iHideInfo` | 1 = hide the "Edited by"/side tooltip from players. |
| `m_iDate`, `m_iTime` | Timestamp `yyyymmdd` / `hhmm` (0 = none). |
| `m_sText`, `m_sLastEditor` | Label; name of the last editor. |

---

## 3. Drawings (`AM_MarkerAPI`)

Strokes and fills are **immutable**: they are added and removed whole (a partial erase is remove + add pieces). There is no "changed" event.

### Reading

```c
int  AM_MarkerAPI.GetDrawingCount();
void AM_MarkerAPI.GetAllDrawings(out array<SM_MapDrawingData> outDrawings);
SM_MapDrawingData AM_MarkerAPI.GetDrawingById(int id);
void AM_MarkerAPI.GetDrawingsByOwner(int playerId, out array<SM_MapDrawingData> outDrawings);
void AM_MarkerAPI.GetAllFills(out array<SM_MapDrawingData> outFills); // closed polygons = "drawn zones"

// Rendering a drawing yourself? Take the render-ready form instead of the raw fields (API v9):
AM_DrawingRenderData AM_MarkerAPI.GetDrawingRenderData(SM_MapDrawingData d);  // cached; see section 1
```

### Events

```c
AM_MarkerAPI.GetOnDrawingAdded().Insert(cb);    // void cb(SM_MapDrawingData data)
AM_MarkerAPI.GetOnDrawingRemoved().Insert(cb);  // void cb(int drawingId)
```

### Writing

```c
bool AM_MarkerAPI.RequestAddDrawing(SM_MapDrawingData data);   // both sides
bool AM_MarkerAPI.RequestRemoveDrawing(int id);                // both sides
SM_MapDrawingData AM_MarkerAPI.ServerAddDrawing(SM_MapDrawingData data);  // server only
bool AM_MarkerAPI.ServerRemoveDrawing(int id);                            // server only

// widthIdx 0..4 = 2/5/10/20/40 m brush presets. fill=true: pointsWorld is a closed
// outline, minimum 3 points.
SM_MapDrawingData AM_MarkerAPI.NewDrawing(int argb, int widthIdx, bool fill,
    SM_EMarkerVisibility visibility, array<vector> pointsWorld);
```

### `SM_MapDrawingData` fields (read)

| Field | Meaning |
|---|---|
| `m_iId` | Server ≥ 1; client-side Local ≤ -2; -1 = unassigned. |
| `m_iOwnerId`, `m_sOwnerName` | Author. |
| `m_iColor` | ARGB; opacity is baked into the alpha channel. |
| `m_iWidthIdx` | Brush preset 0..4 (2/5/10/20/40 m). |
| `m_iVisibility`, `m_iChannel` | Channel, same rules as markers. |
| `m_iFill` | 1 = filled area (points form a closed polygon), 0 = polyline. |
| `m_iShape` | 0 = freehand. Non-zero = a parametric shape (`SM_ShapeGeometry.SHAPE_RECT/CIRCLE/GRID`): `m_aPoints` then holds exactly 2 **parameter** points, not geometry (rect corners; circle centre + rim point; grid A1 top-left + field bottom-right). Never draw the 2 raw points — take `GetDrawingRenderData` (API v9), which hands you the expanded polylines. |
| `m_iGmLocked`, `m_iHideInfo` | GM flags. |
| `m_aPoints` | World coordinates, flat `x,z` pairs in meters (but see `m_iShape`). |

### Drawing templates (API v8)

A template is a set of strokes the player saved to stamp down again anywhere on the map. Templates are **entirely client-side**: each is a plain JSON file in `$profile:SavingMarkers/Templates/` (one file per template, the file name is the id, shareable by copying the file), and when one is drawn its strokes go through the normal drawing path under all the usual server limits. A dedicated server has no templates. The in-game placement flow (ghost, auto-draw pacing) belongs to our map UI and is not exposed.

```c
AM_MarkerAPI.AreTemplatesAllowed();                  // server's allowTemplates switch (replicated)
AM_MarkerAPI.GetTemplateCount();
AM_MarkerAPI.GetAllTemplates(out array<SM_DrawTemplate> list);  // sorted by name, read-only objects
AM_MarkerAPI.FindTemplate(id);                       // id = file name without extension, null if absent
AM_MarkerAPI.ReloadTemplates();                      // re-scan the folder (after dropping files in)
AM_MarkerAPI.SaveTemplate(name, strokes);            // array<SM_MapDrawingData> -> new id, "" on failure
AM_MarkerAPI.DeleteTemplate(id);                     // deletes the file; built-ins refuse
```

`SM_DrawTemplate` read fields: `m_sId`, `m_sName`, `m_bBuiltIn`, `m_iSpanX`/`m_iSpanZ` (bounding box in meters), `m_aStrokes` (`SM_DrawTemplateStroke`: `m_iColor`, `m_iWidthIdx`, `m_iFill`, `m_aPoints` — flat `x,z` pairs in meters **relative to the template's anchor**, its bounding-box centre).

Server owners can remove the feature's UI for their players with `allowTemplates=false` in `SM_Config.cfg`; `AreTemplatesAllowed()` reflects that on clients, and a host screen with its own template UI should hide it when this says no.

---

## 4. The Local channel (PERSONAL) — special behavior

Since v1.0.21, Local markers/drawings **never touch the server.** They live in files on the player's machine (`$profile:SavingMarkers/SM_LocalMarkers.json` / `SM_LocalDrawings.json`), keyed by a per-game random server code and the player's faction.

Consequences for API users:

- `RequestPlaceMarker`/`RequestAddDrawing` with `PERSONAL` visibility store the object **client-side** and return `false` when there is no usable slice yet (no local player, server code not received, dedicated server). `ServerPlaceMarker`/`ServerAddDrawing` with `PERSONAL` always return `null`.
- Local records appear in reads/events **only on the owning client**, with ids ≤ -2. Filter them out with `id <= -2` if you only care about shared state.
- The server never lists, saves, or replicates Local records.

---

## 5. Vanilla marker bridge (`AM_VanillaBridge`)

Anarchy Markers replaces vanilla player-placed markers with its own system, so mods that read `SCR_MapMarkerManagerComponent.GetStaticMarkers()` do **not** see our markers.

**For new integrations, use `AM_MarkerAPI` instead** — it is richer and has no side effects. The bridge exists only for mods you cannot change (EE Transport, older marker consumers).

### How it works, and why it is off by default

The bridge mixes lightweight **read-only proxy objects** into the array returned by `GetStaticMarkers()`. Those proxies are never rendered and never registered with the vanilla manager — but they *do* appear for **every** consumer of that call. That is unavoidable for a shim, and it is why nothing happens until someone explicitly opts in: otherwise any mod that counts, iterates or deletes vanilla markers would silently see objects it never created.

When enabled, the bridge still exposes as little as it can:

- only markers the **local player is allowed to see** (channel/faction rules). On a dedicated server (no local player) everything in the store is exposed to server-side consumers;
- only markers matching the **text filter**, if one is set;
- **client-side Local markers** are included only if you ask for them (`SetIncludeLocal`). They live solely in their author's store, so no eligibility check applies — and none could: an owner id is assigned by the server, which a Local marker never reaches, so it reports the local player as its owner instead;
- proxies are **suppressed entirely** while the vanilla UI builds its own static markers, so nothing is drawn twice.

A proxy carries **text, owner and world position — nothing else.** Type and marker id stay at their defaults. A consumer that reads those cannot be served by the bridge at all; point it at `AM_MarkerAPI`.

Writes are not bridged: proxies are read-only snapshots, rebuilt on each call.

### API

```c
static void AM_VanillaBridge.SetEnabled(bool enabled);   // default false
static bool AM_VanillaBridge.IsEnabled();

// Expose only markers whose text starts with one of these prefixes (case-insensitive).
// Empty array = every eligible marker. Narrow this as much as you can.
static void AM_VanillaBridge.SetTextFilter(array<string> prefixes);

static void AM_VanillaBridge.SetIncludeLocal(bool include); // default false
static int  AM_VanillaBridge.GetExposedCount();             // proxies handed out by the last call
```

### Example

```c
modded class SCR_BaseGameMode
{
    override void OnGameModeStart()
    {
        super.OnGameModeStart();
        AM_VanillaBridge.SetTextFilter({"lz", "ln", "cas"});  // narrow it if you can; empty = all
        AM_VanillaBridge.SetIncludeLocal(true);               // markers the player drew for himself
        AM_VanillaBridge.SetEnabled(true);
    }
}
```

Use `GetExposedCount()` to verify the filter is as narrow as you think it is.

Ready-made: the **`Anarchy Markers - EE Compat`** addon does this and exposes the settings in `$profile:SavingMarkers/SM_VanillaBridge.cfg`, replicated server → client. It is not EE-specific — install it next to any mod that reads vanilla markers.

---

## 6. Other compatibility notes

- **Server config** (`$profile:SavingMarkers/SM_Config.cfg`) always wins: channel toggles, marker/drawing limits, rate limits, pointer/drawing switches apply to API writes exactly like to player actions.
- Coordinates everywhere are world meters, X/Z plane; drawings and markers ignore height.
- Fills (`m_iFill = 1`) are computed client-side by a flood fill. An enclosed region up to roughly 8 km across (~53 km²) can be filled in one click; a bigger one is filled partially and the next click continues it. A region that is not enclosed (paint escapes the largest search window) is rejected.
