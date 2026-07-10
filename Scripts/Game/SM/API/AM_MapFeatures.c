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
	static const int FULL = 31;

	protected static ref map<int, int> s_mModeOverrides = new map<int, int>();
	protected static int s_iNextOpenMask = -1;

	protected static ref map<int, ref AM_MapRenderPolicy> s_mModePolicies = new map<int, ref AM_MapRenderPolicy>();
	protected static ref AM_MapRenderPolicy s_NextOpenPolicy;

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
