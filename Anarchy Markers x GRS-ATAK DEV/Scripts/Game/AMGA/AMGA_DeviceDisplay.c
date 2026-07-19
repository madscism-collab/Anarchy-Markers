// Anarchy markers and drawings on ATAK's in-hand device screen.
//
// The tablet's MAP is a real SCR_MapEntity, so our normal map layer renders there as soon as it is
// spliced into ATAK's map config (see AMGA_MapConfig). The DEVICE screen is a different animal: a
// render target GRS paints by hand — contours, roads, grid, markers, drawings — projected around
// the player at its own scale and heading. There is no SCR_MapEntity behind it, so our layer can
// never attach and we have to draw ourselves.
//
// We draw into their RT tree with their projection, so our stuff lines up with theirs exactly:
//     px = HUD_CENTER_X + ( dx * cosH - dz * sinH) * scale
//     py = HUD_CENTER_Y + (-dx * sinH - dz * cosH) * scale
// (cosH/sinH are identity in north-up mode; they rotate the world in heading-up mode.)
//
// Markers go through AM_MarkerWidgets, the same factory the map uses, so a marker looks the same on
// the tablet as on the map — civilian icons, APP-6 symbols, colours, labels. Strokes and fills go on
// our own CanvasWidget: that keeps FILLED areas, which GRS's own drawing renderer cannot do at all
// (it is a pool of rotated image strips).
//
// Everything is read through AM_MarkerAPI on the client and renders only what this client can
// already see. Nothing is mirrored into GRS's data model, nothing touches the server, and channel
// visibility (Local / Group / Side / Global) is respected for free.

