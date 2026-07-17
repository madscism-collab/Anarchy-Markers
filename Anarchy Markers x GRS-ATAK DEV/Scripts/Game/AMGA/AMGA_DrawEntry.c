// DEV ONLY: unlike the stable ATAK compat, DEV's own stylus button now runs real GRS functionality
// of its own — hijacking OnStylusClicked/EnterDrawingMode here would fight code GRS actively develops.
// So we leave their button alone entirely (no override of it, no suppression of EnterDrawingMode) and
// add a SEPARATE button next to it instead: a copy of the same GRS_StylusButton.layout resource (same
// background/click styling as every other tool tile), with OUR pencil icon on it, wired to open our
// panel. Two independent buttons, two independent tools — GRS's stylus is untouched.
//
// Coexistence note: because their stylus is no longer suppressed, a player COULD arm their drawing
// mode and ours at the same time. The original hijack avoided that on purpose (see the old revision's
// comment on two cursors/erasers fighting). This trade was made deliberately per instruction — if it
// turns out to matter in practice, the fix is to make each button's OnClicked call the other's "close"
// path before opening its own.
//
// AMGA_MapConfig starts our panel hidden; this new button is the entry point for it, same as the
// stylus was before.

modded class GRS_ATAKMenuUI
{
	protected ButtonWidget m_wAMPencilBtn;
	protected ImageWidget  m_wAMPencilIcon;

	//! Called once, right after GRS resolves m_wStylusBtn/m_wStylusIcon — the natural place to add a
	//! sibling tool tile of our own.
	protected override void WireDrawingControls()
	{
		super.WireDrawingControls();
		AMGA_BuildPencilButton();
	}

	//! Icons are SIBLING overlays over button tiles here, not button children (GRS's own comment above
	//! WireButtons: an ImageWidget nested inside a ButtonWidget silently doesn't render) — so this
	//! creates two widgets, a button and an icon on top of it, exactly like StylusBtn/StylusIcon are.
	protected void AMGA_BuildPencilButton()
	{
		if (!m_wStylusBtn)
			return;
		Widget parent = m_wStylusBtn.GetParent();
		if (!parent)
			return;
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;

		// Same resource as their stylus tile, so it matches the tray's look exactly (background,
		// hover/click tint). Same X as the stylus (145 — proven to sit inside the tray). Y pushed well
		// clear of the stylus tile (its own bottom edge is 510+56=566) so the two don't touch.
		Widget btn = ws.CreateWidgets("{8DC15225806A3646}UI/Layouts/ATAK/GRS_StylusButton.layout", parent);
		if (!btn)
			return;
		btn.SetFlags(WidgetFlags.NOFOCUS);
		const int BTN_X = 145;
		const int BTN_Y = 585;
		const int BTN_SIZE = 56;
		FrameSlot.SetPos(btn, BTN_X, BTN_Y);
		FrameSlot.SetSize(btn, BTN_SIZE, BTN_SIZE);
		m_wAMPencilBtn = ButtonWidget.Cast(btn);

		SCR_ButtonBaseComponent bbc = SCR_ButtonBaseComponent.Cast(btn.FindHandler(SCR_ButtonBaseComponent));
		if (bbc)
			bbc.m_OnClicked.Insert(AMGA_OnPencilClicked);

		// Smaller than StylusIcon's 34x34 (per request) — kept CENTRED on the button tile, so the
		// offset grows as the icon shrinks: (56-24)/2 = 16, vs. StylusIcon's own (56-34)/2 = 11.
		ImageWidget icon = ImageWidget.Cast(ws.CreateWidget(
			WidgetType.ImageWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.BLEND | WidgetFlags.STRETCH | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(0xFFFFFFFF), 0, parent));
		if (!icon)
			return;
		icon.LoadImageTexture(0, "{9208F7B7CF9BA33E}pencil.edds");
		// A fresh FrameWidgetSlot defaults to SizeToContent, which a just-created image with no
		// established content measures as 0x0 — SetSize below would get silently overridden back to
		// zero without this. (Same trap this codebase already hit once with CanvasWidget.)
		FrameSlot.SetSizeToContent(icon, false);
		const int ICON_SIZE = 24;
		int iconOfs = (BTN_SIZE - ICON_SIZE) / 2;
		FrameSlot.SetPos(icon, BTN_X + iconOfs, BTN_Y + iconOfs);
		FrameSlot.SetSize(icon, ICON_SIZE, ICON_SIZE);
		m_wAMPencilIcon = icon;

		AMGA_UpdatePencilLit(false);
	}

	protected void AMGA_OnPencilClicked(SCR_ButtonBaseComponent comp)
	{
		if (m_wColorPalette)
			m_wColorPalette.SetVisible(false);

		SCR_MapMarkersUI layer = AMGA_ResolveLayer();
		if (!layer)
			return;

		layer.AM_ToggleDrawPanel();
		AMGA_UpdatePencilLit(layer.AM_IsDrawPanelShown());
	}

	//! Same idle azure / lit amber the rest of the DEV toolbar family uses, kept on OUR OWN icon now —
	//! m_wStylusIcon belongs to GRS's real stylus tool and is none of our business any more.
	protected void AMGA_UpdatePencilLit(bool lit)
	{
		if (!m_wAMPencilIcon)
			return;
		if (lit)
			m_wAMPencilIcon.SetColor(new Color(1.0, 0.72, 0.20, 1.0));
		else
			m_wAMPencilIcon.SetColor(new Color(0.25, 0.55, 1, 1));
	}

	protected SCR_MapMarkersUI AMGA_ResolveLayer()
	{
		if (!m_MapEntity)
			return null;
		return SCR_MapMarkersUI.Cast(m_MapEntity.GetMapUIComponent(SCR_MapMarkersUI));
	}
}
