// Public API of the "Anarchy Markers" mod. This is the one stable entry point — the internals
// (SM_MapMarkerStore, SM_MarkerNet and friends) are not meant to be touched directly.
//
// Add "Anarchy Markers" to Dependencies in your addon.gproj, then call AM_MarkerAPI.* directly.
//
// Reads and events work on either side: the server sees everything, a client only what its
// channel/faction allows. Request* methods pick the right path themselves — on a client they go
// through an RPC (the server assigns id/owner/channel and enforces limits and permissions), on the
// server they run straight away. Server* methods are for server code only and hand back the object.
//
// Coordinates are world metres; in a vector that means [0]=X and [2]=Z, as with entity GetOrigin().
//
// Subscribing to events:
//     AM_MarkerAPI.GetOnMarkerAdded().Insert(OnMarkerAdded);
//     void OnMarkerAdded(SM_MapMarkerData data) { ... }
//
// See Docs/API.md for the full reference, including AM_MapFeatures and AM_VanillaBridge.

//! Everything needed to RENDER one drawing on a custom surface (a tablet screen, a minimap, any
//! canvas that is not our map layer), with the mod's internals fully resolved:
//!  - m_aLines: the polylines to stroke, world-metre x,z pairs, each with its own width in metres.
//!    A plain drawing is one line; a parametric shape (rectangle/circle/grid) is already expanded
//!    into its real geometry — grid labels included — so a consumer never needs to know shapes exist.
//!  - m_aFillPts/m_aFillIndices: for a filled area, the outline and its triangulation (ready for a
//!    TriMeshDrawCommand). m_aFillIndices is null when the outline could not be triangulated — skip
//!    the fill then, do not feed it to PolygonDrawCommand (it chokes on concave shapes).
//!  - the AABB is shape-aware (a circle's box is centre±radius, not the box of its two parameters).
//! Get one from AM_MarkerAPI.GetDrawingRenderData(); it is cached, so call it every tick freely.
class AM_DrawingRenderData
{
	ref array<ref SM_ShapeLine> m_aLines = {};	// stroke polylines: world pts + width metres each
	ref array<int> m_aFillPts;					// fill outline (x,z pairs); null for a plain stroke
	ref array<int> m_aFillIndices;				// fill triangulation; null = не triangulable, skip
	int m_iMinX, m_iMaxX, m_iMinZ, m_iMaxZ;		// world AABB for culling (shape-aware)
	int m_iColor;								// ARGB, opacity already applied
	bool m_bFill;
}

class AM_MarkerAPI
{
	//! Contract version. Signatures only ever get extended, never broken.
	//! 2 = map feature flags (AM_MapFeatures) added, Local markers/drawings moved client-side.
	//! 3 = render policy for always-on screens (AM_MapFeatures.SetRenderPolicy).
	//! 4 = AM_MarkerWidgets: build/dress/place marker widgets on ANY surface, not just the map
	//!     (for tablet or terminal mods that paint their own screen instead of using SCR_MapEntity).
	//! 5 = AM_MapFeatures.SetLayerVisible (live show/hide, for a host with its own layer toggles) and
	//!     SetForcedVisibilityForMode (pin the channel, drop our channel picker from the panel).
	//! 6 = AM_MapFeatures.SetDrawPanelHiddenForMode + SCR_MapMarkersUI.AM_ToggleDrawPanel (the host's
	//!     own toolbar button owns our panel) and SetHintNudgeForMode (move our hints off its chrome).
	//! 7 = AM_VanillaBridge.SetIncludeLocal actually works. Local markers were silently dropped by the
	//!     eligibility check regardless of the flag, so no consumer could ever see one.
	//! 8 = drawing templates: read/save/delete the player's template files, AreTemplatesAllowed
	//!     (the server's allowTemplates switch, replicated).
	//! 9 = GetDrawingRenderData (render-ready, cached geometry for custom surfaces — expands
	//!     parametric shapes, triangulates fills, shape-aware AABB, widths in metres) and
	//!     AttachToMapConfig (splice our layer into a host map config in one call).
	static const int API_VERSION = 9;

