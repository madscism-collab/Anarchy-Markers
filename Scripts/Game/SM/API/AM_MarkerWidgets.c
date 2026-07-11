// How an Anarchy marker LOOKS, split away from where it gets drawn.
//
// The map layer used to own this: it built the widgets and positioned them straight from
// SCR_MapEntity.WorldToScreen. That works right up until someone wants our markers on a surface
// that is not the map — an ATAK-style tablet screen, a render target, a custom terminal — where
// the projection is entirely different (centred on the player, own scale, rotated by heading).
//
// So the two concerns are separated here. This factory builds and dresses the widgets; Place()
// takes SCREEN coordinates and a size, and does not care how the caller arrived at them. The map
// layer projects through WorldToScreen, an RT screen projects around the player — both end up with
// identical-looking markers, and there is one place to change when the look changes.
//
// Coordinates are DPI-UNSCALED (layout units), the same space FrameSlot.SetPos speaks.
// Everything is static: no instance, no state, nothing to reset between missions.
class AM_MarkerWidgets
{
	//! Icon/symbol size at 100% marker size and zoom factor 1. Scale it, don't change it.
	static const float BASE_SIZE = 720;
	static const float TEXT_RATIO = 0.22;		// label font = icon size * this
	static const float LABEL_OFFSET = 0.32;		// label TOP sits this * size below the icon CENTRE
	static const int HEART_ICON_BASE = 9000;	// 9000 = anarchyHeart1, 9001 = anarchyHeart2
	static const ResourceName HEART_IMAGESET = "{85C4164C04E12DB8}AnarchyHeart.imageset";

	//! Build a complete visual (icon or APP-6 symbol, label, timestamp) for a marker under parent,
	//! and dress it with the marker's data. Still needs a Place() call to land somewhere.
	//! Returns null if the widgets could not be created.
	static SM_MarkerVisual Create(notnull SM_MapMarkerData data, notnull Widget parent)
	{
		SM_MarkerVisual vis = new SM_MarkerVisual(data);
		RebuildMain(vis, parent);
		if (!vis.GetMainWidget())
			return null;

		vis.m_wLabel = BuildLabel(parent);
		vis.m_wTime = BuildLabel(parent);	// timestamp is its own widget (half the font size)
		Apply(vis);
		return vis;
	}

	//! (Re)build the main widget — the civilian icon or the military symbol — to match the marker's
	//! kind. Call after m_iKind changes; the label widgets are untouched.
	static void RebuildMain(notnull SM_MarkerVisual vis, notnull Widget parent)
	{
		if (!vis.m_Data)
			return;

		if (vis.m_wIcon)
		{
			vis.m_wIcon.RemoveFromHierarchy();
			vis.m_wIcon = null;
		}
		if (vis.m_wSymbol)
		{
			vis.m_wSymbol.RemoveFromHierarchy();
			vis.m_wSymbol = null;
		}
		vis.m_SymbolComp = null;

		if (vis.m_Data.m_iKind == SM_EMarkerKind.MILITARY)
			vis.m_wSymbol = BuildSymbol(vis, parent);
		else
			vis.m_wIcon = BuildIcon(parent);
	}

	//! Push the marker's data onto the widgets: icon/symbol, colour, rotation, label, timestamp.
	static void Apply(notnull SM_MarkerVisual vis)
	{
		SM_MapMarkerData d = vis.m_Data;
		if (!d)
			return;

		if (vis.m_wIcon)	// CIVILIAN
		{
			ResourceName imageset;
			string quad;
			if (ResolveCivIcon(d.m_iIconEntry, imageset, quad) && imageset != "" && quad != "")
				vis.m_wIcon.LoadImageFromSet(0, imageset, quad);
			vis.m_wIcon.SetColor(Color.FromInt(d.m_iColor));
			vis.m_wIcon.SetRotation(d.m_iRotation);
		}
		else if (vis.m_wSymbol && vis.m_SymbolComp)	// MILITARY (APP-6)
		{
			vis.m_SymbolComp.Update(BuildMilitarySymbol(d));
			vis.m_wSymbol.SetColor(Color.FromInt(d.m_iColor));
			// OverlayWidget has no SetRotation — turn each ImageWidget layer by the same angle.
			// The layers are stacked on the same centre, so this rotates the whole symbol.
			RotateChildren(vis.m_wSymbol, d.m_iRotation);
		}

		int labelColor = 0xFF000000;	// black unless the marker asked for its own colour
		if (d.m_iTextColored != 0)
			labelColor = d.m_iColor;

		if (vis.m_wLabel)
		{
			vis.m_wLabel.SetText(d.m_sText);
			vis.m_wLabel.SetColor(Color.FromInt(labelColor));
		}
		if (vis.m_wTime)
		{
			if (d.m_iDate != 0)
			{
				vis.m_wTime.SetText(DateTimeString(d.m_iDate, d.m_iTime));
				vis.m_wTime.SetColor(Color.FromInt(labelColor));
				vis.m_wTime.SetVisible(true);
			}
			else
			{
				vis.m_wTime.SetVisible(false);
			}
		}
	}

