// Read-only bridge for mods that only know the vanilla marker framework, i.e. that read
// SCR_MapMarkerManagerComponent.GetStaticMarkers() and therefore cannot see ours.
//
// It works by mixing proxy objects into the array that call returns. The proxies are never rendered
// and never registered with the vanilla manager, but every other consumer of that call does see
// them — which is precisely why the bridge stays off until somebody asks for it. Turn it on blindly
// and any mod that counts, iterates or deletes vanilla markers starts tripping over objects it never
// created.
//
// Once enabled it still hands out as little as it can: only markers the local player is allowed to
// see, only those matching the text filter, and no client-side Local markers (their ids are negative
// and consumers assume vanilla ones). The proxies also disappear while the vanilla UI builds its own
// static markers, so nothing gets drawn twice.
//
// Writes are not bridged. Proxies are snapshots, rebuilt on every call.
//
//     AM_VanillaBridge.SetTextFilter({"lz", "ln", "cas"});
//     AM_VanillaBridge.SetEnabled(true);
//
// New integrations should use AM_MarkerAPI instead — it is richer and has no side effects.

class AM_VanillaMarkerProxy : SCR_MapMarkerBase
{
	override void OnCreateMarker(bool skipProfanityFilter = false)
	{
	}
}

class AM_VanillaBridge
{
	protected static bool s_bEnabled;
	protected static bool s_bIncludeLocal;
	protected static bool s_bSuppressed;	// vanilla UI is drawing its own static markers right now
	protected static int  s_iExposedCount;

	protected static ref array<string> s_aTextPrefixes = {};

	// The array GetStaticMarkers() returns holds weak references, so without this cache the proxies
	// die the moment the call returns and consumers iterate over nulls.
	protected static ref array<ref AM_VanillaMarkerProxy> s_aAlive = {};

	static void SetEnabled(bool enabled)
	{
		if (s_bEnabled == enabled)
			return;
		s_bEnabled = enabled;
		if (!enabled)
		{
			s_aAlive.Clear();
			s_iExposedCount = 0;
		}
		Print(string.Format("[SM] Vanilla marker bridge %1", enabled), LogLevel.NORMAL);
	}

	static bool IsEnabled()
	{
		return s_bEnabled;
	}

	//! Expose only markers whose text starts with one of these prefixes (case-insensitive).
	//! An empty array exposes everything eligible. Keep it as narrow as your mod allows.
	static void SetTextFilter(array<string> prefixes)
	{
		s_aTextPrefixes.Clear();
		if (!prefixes)
			return;
		foreach (string p : prefixes)
		{
			string lo = p;
			lo.ToLower();
			lo.TrimInPlace();
			if (lo != "")
				s_aTextPrefixes.Insert(lo);
		}
	}

	//! Also expose the local player's Local-channel markers (ids <= -2). Off by default.
	static void SetIncludeLocal(bool include)
	{
		s_bIncludeLocal = include;
	}

	//! Proxies handed out by the last call. Use it to check your filter is as tight as you think.
	static int GetExposedCount()
	{
		return s_iExposedCount;
	}

	static void SetSuppressed(bool suppressed)
	{
		s_bSuppressed = suppressed;
	}

	static bool IsSuppressed()
	{
		return s_bSuppressed;
	}

	//! -1 on a dedicated server, where there is no local player to filter against.
	protected static int LocalPlayerId()
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return -1;
		return pc.GetPlayerId();
	}

	protected static bool PassesTextFilter(notnull SM_MapMarkerData d)
	{
		if (s_aTextPrefixes.IsEmpty())
			return true;
		string lo = d.m_sText;
		lo.ToLower();
		lo.TrimInPlace();
		foreach (string p : s_aTextPrefixes)
		{
			if (lo.StartsWith(p))
				return true;
		}
		return false;
	}

	static void Inject(notnull array<SCR_MapMarkerBase> output)
	{
		s_iExposedCount = 0;
		if (!s_bEnabled || s_bSuppressed)
			return;

		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
		if (!store)
			return;

		array<SM_MapMarkerData> ours = {};
		store.GetAll(ours);

		int localId = LocalPlayerId();

		s_aAlive.Clear();
		foreach (SM_MapMarkerData d : ours)
		{
			if (!d)
				continue;
			if (SM_MapMarkerStore.IsLocalId(d.m_iId) && !s_bIncludeLocal)
				continue;
			// A listen-host store holds everyone's markers, the enemy's included. Never hand a
			// consumer something the local player could not see on his own map.
			if (localId != -1 && !SM_MarkerNet.IsEligible(localId, d))
				continue;
			if (!PassesTextFilter(d))
				continue;

			AM_VanillaMarkerProxy p = new AM_VanillaMarkerProxy();
			p.SetCustomText(d.m_sText);
			p.SetMarkerOwnerID(d.m_iOwnerId);
			p.SetWorldPos(d.m_iPosX, d.m_iPosY);
			s_aAlive.Insert(p);
			output.Insert(p);
			s_iExposedCount++;
		}
	}
}

modded class SCR_MapMarkerManagerComponent
{
	override array<SCR_MapMarkerBase> GetStaticMarkers()
	{
		array<SCR_MapMarkerBase> output = super.GetStaticMarkers();
		AM_VanillaBridge.Inject(output);	// no-op while the bridge is off
		return output;
	}
}

modded class SCR_MapMarkersUI
{
	// SM_MapMarkerLayer already draws our markers, so hide the proxies from the vanilla renderer.
	override protected void CreateStaticMarkers()
	{
		AM_VanillaBridge.SetSuppressed(true);
		super.CreateStaticMarkers();
		AM_VanillaBridge.SetSuppressed(false);
	}
}