	//! Splice our marker/drawing layer into a host map's configuration — THE call for a tablet or
	//! terminal mod whose screen is a real SCR_MapEntity. Use it from your SetupMapConfig override,
	//! after super built the config and after you verified the config is YOURS (check for your own
	//! components in cfg.Components — the map mode alone is not enough, other mods open PLAIN maps
	//! too). Idempotent: SetupMapConfig results are cached by the engine and re-issued on re-open,
	//! so the dedup lives here and not in every host.
	//!
	//! What the layer may DO on your screen is a separate, per-mode decision — set it right after:
	//!     AM_MarkerAPI.AttachToMapConfig(cfg);
	//!     AM_MapFeatures.SetForMode(mapMode, AM_MapFeatures.VIEW | AM_EMapFeature.DRAWING_TOOLS);
	//! (see AM_MapFeatures for channel pinning, hiding our panel behind your own button, hint nudges).
	//! Returns false only when the config has no component list to join.
	static bool AttachToMapConfig(notnull MapConfiguration cfg)
	{
		if (!cfg.Components)
			return false;
		foreach (SCR_MapUIBaseComponent c : cfg.Components)
		{
			if (SCR_MapMarkersUI.Cast(c))
				return true;	// already spliced (cached config re-issued)
		}
		cfg.Components.Insert(new SCR_MapMarkersUI());
		return true;
	}

	//! true on the authoritative side: dedicated server, listen-host or the SP editor.
	static bool IsServer()
	{
		return Replication.IsServer();
	}

	//! null on a dedicated server, where there is no local player.
	protected static SCR_PlayerController LocalPC()
	{
		return SCR_PlayerController.Cast(GetGame().GetPlayerController());
	}

	// --- Markers: reading ---

	static int GetMarkerCount()
	{
		return SM_MapMarkerStore.GetInstance().Count();
	}

	//! Every marker visible on THIS side. These are the live objects, so treat them as read-only —
	//! mutating them desyncs you from the server. Change things through Request*/Server*, and take
	//! data.SM_Clone() when you need a copy.
	static void GetAllMarkers(out array<SM_MapMarkerData> outMarkers)
	{
		SM_MapMarkerStore.GetInstance().GetAll(outMarkers);
	}

	static SM_MapMarkerData GetMarkerById(int id)
	{
		return SM_MapMarkerStore.GetInstance().FindById(id);
	}

	static void GetMarkersByOwner(int playerId, out array<SM_MapMarkerData> outMarkers)
	{
		if (!outMarkers)
			outMarkers = {};
		outMarkers.Clear();

		array<SM_MapMarkerData> all = {};
		SM_MapMarkerStore.GetInstance().GetAll(all);
		foreach (SM_MapMarkerData m : all)
		{
			if (m && m.m_iOwnerId == playerId)
				outMarkers.Insert(m);
		}
	}

	//! centerWorld: [0]=X, [2]=Z. Radius in metres.
	static void GetMarkersInRadius(vector centerWorld, float radiusMeters, out array<SM_MapMarkerData> outMarkers)
	{
		if (!outMarkers)
			outMarkers = {};
		outMarkers.Clear();

		float cx = centerWorld[0];
		float cz = centerWorld[2];
		float r2 = radiusMeters * radiusMeters;

		array<SM_MapMarkerData> all = {};
		SM_MapMarkerStore.GetInstance().GetAll(all);
		foreach (SM_MapMarkerData m : all)
		{
			if (!m)
				continue;
			float dx = m.m_iPosX - cx;
			float dz = m.m_iPosY - cz;
			if (dx * dx + dz * dz <= r2)
				outMarkers.Insert(m);
		}
	}

	static vector GetMarkerWorldPos(notnull SM_MapMarkerData m)
	{
		return Vector(m.m_iPosX, 0, m.m_iPosY);
	}

	// --- Markers: events. Added/Changed pass SM_MapMarkerData, Removed passes the id.
	//     They fire on whichever side you subscribed on; use IsServer() to tell them apart. ---

	static ScriptInvokerBase<SM_MarkerChangeInvoker> GetOnMarkerAdded()
	{
		return SM_MapMarkerStore.GetInstance().GetOnMarkerAdded();
	}

	static ScriptInvokerBase<SM_MarkerChangeInvoker> GetOnMarkerChanged()
	{
		return SM_MapMarkerStore.GetInstance().GetOnMarkerChanged();
	}

