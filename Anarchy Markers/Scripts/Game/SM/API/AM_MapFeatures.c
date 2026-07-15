// Per-map-screen feature control. Public API for other mods.
//
// We hook the vanilla map framework, so we show up on every map the game opens — including the map
// screens other mods build: GPS tablets, base terminals, mission boards. Our panels and hotkey
// listeners have no business being there.
//
// Every map open therefore resolves a feature mask. The player's fullscreen map and the Game Master
// map get everything; every other mode gets VIEW, which renders markers and drawings and captures
// nothing. A mod can override that per mode, or for its next map open only.
//
//     // tablet that shows the drawings and lets the player point at the map
//     AM_MapFeatures.SetNextOpen(AM_MapFeatures.VIEW | AM_EMapFeature.POINTER);
//
//     // tablet that also wants the drawing panel
//     AM_MapFeatures.SetNextOpen(AM_MapFeatures.VIEW | AM_EMapFeature.DRAWING_TOOLS);
//
//     // or once, for a custom mode
//     AM_MapFeatures.SetForMode(EMapEntityMode.PLAIN, AM_MapFeatures.FULL);
//
// Server config still applies on top: allowDrawing=false kills the canvas everywhere, a disabled
// pointer stays disabled, channel switches keep being enforced.

enum AM_EMapFeature
{
	MARKERS       = 1,	// render markers (+ the hover tooltip)
	DRAWINGS      = 2,	// render strokes and fills
	POINTER       = 4,	// point at the map: hold LMB/A on empty ground, nearby allies see it
	MARKER_TOOLS  = 8,	// place/move/edit/delete markers, the edit dialog, Del, gamepad actions
	DRAWING_TOOLS = 16,	// the drawing panel and its input
	TEMPLATES     = 32,	// the Templates tab in the drawing panel. NOT part of DRAWING_TOOLS: its
						// placement flow needs the fullscreen map's input, so a tablet that asks for
						// DRAWING_TOOLS does not get it. Opt in explicitly (VIEW|DRAWING_TOOLS|TEMPLATES).
}

// Render budget for always-on screens (a tablet strapped to the arm, a base terminal). Without one
// we track every marker on the map each frame, which a small screen that never closes can't afford.
class AM_MapRenderPolicy
{
	float m_fRadiusMeters;		// render only within this distance of the view centre; 0 = no limit
	int   m_iMembershipMs;		// how often the visible set may be recomputed (markers entering /
								// leaving the radius); positions of what IS shown stay exact every frame
}

class AM_MapFeatures
{
	static const int NONE = 0;
	static const int VIEW = 3;	// MARKERS | DRAWINGS
	static const int FULL = 63;	// everything, incl. TEMPLATES — the player's map and the GM editor

	protected static ref map<int, int> s_mModeOverrides = new map<int, int>();
	protected static int s_iNextOpenMask = -1;

	protected static ref map<int, ref AM_MapRenderPolicy> s_mModePolicies = new map<int, ref AM_MapRenderPolicy>();
	protected static ref AM_MapRenderPolicy s_NextOpenPolicy;

	protected static ref map<int, int> s_mModeForcedVis = new map<int, int>();
	protected static int s_iNextOpenForcedVis = -1;

	protected static ref map<int, bool> s_mModePanelHidden = new map<int, bool>();
	protected static ref map<int, ref array<float>> s_mModeHintNudge = new map<int, ref array<float>>();

	// --- Control hints: nudge -------------------------------------------------------------------
	// Our hints anchor to the bottom-left of the map. A host screen has its own chrome down there, so
	// it can shove them out of the way. Layout units, added to our own position; positive x = right,
	// positive y = down.

	static void SetHintNudgeForMode(EMapEntityMode mode, float dx, float dy)
	{
		array<float> n = {dx, dy};
		s_mModeHintNudge.Set(mode, n);
	}

	static void ClearHintNudgeForMode(EMapEntityMode mode)
	{
		s_mModeHintNudge.Remove(mode);
	}

	//! Called by the map layer when it builds the hints.
	static void ResolveHintNudgeForOpen(MapConfiguration config, out float dx, out float dy)
	{
		dx = 0;
		dy = 0;

		int mode = EMapEntityMode.PLAIN;
		if (config)
			mode = config.MapEntityMode;

		array<float> n;
		if (!s_mModeHintNudge.Find(mode, n) || !n || n.Count() < 2)
			return;
		dx = n[0];
		dy = n[1];
	}

	// --- Drawing panel: start hidden ------------------------------------------------------------
	// A host with its own toolbar wants to own the entry point: our panel starts hidden and their
	// button toggles it (see AM_ToggleDrawPanel on the map layer). The tools still EXIST — this is
	// about who opens the panel, not whether drawing is allowed, which is DRAWING_TOOLS' job.

	static void SetDrawPanelHiddenForMode(EMapEntityMode mode, bool hidden)
	{
		s_mModePanelHidden.Set(mode, hidden);
	}

	static void ClearDrawPanelHiddenForMode(EMapEntityMode mode)
	{
		s_mModePanelHidden.Remove(mode);
	}

	//! Called by the map layer on every open.
	static bool ResolveDrawPanelHiddenForOpen(MapConfiguration config)
	{
		int mode = EMapEntityMode.PLAIN;
		if (config)
			mode = config.MapEntityMode;

		bool hidden;
		if (s_mModePanelHidden.Find(mode, hidden))
			return hidden;
		return false;
	}

	protected static bool s_bMarkersVisible = true;
	protected static bool s_bDrawingsVisible = true;