	//! Put the visual at sx/sy (DPI-unscaled screen coords, the marker's CENTRE) at sizePx across.
	//! Projection-agnostic: the map layer feeds it WorldToScreen, an RT screen its own math.
	//!
	//! Labels get hidden once their font would go sub-pixel (nothing sane renders at ~0px), but this
	//! never SHOWS them again: the caller owns label visibility (the map culls off-screen markers and
	//! the GM can hide marker info), and re-showing here would fight that every frame.
	static void Place(notnull SM_MarkerVisual vis, float sx, float sy, float sizePx)
	{
		Widget main = vis.GetMainWidget();
		if (!main)
			return;

		FrameSlot.SetSize(main, sizePx, sizePx);
		FrameSlot.SetPos(main, sx, sy);

		float mainFont = sizePx * TEXT_RATIO;
		float labelY = sy + sizePx * LABEL_OFFSET;	// below the visible icon, not the widget edge
		bool tinyText = (mainFont < 2.0);

		if (vis.m_wLabel)
		{
			if (tinyText)
			{
				vis.m_wLabel.SetVisible(false);
			}
			else
			{
				vis.m_wLabel.SetExactFontSize(mainFont);
				FrameSlot.SetPos(vis.m_wLabel, sx, labelY);
			}
		}
		if (vis.m_wTime)
		{
			if (tinyText)
			{
				vis.m_wTime.SetVisible(false);
				return;
			}
			vis.m_wTime.SetExactFontSize(mainFont * 0.5);
			float timeY = labelY;
			if (vis.m_Data && vis.m_Data.m_sText != "")
				timeY = labelY + mainFont * 1.1;	// on the line below the label
			FrameSlot.SetPos(vis.m_wTime, sx, timeY);
		}
	}

	//! Show/hide a visual's label + timestamp together (the timestamp also needs a date to show).
	//! Surfaces that do their own culling drive this; Place() only ever hides on sub-pixel fonts.
	static void SetLabelsVisible(notnull SM_MarkerVisual vis, bool visible)
	{
		if (vis.m_wLabel)
			vis.m_wLabel.SetVisible(visible);
		if (vis.m_wTime)
			vis.m_wTime.SetVisible(visible && vis.m_Data && vis.m_Data.m_iDate != 0);
	}

	//! Pixel size of a marker: BASE_SIZE * its size setting * the caller's zoom/scale factor.
	static float SizePx(notnull SM_MapMarkerData data, float factor)
	{
		return BASE_SIZE * SizeFactor(data.m_iSize) * factor;
	}

	//! m_iSize is a percentage; anything under 10 is treated as "unset" and means 100%.
	static float SizeFactor(int sizePercent)
	{
		if (sizePercent < 10)
			return 1.0;
		return sizePercent * 0.01;
	}

	//! Civilian icon: our two hearts, otherwise the vanilla PLACED_CUSTOM catalog.
	static bool ResolveCivIcon(int iconEntry, out ResourceName imageset, out string quad)
	{
		if (iconEntry >= HEART_ICON_BASE)
		{
			imageset = HEART_IMAGESET;
			if (iconEntry == HEART_ICON_BASE)
				quad = "anarchyHeart1";
			else
				quad = "anarchyHeart2";
			return true;
		}

		SCR_MapMarkerManagerComponent mgr = SCR_MapMarkerManagerComponent.GetInstance();
		if (!mgr)
			return false;

		SCR_MapMarkerConfig cfg = mgr.GetMarkerConfig();
		if (!cfg)
			return false;

		SCR_MapMarkerEntryPlaced placed = SCR_MapMarkerEntryPlaced.Cast(cfg.GetMarkerEntryConfigByType(SCR_EMapMarkerType.PLACED_CUSTOM));
		if (!placed)
			return false;

		ResourceName glow;
		return placed.GetIconEntry(iconEntry, imageset, glow, quad);
	}