	static ScriptInvokerBase<SM_MarkerRemoveInvoker> GetOnMarkerRemoved()
	{
		return SM_MapMarkerStore.GetInstance().GetOnMarkerRemoved();
	}

	// --- Markers: writing ---

	//! Place a marker; build data with NewCivilianMarker/NewMilitaryMarker. On a client the result
	//! arrives as an OnMarkerAdded event — use ServerPlaceMarker when you need the object itself.
	static bool RequestPlaceMarker(notnull SM_MapMarkerData data)
	{
		// Local (PERSONAL) markers never reach the server; they live in this machine's client file,
		// keyed by server code and faction. That needs a local player with a faction and a server
		// code already received, so this can legitimately fail.
		if (data.m_iVisibility == SM_EMarkerVisibility.PERSONAL)
			return SM_LocalMarkerPersistence.GetInstance().AddLocal(data.SM_Clone()) != 0;

		if (Replication.IsServer())
			return ServerPlaceMarker(data) != null;

		SCR_PlayerController pc = LocalPC();
		if (!pc)
			return false;
		pc.SM_RequestPlace(data.PackInts(), data.m_sText);
		return true;
	}

	//! Change the editable fields (colour, text, visibility, kind, size...). id and owner stay put.
	static bool RequestEditMarker(int id, notnull SM_MapMarkerData data)
	{
		if (Replication.IsServer())
		{
			SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
			if (!store.ServerUpdate(id, data.PackInts(), data.m_sText))
				return false;
			SM_MapMarkerData m = store.FindById(id);
			if (!m)
				return false;
			SM_MarkerNet.AssignChannel(m.m_iOwnerId, m);	// visibility may have moved, so redo the channel
			SM_MarkerNet.BroadcastUpsertOrRemove(m);
			return true;
		}

		SCR_PlayerController pc = LocalPC();
		if (!pc)
			return false;
		pc.SM_RequestEdit(id, data.PackInts(), data.m_sText);
		return true;
	}

	//! world: [0]=X, [2]=Z.
	static bool RequestMoveMarker(int id, vector world)
	{
		int x = world[0];
		int z = world[2];

		if (Replication.IsServer())
		{
			SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
			if (!store.ServerMove(id, x, z))
				return false;
			SM_MapMarkerData m = store.FindById(id);
			if (m)
				SM_MarkerNet.BroadcastMove(m);
			return true;
		}

		SCR_PlayerController pc = LocalPC();
		if (!pc)
			return false;
		pc.SM_RequestMove(id, x, z);
		return true;
	}

	static bool RequestRemoveMarker(int id)
	{
		if (Replication.IsServer())
		{
			if (!SM_MapMarkerStore.GetInstance().ServerRemove(id))
				return false;
			SM_MarkerNet.BroadcastRemove(id);
			return true;
		}

		SCR_PlayerController pc = LocalPC();
		if (!pc)
			return false;
		pc.SM_RequestRemove(id);
		return true;
	}

	//! Server only. Returns the created object with its m_iId assigned, or null.
	//! A data.m_iChannel >= 0 is taken as given; otherwise the channel comes from the owner.
	static SM_MapMarkerData ServerPlaceMarker(notnull SM_MapMarkerData data)
	{
		if (!Replication.IsServer())
			return null;
		if (data.m_iVisibility == SM_EMarkerVisibility.PERSONAL)
			return null;	// Local markers are client-side; the server store never holds them

		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
		SM_MapMarkerData created = store.ServerCreate(data.m_iOwnerId, data.PackInts(), data.m_sText);
		if (!created)
			return null;

		if (data.m_iChannel >= 0)
			created.m_iChannel = data.m_iChannel;
		else
			SM_MarkerNet.AssignChannel(created.m_iOwnerId, created);

		SM_MarkerNet.BroadcastUpsert(created);
		return created;
	}

	//! Server only; same thing RequestRemoveMarker does on the server.
	static bool ServerRemoveMarker(int id)
	{
		if (!Replication.IsServer())
			return false;
		if (!SM_MapMarkerStore.GetInstance().ServerRemove(id))
			return false;
		SM_MarkerNet.BroadcastRemove(id);
		return true;
	}

	// --- Builders ---