	// --- Live render switches -------------------------------------------------------------------
	// The feature mask is resolved once, when the map opens. These two are live: a host screen with
	// its own "show markers / show drawings" toggles pushes them every frame and our layer obeys.
	// They gate RENDERING only — nothing is deleted, so flipping one back brings everything straight
	// back. Global (one map is open at a time) and reset to visible on every map open.

	//! Show/hide our markers and drawings on the map screen that is open right now.
	static void SetLayerVisible(bool markers, bool drawings)
	{
		s_bMarkersVisible = markers;
		s_bDrawingsVisible = drawings;
	}

	static bool MarkersVisible()
	{
		return s_bMarkersVisible;
	}

	static bool DrawingsVisible()
	{
		return s_bDrawingsVisible;
	}

	//! Map layer calls this on open — a host's switches must not leak into the next screen.
	static void ResetLayerVisible()
	{
		s_bMarkersVisible = true;
		s_bDrawingsVisible = true;
	}

	// --- Forced visibility channel --------------------------------------------------------------
	// Pin everything the player creates on a screen to ONE visibility channel, and hide the channel
	// picker from our panel — the host owns that decision. For a host with its own audience model:
	// an ATAK-style tablet scopes everything to the player's faction, so leaving a Group/Global
	// switch in our panel would just be lying to the user about who will see the drawing.
	// Resolved per map open, exactly like the feature mask, so it cannot leak onto the normal map.

	//! Every future open in this mode pins new markers/drawings to `visibility` (an SM_EMarkerVisibility).
	static void SetForcedVisibilityForMode(EMapEntityMode mode, int visibility)
	{
		s_mModeForcedVis.Set(mode, visibility);
	}

	static void ClearForcedVisibilityForMode(EMapEntityMode mode)
	{
		s_mModeForcedVis.Remove(mode);
	}

	//! Next open only; pairs with SetNextOpen.
	static void SetForcedVisibilityNextOpen(int visibility)
	{
		s_iNextOpenForcedVis = visibility;
	}

	//! Called by the map layer on every open. -1 = the player picks the channel, as usual.
	static int ResolveForcedVisibilityForOpen(MapConfiguration config)
	{
		if (s_iNextOpenForcedVis >= 0)
		{
			int oneShot = s_iNextOpenForcedVis;
			s_iNextOpenForcedVis = -1;
			return oneShot;
		}

		int mode = EMapEntityMode.PLAIN;
		if (config)
			mode = config.MapEntityMode;

		int over;
		if (s_mModeForcedVis.Find(mode, over))
			return over;
		return -1;
	}

	//! Every future map open in this mode uses the given mask.
	static void SetForMode(EMapEntityMode mode, int featureMask)
	{
		s_mModeOverrides.Set(mode, featureMask);
	}

	static void ClearForMode(EMapEntityMode mode)
	{
		s_mModeOverrides.Remove(mode);
	}

	//! Consumed by the next map open, whatever its mode. Call it right before opening your screen.
	static void SetNextOpen(int featureMask)
	{
		s_iNextOpenMask = featureMask;
	}

	//! Render budget for the next map open. Pairs with SetNextOpen:
	//!     AM_MapFeatures.SetNextOpen(AM_MapFeatures.VIEW);
	//!     AM_MapFeatures.SetRenderPolicy(1500, 2000);  // 1.5 km around the view, refresh set every 2 s
	static void SetRenderPolicy(float radiusMeters, int membershipIntervalMs)
	{
		s_NextOpenPolicy = MakePolicy(radiusMeters, membershipIntervalMs);
	}

	//! Same, but for every future open in the given mode.
	static void SetRenderPolicyForMode(EMapEntityMode mode, float radiusMeters, int membershipIntervalMs)
	{
		s_mModePolicies.Set(mode, MakePolicy(radiusMeters, membershipIntervalMs));
	}

	static void ClearRenderPolicyForMode(EMapEntityMode mode)
	{
		s_mModePolicies.Remove(mode);
	}

	protected static AM_MapRenderPolicy MakePolicy(float radiusMeters, int membershipIntervalMs)
	{
		AM_MapRenderPolicy p = new AM_MapRenderPolicy();
		p.m_fRadiusMeters = radiusMeters;
		if (p.m_fRadiusMeters < 0)
			p.m_fRadiusMeters = 0;
		p.m_iMembershipMs = membershipIntervalMs;
		if (p.m_iMembershipMs < 250)
			p.m_iMembershipMs = 250;	// a shorter interval buys nothing, the set barely changes
		return p;
	}

	//! Called by the map layer on every open. null = no policy, render everything (the default —
	//! the fullscreen map and the GM editor are open briefly and want the full picture).
	static AM_MapRenderPolicy ResolvePolicyForOpen(MapConfiguration config)
	{
		if (s_NextOpenPolicy)
		{
			AM_MapRenderPolicy oneShot = s_NextOpenPolicy;
			s_NextOpenPolicy = null;
			return oneShot;
		}

		int mode = EMapEntityMode.PLAIN;
		if (config)
			mode = config.MapEntityMode;

		AM_MapRenderPolicy over;
		if (s_mModePolicies.Find(mode, over))
			return over;
		return null;
	}

	//! Called by the map layer on every open. One-shot mask wins, then the per-mode override.
	static int ResolveForOpen(MapConfiguration config)
	{
		if (s_iNextOpenMask >= 0)
		{
			int oneShot = s_iNextOpenMask;
			s_iNextOpenMask = -1;
			return oneShot;
		}

		int mode = EMapEntityMode.PLAIN;
		if (config)
			mode = config.MapEntityMode;

		int over;
		if (s_mModeOverrides.Find(mode, over))
			return over;

		if (mode == EMapEntityMode.FULLSCREEN || mode == EMapEntityMode.EDITOR)
			return FULL;
		return VIEW;
	}
}
