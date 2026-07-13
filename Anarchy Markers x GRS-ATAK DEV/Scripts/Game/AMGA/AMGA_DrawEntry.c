// The tablet's stylus button (left column) now opens OUR drawing panel instead of ATAK's own.
//
// It is NOT the map's tool menu: ATAK's map config ships no SCR_MapToolMenuUI, so GRS_MapDrawingUI's
// tool entry never even registers. The pencil is StylusBtn in the ATAK menu itself. In DEV its click
// does one of two things: in drawing mode it commits the stroke (ExitDrawingMode), otherwise it pops
// the colour palette — and picking a swatch runs SelectColorAndArm -> EnterDrawingMode, which arms
// their canvas capture. That is the chain to cut, and cutting EnterDrawingMode cuts all of it: with
// the capture never armed, m_bDrawingMode never turns on and the commit branch is unreachable.
//
// Why replace rather than coexist: two drawing systems on one map means two cursors, two erasers and
// two sets of hotkeys fighting each other. Ours is the one whose strokes reach our channels, keep
// filled areas, and land in our persistence — so it wins outright, and their capture never arms.
//
// AMGA_MapConfig starts our panel hidden here, which makes the stylus the genuine entry point.

modded class GRS_ATAKMenuUI
{
	//! The stylus click is the entry point on both mouse and gamepad (whatever activates the button
	//! ends up here), so there is one path and console needs no special case.
	protected override void OnStylusClicked(SCR_ButtonBaseComponent comp)
	{
		// Deliberately no super: neither their palette nor their commit branch may run.
		if (m_wColorPalette)
			m_wColorPalette.SetVisible(false);

		SCR_MapMarkersUI layer = AMGA_ResolveLayer();
		if (!layer)
			return;

		layer.AM_ToggleDrawPanel();
		AMGA_UpdateStylusLit(layer.AM_IsDrawPanelShown());
	}

	//! Nothing may arm ATAK's own capture any more — SelectColorAndArm and any other route through
	//! EnterDrawingMode land here and do nothing.
	protected override void EnterDrawingMode()
	{
		// no-op: our panel owns drawing on this map
	}

	//! Keep the stylus icon's lit state honest — it reflects OUR panel now. Idle is the azure the DEV
	//! toolbar family uses (their UpdateStylusVisual paints the same colour), so a closed panel leaves
	//! the button looking like every other tool on that column.
	protected void AMGA_UpdateStylusLit(bool lit)
	{
		if (!m_wStylusIcon)
			return;
		if (lit)
			m_wStylusIcon.SetColor(new Color(1.0, 0.72, 0.20, 1.0));
		else
			m_wStylusIcon.SetColor(new Color(0.25, 0.55, 1, 1));
	}

	protected SCR_MapMarkersUI AMGA_ResolveLayer()
	{
		if (!m_MapEntity)
			return null;
		return SCR_MapMarkersUI.Cast(m_MapEntity.GetMapUIComponent(SCR_MapMarkersUI));
	}
}
