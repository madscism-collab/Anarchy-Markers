// Anarchy markers and drawings on the corner minimap (DEV's GPS OSD).
//
// The minimap is neither a map entity nor the device render target — it is a third surface, an
// SCR_InfoDisplayExtended that paints its own layers around the player at its own zoom and heading.
// So, as with the device screen, our layer cannot attach and we draw ourselves, using their
// projection so our stuff sits exactly where theirs does:
//     tdx =  dx * cosH - dz * sinH
//     tdz = -dx * sinH - dz * cosH
//     px  = m_fHalfPx + tdx * scale      (cosH/sinH are identity in north-up mode)
//
// GRS's own minimap has a MARKER layer and NO drawing layer at all — their drawings never reach this
// screen. Ours do.
//
// We hook UpdateMarkerLayer rather than DisplayUpdate: it is called once per tick with the tick's
// player position and scale, after the heading has been resolved, and only when the minimap is
// actually shown. Hooking the tick itself would mean re-deriving all of that.

modded class GRS_MinimapDisplay
{
	// Size of a DEFAULT marker. Anything the author scaled up scales up here too, without a ceiling:
	// a marker's size is a property of the marker, not of the screen it lands on, and a giant one is
	// meant to be seen. Their own dots are a flat 22 px; ours carry a symbol, so the base sits above.
	protected static const float AMGA_MM_MARKER_PX     = 26;
	protected static const float AMGA_MM_MIN_STROKE_PX = 1.0;

	protected CanvasWidget m_wAMGA_Canvas;		// our strokes + fills
	protected bool         m_bAMGA_Subscribed;
	// Marker-layer pixels -> canvas units. Measured every tick from the two widgets themselves.
	protected float        m_fAMGA_Scale = 1;
	protected float        m_fAMGA_OffX;
	protected float        m_fAMGA_OffY;

	protected ref map<int, ref SM_MarkerVisual> m_mAMGA_Vis = new map<int, ref SM_MarkerVisual>();
	protected ref array<ref CanvasWidgetCommand> m_aAMGA_Cmds = {};
	protected ref map<int, ref AMGA_DrawCache> m_mAMGA_Draw = new map<int, ref AMGA_DrawCache>();

	// --- lifecycle -----------------------------------------------------------------------------

	//! Markers go on their marker layer, drawings on a canvas under it. Parenting the canvas to the
	//! ROAD layer is what puts our strokes beneath every icon on the screen — a canvas hung off the
	//! root would be the last sibling and paint over markers, units and the player chevron alike.
	override protected void DisplayStartDraw(IEntity owner)
	{
		super.DisplayStartDraw(owner);

		if (m_wAMGA_Canvas || !m_wMarkerLayer || !m_wRoadLayer)
			return;

		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;

		m_wAMGA_Canvas = CanvasWidget.Cast(ws.CreateWidget(WidgetType.CanvasWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(0x00000000), 0, m_wRoadLayer));
		if (!m_wAMGA_Canvas)
			return;

		FrameSlot.SetAnchorMin(m_wAMGA_Canvas, 0, 0);
		FrameSlot.SetAnchorMax(m_wAMGA_Canvas, 1, 1);

		AMGA_Subscribe();
	}

	override protected void DisplayStopDraw(IEntity owner)
	{
		AMGA_Teardown();
		super.DisplayStopDraw(owner);
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
		m_mAMGA_Draw.Clear();
		m_aAMGA_Cmds.Clear();

		if (m_wAMGA_Canvas)
			m_wAMGA_Canvas.RemoveFromHierarchy();
		m_wAMGA_Canvas = null;
	}

	// --- marker bookkeeping --------------------------------------------------------------------
	// Widgets are built on store events, never per tick. The tick only projects, culls and shows.

	protected void AMGA_Subscribe()
	{
		if (m_bAMGA_Subscribed)
			return;
		AM_MarkerAPI.GetOnMarkerAdded().Insert(AMGA_OnMarkerAdded);
		AM_MarkerAPI.GetOnMarkerChanged().Insert(AMGA_OnMarkerChanged);
		AM_MarkerAPI.GetOnMarkerRemoved().Insert(AMGA_OnMarkerRemoved);
		AM_MarkerAPI.GetOnDrawingRemoved().Insert(AMGA_OnDrawingRemoved);
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
		AM_MarkerAPI.GetOnDrawingRemoved().Remove(AMGA_OnDrawingRemoved);
		m_bAMGA_Subscribed = false;
	}

	protected void AMGA_OnMarkerAdded(SM_MapMarkerData data)
	{
		if (!data || !m_wMarkerLayer)
			return;

		AMGA_OnMarkerRemoved(data.m_iId);	// re-add after a change: drop the old widgets first

		SM_MarkerVisual vis = AM_MarkerWidgets.Create(data, m_wMarkerLayer);
		if (!vis)
			return;

		// No labels here at any size: a name tag next to a 26 px icon on a 240 px screen is noise.
		AM_MarkerWidgets.SetLabelsVisible(vis, false);
		AMGA_SetVisualVisible(vis, false);	// hidden until the tick decides it is in view
		m_mAMGA_Vis.Set(data.m_iId, vis);
	}

	protected void AMGA_OnMarkerChanged(SM_MapMarkerData data)
	{
		AMGA_OnMarkerAdded(data);	// a kind flip swaps the whole widget — rebuild rather than patch
	}

	protected void AMGA_OnMarkerRemoved(int markerId)
	{
		SM_MarkerVisual vis = m_mAMGA_Vis.Get(markerId);
		if (vis)
			vis.Destroy();
		m_mAMGA_Vis.Remove(markerId);
	}

	protected void AMGA_OnDrawingRemoved(int drawingId)
	{
		m_mAMGA_Draw.Remove(drawingId);	// bounds + triangulation die with the drawing
	}

	protected void AMGA_SetVisualVisible(notnull SM_MarkerVisual vis, bool visible)
	{
		Widget main = vis.GetMainWidget();
		if (main)
			main.SetVisible(visible);
	}

	// --- rendering -----------------------------------------------------------------------------

	//! Called once per tick from DisplayUpdate, with the heading already resolved into m_fCosH /
	//! m_fSinH — and only while the minimap is shown, so a hidden OSD costs us nothing.
	override protected void UpdateMarkerLayer(vector playerPos, float scale)
	{
		super.UpdateMarkerLayer(playerPos, scale);

		AMGA_RenderMarkers(playerPos, scale);
		AMGA_RenderDrawings(playerPos, scale);
	}

	protected void AMGA_Project(float wx, float wz, vector playerPos, float scale, out float px, out float py)
	{
		float dx = wx - playerPos[0];
		float dz = wz - playerPos[2];
		px = m_fHalfPx + ( dx * m_fCosH - dz * m_fSinH) * scale;
		py = m_fHalfPx + (-dx * m_fSinH - dz * m_fCosH) * scale;
	}

	//! The minimap's own MARKERS switch governs ours too. One screen, one set of layers, as far as
	//! the player is concerned.
	protected void AMGA_RenderMarkers(vector playerPos, float scale)
	{
		if (!m_wMarkerLayer)
			return;

		if (!GRS_SettingsService.GetMinimapShowMarkers())
		{
			foreach (int hid, SM_MarkerVisual hv : m_mAMGA_Vis)
			{
				if (hv)
					AMGA_SetVisualVisible(hv, false);
			}
			return;
		}

		// The viewport is a SQUARE, so cull against the square — in minimap metres, after the heading
		// rotation. A round cull at m_fZoomMeters (what their own dots use) would blank the corners,
		// where there is screen left to draw on. The margin keeps an icon that straddles the edge:
		// the viewport clips it, which is what we want, rather than popping it out of existence.
		float half = m_fZoomMeters * 1.1;

		foreach (int id, SM_MarkerVisual vis : m_mAMGA_Vis)
		{
			if (!vis || !vis.m_Data)
				continue;

			float dx = vis.m_Data.m_iPosX - playerPos[0];
			float dz = vis.m_Data.m_iPosY - playerPos[2];
			float tdx =  dx * m_fCosH - dz * m_fSinH;
			float tdz = -dx * m_fSinH - dz * m_fCosH;

			if (tdx < -half || tdx > half || tdz < -half || tdz > half)
			{
				AMGA_SetVisualVisible(vis, false);
				continue;
			}

			float sizePx = AMGA_MM_MARKER_PX * AM_MarkerWidgets.SizeFactor(vis.m_Data.m_iSize);

			AMGA_SetVisualVisible(vis, true);
			AM_MarkerWidgets.Place(vis, m_fHalfPx + tdx * scale, m_fHalfPx + tdz * scale, sizePx);
		}
	}

	//! Drawings have no minimap switch of their own — GRS never put them on this screen — so they
	//! follow the DRAWINGS layer toggle from the Comms app, the same one that governs them on the
	//! tablet map and the device screen.
	protected void AMGA_RenderDrawings(vector playerPos, float scale)
	{
		if (!m_wAMGA_Canvas)
			return;

		// Bridge the canvas to the MARKER LAYER, not to a guess.
		//
		// A CanvasWidget draws in its own unit space, and both ways of pinning that space down assume
		// something about which rect the canvas actually occupies — assume wrong and the whole picture
		// rotates about a point that is not the player, which is exactly what "the drawings orbit us"
		// looks like. So assume nothing: measure where the canvas sits on screen, measure where the
		// marker layer sits, and convert between them. Markers land correctly by definition, so a
		// stroke expressed in their space cannot land anywhere else.
		float edge = m_fHalfPx * 2;
		float mlw, mlh, mlx, mly, cw, ch, cx, cy;
		m_wMarkerLayer.GetScreenSize(mlw, mlh);
		m_wMarkerLayer.GetScreenPos(mlx, mly);
		m_wAMGA_Canvas.GetScreenSize(cw, ch);
		m_wAMGA_Canvas.GetScreenPos(cx, cy);
		if (edge <= 0 || mlw <= 0 || cw <= 0)
			return;	// not laid out yet

		// One canvas unit = one physical pixel; then a marker-layer pixel is m_fAMGA_Scale of them,
		// offset by however far the two widgets are apart on screen.
		m_wAMGA_Canvas.SetSizeInUnits(Vector(cw, ch, 0));
		m_wAMGA_Canvas.SetZoom(1.0);
		m_wAMGA_Canvas.SetOffsetPx(Vector(0, 0, 0));
		m_fAMGA_Scale = mlw / edge;
		m_fAMGA_OffX  = mlx - cx;
		m_fAMGA_OffY  = mly - cy;

		m_aAMGA_Cmds.Clear();

		if (!GRS_MapOverlayController.GetLayerVisible(GRS_EMapLayer.DRAWINGS))
		{
			m_wAMGA_Canvas.SetDrawCommands(m_aAMGA_Cmds);	// empty list = nothing drawn
			return;
		}

		array<SM_MapDrawingData> all = {};
		AM_MarkerAPI.GetAllDrawings(all);

		// Heading-up spins the visible square, so a world-axis box big enough to hold it in any
		// rotation is the cheap conservative reject: half-diagonal, the same 1.42 their own road and
		// contour layers cull with. Nothing outside it can touch the screen at any heading.
		float cull = m_fZoomMeters * 1.42;
		float minX = playerPos[0] - cull;
		float maxX = playerPos[0] + cull;
		float minZ = playerPos[2] - cull;
		float maxZ = playerPos[2] + cull;

		foreach (SM_MapDrawingData d : all)
		{
			if (!d || d.GetPointCount() < 2)
				continue;

			AMGA_DrawCache cache = AMGA_GetDrawCache(d);
			if (!cache)
				continue;

			if (cache.m_iHiX < minX || cache.m_iLoX > maxX || cache.m_iHiZ < minZ || cache.m_iLoZ > maxZ)
				continue;	// nowhere near the screen — never even projected

			array<float> scr = {};
			int n = d.GetPointCount();
			scr.Resize(n * 2);
			for (int i = 0; i < n; i++)
			{
				int wx, wz;
				d.GetPoint(i, wx, wz);
				float px, py;
				AMGA_Project(wx, wz, playerPos, scale, px, py);
				scr[i * 2]     = m_fAMGA_OffX + px * m_fAMGA_Scale;
				scr[i * 2 + 1] = m_fAMGA_OffY + py * m_fAMGA_Scale;
			}

			if (d.m_iFill != 0)
				AMGA_AddFill(d, cache, scr);
			else
				AMGA_AddStroke(d, scr, scale);
		}

		m_wAMGA_Canvas.SetDrawCommands(m_aAMGA_Cmds);
	}

	//! Bounds (and, for a fill, the triangulation) computed on first sight and kept until the drawing
	//! goes away. Our drawings never change in place — an edit arrives as a remove plus an add with a
	//! fresh id — so an entry can never go stale.
	protected AMGA_DrawCache AMGA_GetDrawCache(notnull SM_MapDrawingData d)
	{
		AMGA_DrawCache cache = m_mAMGA_Draw.Get(d.m_iId);
		if (cache)
			return cache;

		int n = d.GetPointCount();
		if (n < 2)
			return null;

		cache = new AMGA_DrawCache();
		d.GetPoint(0, cache.m_iLoX, cache.m_iLoZ);
		cache.m_iHiX = cache.m_iLoX;
		cache.m_iHiZ = cache.m_iLoZ;
		for (int i = 1; i < n; i++)
		{
			int x, z;
			d.GetPoint(i, x, z);
			if (x < cache.m_iLoX) cache.m_iLoX = x;
			if (x > cache.m_iHiX) cache.m_iHiX = x;
			if (z < cache.m_iLoZ) cache.m_iLoZ = z;
			if (z > cache.m_iHiZ) cache.m_iHiZ = z;
		}

		m_mAMGA_Draw.Set(d.m_iId, cache);
		return cache;
	}

	protected void AMGA_AddStroke(notnull SM_MapDrawingData d, array<float> scr, float scale)
	{
		// Brush widths are METRES — a stroke is drawn on the world, not on the screen — so on the
		// minimap they scale with its zoom, like the terrain under them.
		float widthPx = SM_DrawCanvas.WidthMeters(d.m_iWidthIdx) * scale;
		if (widthPx < AMGA_MM_MIN_STROKE_PX)
			widthPx = AMGA_MM_MIN_STROKE_PX;
		widthPx *= m_fAMGA_Scale;	// the vertices are in canvas units, so the width has to be too

		LineDrawCommand line = new LineDrawCommand();
		line.m_iColor = d.m_iColor;
		line.m_fWidth = widthPx;
		array<float> v = {};
		v.Copy(scr);
		line.m_Vertices = v;
		m_aAMGA_Cmds.Insert(line);
	}

	//! An outline the triangulator cannot handle (self-intersecting, degenerate) is skipped rather
	//! than shoved into PolygonDrawCommand — that chokes on concave shapes and spams the log.
	protected void AMGA_AddFill(notnull SM_MapDrawingData d, notnull AMGA_DrawCache cache, array<float> scr)
	{
		if (scr.Count() < 6)
			return;

		if (!cache.m_bTriTried)
		{
			cache.m_bTriTried = true;
			array<int> tri = {};
			if (SM_MapFloodFill.Triangulate(d.m_aPoints, tri) && tri.Count() >= 3)
				cache.m_aTri = tri;
		}
		if (!cache.m_aTri)
			return;

		TriMeshDrawCommand mesh = new TriMeshDrawCommand();
		mesh.m_iColor = d.m_iColor;
		array<float> v = {};
		v.Copy(scr);
		mesh.m_Vertices = v;
		mesh.m_Indices = cache.m_aTri;	// shared cache; the renderer does not mutate it
		m_aAMGA_Cmds.Insert(mesh);
	}
}
