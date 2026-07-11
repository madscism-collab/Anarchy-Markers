// Anarchy Markers -> vanilla marker bridge, switched on.
//
// Anarchy Markers replaces vanilla player markers with its own system, so a mod that reads
// SCR_MapMarkerManagerComponent.GetStaticMarkers() sees an empty map. The core ships AM_VanillaBridge
// for exactly that case and keeps it off by default (it is a shim: enabling it makes our markers
// appear to EVERY consumer of that call, wanted or not). This addon is the opt-in.
//
// It is not tied to any one mod. EE Transport (Heli Enhanced Frame) is what it was written for, and
// works out of the box, but nothing here mentions EE — install this next to any mod that reads
// vanilla markers. Dependencies stay at base game + Anarchy Markers, so it can never break a server
// because a third mod was missing.
//
// Read-only in both directions: consumers see snapshots, and nothing they do comes back to us.

modded class SCR_BaseGameMode
{
	override void OnGameModeStart()
	{
		super.OnGameModeStart();

		AMVB_Config cfg = AMVB_Config.GetInstance();

		// A listen-host is its own server: it reads the file and never waits for an RPC. A client
		// starts on the defaults (bridge on, no filter) and gets the server's settings on sync —
		// so a consumer mod is never left with an empty list while the handshake is in flight.
		if (Replication.IsServer())
			cfg.ServerLoadOrCreate();

		cfg.Apply();
	}
}

modded class SCR_PlayerController
{
	// The client already asks the core for a sync when it spawns. Ride that handshake instead of
	// inventing a second one — the settings then land in the same packet burst as the markers they
	// describe, and there is no window where a client filters differently from the server.
	override protected void RpcAsk_RequestSync()
	{
		super.RpcAsk_RequestSync();

		if (!Replication.IsServer())
			return;

		AMVB_Config cfg = AMVB_Config.GetInstance();
		Rpc(AMVB_RpcDo_BridgeConfig, cfg.m_bEnabled, cfg.PrefixesCsv(), cfg.m_bIncludeLocal);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void AMVB_RpcDo_BridgeConfig(bool enabled, string prefixesCsv, bool includeLocal)
	{
		if (Replication.IsServer())
			return;

		AMVB_Config cfg = AMVB_Config.GetInstance();
		cfg.m_bEnabled      = enabled;
		cfg.m_bIncludeLocal = includeLocal;
		cfg.SetPrefixesCsv(prefixesCsv);
		cfg.Apply();
	}
}