	//! SCR_MilitarySymbol from our identity/dimension/icon fields.
	static SCR_MilitarySymbol BuildMilitarySymbol(notnull SM_MapMarkerData d)
	{
		SCR_MilitarySymbol sym = new SCR_MilitarySymbol();
		sym.SetIdentity(d.m_iIdentity);
		sym.SetDimension(d.m_iDimension);
		sym.SetIcons(d.m_iSymbolFlags);
		return sym;
	}

	//! Rotate every child ImageWidget (the military symbol is a stack of layers, not one image).
	static void RotateChildren(Widget root, float angle)
	{
		if (!root)
			return;
		Widget child = root.GetChildren();
		while (child)
		{
			ImageWidget img = ImageWidget.Cast(child);
			if (img)
				img.SetRotation(angle);
			RotateChildren(child, angle);	// nested layers
			child = child.GetSibling();
		}
	}

	//! Bare icon widget, pivot centred so SetPos places the CENTRE on the point.
	//! Size goes through the SLOT, not ImageWidget.SetSize — otherwise the widget fills the parent
	//! and STRETCH blows the image up with it.
	static ImageWidget BuildIcon(notnull Widget parent)
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		ImageWidget icon = ImageWidget.Cast(ws.CreateWidget(
			WidgetType.ImageWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.BLEND | WidgetFlags.STRETCH,
			Color.FromInt(Color.WHITE), 0, parent));
		if (!icon)
			return null;

		FrameSlot.SetAnchorMin(icon, 0, 0);
		FrameSlot.SetAnchorMax(icon, 0, 0);
		FrameSlot.SetAlignment(icon, 0.5, 0.5);
		FrameSlot.SetSize(icon, BASE_SIZE, BASE_SIZE);
		FrameSlot.SetPos(icon, 0, 0);
		return icon;
	}

	//! APP-6 symbol overlay; SCR_MilitarySymbolUIComponent draws the layers into it.
	static Widget BuildSymbol(notnull SM_MarkerVisual vis, notnull Widget parent)
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		Widget overlay = ws.CreateWidget(WidgetType.OverlayWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(Color.WHITE), 0, parent);
		if (!overlay)
			return null;

		FrameSlot.SetAnchorMin(overlay, 0, 0);
		FrameSlot.SetAnchorMax(overlay, 0, 0);
		FrameSlot.SetAlignment(overlay, 0.5, 0.5);
		FrameSlot.SetSize(overlay, BASE_SIZE, BASE_SIZE);
		FrameSlot.SetPos(overlay, 0, 0);

		SCR_MilitarySymbolUIComponent comp = new SCR_MilitarySymbolUIComponent();
		overlay.AddHandler(comp);	// HandlerAttached sets m_Widget = overlay
		vis.m_SymbolComp = comp;
		return overlay;
	}

	//! Bare label, pivot top-centre so it sits centred under the icon.
	//! SetSizeToContent hugs the text — without it the slot fills the parent and the label drifts.
	static TextWidget BuildLabel(notnull Widget parent)
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		TextWidget label = TextWidget.Cast(ws.CreateWidget(
			WidgetType.TextWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.NOWRAP | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(Color.WHITE), 0, parent));
		if (!label)
			return null;

		label.SetFont("{3E7733BAC8C831F6}UI/Fonts/RobotoCondensed/RobotoCondensed_Regular.fnt");
		FrameSlot.SetAnchorMin(label, 0, 0);
		FrameSlot.SetAnchorMax(label, 0, 0);
		FrameSlot.SetAlignment(label, 0.5, 0.0);
		FrameSlot.SetSizeToContent(label, true);
		return label;
	}

	//! "DD.MM.YYYY HH:MM" from the scenario date (yyyymmdd) and time (hhmm).
	static string DateTimeString(int date, int time)
	{
		int h = time / 100;
		int mi = time % 100;
		return DateString(date) + " " + Pad2(h) + ":" + Pad2(mi);
	}

	static string DateString(int date)
	{
		int y = date / 10000;
		int m = (date / 100) % 100;
		int d = date % 100;
		return Pad2(d) + "." + Pad2(m) + "." + y.ToString();
	}

	static string Pad2(int v)
	{
		if (v < 10)
			return "0" + v.ToString();
		return v.ToString();
	}
}