modded class GRS_DeviceDisplayComponent
{
	// Size of a DEFAULT marker on the RT, in HUD units. The map's BASE_SIZE (720 at max zoom) is
	// meaningless here, and so is matching GRS's 42-unit dots: the RT is a 1920x1080 texture squeezed
	// onto a palm-sized screen in the world, so 42 units comes out around a dozen actual pixels —
	// legible in a UI mock-up, unreadable on a device strapped to your chest. Sized against what else
	// is on that screen instead: a mid-width stroke lands near 80 units, so the marker sits above it.
	//
	// This ONE screen keeps a floor and a ceiling, against the philosophy everywhere else (a marker's
	// size belongs to the marker, not to the screen). The device is a few centimetres of glass on your
	// chest, seen at arm's length: a marker sized for a wall map covers the whole thing, and a tablet
	// showing one icon and no terrain is worse than useless. The minimap and the maps stay unclamped.
	protected static const float AMGA_MARKER_BASE_PX = 120;
	protected static const float AMGA_MARKER_MIN_PX  = 72;
	protected static const float AMGA_MARKER_MAX_PX  = 280;
	protected static const float AMGA_MIN_STROKE_PX  = 1.5;

	protected CanvasWidget   m_wAMGA_Canvas;		// our strokes + fills
	protected bool           m_bAMGA_Subscribed;
	// HUD pixels -> canvas units. Measured every tick from the two widgets themselves, never assumed.
	protected float          m_fAMGA_Scale = 1;
	protected float          m_fAMGA_OffX;
	protected float          m_fAMGA_OffY;

	protected ref map<int, ref SM_MarkerVisual> m_mAMGA_Vis = new map<int, ref SM_MarkerVisual>();
	protected ref array<ref CanvasWidgetCommand> m_aAMGA_Cmds = {};
	// Drawing geometry (shape expansion, fill triangulation, bounds) comes ready-made and cached from
	// AM_MarkerAPI.GetDrawingRenderData — no per-surface cache to keep in sync any more.

	// --- lifecycle ---------------------------------------------------------------------------

	//! We hang our widgets off GRS's own containers rather than off HudRoot. Two reasons: their
	//! layering already puts drawings under markers (a canvas parented straight to HudRoot would be
	//! the last sibling and paint over everything, ATAK's markers included), and both containers are
	//! frames spanning the HUD space — which is what FrameSlot needs and what makes their pixel
	//! coordinates ours.
	override protected void SetupHud()
	{
		super.SetupHud();

		if (m_wAMGA_Canvas || !m_wDrawingsRoot || !m_wMarkerContainer)
			return;

		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;

		m_wAMGA_Canvas = CanvasWidget.Cast(ws.CreateWidget(WidgetType.CanvasWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(0x00000000), 0, m_wDrawingsRoot));
		if (!m_wAMGA_Canvas)
			return;

		FrameSlot.SetAnchorMin(m_wAMGA_Canvas, 0, 0);
		FrameSlot.SetAnchorMax(m_wAMGA_Canvas, 1, 1);

		AMGA_Subscribe();
	}

	override protected void DestroyScreen()
	{
		AMGA_Teardown();
		super.DestroyScreen();
	}

	protected void AMGA_Teardown()
	{
		AMGA_Unsubscribe();

		foreach (int id, SM_MarkerVisual vis : m_mAMGA_Vis)
		{
			if (vis)
				vis.Destroy();
		}
		m_mAMGA_Vis.Clear();
		m_aAMGA_Cmds.Clear();

		if (m_wAMGA_Canvas)
			m_wAMGA_Canvas.RemoveFromHierarchy();
		m_wAMGA_Canvas = null;
	}

	// --- marker bookkeeping ------------------------------------------------------------------
	// Widgets are created and destroyed on store events, never per tick; the tick only projects and
	// culls what already exists. Marker churn on a tablet is rare, projection happens every frame.

	protected void AMGA_Subscribe()
	{
		if (m_bAMGA_Subscribed)
			return;
		AM_MarkerAPI.GetOnMarkerAdded().Insert(AMGA_OnMarkerAdded);
		AM_MarkerAPI.GetOnMarkerChanged().Insert(AMGA_OnMarkerChanged);
		AM_MarkerAPI.GetOnMarkerRemoved().Insert(AMGA_OnMarkerRemoved);
		m_bAMGA_Subscribed = true;

		array<SM_MapMarkerData> all = {};
		AM_MarkerAPI.GetAllMarkers(all);
		foreach (SM_MapMarkerData m : all)
		{
			if (m)
				AMGA_OnMarkerAdded(m);
		}
	}

	protected void AMGA_Unsubscribe()
	{
		if (!m_bAMGA_Subscribed)
			return;
		AM_MarkerAPI.GetOnMarkerAdded().Remove(AMGA_OnMarkerAdded);
		AM_MarkerAPI.GetOnMarkerChanged().Remove(AMGA_OnMarkerChanged);
		AM_MarkerAPI.GetOnMarkerRemoved().Remove(AMGA_OnMarkerRemoved);
		m_bAMGA_Subscribed = false;
	}

	protected void AMGA_OnMarkerAdded(SM_MapMarkerData data)
	{
		if (!data || !m_wMarkerContainer)
			return;

		AMGA_OnMarkerRemoved(data.m_iId);	// re-add after a change: drop the old widgets first

		SM_MarkerVisual vis = AM_MarkerWidgets.Create(data, m_wMarkerContainer);
		if (!vis)
			return;

		AMGA_SetVisualVisible(vis, false);	// stays hidden until the tick decides it is in range
		m_mAMGA_Vis.Set(data.m_iId, vis);
	}

	//! A marker's kind can flip (civilian <-> military), which swaps the whole main widget — cheapest
	//! correct answer is to rebuild the visual rather than patch it in place.
	protected void AMGA_OnMarkerChanged(SM_MapMarkerData data)
	{
		AMGA_OnMarkerAdded(data);
	}

	protected void AMGA_OnMarkerRemoved(int markerId)
	{
		SM_MarkerVisual vis = m_mAMGA_Vis.Get(markerId);
		if (vis)
			vis.Destroy();
		m_mAMGA_Vis.Remove(markerId);
	}

	protected void AMGA_SetVisualVisible(notnull SM_MarkerVisual vis, bool visible)
	{
		Widget main = vis.GetMainWidget();
		if (main)
			main.SetVisible(visible);
		AM_MarkerWidgets.SetLabelsVisible(vis, visible);
	}

	// --- rendering ---------------------------------------------------------------------------

	//! GRS calls this from TickHud with the tick's player position and metres->pixels scale, and it
	//! runs BEFORE the RT is pulsed, so whatever we draw here lands in this frame's texture.
	//! m_fTickCosH / m_fTickSinH were already set for this tick by TickHud.
	override protected void UpdateDrawings(vector playerPos, float scale)
	{
		super.UpdateDrawings(playerPos, scale);

		AMGA_RenderMarkers(playerPos, scale);
		AMGA_RenderDrawings(playerPos, scale);
	}

	protected void AMGA_ProjectWorld(float wx, float wz, vector playerPos, float scale, out float px, out float py)
	{
		float dx = wx - playerPos[0];
		float dz = wz - playerPos[2];
		px = HUD_CENTER_X + ( dx * m_fTickCosH - dz * m_fTickSinH) * scale;
		py = HUD_CENTER_Y + (-dx * m_fTickSinH - dz * m_fTickCosH) * scale;
	}

	//! The device's own layer toggles (Comms app) govern our markers and drawings too — one screen,
	//! one set of layers, as far as the player is concerned.
	protected void AMGA_RenderMarkers(vector playerPos, float scale)
	{
		if (!m_wMarkerContainer)
			return;

		if (!GRS_MapOverlayController.GetLayerVisible(GRS_EMapLayer.MARKERS))
		{
			foreach (int hid, SM_MarkerVisual hv : m_mAMGA_Vis)
			{
				if (hv)
					AMGA_SetVisualVisible(hv, false);
			}
			return;
		}

		float rangeSq = m_fHudRangeMeters * m_fHudRangeMeters;

		foreach (int id, SM_MarkerVisual vis : m_mAMGA_Vis)
		{
			if (!vis || !vis.m_Data)
				continue;

			float dx = vis.m_Data.m_iPosX - playerPos[0];
			float dz = vis.m_Data.m_iPosY - playerPos[2];
			if (dx * dx + dz * dz > rangeSq)
			{
				AMGA_SetVisualVisible(vis, false);
				continue;
			}

			float px, py;
			AMGA_ProjectWorld(vis.m_Data.m_iPosX, vis.m_Data.m_iPosY, playerPos, scale, px, py);

			float sizePx = Math.Clamp(AMGA_MARKER_BASE_PX * AM_MarkerWidgets.SizeFactor(vis.m_Data.m_iSize),
				AMGA_MARKER_MIN_PX, AMGA_MARKER_MAX_PX);

			AMGA_SetVisualVisible(vis, true);
			AM_MarkerWidgets.Place(vis, px, py, sizePx);
		}
	}

	protected void AMGA_RenderDrawings(vector playerPos, float scale)
	{
		if (!m_wAMGA_Canvas)
			return;

		// Bridge the canvas to the MARKER CONTAINER, not to the HUD grid.
		//
		// A CanvasWidget draws in its own unit space, and deriving the conversion from HUD_CENTER_X
		// assumes the canvas rect and the marker container are the same rectangle. They are two
		// different widgets: let them differ by a hair and the drawings come out at a slightly wrong
		// zoom from the markers, which is exactly what that looks like. So measure both and convert
		// between them — markers land correctly by definition, so a stroke expressed in their space
		// cannot land anywhere else.
		float mcw, mch, mcx, mcy, cw, ch, cx, cy;
		m_wMarkerContainer.GetScreenSize(mcw, mch);
		m_wMarkerContainer.GetScreenPos(mcx, mcy);
		m_wAMGA_Canvas.GetScreenSize(cw, ch);
		m_wAMGA_Canvas.GetScreenPos(cx, cy);
		if (mcw <= 0 || cw <= 0)
			return;	// not laid out yet

		// One canvas unit = one physical pixel; a HUD pixel is m_fAMGA_Scale of them, offset by however
		// far the two widgets sit apart on screen.
		m_wAMGA_Canvas.SetSizeInUnits(Vector(cw, ch, 0));
		m_wAMGA_Canvas.SetZoom(1.0);
		m_wAMGA_Canvas.SetOffsetPx(Vector(0, 0, 0));
		m_fAMGA_Scale = mcw / (HUD_CENTER_X * 2);
		m_fAMGA_OffX  = mcx - cx;
		m_fAMGA_OffY  = mcy - cy;

		m_aAMGA_Cmds.Clear();

		if (!GRS_MapOverlayController.GetLayerVisible(GRS_EMapLayer.DRAWINGS))
		{
			m_wAMGA_Canvas.SetDrawCommands(m_aAMGA_Cmds);	// empty list = nothing drawn
			return;
		}

		array<SM_MapDrawingData> all = {};
		AM_MarkerAPI.GetAllDrawings(all);

		float range = m_fHudRangeMeters;
		float minX = playerPos[0] - range;
		float maxX = playerPos[0] + range;
		float minZ = playerPos[2] - range;
		float maxZ = playerPos[2] + range;

		foreach (SM_MapDrawingData d : all)
		{
			if (!d)
				continue;

			// Ready-made geometry: shapes expanded, fills triangulated, bounds shape-aware. Before
			// this a rectangle/circle/grid rendered as a diagonal line here — the raw wire data is
			// just their two parameter points.
			AM_DrawingRenderData rd = AM_MarkerAPI.GetDrawingRenderData(d);
			if (!rd)
				continue;

			// Cheap reject: bounds against the visible square. A stroke that straddles the edge is
			// drawn whole rather than clipped — same as GRS does with its own drawings.
			if (rd.m_iMaxX < minX || rd.m_iMinX > maxX || rd.m_iMaxZ < minZ || rd.m_iMinZ > maxZ)
				continue;

			if (rd.m_bFill)
			{
				AMGA_AddFill(rd, playerPos, scale);
			}
			else
			{
				foreach (SM_ShapeLine l : rd.m_aLines)
				{
					if (l && l.m_aPts.Count() >= 4)
						AMGA_AddStroke(rd.m_iColor, l, playerPos, scale);
				}
			}
		}

		m_wAMGA_Canvas.SetDrawCommands(m_aAMGA_Cmds);
	}

	//! World metres -> canvas units, so the commands land exactly where the markers do. Projects
	//! straight into the array the draw command will own — LineDrawCommand keeps the reference, so
	//! a shared scratch here would end up with every command showing the last stroke's points.
	protected void AMGA_ProjectPts(notnull array<int> pts, vector playerPos, float scale, notnull array<float> outScr)
	{
		int n = pts.Count() / 2;
		outScr.Resize(n * 2);
		for (int i = 0; i < n; i++)
		{
			float px, py;
			AMGA_ProjectWorld(pts[i * 2], pts[i * 2 + 1], playerPos, scale, px, py);
			outScr[i * 2]     = m_fAMGA_OffX + px * m_fAMGA_Scale;
			outScr[i * 2 + 1] = m_fAMGA_OffY + py * m_fAMGA_Scale;
		}
	}

	protected void AMGA_AddStroke(int color, notnull SM_ShapeLine l, vector playerPos, float scale)
	{
		// Widths are METRES (a stroke is drawn on the world, not on the screen), so on the RT they
		// scale with the device's zoom just like the terrain does. Each line carries its own width —
		// a grid's inner lines are thinner than its border.
		float widthPx = l.m_fWidthMeters * scale;
		if (widthPx < AMGA_MIN_STROKE_PX)
			widthPx = AMGA_MIN_STROKE_PX;
		widthPx *= m_fAMGA_Scale;	// the vertices are in canvas units, so the width has to be too

		LineDrawCommand line = new LineDrawCommand();
		line.m_iColor = color;
		line.m_fWidth = widthPx;
		array<float> v = {};
		AMGA_ProjectPts(l.m_aPts, playerPos, scale, v);
		line.m_Vertices = v;
		m_aAMGA_Cmds.Insert(line);
	}

	//! Filled area: the outline's triangulation is cached API-side; the projection is per tick. An
	//! outline the triangulator couldn't handle arrives with null indices and is skipped rather than
	//! shoved into PolygonDrawCommand — that chokes on concave shapes and spams the log every frame.
	protected void AMGA_AddFill(notnull AM_DrawingRenderData rd, vector playerPos, float scale)
	{
		if (!rd.m_aFillIndices || !rd.m_aFillPts || rd.m_aFillPts.Count() < 6)
			return;

		TriMeshDrawCommand mesh = new TriMeshDrawCommand();
		mesh.m_iColor = rd.m_iColor;
		array<float> v = {};
		AMGA_ProjectPts(rd.m_aFillPts, playerPos, scale, v);
		mesh.m_Vertices = v;
		mesh.m_Indices = rd.m_aFillIndices;	// shared cache; the renderer does not mutate it
		m_aAMGA_Cmds.Insert(mesh);
	}
}