	//! iconEntry indexes the vanilla PLACED_CUSTOM icon catalog.
	static SM_MapMarkerData NewCivilianMarker(vector world, int iconEntry, int argb, string text, SM_EMarkerVisibility visibility = SM_EMarkerVisibility.FACTION)
	{
		SM_MapMarkerData d = new SM_MapMarkerData();
		d.m_iPosX       = world[0];
		d.m_iPosY       = world[2];
		d.m_iKind       = SM_EMarkerKind.CIVILIAN;
		d.m_iIconEntry  = iconEntry;
		d.m_iColor      = argb;
		d.m_sText       = text;
		d.m_iVisibility = visibility;
		return d;
	}

	//! APP-6 symbol. identity = EMilitarySymbolIdentity, dimension = EMilitarySymbolDimension,
	//! symbolFlags = EMilitarySymbolIcon bitmask (0 draws the frame alone).
	static SM_MapMarkerData NewMilitaryMarker(vector world, int identity, int dimension, int symbolFlags, int argb, string text, SM_EMarkerVisibility visibility = SM_EMarkerVisibility.FACTION)
	{
		SM_MapMarkerData d = new SM_MapMarkerData();
		d.m_iPosX        = world[0];
		d.m_iPosY        = world[2];
		d.m_iKind        = SM_EMarkerKind.MILITARY;
		d.m_iIdentity    = identity;
		d.m_iDimension   = dimension;
		d.m_iSymbolFlags = symbolFlags;
		d.m_iColor       = argb;
		d.m_sText        = text;
		d.m_iVisibility  = visibility;
		return d;
	}

	// --- Drawings (strokes and fills): reading ---

	static int GetDrawingCount()
	{
		return SM_MapDrawingStore.GetInstance().Count();
	}

	//! Every drawing visible on this side. Geometry sits in data.m_aPoints as x,z pairs in metres.
	//! data.m_iFill != 0 means a closed, filled area; 0 means a plain polyline.
	static void GetAllDrawings(out array<SM_MapDrawingData> outDrawings)
	{
		SM_MapDrawingStore.GetInstance().GetAll(outDrawings);
	}

	static SM_MapDrawingData GetDrawingById(int id)
	{
		return SM_MapDrawingStore.GetInstance().FindById(id);
	}

	static void GetDrawingsByOwner(int playerId, out array<SM_MapDrawingData> outDrawings)
	{
		if (!outDrawings)
			outDrawings = {};
		outDrawings.Clear();

		array<SM_MapDrawingData> all = {};
		SM_MapDrawingStore.GetInstance().GetAll(all);
		foreach (SM_MapDrawingData d : all)
		{
			if (d && d.m_iOwnerId == playerId)
				outDrawings.Insert(d);
		}
	}

	//! Fills only — closed polygons, handy to read as "drawn zones".
	static void GetAllFills(out array<SM_MapDrawingData> outFills)
	{
		if (!outFills)
			outFills = {};
		outFills.Clear();

		array<SM_MapDrawingData> all = {};
		SM_MapDrawingStore.GetInstance().GetAll(all);
		foreach (SM_MapDrawingData d : all)
		{
			if (d && d.m_iFill != 0)
				outFills.Insert(d);
		}
	}

	// --- Drawings: events. Added passes SM_MapDrawingData, Removed passes the id.
	//     A stroke never changes once created, so there is no Changed event. ---

	static ScriptInvokerBase<SM_DrawingChangeInvoker> GetOnDrawingAdded()
	{
		return SM_MapDrawingStore.GetInstance().GetOnAdded();
	}

	static ScriptInvokerBase<SM_DrawingRemoveInvoker> GetOnDrawingRemoved()
	{
		return SM_MapDrawingStore.GetInstance().GetOnRemoved();
	}

	// --- Drawings: rendering on a custom surface ---

	protected static ref map<int, ref AM_DrawingRenderData> s_mRenderCache = new map<int, ref AM_DrawingRenderData>();
	protected static bool s_bRenderCacheHooked;

