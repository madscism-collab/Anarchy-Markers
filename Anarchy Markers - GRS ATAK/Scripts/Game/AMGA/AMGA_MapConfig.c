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

		// Splice + dedup in one call (the config is cached by the engine and re-issued on re-open).
		if (!AM_MarkerAPI.AttachToMapConfig(cfg))
			return cfg;

		// What the tablet may do with our layer. Markers and drawings render; the player also gets our
		// drawing panel and the pointer. Marker TOOLS stay off — ATAK has its own marker workflow and
		// two competing marker dialogs on one screen would be a mess. TEMPLATES stay off too: their
		// place/save flow needs the fullscreen map's input and does nothing useful on the tablet.
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

		// The bar FITS here, so auto-fit leaves it alone — but fitting is not the same as belonging. At
		// full size it eats a large share of a tablet screen, and this is a map you read as much as you
		// draw on. Explicit shrink.
		AM_MapFeatures.SetPanelScaleForMode(mapMode, 0.7);

		// ATAK's own status strip runs along the top of the tablet screen, and our bar rests right under
		// it. Drop it clear. Layout units, so this keeps its proportion at whatever scale is set above.
		//
		// Per-mode, not the one-shot SetPanelOffsetNextOpen: this tablet is one device on one map mode,
		// so there is nothing for a sticky value to bleed onto. The one-shot form is for a host with
		// several screens sharing a mode.
		AM_MapFeatures.SetPanelOffsetForMode(mapMode, 0, 30);

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
