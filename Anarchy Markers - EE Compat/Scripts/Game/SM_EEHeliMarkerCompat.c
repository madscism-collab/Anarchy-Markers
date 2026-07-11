// Anarchy Markers -> EE Transport (DropAndReturn / Heli Enhanced Frame) compat.
//
// EE reads markers through the vanilla SCR_MapMarkerManagerComponent.GetStaticMarkers() and so
// cannot see ours. Anarchy Markers ships a read-only bridge for exactly this case; it is off by
// default and this addon is what switches it on.
//
// Exposure is kept to what EE actually reads — markers whose text starts with lz / ln / cas. The
// bridge handles the rest: channel visibility, skipping client-side Local markers, and staying out
// of the vanilla renderer.

modded class SCR_BaseGameMode
{
	override void OnGameModeStart()
	{
		super.OnGameModeStart();

		AM_VanillaBridge.SetTextFilter({"lz", "ln", "cas"});
		AM_VanillaBridge.SetEnabled(true);
	}
}
