// Anarchy Markers renders through a modded SCR_MapMarkersUI. ATAK's tablet map IS a real
// SCR_MapEntity (opened with EMapEntityMode.PLAIN from GRS_ATAKMenuUI), but it ships its own
// GRS_MapConfig.conf, and that config has no SCR_MapMarkersUI in m_aUIComponents — so our layer
// is never instantiated there and our markers/drawings simply don't exist on the tablet.
//
// The fix is the same one we use for the Game Master map: don't replace their config (other mods
// touch it too), just splice our component into the list SetupMapConfig already returned. From
// then on the tablet renders our markers and drawings with OUR renderer — real icons, colours,
// fills, sizes — with no data mirroring, no extra replication, and channel visibility respected
// for free (a client's store only ever holds what that client may see).
//
// What the tablet is ALLOWED to do comes from AM_MapFeatures. PLAIN resolves to VIEW by default:
// markers and drawings render, but no drawing panel, no control hints and — importantly — no input
// listeners, so our hotkeys never fight ATAK's radial menu, cursor module or its own draw tools.

modded class SCR_MapEntity
{
	override MapConfiguration SetupMapConfig(EMapEntityMode mapMode, ResourceName configPath, Widget rootWidget)
	{
		MapConfiguration cfg = super.SetupMapConfig(mapMode, configPath, rootWidget);
		if (!cfg || !cfg.Components)
			return cfg;

		if (!AMGA_IsAtakConfig(cfg))
			return cfg;

		// SetupMapConfig caches the built config, so a re-open hands back the same instance —
		// bail if we already spliced ourselves in.
		foreach (SCR_MapUIBaseComponent c : cfg.Components)
		{
			if (SCR_MapMarkersUI.Cast(c))
				return cfg;
		}

		cfg.Components.Insert(new SCR_MapMarkersUI());	// our modded class (SM_MapMarkerLayer)

		// What the tablet may do with our layer. Markers and drawings render; the player also gets our
		// drawing panel and the pointer. Marker TOOLS stay off — ATAK has its own marker workflow and
		// two competing marker dialogs on one screen would be a mess.
		//
		// The channel is pinned to FACTION and our channel picker comes off the panel: ATAK scopes
		// every marker and drawing it owns to the player's faction (GRS_MarkerData.m_iFactionIdx, and
		// IsMarkerVisibleLocally compares faction indices), so a Group/Everyone switch in our panel
		// would promise the player an audience the tablet doesn't actually have.
		AM_MapFeatures.SetForMode(mapMode,
			AM_MapFeatures.VIEW | AM_EMapFeature.DRAWING_TOOLS | AM_EMapFeature.POINTER);
		AM_MapFeatures.SetForcedVisibilityForMode(mapMode, SM_EMarkerVisibility.FACTION);

		// The panel starts hidden: the tablet's own pencil (left tool column) is what opens it, so
		// the tablet keeps one entry point instead of showing a toolbar the player never asked for.
		// AMGA_DrawEntry repurposes that button.
		AM_MapFeatures.SetDrawPanelHiddenForMode(mapMode, true);

		// The tablet's tool column lives in the bottom-left, right where our hints anchor — shift them
		// clear of it. Tune here if ATAK's chrome moves.
		AM_MapFeatures.SetHintNudgeForMode(mapMode, 90, 0);

		return cfg;
	}

	//! ATAK's map config is recognised by its own UI components. The map mode alone is not enough:
	//! other tablet/terminal mods open PLAIN maps too, and we must not attach to theirs.
	protected bool AMGA_IsAtakConfig(notnull MapConfiguration cfg)
	{
		foreach (SCR_MapUIBaseComponent c : cfg.Components)
		{
			if (GRS_UserMarkersUI.Cast(c) || GRS_MapDrawingUI.Cast(c))
				return true;
		}
		return false;
	}
}
