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
	static const int API_VERSION = 7;

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