	//! Render-ready geometry for one drawing — call this instead of reading m_aPoints when you draw
	//! on your own surface. What it saves you from knowing:
	//!  - parametric shapes (m_iShape != 0) carry only two parameter points; drawing those raw gives
	//!    you a diagonal line. Here they arrive expanded into the real rectangle/circle/grid.
	//!  - fills need triangulation; it is done (and its failure remembered) here.
	//!  - brush widths are preset indices; here every line carries its width in metres.
	//!  - the AABB accounts for shape extents, so culling against it is correct.
	//!
	//! Cached per drawing id and evicted on the removal event, which our store also fires for every
	//! entry on bulk clears (faction change, new mission) — so a recycled id can never serve stale
	//! geometry. Drawings never mutate in place (an edit arrives as remove + add), which is what
	//! makes the cache safe. Call it every tick; after the first sight of a drawing it is a lookup.
	static AM_DrawingRenderData GetDrawingRenderData(notnull SM_MapDrawingData d)
	{
		if (!s_bRenderCacheHooked)
		{
			s_bRenderCacheHooked = true;
			GetOnDrawingRemoved().Insert(OnRenderCacheEvict);
		}

		AM_DrawingRenderData rd = s_mRenderCache.Get(d.m_iId);
		if (rd)
			return rd;

		if (d.GetPointCount() < 2)
			return null;

		rd = new AM_DrawingRenderData();
		rd.m_iColor = d.m_iColor;
		rd.m_bFill  = d.m_iFill != 0;

		if (d.m_iShape != SM_ShapeGeometry.SHAPE_NONE)
		{
			SM_ShapeGeometry.Build(d.m_iShape, d.m_aPoints, SM_DrawCanvas.WidthMeters(d.m_iWidthIdx), rd.m_aLines);
		}
		else if (rd.m_bFill)
		{
			rd.m_aFillPts = d.m_aPoints;	// live object, read-only by contract — no copy needed
			array<int> tri = {};
			if (SM_MapFloodFill.Triangulate(d.m_aPoints, tri) && tri.Count() >= 3)
				rd.m_aFillIndices = tri;
		}
		else
		{
			SM_ShapeLine line = new SM_ShapeLine();
			line.m_aPts = d.m_aPoints;
			line.m_fWidthMeters = SM_DrawCanvas.WidthMeters(d.m_iWidthIdx);
			rd.m_aLines.Insert(line);
		}

		// The data's own AABB is shape-aware; the fallback (invalid AABB) recomputes from raw points,
		// which for non-shapes is the same thing.
		if (!d.GetAABB(rd.m_iMinX, rd.m_iMaxX, rd.m_iMinZ, rd.m_iMaxZ))
		{
			int n = d.GetPointCount();
			d.GetPoint(0, rd.m_iMinX, rd.m_iMinZ);
			rd.m_iMaxX = rd.m_iMinX;
			rd.m_iMaxZ = rd.m_iMinZ;
			for (int i = 1; i < n; i++)
			{
				int x, z;
				d.GetPoint(i, x, z);
				if (x < rd.m_iMinX) rd.m_iMinX = x;
				if (x > rd.m_iMaxX) rd.m_iMaxX = x;
				if (z < rd.m_iMinZ) rd.m_iMinZ = z;
				if (z > rd.m_iMaxZ) rd.m_iMaxZ = z;
			}
		}

		s_mRenderCache.Set(d.m_iId, rd);
		return rd;
	}

	protected static void OnRenderCacheEvict(int drawingId)
	{
		s_mRenderCache.Remove(drawingId);
	}

	// --- Drawings: writing ---

	//! Add a stroke or a fill; build data with NewDrawing below.
	static bool RequestAddDrawing(notnull SM_MapDrawingData data)
	{
		if (data.m_iVisibility == SM_EMarkerVisibility.PERSONAL)
			return SM_LocalDrawingPersistence.GetInstance().AddLocal(data.SM_Clone()) != 0;	// client-side, as with markers

		if (Replication.IsServer())
			return ServerAddDrawing(data) != null;

		SCR_PlayerController pc = LocalPC();
		if (!pc)
			return false;
		pc.SM_DrawRequestAdd(data.PackMeta(), data.m_aPoints);
		return true;
	}

	static bool RequestRemoveDrawing(int id)
	{
		if (Replication.IsServer())
		{
			if (!SM_MapDrawingStore.GetInstance().ServerRemove(id))
				return false;
			SM_DrawingNet.BroadcastRemove(id);
			return true;
		}

		SCR_PlayerController pc = LocalPC();
		if (!pc)
			return false;
		pc.SM_DrawRequestRemove(id);
		return true;
	}

