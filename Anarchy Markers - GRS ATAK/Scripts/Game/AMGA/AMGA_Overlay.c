// The tablet's own "show markers / show drawings" toggles (Comms app -> GRS_MapOverlayController)
// should govern OUR markers and drawings too. From the player's side there is one map with one set
// of layers on it; having ATAK's markers vanish while ours stayed put would just look broken.
//
// GRS drives its own layers by SetVisible on a registered container widget. Our layer has no single
// container to hand them (marker widgets sit directly on the map frame, and drawings are canvas
// commands), so instead we push the toggle state into AM_MapFeatures every frame and our layer
// obeys. It gates RENDERING only — nothing is destroyed, so flipping a toggle back is instant.
//
// GRS_UserMarkersUI lives only on ATAK's own map config, which makes it the right place to hook:
// this never runs on the normal fullscreen map. And our layer resets the switches on every map open,
// so a toggle left off in the tablet cannot follow the player to the M map.

modded class GRS_UserMarkersUI
{
	override void Update(float timeSlice)
	{
		super.Update(timeSlice);

		AM_MapFeatures.SetLayerVisible(
			GRS_MapOverlayController.GetLayerVisible(GRS_EMapLayer.MARKERS),
			GRS_MapOverlayController.GetLayerVisible(GRS_EMapLayer.DRAWINGS));
	}
}