	//! Server only. A data.m_iChannel >= 0 is taken as given; otherwise the channel comes from the
	//! owner's faction or group. Returns the created object, or null.
	static SM_MapDrawingData ServerAddDrawing(notnull SM_MapDrawingData data)
	{
		if (!Replication.IsServer())
			return null;
		if (data.m_iVisibility == SM_EMarkerVisibility.PERSONAL)
			return null;	// Local drawings are client-side; the server store never holds them

		int channel = data.m_iChannel;
		if (channel < 0)
			channel = SM_DrawingNet.ChannelFor(data.m_iOwnerId, data.m_iVisibility);

		SM_MapDrawingData d = SM_MapDrawingStore.GetInstance().ServerCreate(
			data.m_iOwnerId, data.PackMeta(), data.m_aPoints, channel, System.GetTickCount(), data.m_sOwnerName);
		if (!d)
			return null;

		SM_DrawingNet.BroadcastAdd(d);
		return d;
	}

	static bool ServerRemoveDrawing(int id)
	{
		if (!Replication.IsServer())
			return false;
		if (!SM_MapDrawingStore.GetInstance().ServerRemove(id))
			return false;
		SM_DrawingNet.BroadcastRemove(id);
		return true;
	}

	// --- Drawing templates ---
	//
	// A template is a set of strokes the player saved to stamp down again anywhere; it lives as a
	// plain JSON file in $profile:SavingMarkers/Templates/, one file per template. Everything here
	// is CLIENT-SIDE: a dedicated server has no templates, and when a template is drawn its strokes
	// go through the normal drawing path under all the usual server limits. The in-game placement
	// flow (ghost, auto-draw) belongs to our map UI and is not exposed.

	//! false when this server switched templates off (allowTemplates in SM_Config.cfg, replicated).
	//! A host screen with its own template UI should hide it when this says no.
	static bool AreTemplatesAllowed()
	{
		return SM_MarkerConfig.GetInstance().m_bAllowTemplates;
	}

	static int GetTemplateCount()
	{
		return SM_TemplateStore.GetInstance().Count();
	}

	//! Every template on disk, sorted by name. Live objects — treat them as read-only.
	static void GetAllTemplates(out array<SM_DrawTemplate> outTemplates)
	{
		SM_TemplateStore.GetInstance().GetAll(outTemplates);
	}

	//! id = the file name without extension. null if there is no such template.
	static SM_DrawTemplate FindTemplate(string id)
	{
		return SM_TemplateStore.GetInstance().Find(id);
	}

	//! Re-scan the folder. Call after dropping files in from outside; cheap.
	static void ReloadTemplates()
	{
		SM_TemplateStore.GetInstance().Reload();
	}

	//! Save a set of drawings as a new template file. Points are taken in world metres and stored
	//! relative to the centre of their combined bounding box. Returns the new template's id, or ""
	//! when there was nothing usable to save.
	static string SaveTemplate(string name, notnull array<SM_MapDrawingData> strokes)
	{
		return SM_TemplateStore.GetInstance().SaveFromStrokes(name, strokes);
	}

	//! Deletes the file too. Built-in templates refuse.
	static bool DeleteTemplate(string id)
	{
		return SM_TemplateStore.GetInstance().Delete(id);
	}

	//! widthIdx 0..4 picks the brush preset (2/5/10/20/40 m). A fill wants a closed outline of at
	//! least 3 points.
	static SM_MapDrawingData NewDrawing(int argb, int widthIdx, bool fill, SM_EMarkerVisibility visibility, notnull array<vector> pointsWorld)
	{
		SM_MapDrawingData d = new SM_MapDrawingData();
		d.m_iColor      = argb;
		d.m_iWidthIdx   = widthIdx;
		d.m_iVisibility = visibility;
		if (fill)
			d.m_iFill = 1;
		else
			d.m_iFill = 0;

		array<int> pts = {};
		foreach (vector p : pointsWorld)
		{
			int px = p[0];
			int pz = p[2];
			pts.Insert(px);
			pts.Insert(pz);
		}
		d.SetPoints(pts);
		return d;
	}
}
