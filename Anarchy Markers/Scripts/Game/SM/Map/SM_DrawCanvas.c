// Полотно малювання Anarchy Markers. Один CanvasWidget поверх map-frame, по одному
// LineDrawCommand на штрих + окрема preview-команда для штриха «в процесі».
//
// Оптимізація (наша модель): екранні вершини КЕШУЮТЬСЯ. Дорогий WorldToScreen на всі
// точки робимо лише коли змінився ЗУМ або набір штрихів. Панорамування — найчастіша дія —
// це чистий зсув виду в екранному просторі, тож кешовані вершини просто ЗСУВАЄМО на дельту
// (ділимо позицію опорної точки світу 0,0 між кадрами). Жодних проєкцій на пан.
// Товщина штриха — у ПІКСЕЛЯХ (постійна на екрані, не масштабується зумом); форма/позиція
// привʼязані до світу.

// Один намальований штрих у кеші рендеру: набір draw-команд — полілінію ріжемо на гострих
// кутах (щоб не було miter-шипів), стики/кінці/крапку закриваємо залитими колами.
class SM_RenderStroke
{
	int m_iId;
	ref array<ref CanvasWidgetCommand> m_aCmds = {};

	// Kept so ProjectAll never calls the store's FindById, which scans linearly — with 500 strokes
	// that made every reprojection quadratic. Refreshed by SyncStrokes, which walks the store anyway.
	ref SM_MapDrawingData m_Data;

	// Світовий AABB (метри) — для куллінгу поза екраном. Не змінюється на пан/зум,
	// оновлюється разом із командами у ProjectAll.
	int  m_iMinX, m_iMaxX, m_iMinZ, m_iMaxZ;
	bool m_bCull;	// AABB валідний → можна куллити; false → завжди малюємо

	// Заливка: контур рендериться TriMesh-ом; тріангуляція рахується РАЗ у світових
	// координатах (індекси лишаються валідними після афінної проєкції на екран).
	bool m_bFill;
	ref array<int> m_aTriIndices;

	void SM_RenderStroke(int id)
	{
		m_iId = id;
	}
}

// One own server stroke being cut by an eraser drag. The cutting runs on the client against the
// ORIGINAL geometry (progressive, as temps), and a single authoritative erase-part is sent for the
// original real id when the drag ends — the id is still valid on the server because nothing went out
// mid-drag. Sending per hit instead would need the server's piece ids, which don't come back in time.
class SM_EraseWork
{
	int m_iOrigId;		// the original server stroke id (>= 1)
	int m_iColor;
	int m_iWidthIdx;
	int m_iVis;
	int m_iChannel;
	int m_iOwnerId;
	ref array<ref array<int>> m_aPieces = {};	// leftover pieces so far (each: x,z,... world ints)
	ref array<int> m_aTemps = {};				// store temps currently showing those pieces

	// Original stroke's world AABB — pieces only ever shrink inside it, so a stamp outside it can be
	// rejected without touching a single piece. m_bHasAABB false = never reject.
	bool m_bHasAABB;
	int m_iMinX, m_iMaxX, m_iMinZ, m_iMaxZ;
}

class SM_DrawCanvas
{
	protected CanvasWidget   m_wCanvas;			// закомічені штрихи (тесселяція раз на зум, пан = SetOffsetPx)
	protected CanvasWidget   m_wPreviewCanvas;	// лише штрих «у процесі» (перебудова щокадру, дешево)
	protected SCR_MapEntity  m_Map;
	protected Widget         m_wMapFrame;

	// Пензель (модель «палітра» — кожен штрих бере поточні налаштування).
	// Колір/ширина/канал — СТАТИЧНІ: полотно перестворюється на кожне відкриття мапи,
	// а вибір гравця має пережити закриття мапи (і читатись діалогом міток як дефолт).
	protected bool m_bActive;
	protected int  m_iTool = 0;	// 0 = олівець, 1 = гумка, 2 = заливка (скидається щовідкриття — ок)
	protected static int s_iColor      = 0xFFFFD040;	// ARGB
	protected static int s_iWidthIdx   = 1;
	protected static int s_iVisibility = SM_EMarkerVisibility.FACTION;
	protected static int s_iOpacityPct = 100;	// 10..100% — вшивається в альфа-канал кольору при комміті

	// Доступ для інших систем (діалог міток бере як дефолти нової мітки).
	static int GetBrushColor()      { return s_iColor; }
	static int GetBrushVisibility() { return s_iVisibility; }

	void SetOpacityPct(int pct)
	{
		if (pct < 10) pct = 10;		// нижня межа: повністю невидиме малювання = плутанина
		if (pct > 100) pct = 100;
		s_iOpacityPct = pct;
	}
	int GetOpacityPct() { return s_iOpacityPct; }

	// Колір пензля з урахуванням прозорості (альфа = 255 × pct/100). Прозорість живе В КОЛЬОРІ
	// малюнка — тому мережа, JSON-збереження і рендер працюють без жодних нових полів.
	protected int ApplyOpacity(int argb)
	{
		int a = Math.Round(255.0 * s_iOpacityPct / 100.0);
		if (a < 0) a = 0;
		if (a > 255) a = 255;
		return (argb & 0x00FFFFFF) | (a << 24);
	}

	// Кеш рендеру
	protected ref map<int, ref SM_RenderStroke>  m_mStrokes  = new map<int, ref SM_RenderStroke>();
	protected ref array<int>                     m_aOrder    = {};	// id у порядку малювання (зрост. = нові поверх старих)
	protected ref array<ref CanvasWidgetCommand> m_aPreviewCmds = {};	// команди штриха «в процесі»

	// Templates. The selection rectangle (tool 4) and the placement ghost (tool 3) both ride the
	// preview canvas: they are live, rebuilt every frame, and must never touch the committed strokes.
	protected bool m_bSelActive;			// dragging the rectangle right now
	protected bool m_bSelValid;				// a rectangle is on the map, waiting to be saved
	protected int  m_iSelX0, m_iSelZ0, m_iSelX1, m_iSelZ1;	// world metres, unordered
	protected bool m_bTplHeld;				// holding the button on a placed template
	protected ref array<ref CanvasWidgetCommand> m_aGhostTmp = {};	// scratch: BuildStrokeCommands clears what it is given
	protected const int GHOST_ALPHA_STROKE = 200;
	protected const int GHOST_ALPHA_FILL   = 90;
	protected ref array<ref CanvasWidgetCommand> m_aCommands = {};
	protected bool m_bMembershipDirty = true;	// штрих додано/прибрано

	// Різати полілінію, коли cos кута між сусідніми сегментами < цього (turn > ~104°) — інакше
	// LineDrawCommand дає miter-«шип». Стики/кінці закриваємо колом (SM_CIRCLE_SEG-кутник).
	protected const float SM_SHARP_DOT  = -0.25;
	protected const int   SM_CIRCLE_SEG = 12;
	protected const float SM_DECIM_PX     = 1.5;	// LOD: точки ближче цього на екрані — зливаємо
	protected const float SM_ROUND_MIN_PX = 2.5;	// LOD: тонше цього на екрані — без кіл-стиків

	// Відстеження виду (для зсув-на-пан проти повної проєкції)
	protected bool  m_bViewValid;
	protected float m_fLastZoom = -1;
	// Anchor: a world point that sat on screen at the last projection (canvas top-left). Pan delta
	// is the change of its screen position.
	// The last argument of WorldToScreen is dpiScale, NOT a clamp. Always pass true — the canvas,
	// GetScreenPos and ScreenToWorld all speak physical pixels. Passing false hands back layout
	// units, the pan delta comes out at the wrong scale and the drawings ride along with the camera.
	protected float m_fRefWX, m_fRefWZ;
	protected int   m_iAnchorSx, m_iAnchorSy;	// where the anchor is now (moves with every pan)
	protected int   m_iProjSx, m_iProjSy;		// where it was at the last ProjectAll
	// Panning far away from the box we clipped against leaves the cut-off geometry incomplete, so
	// reproject once the pan exceeds half the clip margin. Only needed when the box actually cut
	// something: with every stroke fully inside it (the usual case when the whole drawing fits the
	// screen) the cached vertices stay valid forever and panning is a pure shift.
	protected bool m_bProjClipped;

	// Кешовані коефіцієнти афінної проєкції світ→екран (масштаб+зсув, без обертання): рахуються раз
	// на ProjectAll із 3 референсних WorldToScreen, далі всі точки проєктуємо арифметикою (без нативних викликів).
	protected float m_fPox, m_fPoy;	// екранна проєкція світової (0,0)
	protected float m_fDxx, m_fDyx;	// приріст екрана (x,y) на 1 м світового X
	protected float m_fDxz, m_fDyz;	// приріст екрана (x,y) на 1 м світового Z
	protected float m_fScrW, m_fScrH;	// розмір полотна на момент проєкції (для кліпінгу)
	// Slack around the screen. Geometry is CLIPPED against this box, never clamped into it: clamping
	// keeps the vertex count (handy for the cached triangulation) but turns a long stroke into a
	// straight line between two corners of the box. Zoomed in, nearly every vertex falls outside,
	// which is where the giant diagonals across the map came from. Huge screen coordinates blow up
	// the engine's tessellator, so we cannot simply pass them through either.
	// Proportional to the canvas, not flat: on a 400 px tablet a flat 2048 px margin would make the
	// box ten screens wide and the early AABB rejection would never fire. Set in ComputeAffine.
	protected float m_fClipMarginPx = 2048;

	// Render budget of the current screen (AM_MapRenderPolicy); 0 = render everything.
	protected float m_fRenderRadius;

	// TRAP: SCR_MapEntity's two projection calls speak DIFFERENT spaces and are not inverses:
	//     WorldToScreen  RETURNS pixels local to the MAP WIDGET
	//     ScreenToWorld  TAKES   pixels local to the MAP FRAME
	// On the fullscreen map widget, frame and screen all sit at the origin, so this never showed. A
	// tablet hosts the map in a small inset widget and the difference becomes a constant pixel offset
	// — nearly right zoomed in, kilometres off zoomed out (constant pixels = growing metres).
	//
	// Everything we emit must be CANVAS-local (draw commands, hit tests). The three bridges:
	//   ScreenToWorld input:  canvas corner in frame space   = m_fCanvasInFrame
	//   WorldToScreen output: + m_fMapToCanvas               -> canvas-local
	//   cursor (screen px):   - m_fCanvasAbs                 -> canvas-local
	// All refreshed in ComputeAffine; all 0 on the fullscreen map.
	protected float m_fCanvasInFrameX, m_fCanvasInFrameY;	// canvas origin, in map-frame space
	protected float m_fMapToCanvasX, m_fMapToCanvasY;		// map-widget space -> canvas space
	protected float m_fCanvasAbsX, m_fCanvasAbsY;			// canvas origin, in screen space

	void SetRenderRadius(float radiusMeters)
	{
		m_fRenderRadius = radiusMeters;
		m_bViewValid = false;	// next Tick reprojects with the radius applied
	}

	// Захоплення штриха (світові метри, парами)
	protected ref array<int> m_aCapture = {};
	protected bool m_bCapturing;
	protected int  m_iLastSampleX, m_iLastSampleZ;
	protected int  m_iMaxCapturePts;	// стеля точок штриха (= конфіг); далі не малюємо
	protected const float SAMPLE_MIN_DIST = 3.0;	// мін. світова відстань між семплами (м)
	protected const float ERASE_SLACK_PX  = 2.0;	// невеликий доп. поріг гумки (фіз. px)

	// Photoshop-style eraser: drag it and everything the circle touches gets erased. This set holds
	// the strokes already handled once this drag (Local strokes, others' whole removes, add-temps) so
	// a single pass isn't repeated. Own REAL server strokes are tracked as works instead (below).
	protected ref set<int> m_ErasedThisDrag = new set<int>();
	protected bool m_bErasing;
	protected int m_iLastStampX, m_iLastStampZ;	// where the last eraser stamp landed (world m)
	// Own server strokes cut by the CURRENT drag: cut client-side against the original geometry,
	// one erase-part sent per stroke at drag end (see SM_EraseWork). m_EraseWorkTemps holds the ids
	// of the temps these works show, so the eraser loop skips its own optimistic pieces.
	protected ref array<ref SM_EraseWork> m_aEraseWork = {};
	protected ref set<int> m_EraseWorkTemps = new set<int>();
	// Shift + pencil draws a polyline. While Shift is held every click continues from the end of the
	// previous segment, and the whole chain commits as ONE stroke once Shift goes up (or the point
	// cap is hit, or the tool changes).
	protected bool m_bLineStroke;		// dragging a segment right now (LMB down, straight-line mode)
	protected bool m_bLineChain;		// chain is open: vertices placed, waiting for the next click
	protected int  m_iLineFixedInts;	// committed vertices in m_aCapture; anything past them is the
										// rubber band to the cursor and never reaches the stroke

	// Курсор-кружечок пензля/гумки: фізичний розмір штриха на мапі за поточного зуму.
	protected ImageWidget m_wCursor;
	protected int m_iCursorTool = -1;	// яка текстура зараз завантажена (-1 = жодна)
	protected const ResourceName CURSOR_TEX_PENCIL = "{E278DB118260244A}circleWhite.edds";
	protected const ResourceName CURSOR_TEX_ERASER = "{B72B0A3DAD46568C}circleLine.edds";

	// Пресети ширини у СВІТОВИХ метрах (паперова поведінка: товщина прив'язана до землі,
	// тож широка лінія завжди закриває велику ділянку; на екрані товщає при наближенні).
	// idx 0..4 are shared (pencil/eraser/fill); idx 5 (100 m) is ERASER-ONLY (bulk erasing).
	static const int WIDTH_IDX_MAX_PENCIL = 4;	// regular tools: up to idx 4 (40 m)
	static const int WIDTH_IDX_MAX_ERASER = 5;	// eraser: extra idx 5 (100 m)
	static float WidthMeters(int idx)
	{
		switch (idx)
		{
			case 0: return 2;
			case 1: return 5;
			case 2: return 10;
			case 3: return 20;
			case 4: return 40;
			case 5: return 100;	// eraser-only
		}
		return 5;
	}
	static int ClampWidthIdx(int i)
	{
		if (i < 0) return 0;
		if (i > WIDTH_IDX_MAX_ERASER) return WIDTH_IDX_MAX_ERASER;
		return i;
	}

	// Скільки екранних px на 1 світовий метр за поточного зуму (через WorldToScreen опорних точок).
	protected float PxPerMeter()
	{
		if (!m_Map)
			return 0.05;
		int ax, ay, bx, by;
		m_Map.WorldToScreen(0, 0, ax, ay, true);
		m_Map.WorldToScreen(100, 0, bx, by, true);
		float dx = bx - ax;
		float dy = by - ay;
		float d = Math.Sqrt(dx * dx + dy * dy);
		if (d < 0.0001)
			return 0.01;
		return d / 100.0;
	}

	// Екранна товщина штриха за поточного зуму (мін. 1px, щоб не зникала на сильному віддаленні).
	protected float WidthPxForZoom(int idx, float pxPerMeter)
	{
		float w = WidthMeters(idx) * pxPerMeter;
		if (w < 1)
			w = 1;
		return w;
	}

	//------------------------------------------------------------------------------
	protected bool m_bEditorMap;		// полотно живе в GM-мапі (зевс)
	protected bool m_bRenderEnabled = true;	// false (GM, показ вимкнено) → штрихи не малюємо

	void SetRenderEnabled(bool e)
	{
		if (e == m_bRenderEnabled)
			return;
		m_bRenderEnabled = e;
		m_bMembershipDirty = true;	// перебудувати команди (порожньо/повно) наступним Tick
	}

	void Init(notnull CanvasWidget canvas, notnull SCR_MapEntity mapEnt, Widget mapFrame, bool editorMap = false)
	{
		m_wCanvas   = canvas;
		m_Map       = mapEnt;
		m_wMapFrame = mapFrame;
		m_bEditorMap = editorMap;

		SM_MapDrawingStore s = SM_MapDrawingStore.GetInstance();
		s.GetOnAdded().Insert(OnStoreAdded);
		s.GetOnRemoved().Insert(OnStoreRemoved);

		// Окреме полотно для прев'ю штриха «в процесі» (z=1: над закоміченими, під панеллю z=100).
		// Так під час малювання ми не чіпаємо головне полотно (де можуть бути 1000 штрихів).
		WorkspaceWidget ws = GetGame().GetWorkspace();
		m_wPreviewCanvas = CanvasWidget.Cast(ws.CreateWidget(
			WidgetType.CanvasWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(0x00000000), 1, m_wMapFrame));
		if (m_wPreviewCanvas)
		{
			FrameSlot.SetAnchorMin(m_wPreviewCanvas, 0, 0);
			FrameSlot.SetAnchorMax(m_wPreviewCanvas, 1, 1);
		}

		// Курсор інструмента (кружечок фізичного розміру пензля). BLEND — альфа текстури.
		m_wCursor = ImageWidget.Cast(ws.CreateWidget(
			WidgetType.ImageWidgetTypeID,
			WidgetFlags.BLEND | WidgetFlags.STRETCH | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(0xFFFFFFFF), 150, m_wMapFrame));	// НАД панеллю (100): видно, куди цілишся й на кнопках
		if (m_wCursor)
		{
			FrameSlot.SetAnchorMin(m_wCursor, 0, 0);
			FrameSlot.SetAnchorMax(m_wCursor, 0, 0);
			FrameSlot.SetAlignment(m_wCursor, 0.5, 0.5);	// пивот центр — SetPos центрує на курсорі
			m_wCursor.SetVisible(false);
		}
		m_iCursorTool = -1;

		m_bMembershipDirty = true;
		m_bViewValid       = false;
	}

	void Destroy()
	{
		SM_MapDrawingStore s = SM_MapDrawingStore.GetInstance();
		s.GetOnAdded().Remove(OnStoreAdded);
		s.GetOnRemoved().Remove(OnStoreRemoved);

		m_mStrokes.Clear();
		m_aPreviewCmds.Clear();
		m_aCommands.Clear();
		if (m_wCursor)
			m_wCursor.RemoveFromHierarchy();
		m_wCursor = null;
		if (m_wPreviewCanvas)
			m_wPreviewCanvas.RemoveFromHierarchy();
		m_wPreviewCanvas = null;
		if (m_wCanvas)
			m_wCanvas.RemoveFromHierarchy();
		m_wCanvas = null;
	}

	// --- Стан пензля ---
	bool IsActive()           { return m_bActive; }
	void SetActive(bool a)
	{
		m_bActive = a;
		if (!a)
		{
			FinishLineChain();	// keep whatever vertices the player already placed
			CancelStroke();
		}
	}
	void SetTool(int t)
	{
		if (t != m_iTool)
			FinishLineChain();
		if (t != TOOL_TEMPLATE)
			CancelShapePlacement();	// leaving the tool drops a half-placed shape, like everything else
		else
			m_fShapeArmedAt = System.GetTickCount() / 1000.0;	// arming backstop — shapes AND session templates
		m_iTool = t;
		// The 100 m preset (idx 5) is eraser-only — clamp back to 40 m when switching to pencil/fill.
		if (t != 1 && s_iWidthIdx > WIDTH_IDX_MAX_PENCIL)
			s_iWidthIdx = WIDTH_IDX_MAX_PENCIL;
	}
	int  GetTool()            { return m_iTool; }

	static const int TOOL_TEMPLATE = 3;
	static const int TOOL_SELECT   = 4;

	// ---------------------------------------------------------------- templates

	//! A template's strokes leave through the same door a hand-drawn one does. No new RPC, no
	//! server-side knowledge of templates, and every limit applies for free.
	//!
	//! GM extras go on HERE, not in the session: the session builds the stroke from the template's own
	//! geometry and has no idea it is being placed on the editor map. A committed stroke and a built-in
	//! shape both get them (CommitStroke / ShapePressDown) — a user template placed by the GM must too,
	//! or his saved drawings come out unlocked and on the wrong side while everything else is locked.
	void SM_TemplateEmit(notnull SM_MapDrawingData d, notnull array<int> points)
	{
		if (m_bEditorMap)
		{
			d.m_iChannel = SM_GmState.s_iDrawSideChannel;
			if (SM_GmState.s_bDrawGmLock)
				d.m_iGmLocked = 1;
			if (SM_GmState.s_bDrawHideInfo)
				d.m_iHideInfo = 1;
		}
		SM_DrawAddOrLocal(d, points);
	}

	//! Click with the template tool: the first click drops the anchor, and from then on the button is
	//! held to draw. Clicking again with nothing placed re-anchors; clicking while one is placed just
	//! starts drawing it (that is what "hold on your template" means).
	protected void TemplatePressDown(int wx, int wz)
	{
		// A built-in shape shares the tool slot but not the flow: two clicks, no session.
		if (m_iShapeMode != SM_ShapeGeometry.SHAPE_NONE)
		{
			ShapePressDown(wx, wz);
			return;
		}

		// Same backstop ShapePressDown has, for the same reason: on the pad the A that pressed Place
		// leaks back in as a fresh edge one frame later (the eat reads a stale action value), and here
		// it ANCHORED the ghost under the panel before the player ever aimed — a session ghost stops
		// following the cursor the moment it is placed, so it looked simply frozen.
		if (System.GetTickCount() / 1000.0 - m_fShapeArmedAt < 0.2)
			return;

		SM_TemplateSession sess = SM_TemplateSession.GetInstance();

		// Confirmed: the button is what draws it, and holding it is what keeps it going.
		if (sess.IsConfirmed())
		{
			sess.ClearDenied();	// pressing again IS the decision to carry on after a refusal
			m_bTplHeld = true;
			return;
		}

		// Anchored: the map is deaf, the panel owns this step. A click that re-anchored here would land
		// on the same press that hit Apply — the button confirmed, the map un-confirmed, and from the
		// outside the buttons did nothing. Moving the ghost again is what Cancel is for.
		if (sess.IsAnchored())
			return;

		SM_DrawTemplate t = SM_TemplateStore.GetInstance().Selected();
		if (!t)
			return;	// nothing in hand

		// A click drops the anchor. Nothing is drawn until Apply — a template is forty minutes of
		// drawing, and a stray click must not start it.
		sess.Place(t, wx, wz, s_iVisibility);
	}

	//! Frame-by-frame while the template tool is armed. Emits at most one stroke, paced under the
	//! server's per-minute window.
	//! A template waiting on the map, or one in hand with the tool armed (then it follows the cursor).
	protected bool TemplateGhostWanted()
	{
		if (SM_TemplateSession.GetInstance().IsPlaced())
			return true;
		return m_bActive && m_iTool == TOOL_TEMPLATE && SM_TemplateStore.GetInstance().Selected() != null;
	}

	protected void TemplateTick()
	{
		if (!m_bTplHeld)
			return;
		if (!SM_TemplateSession.GetInstance().Tick(this))
			m_bTplHeld = false;	// finished, refused, or not confirmed
	}

	//! Is the draw button being held on a confirmed template right now? The cursor prompt uses it to
	//! stop nagging "hold to draw" the moment the player actually is.
	bool IsTemplateHeld()
	{
		return m_bTplHeld;
	}

	// ---------------------------------------------------------------- built-in shapes
	// Rectangle / circle / grid: two clicks, a live ghost in between, ONE drawing out — carrying only
	// its two parameter points. No session, no auto-draw: a shape is a single stroke.

	protected int  m_iShapeMode;	// SM_ShapeGeometry.SHAPE_* being placed; NONE = not placing
	protected bool m_bShapeFirstSet;
	protected bool m_bShapePlacedSignal;	// one shape just landed -> the panel closes the menu
	protected float m_fShapeArmedAt;		// seconds; when the template tool was armed (backstop in ShapePressDown AND TemplatePressDown)
	protected int  m_iShapeX0, m_iShapeZ0;
	protected ref array<ref SM_ShapeLine> m_aShapeLines = {};			// scratch: geometry builder output
	protected ref array<ref CanvasWidgetCommand> m_aShapeTmp = {};	// scratch: BuildStrokeCommands clears its output
	protected ref array<float> m_aShapeScr = {};						// scratch: projected points, reused per line
	// Ghost cache: the grid ghost is ~260 lines plus every label glyph, and it was rebuilt EVERY frame
	// while placing — even with the cursor sitting in one cell. Rebuild only when the shape it draws
	// actually changes (parameters snapped to the grid, or zoom, or brush).
	protected ref array<int> m_aGhostKeyPts = {};
	protected int   m_iGhostKeyColor = -1;
	protected int   m_iGhostKeyWidth = -1;
	protected float m_fGhostKeyZoom  = -1;

	//! The panel armed a built-in shape template.
	void StartShapePlacement(int shape)
	{
		m_iShapeMode = shape;
		m_bShapeFirstSet = false;
		m_fShapeArmedAt = System.GetTickCount() / 1000.0;
		m_aGhostKeyPts.Clear();	// force the ghost to build fresh for the new shape
		SetTool(TOOL_TEMPLATE);
		SetActive(true);
	}

	void CancelShapePlacement()
	{
		m_iShapeMode = SM_ShapeGeometry.SHAPE_NONE;
		m_bShapeFirstSet = false;
	}

	int GetShapeMode()   { return m_iShapeMode; }
	bool ShapeFirstSet() { return m_bShapeFirstSet; }

	//! True once, on the frame a shape was placed — the panel reads it to close the templates menu and
	//! reset to "nothing selected". One-shot: reading it clears it.
	bool ConsumeShapePlaced()
	{
		bool v = m_bShapePlacedSignal;
		m_bShapePlacedSignal = false;
		return v;
	}

	//! The brush colour exactly as a committed stroke would get it. The panel's shape preview uses
	//! this so what it shows is what lands.
	int BrushColorWithOpacity()
	{
		return ApplyOpacity(s_iColor);
	}

	protected void ShapePressDown(int wx, int wz)
	{
		// The press that ARMED this placement is not its first click. On the pad one A does both (it is
		// the panel's Place and the map action), and it armed the tool a fraction of a second ago —
		// letting it through dropped the first point wherever the cursor sat, before the player could
		// aim. The layer eats that press too; this is the backstop that does not depend on input state.
		if (System.GetTickCount() / 1000.0 - m_fShapeArmedAt < 0.2)
			return;

		if (!m_bShapeFirstSet)
		{
			if (m_iShapeMode == SM_ShapeGeometry.SHAPE_GRID)
			{
				// The clicked CELL becomes A1: anchor on its top-left corner, on the map's own lattice.
				m_iShapeX0 = Math.Floor(wx / 100.0) * SM_ShapeGeometry.GRID_CELL;
				m_iShapeZ0 = Math.Floor(wz / 100.0) * SM_ShapeGeometry.GRID_CELL + SM_ShapeGeometry.GRID_CELL;
			}
			else
			{
				m_iShapeX0 = wx;
				m_iShapeZ0 = wz;
			}
			m_bShapeFirstSet = true;
			return;
		}

		array<int> pts = {};
		if (!ShapeParams(wx, wz, pts))
			return;	// degenerate (zero-size rect, zero radius) — the click just doesn't land

		SM_MapDrawingData d = new SM_MapDrawingData();
		d.m_iColor      = ApplyOpacity(s_iColor);
		d.m_iWidthIdx   = s_iWidthIdx;
		if (d.m_iWidthIdx > WIDTH_IDX_MAX_PENCIL)
			d.m_iWidthIdx = WIDTH_IDX_MAX_PENCIL;
		d.m_iShape      = m_iShapeMode;
		d.m_iVisibility = s_iVisibility;
		d.m_iChannel    = -1;
		if (m_bEditorMap)	// same GM extras a committed stroke gets
		{
			d.m_iChannel = SM_GmState.s_iDrawSideChannel;
			if (SM_GmState.s_bDrawGmLock)
				d.m_iGmLocked = 1;
			if (SM_GmState.s_bDrawHideInfo)
				d.m_iHideInfo = 1;
		}
		SM_DrawAddOrLocal(d, pts);

		// One shape per placement: the flow ends here. The panel sees the signal, closes its menu and
		// clears the selection, so the player is back to a clean slate rather than armed for another.
		CancelShapePlacement();
		SetActive(false);
		m_bShapePlacedSignal = true;
	}

	//! The two parameter points for the shape being placed, first point + current cursor. False when
	//! the shape would be degenerate.
	protected bool ShapeParams(int wx, int wz, notnull array<int> pts)
	{
		pts.Clear();

		if (m_iShapeMode == SM_ShapeGeometry.SHAPE_RECT)
		{
			if (Math.AbsInt(wx - m_iShapeX0) < 4 && Math.AbsInt(wz - m_iShapeZ0) < 4)
				return false;
			pts.Insert(m_iShapeX0); pts.Insert(m_iShapeZ0);
			pts.Insert(wx);         pts.Insert(wz);
			return true;
		}

		if (m_iShapeMode == SM_ShapeGeometry.SHAPE_CIRCLE)
		{
			float dx = wx - m_iShapeX0;
			float dz = wz - m_iShapeZ0;
			if (dx * dx + dz * dz < 16)
				return false;
			pts.Insert(m_iShapeX0); pts.Insert(m_iShapeZ0);
			pts.Insert(wx);         pts.Insert(wz);
			return true;
		}

		if (m_iShapeMode == SM_ShapeGeometry.SHAPE_GRID)
		{
			// Any cell the cursor is IN counts, so a partially covered column/row is included whole.
			int cols = Math.Ceil((wx - m_iShapeX0) / 100.0);
			int rows = Math.Ceil((m_iShapeZ0 - wz) / 100.0);
			cols = SM_ShapeGeometry.ClampI(cols, SM_ShapeGeometry.GRID_MIN_CELLS, SM_ShapeGeometry.GRID_MAX_CELLS);
			rows = SM_ShapeGeometry.ClampI(rows, SM_ShapeGeometry.GRID_MIN_CELLS, SM_ShapeGeometry.GRID_MAX_CELLS);
			pts.Insert(m_iShapeX0);
			pts.Insert(m_iShapeZ0);
			pts.Insert(m_iShapeX0 + cols * SM_ShapeGeometry.GRID_CELL);
			pts.Insert(m_iShapeZ0 - rows * SM_ShapeGeometry.GRID_CELL);
			return true;
		}

		return false;
	}

	protected bool ShapeGhostWanted()
	{
		return m_bActive && m_iTool == TOOL_TEMPLATE && m_iShapeMode != SM_ShapeGeometry.SHAPE_NONE;
	}

	//! The live rubber-band. Before the first click the grid highlights the hovered cell (it snaps, and
	//! the player should see WHERE it will snap); rect and circle show nothing until anchored.
	protected void BuildShapeGhost()
	{
		int wx, wz;
		if (!CursorWorld(wx, wz))
		{
			m_aPreviewCmds.Clear();
			m_aGhostKeyPts.Clear();
			return;
		}

		int shape = m_iShapeMode;
		array<int> pts = {};

		if (!m_bShapeFirstSet)
		{
			if (m_iShapeMode != SM_ShapeGeometry.SHAPE_GRID)
			{
				m_aPreviewCmds.Clear();
				m_aGhostKeyPts.Clear();
				return;	// rect/circle show nothing until the first click
			}
			int cx = Math.Floor(wx / 100.0) * SM_ShapeGeometry.GRID_CELL;
			int cz = Math.Floor(wz / 100.0) * SM_ShapeGeometry.GRID_CELL + SM_ShapeGeometry.GRID_CELL;
			shape = SM_ShapeGeometry.SHAPE_RECT;	// one snapped cell, drawn as a plain box
			pts.Insert(cx);       pts.Insert(cz);
			pts.Insert(cx + 100); pts.Insert(cz - 100);
		}
		else if (!ShapeParams(wx, wz, pts))
		{
			m_aPreviewCmds.Clear();
			m_aGhostKeyPts.Clear();
			return;
		}

		int widthIdx = s_iWidthIdx;
		if (widthIdx > WIDTH_IDX_MAX_PENCIL)
			widthIdx = WIDTH_IDX_MAX_PENCIL;
		int gc = GhostColor(ApplyOpacity(s_iColor), false);
		float zoom = 0;
		if (m_Map)
			zoom = m_Map.GetCurrentZoom();

		// Cache hit: identical geometry, brush and zoom as last frame — last frame's commands stand.
		if (gc == m_iGhostKeyColor && widthIdx == m_iGhostKeyWidth && zoom == m_fGhostKeyZoom
			&& SamePts(pts, m_aGhostKeyPts) && !m_aPreviewCmds.IsEmpty())
			return;

		m_aGhostKeyPts.Copy(pts);
		m_iGhostKeyColor = gc;
		m_iGhostKeyWidth = widthIdx;
		m_fGhostKeyZoom  = zoom;

		m_aPreviewCmds.Clear();
		SM_ShapeGeometry.Build(shape, pts, WidthMeters(widthIdx), m_aShapeLines);

		float ppm = PxPerMeter();
		foreach (SM_ShapeLine l : m_aShapeLines)
		{
			if (!l || l.m_aPts.Count() < 2)
				continue;
			ProjectPointsToScreen(l.m_aPts, m_aShapeScr);
			float wpx = l.m_fWidthMeters * ppm;
			if (wpx < 1)
				wpx = 1;
			BuildStrokeCommands(m_aShapeScr, gc, wpx, m_aShapeTmp);
			foreach (CanvasWidgetCommand c : m_aShapeTmp)
				m_aPreviewCmds.Insert(c);
		}
	}

	protected bool SamePts(notnull array<int> a, notnull array<int> b)
	{
		if (a.Count() != b.Count())
			return false;
		for (int i = 0; i < a.Count(); i++)
		{
			if (a[i] != b[i])
				return false;
		}
		return true;
	}

	//! Committed shape drawing -> projected commands. Same slot BuildFillProjected fills for fills.
	protected void BuildShapeProjected(notnull SM_MapDrawingData d, notnull SM_RenderStroke rs, float ppm)
	{
		rs.m_aCmds.Clear();

		SM_ShapeGeometry.Build(d.m_iShape, d.m_aPoints, WidthMeters(d.m_iWidthIdx), m_aShapeLines);

		foreach (SM_ShapeLine l : m_aShapeLines)
		{
			if (!l || l.m_aPts.Count() < 2)
				continue;
			ProjectPointsToScreen(l.m_aPts, m_aShapeScr);
			float wpx = l.m_fWidthMeters * ppm;
			if (wpx < 1)
				wpx = 1;
			BuildStrokeCommands(m_aShapeScr, d.m_iColor, wpx, m_aShapeTmp);
			foreach (CanvasWidgetCommand c : m_aShapeTmp)
				rs.m_aCmds.Insert(c);
		}
	}


	//! The ghost. Before the anchor it follows the cursor; after it, only the strokes still MISSING are
	//! ghosted — so the ghost thins out as the template lands, and is its own progress bar.
	protected void BuildTemplateGhost()
	{
		m_aPreviewCmds.Clear();

		SM_DrawTemplate t;
		int ax, az;

		SM_TemplateSession sess = SM_TemplateSession.GetInstance();
		array<int> todo = {};

		if (sess.IsPlaced())
		{
			t = sess.Template();
			ax = sess.AnchorX();
			az = sess.AnchorZ();
			sess.GetTodo(todo);
		}
		else
		{
			t = SM_TemplateStore.GetInstance().Selected();
			if (!t)
				return;
			if (!CursorWorld(ax, az))
				return;
			for (int i = 0; i < t.m_aStrokes.Count(); i++)
				todo.Insert(i);
		}

		if (!t)
			return;

		float ppm = PxPerMeter();

		// Two passes: fills FIRST, then strokes. Drawing in template order let a fill that came later
		// paint straight over the lines before it — paint under ink, exactly as the map layers them.
		for (int pass = 0; pass < 2; pass++)
		{
			bool wantFill = (pass == 0);
			foreach (int idx : todo)
			{
				SM_DrawTemplateStroke st = t.m_aStrokes[idx];
				if (!st || st.GetPointCount() < 2)
					continue;
				if ((st.m_iFill != 0) != wantFill)
					continue;

				int n = st.GetPointCount();
				if (wantFill)
				{
					// A fill's mesh keeps its vertex array, so it needs its OWN — the scratch would be
					// overwritten by the next line and every fill would end up sharing the last one's
					// points. Strokes below are fine on the scratch: BuildStrokeCommands copies out.
					array<float> fscr = {};
					fscr.Resize(n * 2);
					for (int fk = 0; fk < n; fk++)
					{
						int fsx, fsy;
						WorldToCanvas(st.m_aPoints[fk * 2] + ax, st.m_aPoints[fk * 2 + 1] + az, fsx, fsy);
						fscr[fk * 2]     = fsx;
						fscr[fk * 2 + 1] = fsy;
					}
					array<int> tri = st.FillIndices();
					if (!tri)
						continue;
					TriMeshDrawCommand mesh = new TriMeshDrawCommand();
					mesh.m_iColor   = GhostColor(st.m_iColor, true);
					mesh.m_Vertices = fscr;
					mesh.m_Indices  = tri;
					m_aPreviewCmds.Insert(mesh);
					continue;
				}

				m_aShapeScr.Resize(n * 2);
				for (int k = 0; k < n; k++)
				{
					int sx, sy;
					WorldToCanvas(st.m_aPoints[k * 2] + ax, st.m_aPoints[k * 2 + 1] + az, sx, sy);
					m_aShapeScr[k * 2]     = sx;
					m_aShapeScr[k * 2 + 1] = sy;
				}

				// BuildStrokeCommands CLEARS the array it is handed — it was written for the one stroke
				// being drawn, where that is exactly right. Called in a loop straight onto the preview
				// list, every stroke wiped the ones before it and the fills under them, and only the
				// last one ever survived. Build into a scratch list and append.
				m_aGhostTmp.Clear();
				BuildStrokeCommands(m_aShapeScr, GhostColor(st.m_iColor, false), WidthPxForZoom(st.m_iWidthIdx, ppm), m_aGhostTmp);
				foreach (CanvasWidgetCommand gc : m_aGhostTmp)
					m_aPreviewCmds.Insert(gc);
			}
		}
	}

	//! Ghost colour: the stroke's own hue at a FIXED alpha.
	//!
	//! Scaling the author's alpha was the mistake — a fill drawn at 25% opacity came out at 8% and was
	//! simply not there, and any stroke short of full opacity faded with it. What the ghost has to say
	//! is "not yet", and that is the same statement whatever the stroke's own transparency happens to
	//! be. Fills sit lower than strokes because they cover ground rather than trace it.
	protected int GhostColor(int argb, bool fill)
	{
		int a = GHOST_ALPHA_STROKE;
		if (fill)
			a = GHOST_ALPHA_FILL;
		return (a << 24) | (argb & 0x00FFFFFF);
	}

	//! The selection rectangle, drawn on the preview canvas like a Windows marquee: a translucent fill
	//! under a solid outline.
	protected void BuildSelectionRect()
	{
		m_aPreviewCmds.Clear();
		if (!m_bSelActive && !m_bSelValid)
			return;

		int loX = Math.Min(m_iSelX0, m_iSelX1);
		int hiX = Math.Max(m_iSelX0, m_iSelX1);
		int loZ = Math.Min(m_iSelZ0, m_iSelZ1);
		int hiZ = Math.Max(m_iSelZ0, m_iSelZ1);

		int c0x, c0y, c1x, c1y;
		WorldToCanvas(loX, loZ, c0x, c0y);
		WorldToCanvas(hiX, hiZ, c1x, c1y);

		array<float> quad = {};
		quad.Insert(c0x); quad.Insert(c0y);
		quad.Insert(c1x); quad.Insert(c0y);
		quad.Insert(c1x); quad.Insert(c1y);
		quad.Insert(c0x); quad.Insert(c1y);

		TriMeshDrawCommand fill = new TriMeshDrawCommand();
		fill.m_iColor = 0x2233AAFF;
		array<float> fv = {};
		fv.Copy(quad);
		fill.m_Vertices = fv;
		array<int> tri = {0, 1, 2, 0, 2, 3};
		fill.m_Indices = tri;
		m_aPreviewCmds.Insert(fill);

		array<float> loop = {};
		loop.Copy(quad);
		loop.Insert(c0x); loop.Insert(c0y);	// close the outline

		LineDrawCommand edge = new LineDrawCommand();
		edge.m_iColor = 0xFF66CCFF;
		edge.m_fWidth = 2;
		edge.m_Vertices = loop;
		m_aPreviewCmds.Insert(edge);
	}

	//! Every OWN stroke the rectangle touches, whole. "Touches" is taken literally: a stroke that only
	//! clips a corner comes along in full, as asked.
	void CollectSelected(out array<SM_MapDrawingData> outStrokes)
	{
		if (!outStrokes)
			outStrokes = {};
		outStrokes.Clear();
		if (!m_bSelValid)
			return;

		int loX = Math.Min(m_iSelX0, m_iSelX1);
		int hiX = Math.Max(m_iSelX0, m_iSelX1);
		int loZ = Math.Min(m_iSelZ0, m_iSelZ1);
		int hiZ = Math.Max(m_iSelZ0, m_iSelZ1);

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		int myId = -1;
		if (pc)
			myId = pc.GetPlayerId();

		array<SM_MapDrawingData> all = {};
		SM_MapDrawingStore.GetInstance().GetAll(all);

		foreach (SM_MapDrawingData d : all)
		{
			if (!d || d.GetPointCount() < 2)
				continue;
			if (d.m_iShape != 0)
				continue;	// shapes carry parameters, not strokes — a template can't absorb one

			// A Local drawing's owner is set on this machine, but leaning on that alone has bitten us
			// before — the id says it is ours by construction, so ask that too.
			if (d.m_iOwnerId != myId && !SM_MapDrawingStore.IsLocalId(d.m_iId))
				continue;

			if (StrokeTouchesRect(d, loX, hiX, loZ, hiZ))
				outStrokes.Insert(d);
		}
	}

	//! AABB is only a prefilter: a long diagonal has a box that overlaps the rectangle while the line
	//! itself passes nowhere near it. So test the segments too.
	protected bool StrokeTouchesRect(notnull SM_MapDrawingData d, int loX, int hiX, int loZ, int hiZ)
	{
		int bx0, bx1, bz0, bz1;
		if (d.GetAABB(bx0, bx1, bz0, bz1)
			&& (bx1 < loX || bx0 > hiX || bz1 < loZ || bz0 > hiZ))
			return false;	// nowhere near it — skip the segment walk

		int n = d.GetPointCount();
		int px, pz;
		d.GetPoint(0, px, pz);
		if (px >= loX && px <= hiX && pz >= loZ && pz <= hiZ)
			return true;

		for (int i = 1; i < n; i++)
		{
			int qx, qz;
			d.GetPoint(i, qx, qz);
			if (qx >= loX && qx <= hiX && qz >= loZ && qz <= hiZ)
				return true;
			if (SM_PolylineUtil.SegmentIntersectsRect(px, pz, qx, qz, loX, loZ, hiX, hiZ))
				return true;
			px = qx;
			pz = qz;
		}
		return false;
	}

	bool HasSelection()
	{
		return m_bSelValid;
	}

	void ClearSelection()
	{
		m_bSelActive = false;
		m_bSelValid  = false;
	}

	void SetColor(int argb)   { s_iColor = argb; }
	int  GetColor()           { return s_iColor; }
	void SetWidthIdx(int i)   { s_iWidthIdx = ClampWidthIdx(i); }
	int  GetWidthIdx()        { return s_iWidthIdx; }
	void SetVisibility(int v) { s_iVisibility = v; }
	int  GetVisibility()      { return s_iVisibility; }

	protected void OnStoreAdded(SM_MapDrawingData d) { m_bMembershipDirty = true; }
	protected void OnStoreRemoved(int id)            { m_bMembershipDirty = true; }

	//------------------------------------------------------------------------------
	// Кадрова робота — кличе Update шару. Працює лише коли є що зробити.
	void Tick()
	{
		SM_DrawOutbox.Tick();	// flush the buffered draw/erase batch once its interval is up

		if (!m_wCanvas || !m_Map)
			return;

		float cw, ch;
		m_wCanvas.GetScreenSize(cw, ch);
		if (cw <= 0 || ch <= 0)
			return;	// ще не розкладено

		// Panning does not reproject, it just slides the cached vertices — but the cursor is converted
		// to world by inverting this affine, so a stale one puts every marker off by the last pan.
		// It costs three WorldToScreen calls; ProjectAll is the expensive one, and it stays gated.
		ComputeAffine();

		bool reproject = false;
		bool changed   = false;

		if (m_bMembershipDirty)
		{
			SyncStrokes();
			m_bMembershipDirty = false;
			reproject = true;	// нові штрихи треба спроєктувати
		}

		float zoom = m_Map.GetCurrentZoom();

		int ax = 0;
		int ay = 0;
		if (m_bViewValid)
			m_Map.WorldToScreen(m_fRefWX, m_fRefWZ, ax, ay, true);

		// Зміна виду: зум → повна (але дешева, афінна) проєкція; чистий пан → зсув кешованих вершин.
		if (!m_bViewValid || zoom != m_fLastZoom)
		{
			reproject = true;
		}
		else if (m_bProjClipped
			&& (Math.AbsInt(ax - m_iProjSx) > m_fClipMarginPx * 0.5 || Math.AbsInt(ay - m_iProjSy) > m_fClipMarginPx * 0.5))
		{
			reproject = true;	// panned off the box we clipped against
		}
		else if (ax != m_iAnchorSx || ay != m_iAnchorSy)
		{
			TranslateAll(ax - m_iAnchorSx, ay - m_iAnchorSy);
			m_iAnchorSx = ax;
			m_iAnchorSy = ay;
			changed = true;
		}

		if (reproject)
		{
			ProjectAll();	// ComputeAffine picks a fresh anchor
			m_Map.WorldToScreen(m_fRefWX, m_fRefWZ, ax, ay, true);
			m_fLastZoom  = zoom;
			m_iAnchorSx  = ax;
			m_iAnchorSy  = ay;
			m_iProjSx    = ax;
			m_iProjSy    = ay;
			m_bViewValid = true;
			changed = true;
		}

		if (changed)
			PushCommitted(cw, ch);

		// Прев'ю штриха в процесі — на окремому легкому полотні, перебудова щокадру під час малювання.
		// An open line chain keeps previewing with LMB up: placed vertices plus the rubber band.
		// The template ghost and the selection box ride the same canvas: live, per-frame, and never
		// mixed into the committed strokes.
		if (m_iTool == TOOL_TEMPLATE)
			TemplateTick();		// at most one stroke goes out, paced under the server's window

		if (m_iTool == TOOL_SELECT)
		{
			BuildSelectionRect();
			PushPreview(cw, ch);
		}
		else if (m_bCapturing || m_bLineChain)
		{
			ProjectPreview();	// a stroke being drawn owns the preview canvas for as long as it lasts
			PushPreview(cw, ch);
		}
		else if (ShapeGhostWanted())
		{
			BuildShapeGhost();	// per-frame rubber-band; snaps live for the grid
			PushPreview(cw, ch);
		}
		else if (TemplateGhostWanted())
		{
			// The ghost stays up even when another tool is in hand. An unfinished template vanishing
			// off the map the moment you pick up the pencil is how you lose track of it entirely.
			BuildTemplateGhost();
			PushPreview(cw, ch);
		}
		else if (!m_aPreviewCmds.IsEmpty())
		{
			m_aPreviewCmds.Clear();
			PushPreview(cw, ch);
		}

		UpdateCursor();
	}

	// Курсор інструмента: кружечок у ФІЗИЧНОМУ розмірі штриха на мапі (метри × px/м),
	// колір пензля для олівця, circleLine для гумки. Слідує за курсором мапи щокадру.
	protected void UpdateCursor()
	{
		if (!m_wCursor)
			return;
		if (!m_bActive)
		{
			m_wCursor.SetVisible(false);
			return;
		}

		if (m_iTool != m_iCursorTool)	// текстуру вантажимо лише на зміну інструмента
		{
			if (m_iTool == 1)
				m_wCursor.LoadImageTexture(0, CURSOR_TEX_ERASER);
			else
				m_wCursor.LoadImageTexture(0, CURSOR_TEX_PENCIL);
			m_iCursorTool = m_iTool;
		}

		if (m_iTool == 1)
			m_wCursor.SetColor(Color.FromInt(0xFFFFFFFF));	// гумка — біла обводка
		else
			m_wCursor.SetColor(Color.FromInt(s_iColor));	// олівець/заливка — колір пензля

		WorkspaceWidget ws = GetGame().GetWorkspace();
		float sizeUnscaled;
		if (m_iTool == 2)
			sizeUnscaled = 12;	// заливка: фіксований маркер кольору (розмір пензля не має значення)
		else
			sizeUnscaled = ws.DPIUnscale(WidthPxForZoom(s_iWidthIdx, PxPerMeter()));
		if (sizeUnscaled < 3)
			sizeUnscaled = 3;	// не даємо курсору зникнути на сильному віддаленні

		// The circle is a child of the map frame, so it wants a frame-local position.
		float cfx, cfy;
		CursorInFrame(m_wMapFrame, cfx, cfy);

		m_wCursor.SetVisible(true);
		FrameSlot.SetSize(m_wCursor, sizeUnscaled, sizeUnscaled);
		FrameSlot.SetPos(m_wCursor, ws.DPIUnscale(cfx), ws.DPIUnscale(cfy));
	}

	//------------------------------------------------------------------------------
	// Захоплення (кличе перехоплення ЛКМ у шарі зі світовими координатами курсора)

	//! lineMode = Shift was down on press: a straight segment from press to release. With a chain
	//! already open the segment starts at its last vertex instead of at the press point.
	void OnPressDown(int wx, int wz, bool lineMode = false)
	{
		if (m_iTool == 1)
		{
			// Гумка: починаємо «мазок стирання» — стираємо все, чого торкається круг.
			m_ErasedThisDrag.Clear();
			m_bErasing = true;
			EraseHit(wx, wz);
			m_iLastStampX = wx;
			m_iLastStampZ = wz;
			return;
		}
		if (m_iTool == 2)
		{
			HandleFillClick(wx, wz);	// заливка — одиночний клік, без перетягування
			return;
		}
		if (m_iTool == TOOL_TEMPLATE)
		{
			TemplatePressDown(wx, wz);
			return;
		}
		if (m_iTool == TOOL_SELECT)
		{
			m_bSelActive = true;
			m_bSelValid  = false;	// a fresh drag replaces whatever was framed before
			m_iSelX0 = wx; m_iSelZ0 = wz;
			m_iSelX1 = wx; m_iSelZ1 = wz;
			return;
		}
		m_iMaxCapturePts = SM_MarkerConfig.GetInstance().m_iDrawMaxPointsPerStroke;

		if (HasLineChain())
		{
			if (lineMode)
			{
				m_aCapture.Resize(m_iLineFixedInts);	// drop the rubber band, keep the vertices
				m_iLastSampleX = wx;
				m_iLastSampleZ = wz;
				m_bCapturing   = true;
				m_bLineStroke  = true;
				return;
			}
			// Shift went up on the very frame of the press — close the chain before starting fresh,
			// otherwise the placed vertices would be silently thrown away.
			FinishLineChain();
		}

		m_aCapture.Clear();
		m_aCapture.Insert(wx);
		m_aCapture.Insert(wz);
		m_iLineFixedInts = 2;
		m_iLastSampleX = wx;
		m_iLastSampleZ = wz;
		m_bCapturing   = true;
		m_bLineStroke  = lineMode;
		m_bLineChain   = false;
	}

	bool HasLineChain()
	{
		return m_bLineChain && m_iLineFixedInts >= 2 && m_aCapture.Count() >= m_iLineFixedInts;
	}

	//! Chain open, LMB up: drag the rubber band from the last vertex to the cursor.
	void OnLineChainHover(int wx, int wz)
	{
		if (!HasLineChain() || m_bCapturing)
			return;
		m_aCapture.Resize(m_iLineFixedInts);
		m_aCapture.Insert(wx);
		m_aCapture.Insert(wz);
	}

	//! Shift released (or the tool/map changed) — close the polyline and commit it as one stroke.
	void FinishLineChain()
	{
		if (!HasLineChain())
			return;
		m_aCapture.Resize(m_iLineFixedInts);	// the rubber band is not part of the stroke
		m_bLineChain  = false;
		m_bLineStroke = false;
		m_bCapturing  = false;

		if (m_iLineFixedInts < 4)
		{
			CancelStroke();	// a lone anchor is an abandoned line, not a dot
			return;
		}
		CommitStroke();
	}

	void OnDrag(int wx, int wz)
	{
		if (m_iTool == 1)
		{
			if (m_bErasing)
			{
				// Drag events land every frame; a stamp that barely moved re-scans the whole store to
				// cut nothing new. Skip it while the spacing stays under a quarter radius — the previous
				// circle already covers everything such a stamp could reach. (World coords are metre
				// ints, so the floor of 1 makes "didn't move a cell" the minimum skip.)
				int ddx = wx - m_iLastStampX;
				int ddz = wz - m_iLastStampZ;
				float minStep = WidthMeters(s_iWidthIdx) * 0.125;	// quarter of the eraser radius
				float minStepSq = Math.Max(1.0, minStep * minStep);
				if (ddx * ddx + ddz * ddz >= minStepSq)
				{
					EraseHit(wx, wz);
					m_iLastStampX = wx;
					m_iLastStampZ = wz;
				}
			}
			return;
		}
		if (m_iTool == 2)
			return;	// заливка не тягнеться
		if (m_iTool == TOOL_TEMPLATE)
			return;	// the anchor is fixed; holding is what draws, and Tick does that
		if (m_iTool == TOOL_SELECT)
		{
			if (m_bSelActive)
			{
				m_iSelX1 = wx;
				m_iSelZ1 = wz;
			}
			return;
		}
		if (!m_bCapturing)
			return;
		// Straight-line mode: fixed vertices plus one moving point (the rubber band).
		if (m_bLineStroke)
		{
			m_aCapture.Resize(m_iLineFixedInts);
			m_aCapture.Insert(wx);
			m_aCapture.Insert(wz);
			return;
		}
		// Досягнуто стелі точок — припиняємо додавати (RDP лише зменшує, тож сервер не обріже хвіст).
		// Лінія перестає рости — це видно гравцю; щоб продовжити, відпусти й почни новий штрих.
		if (m_iMaxCapturePts > 0 && m_aCapture.Count() / 2 >= m_iMaxCapturePts)
			return;
		float dx = wx - m_iLastSampleX;
		float dz = wz - m_iLastSampleZ;
		if (dx * dx + dz * dz < SAMPLE_MIN_DIST * SAMPLE_MIN_DIST)
			return;
		m_aCapture.Insert(wx);
		m_aCapture.Insert(wz);
		m_iLastSampleX = wx;
		m_iLastSampleZ = wz;
	}

	//! keepChain = Shift is still down on release: fix the vertex but hold the stroke back and wait
	//! for the next click (or for Shift to go up, which lands in FinishLineChain).
	void OnRelease(int wx, int wz, bool keepChain = false)
	{
		if (m_iTool == 1)
		{
			FinishErase();
			m_bErasing = false;
			m_ErasedThisDrag.Clear();
			return;
		}
		if (m_iTool == 2)
			return;
		if (m_iTool == TOOL_TEMPLATE)
		{
			m_bTplHeld = false;	// auto-drawing pauses; the instance survives
			return;
		}
		if (m_iTool == TOOL_SELECT)
		{
			if (m_bSelActive)
			{
				m_bSelActive = false;
				m_bSelValid  = (Math.AbsInt(m_iSelX1 - m_iSelX0) > 1 && Math.AbsInt(m_iSelZ1 - m_iSelZ0) > 1);
			}
			return;
		}
		if (!m_bCapturing)
			return;
		m_bCapturing = false;
		if (m_bLineStroke)
		{
			// Pin the segment end; a zero-length click must not duplicate the vertex.
			m_aCapture.Resize(m_iLineFixedInts);
			int ln = m_aCapture.Count();
			if (ln < 2 || m_aCapture[ln - 2] != wx || m_aCapture[ln - 1] != wz)
			{
				m_aCapture.Insert(wx);
				m_aCapture.Insert(wz);
			}
			m_iLineFixedInts = m_aCapture.Count();

			// One vertex is enough to open the chain — it is the anchor the next click draws from.
			// Demanding two here meant a plain Shift+click committed it as a dot and dropped the mode.
			bool full = (m_iMaxCapturePts > 0 && m_iLineFixedInts / 2 >= m_iMaxCapturePts);
			if (keepChain && !full && m_iLineFixedInts >= 2)
			{
				m_bLineChain = true;	// nothing goes out until the chain closes
				return;
			}
			m_bLineStroke = false;
			m_bLineChain  = false;
		}
		else
		{
			int n = m_aCapture.Count();
			if (n < 2 || m_aCapture[n - 2] != wx || m_aCapture[n - 1] != wz)
			{
				m_aCapture.Insert(wx);
				m_aCapture.Insert(wz);
			}
		}
		CommitStroke();
	}

	void CancelStroke()
	{
		m_bCapturing = false;
		m_bLineStroke = false;
		m_bLineChain = false;
		m_iLineFixedInts = 0;
		m_aCapture.Clear();
		m_aPreviewCmds.Clear();
		FinishErase();
		m_bErasing = false;
		m_ErasedThisDrag.Clear();
	}

	protected void CommitStroke()
	{
		if (m_aCapture.Count() < 2)	// < 1 точки — нема чого коммітити (1 точка = крапка, ок)
		{
			CancelStroke();
			return;
		}
		float eps = SM_MarkerConfig.GetInstance().m_iDrawRdpEpsilon;
		array<int> simplified = SM_PolylineUtil.RDPSimplify(m_aCapture, eps);

		SM_MapDrawingData tmp = new SM_MapDrawingData();
		tmp.m_iColor      = ApplyOpacity(s_iColor);
		tmp.m_iWidthIdx   = s_iWidthIdx;
		tmp.m_iVisibility = s_iVisibility;
		tmp.m_iChannel    = -1;	// призначить сервер за видимістю
		if (m_bEditorMap)	// зевс: обрана сторона для Side + прапорці lock/hide (сервер приймає лише від GM)
		{
			tmp.m_iChannel = SM_GmState.s_iDrawSideChannel;
			if (SM_GmState.s_bDrawGmLock)
				tmp.m_iGmLocked = 1;
			if (SM_GmState.s_bDrawHideInfo)
				tmp.m_iHideInfo = 1;
		}
		SM_DrawAddOrLocal(tmp, simplified);	// Local (PERSONAL) -> client file; everything else -> server

		m_aCapture.Clear();
		m_iLineFixedInts = 0;
		m_aPreviewCmds.Clear();	// the committed stroke comes back via the store (network, or right away if Local)
	}

	// Заливка: клік у замкнуту область → flood fill по видимих малюнках (штрихи+заливки як межі).
	// Клік у ВЛАСНУ наявну заливку → перефарбування на місці (нову не створюємо).
	protected void HandleFillClick(int wx, int wz)
	{
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!pc)
			return;

		array<SM_MapDrawingData> all = {};
		SM_MapDrawingStore.GetInstance().GetAll(all);

		// 1) Клік усередині наявної заливки? Беремо найновішу, що містить точку.
		SM_MapDrawingData hit = null;
		foreach (SM_MapDrawingData f : all)
		{
			if (!f || f.m_iFill == 0)
				continue;
			if (!f.AABBOverlapsRect(wx, wx, wz, wz, 0))
				continue;
			if (!SM_MapFloodFill.PointInPolygon(f.m_aPoints, wx, wz))
				continue;
			if (!hit || f.m_iId > hit.m_iId)
				hit = f;
		}
		if (hit)
		{
			// Чужі заливки теж можна: права перевірить сервер. Локально блокуємо лише GM-lock.
			if (hit.m_iGmLocked != 0 && !m_bEditorMap)
			{
				pc.SM_ShowPlaceDenied(SM_EPlaceDenyReason.DRAW_LOCKED, 0);
				return;
			}

			// Мета з поточних налаштувань панелі — оновлюється і колір, і сторона.
			SM_MapDrawingData restamp = new SM_MapDrawingData();
			restamp.m_iColor      = ApplyOpacity(s_iColor);
			restamp.m_iWidthIdx   = s_iWidthIdx;
			restamp.m_iVisibility = s_iVisibility;
			restamp.m_iChannel    = -1;
			restamp.m_iFill       = 1;
			if (m_bEditorMap)
			{
				restamp.m_iChannel = SM_GmState.s_iDrawSideChannel;
				if (SM_GmState.s_bDrawGmLock)
					restamp.m_iGmLocked = 1;
				if (SM_GmState.s_bDrawHideInfo)
					restamp.m_iHideInfo = 1;
			}

			// Local-CHANNEL fill (not an optimistic server temp): recolor/escalation is fully client-side.
			if (SM_MapDrawingStore.IsLocalId(hit.m_iId) && !SM_DrawOutbox.IsServerTemp(hit.m_iId))
			{
				SM_LocalRecolorFill(hit, restamp);
				return;
			}

			bool changed = hit.m_iColor != restamp.m_iColor || hit.m_iVisibility != restamp.m_iVisibility;
			if (m_bEditorMap)
				changed = changed
					|| (restamp.m_iVisibility == SM_EMarkerVisibility.FACTION && hit.m_iChannel != restamp.m_iChannel)
					|| hit.m_iGmLocked != restamp.m_iGmLocked
					|| hit.m_iHideInfo != restamp.m_iHideInfo;
			if (changed)
				pc.SM_DrawRequestRecolor(hit.m_iId, restamp.PackMeta());
			return;
		}

		// 1.5) Click inside a parametric shape (circle / rectangle / grid cell)? Fill its EXACT outline.
		// Flood fill would only stairstep the circle into the 256-grid it rasterises against; we know
		// the real geometry, so there is no reason to approximate it. The SMALLEST shape wins, so a cell
		// inside a big circle fills the cell.
		int fillMaxPts = SM_MarkerConfig.GetInstance().m_iDrawMaxPointsPerStroke;
		if (fillMaxPts <= 0)
			fillMaxPts = 200;
		array<int> shapeContour = {};
		if (TryShapeFill(wx, wz, all, fillMaxPts, shapeContour))
		{
			SM_MapDrawingData sd = new SM_MapDrawingData();
			sd.m_iColor      = ApplyOpacity(s_iColor);
			sd.m_iWidthIdx   = s_iWidthIdx;
			sd.m_iVisibility = s_iVisibility;
			sd.m_iChannel    = -1;
			sd.m_iFill       = 1;
			if (m_bEditorMap)
			{
				sd.m_iChannel = SM_GmState.s_iDrawSideChannel;
				if (SM_GmState.s_bDrawGmLock)
					sd.m_iGmLocked = 1;
				if (SM_GmState.s_bDrawHideInfo)
					sd.m_iHideInfo = 1;
			}
			SM_DrawAddOrLocal(sd, shapeContour);
			return;
		}

		// 2) Розлив. Деталізація — за клієнтським налаштуванням (дефолт off = простіше й без фрізу).
		// Enhanced: щільний ліміт точок (периметр великої заливки потребує деталі, якої в ручного
		// штриха не буває) + ущільнення джерела снапу. Simple: ті самі фікси, але низький ліміт →
		// дешева тріангуляція, миттєве розміщення.
		bool highDetail = SM_ClientPrefs.EnhancedFill();
		int maxPts;
		if (highDetail)
		{
			maxPts = SM_MarkerConfig.GetInstance().m_iDrawMaxPointsPerFill;
			if (maxPts <= 0)
				maxPts = 1000;
		}
		else
		{
			maxPts = 250;	// stays well under the O(n²) triangulation knee — no placement hitch
		}
		SM_FloodFillResult res;
		int code = SM_MapFloodFill.Compute(wx, wz, all, maxPts, highDetail, res);

		if (code == SM_MapFloodFill.NOT_CLOSED)
		{
			pc.SM_ShowPlaceDenied(SM_EPlaceDenyReason.FILL_NOT_CLOSED, 0);
			return;
		}
		if (code != SM_MapFloodFill.OK)
		{
			pc.SM_ShowPlaceDenied(SM_EPlaceDenyReason.FILL_BLOCKED, 0);
			return;
		}

		// 3) Відправляємо як звичайний малюнок із прапорцем fill (та сама мета-схема, що й штрих).
		SM_MapDrawingData tmp = new SM_MapDrawingData();
		tmp.m_iColor      = ApplyOpacity(s_iColor);
		tmp.m_iWidthIdx   = s_iWidthIdx;
		tmp.m_iVisibility = s_iVisibility;
		tmp.m_iChannel    = -1;
		tmp.m_iFill       = 1;
		if (m_bEditorMap)
		{
			tmp.m_iChannel = SM_GmState.s_iDrawSideChannel;
			if (SM_GmState.s_bDrawGmLock)
				tmp.m_iGmLocked = 1;
			if (SM_GmState.s_bDrawHideInfo)
				tmp.m_iHideInfo = 1;
		}
		SM_DrawAddOrLocal(tmp, res.m_aContour);	// Local (PERSONAL) -> client file; everything else -> server
	}

	//! Click inside a parametric shape -> the exact fill polygon for it (circle rim, rectangle, or the
	//! grid cell under the cursor). Picks the SMALLEST shape the click falls in. False = not on a shape,
	//! fall through to the flood fill.
	protected bool TryShapeFill(int wx, int wz, notnull array<SM_MapDrawingData> all, int maxPts, notnull array<int> outContour)
	{
		SM_MapDrawingData best = null;
		float bestArea = 0;
		foreach (SM_MapDrawingData d : all)
		{
			if (!d || d.m_iShape == 0)
				continue;
			if (!d.AABBOverlapsRect(wx, wx, wz, wz, 0))
				continue;
			if (!SM_ShapeGeometry.PointInShape(d.m_iShape, d.m_aPoints, wx, wz))
				continue;
			float area = SM_ShapeGeometry.ShapeArea(d.m_iShape, d.m_aPoints);
			if (!best || area < bestArea)
			{
				best = d;
				bestArea = area;
			}
		}
		if (!best)
			return false;

		SM_ShapeGeometry.FillContour(best.m_iShape, best.m_aPoints, wx, wz, maxPts, outContour);
		return outContour.Count() >= 6;	// need at least a triangle
	}

	// Add a stroke/fill: Local (PERSONAL visibility) goes to the client file, never the server;
	// everything else goes through SM_DrawOutbox (batching + optimistic echo, or instant when
	// batching is off).
	protected void SM_DrawAddOrLocal(notnull SM_MapDrawingData tmp, notnull array<int> points)
	{
		if (tmp.m_iVisibility == SM_EMarkerVisibility.PERSONAL)
		{
			// Local = the player's private drawing: channel/GM flags don't apply.
			SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
			tmp.m_iChannel  = -1;
			tmp.m_iGmLocked = 0;
			tmp.m_iHideInfo = 0;
			if (pc)
				tmp.m_iOwnerId = pc.GetPlayerId();
			tmp.SetPoints(points);
			SM_LocalDrawingPersistence.GetInstance().AddLocal(tmp);
			return;
		}

		tmp.SetPoints(points);
		SM_DrawOutbox.SubmitAdd(tmp);	// server channel: batched/optimistic
	}

	// Recolor/escalate an OWN Local fill. Visibility can only be widened (matches the server rule).
	// Stays PERSONAL -> new Local fill; widened -> hand it to the server (Local -> Side escalation).
	protected void SM_LocalRecolorFill(notnull SM_MapDrawingData hit, notnull SM_MapDrawingData restamp)
	{
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());

		int newVis = restamp.m_iVisibility;
		if (newVis < hit.m_iVisibility)
			newVis = hit.m_iVisibility;	// widen-only (Local=0 is the lowest, so this is always >=)

		array<int> pts = {};
		pts.Copy(hit.m_aPoints);

		SM_LocalDrawingPersistence.GetInstance().RemoveLocal(hit.m_iId);

		SM_MapDrawingData fresh = new SM_MapDrawingData();
		fresh.m_iColor      = restamp.m_iColor;
		fresh.m_iWidthIdx   = restamp.m_iWidthIdx;
		fresh.m_iVisibility = newVis;
		fresh.m_iChannel    = -1;
		fresh.m_iFill       = 1;
		fresh.SetPoints(pts);

		if (newVis == SM_EMarkerVisibility.PERSONAL)
		{
			if (pc)
				fresh.m_iOwnerId = pc.GetPlayerId();
			SM_LocalDrawingPersistence.GetInstance().AddLocal(fresh);
		}
		else
		{
			SM_DrawOutbox.SubmitAdd(fresh);	// escalation to the server (batched/optimistic)
		}
	}

	// Erase/cut an OWN Local stroke: remove the original, re-add the pieces outside the eraser
	// circle as new Local strokes. framed: [pieceCount, len1(points), x,z,..., len2, ...] —
	// same format the server path uses.
	protected void SM_LocalEraseStroke(notnull SM_MapDrawingData d, notnull array<int> framed)
	{
		int color = d.m_iColor;
		int width = d.m_iWidthIdx;
		int vis   = d.m_iVisibility;
		int owner = d.m_iOwnerId;

		SM_LocalDrawingPersistence.GetInstance().RemoveLocal(d.m_iId);
		if (framed.IsEmpty())
			return;	// стерли все

		int nPieces = framed[0];
		int pos = 1;
		for (int p = 0; p < nPieces; p++)
		{
			if (pos >= framed.Count())
				break;
			int len = framed[pos];
			pos++;
			if (len < 2 || pos + len * 2 > framed.Count())
				break;
			array<int> pts = {};
			for (int k = 0; k < len * 2; k++)
				pts.Insert(framed[pos + k]);
			pos += len * 2;

			SM_MapDrawingData piece = new SM_MapDrawingData();
			piece.m_iColor      = color;
			piece.m_iWidthIdx   = width;
			piece.m_iVisibility = vis;
			piece.m_iChannel    = -1;
			piece.m_iFill       = 0;
			piece.m_iOwnerId    = owner;
			piece.SetPoints(pts);
			SM_LocalDrawingPersistence.GetInstance().AddLocal(piece);
		}
	}

	// Photoshop-style eraser: removes only what the eraser circle TOUCHES. Own server strokes are
	// cut client-side against their ORIGINAL geometry for the whole drag (progressive, shown as
	// temps); one authoritative erase-part per stroke goes out when the drag ends (see SM_EraseWork
	// and FinishErase) — sending per hit would need the server's piece ids, which don't arrive in
	// time and left later cuts unsynced. Local strokes are cut in the client file, others' strokes a
	// GM can wipe whole. All geometry is in WORLD metres: threshold = eraser radius + half stroke width.
	protected void EraseHit(int wx, int wz)
	{
		if (!m_Map)
			return;

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!pc)
			return;
		int myId = pc.GetPlayerId();

		float eraserRad = WidthMeters(s_iWidthIdx) * 0.5;

		// Keep cutting the own strokes this drag already picked up — their temps follow the eraser.
		foreach (SM_EraseWork work : m_aEraseWork)
			ApplyStampToWork(work, wx, wz, eraserRad);

		array<SM_MapDrawingData> all = {};
		SM_MapDrawingStore.GetInstance().GetAll(all);

		foreach (SM_MapDrawingData d : all)
		{
			if (!d || d.GetPointCount() < 1)
				continue;
			if (m_EraseWorkTemps.Contains(d.m_iId))
				continue;	// our own drag piece — handled by the works above
			if (m_ErasedThisDrag.Contains(d.m_iId))
				continue;
			// Залочений зевсом штрих у не-GM гумка тихо оминає (сервер однаково відхилить; Del покаже чому).
			if (d.m_iGmLocked != 0 && !m_bEditorMap)
				continue;
			if (d.m_iFill != 0)
				continue;	// гумка заливки НЕ чіпає (інакше зачепиш заливку під лініями, які правиш); видалення — Del/X

			float thr = eraserRad + WidthMeters(d.m_iWidthIdx) * 0.5;
			float thrSq = thr * thr;

			// O(1) reject before any per-point loop: the stamp can't touch what its circle can't reach.
			// The cached AABB covers shape outlines too (SetPoints builds it shape-aware); an invalid
			// AABB reports "maybe" and falls through, so nothing can be wrongly skipped.
			if (!d.AABBOverlapsRect(wx, wx, wz, wz, Math.Ceil(thr)))
				continue;

			// A shape is parameters, not geometry — there is nothing meaningful to CUT. Touching its
			// outline erases it whole (rights checked by the server / the Local file is the player's own).
			// The grid is exempt: it's a big deliberate object with lines all over the map, so a stray
			// brush stroke must not wipe it — only Delete (a click on it) removes a grid.
			if (d.m_iShape != 0)
			{
				if (d.m_iShape == SM_ShapeGeometry.SHAPE_GRID)
					continue;
				if (WorldDistSqToStroke(d, wx, wz) <= thrSq)
				{
					m_ErasedThisDrag.Insert(d.m_iId);
					if (SM_MapDrawingStore.IsLocalId(d.m_iId) && !SM_DrawOutbox.IsServerTemp(d.m_iId))
						SM_LocalDrawingPersistence.GetInstance().RemoveLocal(d.m_iId);
					else
						SM_DrawOutbox.SubmitRemove(d.m_iId);
				}
				continue;
			}

			// Local-CHANNEL stroke (id <= -2, but NOT an optimistic server temp): cut/erase it in
			// the client file. Server temps (also negative) fall to the branches below.
			if (SM_MapDrawingStore.IsLocalId(d.m_iId) && !SM_DrawOutbox.IsServerTemp(d.m_iId))
			{
				array<int> lframed = {};
				if (SplitStrokeByEraser(d, wx, wz, thrSq, lframed) == 0)
					continue;
				m_ErasedThisDrag.Insert(d.m_iId);
				SM_LocalEraseStroke(d, lframed);
				continue;
			}

			if (d.m_iOwnerId != myId)
			{
				// Someone else's stroke: partial cutting isn't ours to do (server owns the rights).
				// A GM can erase it whole — send a full remove, the server validates.
				if (WorldDistSqToStroke(d, wx, wz) <= thrSq)
				{
					m_ErasedThisDrag.Insert(d.m_iId);
					SM_DrawOutbox.SubmitRemove(d.m_iId);
				}
				continue;
			}

			// Own optimistic temp the server hasn't answered for yet — either an ADD still in flight or
			// a leftover piece from an earlier erase-part. Keep the local split: it re-adds the pieces
			// as fresh adds so they still reach the server, and cancelling the temp is remembered until
			// reconcile names the real stroke standing behind it.
			if (SM_DrawOutbox.IsServerTemp(d.m_iId))
			{
				array<int> tframed = {};
				if (SplitStrokeByEraser(d, wx, wz, thrSq, tframed) == 0)
					continue;
				m_ErasedThisDrag.Insert(d.m_iId);
				if (tframed.IsEmpty())
					SM_DrawOutbox.SubmitRemove(d.m_iId);
				else
					SM_DrawOutbox.SubmitErasePart(d.m_iId, tframed);
				continue;
			}

			// Own real server stroke.
			if (SM_DrawOutbox.Enabled())
			{
				// Batched remote client: the store is a client replica, so hiding the original is pure
				// optimism. Start a drag work (cut the copy client-side) only if the eraser really cuts
				// it; one authoritative erase-part goes out at drag end (StartEraseWork / FinishErase).
				array<ref array<int>> firstPieces = {};
				if (!CutPtsByCircle(d.m_aPoints, wx, wz, thrSq, firstPieces))
					continue;
				StartEraseWork(d, firstPieces);
			}
			else
			{
				// Host / no batching: the store is authoritative and the erase-part runs immediately,
				// so the leftover pieces come back as REAL strokes the continuing drag can re-cut.
				// Removing the original here (as a work would) before the RPC would delete it outright.
				array<int> framed = {};
				if (SplitStrokeByEraser(d, wx, wz, thrSq, framed) == 0)
					continue;
				m_ErasedThisDrag.Insert(d.m_iId);
				if (framed.IsEmpty())
					SM_DrawOutbox.SubmitRemove(d.m_iId);
				else
					SM_DrawOutbox.SubmitErasePart(d.m_iId, framed);
			}
		}
	}

	// Begin cutting an own real stroke this drag: snapshot its meta, hide the original, show the
	// pieces the first stamp already produced.
	protected void StartEraseWork(notnull SM_MapDrawingData d, array<ref array<int>> firstPieces)
	{
		SM_EraseWork work = new SM_EraseWork();
		work.m_iOrigId   = d.m_iId;
		work.m_iColor    = d.m_iColor;
		work.m_iWidthIdx = d.m_iWidthIdx;
		work.m_iVis      = d.m_iVisibility;
		work.m_iChannel  = d.m_iChannel;
		work.m_iOwnerId  = d.m_iOwnerId;
		work.m_aPieces   = firstPieces;
		work.m_bHasAABB  = d.GetAABB(work.m_iMinX, work.m_iMaxX, work.m_iMinZ, work.m_iMaxZ);
		SM_MapDrawingStore.GetInstance().ApplyRemove(d.m_iId);	// hide it; the server copy stays until drag end
		m_aEraseWork.Insert(work);
		RefreshWorkTemps(work);
	}

	// Cut every leftover piece of a work by one more eraser stamp; refresh the temps if anything changed.
	protected void ApplyStampToWork(notnull SM_EraseWork work, int ex, int ez, float eraserRad)
	{
		float thr = eraserRad + WidthMeters(work.m_iWidthIdx) * 0.5;

		// Stamp can't reach the original stroke's box -> can't reach any leftover piece either.
		if (work.m_bHasAABB)
		{
			int slack = Math.Ceil(thr);
			if (ex + slack < work.m_iMinX || ex - slack > work.m_iMaxX
			 || ez + slack < work.m_iMinZ || ez - slack > work.m_iMaxZ)
				return;
		}

		float thrSq = thr * thr;
		array<ref array<int>> next = {};
		bool changed = false;
		foreach (array<int> piece : work.m_aPieces)
		{
			array<ref array<int>> cut = {};
			if (CutPtsByCircle(piece, ex, ez, thrSq, cut))
			{
				changed = true;
				foreach (array<int> c : cut)
					next.Insert(c);
			}
			else
			{
				next.Insert(piece);
			}
		}
		if (!changed)
			return;
		work.m_aPieces = next;
		RefreshWorkTemps(work);
	}

	// Replace a work's visible temps with fresh ones matching its current pieces.
	protected void RefreshWorkTemps(notnull SM_EraseWork work)
	{
		SM_MapDrawingStore store = SM_MapDrawingStore.GetInstance();
		foreach (int t : work.m_aTemps)
		{
			store.ApplyRemove(t);
			m_EraseWorkTemps.RemoveItem(t);	// set.Remove(x) is BY INDEX — with a negative temp id that's garbage
		}
		work.m_aTemps.Clear();

		foreach (array<int> piece : work.m_aPieces)
		{
			if (piece.Count() < 4)
				continue;
			SM_MapDrawingData vis = new SM_MapDrawingData();
			vis.m_iColor      = work.m_iColor;
			vis.m_iWidthIdx   = work.m_iWidthIdx;
			vis.m_iVisibility = work.m_iVis;
			vis.m_iChannel    = work.m_iChannel;
			vis.m_iFill       = 0;
			vis.m_iOwnerId    = work.m_iOwnerId;
			vis.SetPoints(piece);
			SM_MapDrawingData created = store.LocalCreate(vis);
			if (!created)
				continue;
			work.m_aTemps.Insert(created.m_iId);
			m_EraseWorkTemps.Insert(created.m_iId);
		}
	}

	// Drag ended: send one authoritative erase-part per work (or a full remove if nothing is left).
	// The original real ids are still valid — nothing went to the server mid-drag.
	protected void FinishErase()
	{
		if (m_aEraseWork.IsEmpty())
			return;

		SM_MapDrawingStore store = SM_MapDrawingStore.GetInstance();
		int maxPieces = SM_DrawingNet.MAX_ERASE_PIECES;
		foreach (SM_EraseWork work : m_aEraseWork)
		{
			bool changed = PruneSlivers(work.m_aPieces);

			if (work.m_aPieces.IsEmpty())
			{
				foreach (int t : work.m_aTemps)
					store.ApplyRemove(t);
				SM_DrawOutbox.SubmitRemove(work.m_iOrigId);
				continue;
			}

			// The server caps pieces per erase; if a very wiggly drag made more, keep the longest so
			// the request isn't rejected (dropping tiny slivers is what the eraser was doing anyway).
			// Any change to the piece set has to land BEFORE the temps are snapshotted: reconcile pairs
			// temp[i] with the server's piece[i], so the temps must mirror exactly what goes out, in order.
			if (ClampPieces(work.m_aPieces, maxPieces))
				changed = true;
			if (changed)
				RefreshWorkTemps(work);

			array<int> temps = {};
			temps.Copy(work.m_aTemps);
			array<int> framed = FramePieces(work.m_aPieces);
			SM_DrawOutbox.AdoptErasePart(work.m_iOrigId, framed, temps);
		}

		m_aEraseWork.Clear();
		m_EraseWorkTemps.Clear();
	}

	// Frame pieces into the wire format: [pieceCount, len1(points), x,z,..., len2, ...].
	protected array<int> FramePieces(notnull array<ref array<int>> pieces)
	{
		array<int> framed = {};
		framed.Insert(pieces.Count());
		foreach (array<int> p : pieces)
		{
			framed.Insert(p.Count() / 2);
			foreach (int v : p)
				framed.Insert(v);
		}
		return framed;
	}

	// Keep only the `cap` longest pieces (in place). No-op when already within the cap.
	// Returns true if anything was dropped — the caller has to re-show the temps when that happens,
	// so what the player sees still matches what goes on the wire.
	// RemoveOrdered, not Remove: plain Remove is a swap-remove and would shuffle the surviving pieces,
	// and reconcile pairs the server's real piece ids with our temps strictly by position.
	protected bool ClampPieces(notnull array<ref array<int>> pieces, int cap)
	{
		if (cap < 1 || pieces.Count() <= cap)
			return false;
		// simple selection: repeatedly drop the shortest until we fit
		while (pieces.Count() > cap)
		{
			int shortest = 0;
			for (int i = 1; i < pieces.Count(); i++)
			{
				if (pieces[i].Count() < pieces[shortest].Count())
					shortest = i;
			}
			pieces.RemoveOrdered(shortest);
		}
		return true;
	}

	// A piece under 2 points has no length to draw, RefreshWorkTemps skips it, and the server drops the
	// WHOLE erase-part when it meets one. CutPtsByCircle shouldn't make them; this keeps the "one temp
	// per piece, in order" invariant that reconcile pairs against true even if one ever slips out.
	// Ordered removal for the same reason as ClampPieces.
	protected bool PruneSlivers(notnull array<ref array<int>> pieces)
	{
		bool changed = false;
		for (int i = pieces.Count() - 1; i >= 0; i--)
		{
			if (pieces[i].Count() < 4)
			{
				pieces.RemoveOrdered(i);
				changed = true;
			}
		}
		return changed;
	}

	// Cut a polyline (x,z world ints) by one eraser circle (centre ex,ez; threshold² thrSq, metres).
	// A vertex inside the circle is dropped; a segment between two OUTSIDE vertices is also broken if
	// it passes through the circle (a quick swipe across the line). outPieces gets the surviving runs
	// (>= 2 points each). Returns true if anything was cut.
	protected bool CutPtsByCircle(notnull array<int> pts, int ex, int ez, float thrSq, array<ref array<int>> outPieces)
	{
		int n = pts.Count() / 2;
		array<int> cur = {};
		bool changed = false;

		for (int i = 0; i < n; i++)
		{
			int px = pts[i * 2];
			int pz = pts[i * 2 + 1];
			float dx = px - ex;
			float dz = pz - ez;
			bool inside = (dx * dx + dz * dz) <= thrSq;

			if (inside)
			{
				changed = true;
				ClosePiece(cur, outPieces);
				continue;
			}

			cur.Insert(px);
			cur.Insert(pz);

			if (i < n - 1)
			{
				int qx = pts[(i + 1) * 2];
				int qz = pts[(i + 1) * 2 + 1];
				float qdx = qx - ex;
				float qdz = qz - ez;
				if ((qdx * qdx + qdz * qdz) > thrSq
					&& PointToSegDistSq(ex, ez, px, pz, qx, qz) <= thrSq)
				{
					changed = true;
					ClosePiece(cur, outPieces);
				}
			}
		}
		ClosePiece(cur, outPieces);
		return changed;
	}

	// Frame a whole stroke cut by one eraser circle (used by the Local-file and add-temp paths, which
	// erase in a single shot). out framed: [pieceCount, len1, x,z,..., ...]; empty = everything erased.
	// Returns 1 if anything was cut, 0 otherwise.
	protected int SplitStrokeByEraser(notnull SM_MapDrawingData d, int ex, int ez, float thrSq, array<int> framed)
	{
		array<ref array<int>> pieces = {};
		if (!CutPtsByCircle(d.m_aPoints, ex, ez, thrSq, pieces))
			return 0;
		framed.Copy(FramePieces(pieces));
		if (pieces.IsEmpty())
			framed.Clear();	// everything erased — signal a full removal
		return 1;
	}

	protected void ClosePiece(array<int> cur, array<ref array<int>> pieces)
	{
		if (cur.Count() >= 4)	// шматок = мін. 2 точки
		{
			array<int> keep = {};
			keep.Copy(cur);
			pieces.Insert(keep);
		}
		cur.Clear();
	}

	//! Найближчий штрих під екранною точкою (фіз. px) у межах порога (пів-ширини + slack).
	//! Для Del-видалення штриха під курсором. Повертає id або -1.
	//! curWX/curWZ — АКТУАЛЬНА світова позиція курсора (з ScreenToWorld шару). Для влучання по
	//! заливках (point-in-polygon) використовуємо саме її, бо кешована афінна застаріває при пані.
	int FindStrokeAtScreen(int px, int py, int curWX, int curWZ, bool haveWorld, float slackPx = 10)
	{
		if (!m_Map)
			return -1;
		array<SM_MapDrawingData> all = {};
		SM_MapDrawingStore.GetInstance().GetAll(all);

		// Callers hand us the cursor in SCREEN pixels; everything below compares against canvas-local
		// projections, and on an inset map (a tablet) those are not the same space.
		px -= m_fCanvasAbsX;
		py -= m_fCanvasAbsY;

		float ppm = PxPerMeter();
		int cwx = curWX;
		int cwz = curWZ;

		// Pick "from the top layer", matching the renderer: strokes draw ABOVE fills, newer
		// (higher id) above older. So among the hits take the stroke with the highest id, and
		// only if no stroke was hit — the fill with the highest id.
		// Careful: Local strokes have NEGATIVE ids (<= -2), so "-1 = none" plus a plain > compare
		// would never let a Local win. Track presence with separate flags instead; among Local
		// ids "newer" is still the higher (less negative) id.
		int bestStrokeId = -1;
		int bestFillId = -1;
		bool haveStroke = false;
		bool haveFill = false;
		foreach (SM_MapDrawingData d : all)
		{
			if (!d || d.GetPointCount() < 1)
				continue;
			if (d.m_iFill != 0)
			{
				// Fill: a hit = cursor inside the polygon.
				if (!haveWorld || !SM_MapFloodFill.PointInPolygon(d.m_aPoints, cwx, cwz))
					continue;
				if (!haveFill || d.m_iId > bestFillId)
				{
					bestFillId = d.m_iId;
					haveFill = true;
				}
			}
			else
			{
				float thr = WidthPxForZoom(d.m_iWidthIdx, ppm) * 0.5 + slackPx;
				if (ScreenDistSqToStroke(d, px, py) > thr * thr)
					continue;
				if (!haveStroke || d.m_iId > bestStrokeId)
				{
					bestStrokeId = d.m_iId;
					haveStroke = true;
				}
			}
		}
		if (haveStroke)
			return bestStrokeId;
		if (haveFill)
			return bestFillId;
		return -1;	// nothing hit
	}

	//! Дані штриха за id (для тултіпа автора).
	static SM_MapDrawingData GetStrokeData(int id)
	{
		return SM_MapDrawingStore.GetInstance().FindById(id);
	}

	// Мін. відстань² від точки до полілінії у СВІТОВИХ метрах (для гумки по чужому штриху).
	protected float WorldDistSqToStroke(notnull SM_MapDrawingData d, int ex, int ez)
	{
		// A shape's stored points are parameters; aim at its OUTLINE, which is what's on screen.
		array<int> pts = d.SM_HitPoints();
		int count = pts.Count() / 2;
		if (count < 1)
			return 1e12;
		int ax = pts[0];
		int az = pts[1];
		if (count < 2)	// крапка — відстань² до єдиної точки
		{
			float ddx = ex - ax;
			float ddz = ez - az;
			return ddx * ddx + ddz * ddz;
		}
		float best = -1;
		for (int i = 1; i < count; i++)
		{
			int bx = pts[i * 2];
			int bz = pts[i * 2 + 1];
			float dsq = PointToSegDistSq(ex, ez, ax, az, bx, bz);
			if (best < 0 || dsq < best)
				best = dsq;
			ax = bx;
			az = bz;
		}
		if (best < 0)
			return 1e12;
		return best;
	}

	//------------------------------------------------------------------------------
	// Рендер-внутрішнє

	// Синхронізувати кеш зі сховищем: прибрати зниклі, додати нові (вершини спроєктує ProjectAll).
	protected void SyncStrokes()
	{
		array<SM_MapDrawingData> all = {};
		SM_MapDrawingStore.GetInstance().GetAll(all);

		set<int> live = new set<int>();
		foreach (SM_MapDrawingData d : all)
		{
			if (d)
				live.Insert(d.m_iId);
		}

		array<int> dead = {};
		foreach (int id, SM_RenderStroke rs : m_mStrokes)
		{
			if (!live.Contains(id))
				dead.Insert(id);
		}
		foreach (int dropId : dead)
			m_mStrokes.Remove(dropId);

		foreach (SM_MapDrawingData d2 : all)
		{
			if (!d2 || d2.GetPointCount() < 1)
				continue;
			SM_RenderStroke ers = m_mStrokes.Get(d2.m_iId);
			if (ers)
			{
				ers.m_Data = d2;	// the store may have re-created the object under the same id
				continue;
			}

			SM_RenderStroke nrs = new SM_RenderStroke(d2.m_iId);
			nrs.m_Data = d2;
			if (d2.m_iFill != 0)
			{
				// Тріангуляція РАЗ у світових координатах — індекси переживають зум/пан.
				nrs.m_bFill = true;
				nrs.m_aTriIndices = new array<int>();
				if (!SM_MapFloodFill.Triangulate(d2.m_aPoints, nrs.m_aTriIndices))
					nrs.m_aTriIndices = null;	// вироджений контур → PolygonDrawCommand як запасний
			}
			m_mStrokes.Set(d2.m_iId, nrs);
		}

		// Порядок малювання: за зростанням id (= хронологія створення) → нові штрихи поверх старих.
		m_aOrder.Clear();
		foreach (int oid, SM_RenderStroke ors : m_mStrokes)
			m_aOrder.Insert(oid);
		m_aOrder.Sort();
	}

	// Повна проєкція світ→екран усіх штрихів. Раз на зум/зміну набору. Дешева: спершу рахуємо
	// афінне перетворення з 3 нативних викликів, далі всі точки — арифметикою.
	// Sorted by the AABB against the clip box: strokes outside it build nothing at all (zoomed in
	// that is nearly all of them), strokes fully inside skip clipping and allocate nothing, and only
	// the handful straddling the border actually get cut.
	protected void ProjectAll()
	{
		ComputeAffine();
		CaptureAnchor();
		float ppm = PxPerMeter();

		float wMinX, wMaxX, wMinZ, wMaxZ;
		ClipBoxWorld(wMinX, wMaxX, wMinZ, wMaxZ);

		// Zoomed far out the screen box can cover the whole map; an always-on tablet caps it with
		// its render radius instead. Drawings get cut at the radius edge — that's the deal.
		if (m_fRenderRadius > 0)
		{
			float ccx = (wMinX + wMaxX) * 0.5;	// the box is centred on the view
			float ccz = (wMinZ + wMaxZ) * 0.5;
			wMinX = Math.Max(wMinX, ccx - m_fRenderRadius);
			wMaxX = Math.Min(wMaxX, ccx + m_fRenderRadius);
			wMinZ = Math.Max(wMinZ, ccz - m_fRenderRadius);
			wMaxZ = Math.Min(wMaxZ, ccz + m_fRenderRadius);
		}

		m_bProjClipped = false;

		array<float> scr = {};
		foreach (int id, SM_RenderStroke rs : m_mStrokes)
		{
			SM_MapDrawingData d = rs.m_Data;
			if (!d)
				continue;

			rs.m_bCull = d.GetAABB(rs.m_iMinX, rs.m_iMaxX, rs.m_iMinZ, rs.m_iMaxZ);

			if (rs.m_bCull
				&& (rs.m_iMaxX < wMinX || rs.m_iMinX > wMaxX || rs.m_iMaxZ < wMinZ || rs.m_iMinZ > wMaxZ))
			{
				rs.m_aCmds.Clear();
				m_bProjClipped = true;	// what we cached now depends on where the box sat
				continue;
			}

			bool inside = rs.m_bCull
				&& rs.m_iMinX >= wMinX && rs.m_iMaxX <= wMaxX
				&& rs.m_iMinZ >= wMinZ && rs.m_iMaxZ <= wMaxZ;
			if (!inside)
				m_bProjClipped = true;

			if (d.m_iShape != 0)
				BuildShapeProjected(d, rs, ppm);	// parameters -> the same geometry on every client
			else if (rs.m_bFill)
				BuildFillProjected(d, rs, scr);
			else
			{
				ProjectPointsToScreen(d.m_aPoints, scr);
				BuildStrokeCommands(scr, d.m_iColor, WidthPxForZoom(d.m_iWidthIdx, ppm), rs.m_aCmds, inside);
			}
		}
	}

	// Outline fully inside the box: reuse the cached triangulation. Otherwise cut the outline in
	// WORLD space and triangulate what's left — clipping changes the vertex count, so the original
	// indices no longer describe it. Sutherland-Hodgman can degenerate on concave outlines; when the
	// result refuses to triangulate we fall back to the full outline, since large coordinates beat a
	// mangled or missing shape.
	protected void BuildFillProjected(notnull SM_MapDrawingData d, notnull SM_RenderStroke rs, array<float> scr)
	{
		rs.m_aCmds.Clear();

		// Triangulation is TOPOLOGY — it depends on the outline, not on the view — so it is done ONCE
		// when the stroke is synced (SyncStrokes) and reused for every projection. The old code instead
		// clipped the outline to the viewport and RE-TRIANGULATED whenever the fill stuck out past the
		// screen edge, which is exactly what happens the moment you zoom IN on a big fill: ear-clipping
		// is O(n²), and on a 1000-point contour that ran ~1M ops PER ZOOM FRAME — the freeze. No clip is
		// needed anyway; strokes already project their full point set off-screen and the map frame clips
		// the raster. Off-screen fills are culled whole by the AABB test in ProjectAll before we get here.
		if (!rs.m_aTriIndices || rs.m_aTriIndices.Count() < 3)
			return;
		ProjectPointsToScreen(d.m_aPoints, scr);
		BuildFillCommands(scr, d.m_iColor, rs.m_aTriIndices, rs);
	}

	// Команди заливки: TriMesh із тріангуляцією (коректно й для увігнутих контурів).
	// Якщо тріангуляція не вдалась (самоперетин/виродження) — НЕ рендеримо: рушійний PolygonDrawCommand
	// теж не тягне складні полігони й засипає консоль "triangulation failed" щокадру.
	protected void BuildFillCommands(array<float> scr, int argb, notnull array<int> tri, notnull SM_RenderStroke rs)
	{
		if (scr.Count() < 6 || tri.Count() < 3)
			return;

		TriMeshDrawCommand mesh = new TriMeshDrawCommand();
		mesh.m_iColor = argb;
		array<float> v = new array<float>();
		v.Copy(scr);
		mesh.m_Vertices = v;
		mesh.m_Indices = tri;	// спільний кеш; рендер його не мутує
		rs.m_aCmds.Insert(mesh);
	}

	// Обчислити афінні коефіцієнти світ→ПОЛОТНО. Беремо 3 кути полотна → ScreenToWorld (світові точки
	// на екрані) → WorldToScreen назад.
	// WorldToScreen's last argument is dpiScale, not a clamp: true everywhere, because
	// GetScreenPos/GetScreenSize/ScreenToWorld all speak physical pixels.
	// The affine's offsets fold in m_fCanvasInMap, so it emits CANVAS-local pixels directly.
	protected void ComputeAffine()
	{
		m_wCanvas.GetScreenSize(m_fScrW, m_fScrH);
		m_fClipMarginPx = Math.Clamp(Math.Max(m_fScrW, m_fScrH), 512, 2048);

		m_wCanvas.GetScreenPos(m_fCanvasAbsX, m_fCanvasAbsY);

		float mfAbsX = 0;
		float mfAbsY = 0;
		if (m_wMapFrame)
			m_wMapFrame.GetScreenPos(mfAbsX, mfAbsY);

		float mwAbsX = 0;
		float mwAbsY = 0;
		CanvasWidget mapW = m_Map.GetMapWidget();
		if (mapW)
			mapW.GetScreenPos(mwAbsX, mwAbsY);

		// ScreenToWorld takes FRAME-local; WorldToScreen hands back MAP-WIDGET-local.
		m_fCanvasInFrameX = m_fCanvasAbsX - mfAbsX;
		m_fCanvasInFrameY = m_fCanvasAbsY - mfAbsY;
		m_fMapToCanvasX = mwAbsX - m_fCanvasAbsX;
		m_fMapToCanvasY = mwAbsY - m_fCanvasAbsY;

		float bx = m_fCanvasInFrameX;
		float by = m_fCanvasInFrameY;

		float w0x, w0z, w1x, w1z, w2x, w2z;
		m_Map.ScreenToWorld(bx,             by,             w0x, w0z);
		m_Map.ScreenToWorld(bx + m_fScrW,   by,             w1x, w1z);
		m_Map.ScreenToWorld(bx,             by + m_fScrH,   w2x, w2z);

		int s0x, s0y, s1x, s1y, s2x, s2y;
		m_Map.WorldToScreen(w0x, w0z, s0x, s0y, true);
		m_Map.WorldToScreen(w1x, w1z, s1x, s1y, true);
		m_Map.WorldToScreen(w2x, w2z, s2x, s2y, true);

		float dwx = w1x - w0x;	// вздовж верхнього краю змінюється світовий X
		float dwz = w2z - w0z;	// вздовж лівого краю змінюється світовий Z
		if (dwx > -0.0001 && dwx < 0.0001) dwx = 0.0001;
		if (dwz > -0.0001 && dwz < 0.0001) dwz = 0.0001;

		// Діагональна модель (мапа north-up, без обертання): sx ← world X, sy ← world Z.
		m_fDxx = (s1x - s0x) / dwx;
		m_fDyz = (s2y - s0y) / dwz;
		m_fDyx = 0;
		m_fDxz = 0;
		// Fold map-widget -> canvas into the offsets, so the affine emits canvas-local pixels directly.
		m_fPox = s0x - w0x * m_fDxx + m_fMapToCanvasX;
		m_fPoy = s0y - w0z * m_fDyz + m_fMapToCanvasY;
	}

	// The anchor is the world point under the canvas corner, and it must stay PUT between
	// reprojections: the pan shift works by watching where it drifts to on screen.
	protected void CaptureAnchor()
	{
		m_fRefWX = -m_fPox / m_fDxx;
		m_fRefWZ = -m_fPoy / m_fDyz;
	}

	//! The map cursor in PHYSICAL SCREEN pixels.
	//!
	//! TRAP: SCR_MapCursorInfo.x/y is NOT a screen position on an inset map. On a gamepad it is
	//! MAP-FRAME-local, and with a mouse the engine does not keep it in screen space either — which is
	//! why GRS reads the real mouse through WidgetManager instead of trusting it. We mirror that: get
	//! a true screen position first, and let the caller move it into whatever space it needs.
	//! On the fullscreen map the frame sits at the origin, so this collapses to the old behaviour.
	static bool CursorPhys(Widget mapFrame, out float px, out float py)
	{
		px = 0;
		py = 0;

		if (SCR_MapCursorInfo.isGamepad)
		{
			float mfx = 0;
			float mfy = 0;
			if (mapFrame)
				mapFrame.GetScreenPos(mfx, mfy);
			px = SCR_MapCursorInfo.Scale(SCR_MapCursorInfo.x) + mfx;
			py = SCR_MapCursorInfo.Scale(SCR_MapCursorInfo.y) + mfy;
			return true;
		}

		int mx, my;
		WidgetManager.GetMousePos(mx, my);
		px = mx;
		py = my;
		return true;
	}

	//! The map cursor in MAP-FRAME-local physical pixels — the space ScreenToWorld takes.
	static bool CursorInFrame(Widget mapFrame, out float px, out float py)
	{
		if (!CursorPhys(mapFrame, px, py))
			return false;
		if (mapFrame)
		{
			float mfx, mfy;
			mapFrame.GetScreenPos(mfx, mfy);
			px -= mfx;
			py -= mfy;
		}
		return true;
	}

	//! Cursor -> world, by inverting OUR OWN affine instead of trusting ScreenToWorld's input space.
	//!
	//! Why not just call ScreenToWorld: it is not the inverse of WorldToScreen (different spaces, see
	//! the note on m_fCanvasInFrame), and exactly which space it wants is guesswork on an inset map —
	//! guessing wrong lands the stroke a few pixels off the brush circle, which is glaring with a thin
	//! brush. The affine, on the other hand, is built from WorldToScreen, which the markers prove
	//! correct, and it is the very thing that renders the stroke. Inverting it therefore GUARANTEES
	//! the stroke lands under the circle, whatever the engine thinks screen space is.
	//! The affine has no rotation, so inverting it is two divisions. false if it isn't built yet.
	bool CursorWorld(out int wx, out int wz)
	{
		if (!m_bViewValid || m_fDxx == 0 || m_fDyz == 0)
			return false;

		float px, py;
		if (!CursorPhys(m_wMapFrame, px, py))
			return false;

		// screen -> canvas-local, the space the affine emits
		float cx = px - m_fCanvasAbsX;
		float cy = py - m_fCanvasAbsY;

		wx = (cx - m_fPox) / m_fDxx;
		wz = (cy - m_fPoy) / m_fDyz;
		return true;
	}

	//! World -> CANVAS-local pixels. WorldToScreen alone lands in map-widget space; on an inset map
	//! (a tablet) that is not where our canvas is.
	protected void WorldToCanvas(int wx, int wz, out int sx, out int sy)
	{
		m_Map.WorldToScreen(wx, wz, sx, sy, true);
		sx += m_fMapToCanvasX;
		sy += m_fMapToCanvasY;
	}

	protected void ClipBox(out float loX, out float hiX, out float loY, out float hiY)
	{
		loX = -m_fClipMarginPx;
		hiX = m_fScrW + m_fClipMarginPx;
		loY = -m_fClipMarginPx;
		hiY = m_fScrH + m_fClipMarginPx;
	}

	// The same box in world metres. The affine has no rotation, so inverting it is a division.
	protected void ClipBoxWorld(out float wMinX, out float wMaxX, out float wMinZ, out float wMaxZ)
	{
		float loX, hiX, loY, hiY;
		ClipBox(loX, hiX, loY, hiY);

		float dxx = m_fDxx;
		float dyz = m_fDyz;
		if (dxx > -0.000001 && dxx < 0.000001) dxx = 0.000001;
		if (dyz > -0.000001 && dyz < 0.000001) dyz = 0.000001;

		float x0 = (loX - m_fPox) / dxx;
		float x1 = (hiX - m_fPox) / dxx;
		float z0 = (loY - m_fPoy) / dyz;
		float z1 = (hiY - m_fPoy) / dyz;

		wMinX = Math.Min(x0, x1);	// m_fDyz is negative on a north-up map, so sort
		wMaxX = Math.Max(x0, x1);
		wMinZ = Math.Min(z0, z1);
		wMaxZ = Math.Max(z0, z1);
	}

	// Plain arithmetic, no bounds applied. Far vertices come out huge; whoever builds the draw
	// commands clips them.
	protected void ProjectPointsToScreen(notnull array<int> pts, array<float> outScr)
	{
		int n = pts.Count() / 2;
		outScr.Resize(n * 2);
		for (int i = 0; i < n; i++)
		{
			float wx = pts[i * 2];
			float wz = pts[i * 2 + 1];
			outScr[i * 2]     = m_fPox + wx * m_fDxx + wz * m_fDxz;
			outScr[i * 2 + 1] = m_fPoy + wx * m_fDyx + wz * m_fDyz;
		}
	}

	// --- Clipping against the screen box -----------------------------------------------------

	//! Liang-Barsky. false = the segment misses the box entirely.
	protected bool ClipSegment(float x0, float y0, float x1, float y1,
		float loX, float hiX, float loY, float hiY,
		out float ox0, out float oy0, out float ox1, out float oy1)
	{
		float t0 = 0;
		float t1 = 1;
		float dx = x1 - x0;
		float dy = y1 - y0;

		for (int e = 0; e < 4; e++)
		{
			float pp, qq;
			if (e == 0)      { pp = -dx; qq = x0 - loX; }
			else if (e == 1) { pp =  dx; qq = hiX - x0; }
			else if (e == 2) { pp = -dy; qq = y0 - loY; }
			else             { pp =  dy; qq = hiY - y0; }

			if (pp > -0.000001 && pp < 0.000001)
			{
				if (qq < 0)
					return false;	// parallel to this edge and outside it
				continue;
			}
			float t = qq / pp;
			if (pp < 0)
			{
				if (t > t1) return false;
				if (t > t0) t0 = t;
			}
			else
			{
				if (t < t0) return false;
				if (t < t1) t1 = t;
			}
		}

		ox0 = x0 + t0 * dx;
		oy0 = y0 + t0 * dy;
		ox1 = x0 + t1 * dx;
		oy1 = y0 + t1 * dy;
		return true;
	}

	//! Split a polyline into the pieces that survive the box; each piece is a polyline of its own.
	protected void ClipPolylineToPieces(notnull array<float> p, float loX, float hiX, float loY, float hiY,
		out array<ref array<float>> pieces)
	{
		pieces = {};
		int n = p.Count() / 2;
		array<float> cur = new array<float>();

		for (int i = 0; i + 1 < n; i++)
		{
			float ax = p[i * 2];
			float ay = p[i * 2 + 1];
			float bx = p[(i + 1) * 2];
			float by = p[(i + 1) * 2 + 1];

			float cx0, cy0, cx1, cy1;
			if (!ClipSegment(ax, ay, bx, by, loX, hiX, loY, hiY, cx0, cy0, cx1, cy1))
			{
				if (cur.Count() >= 4)
					pieces.Insert(cur);
				cur = new array<float>();
				continue;
			}

			if (cur.IsEmpty())
			{
				cur.Insert(cx0);
				cur.Insert(cy0);
			}
			else
			{
				int m = cur.Count();
				// Re-entered the box somewhere else than we left it, so this is a new piece.
				if (Math.AbsFloat(cur[m - 2] - cx0) > 0.01 || Math.AbsFloat(cur[m - 1] - cy0) > 0.01)
				{
					if (cur.Count() >= 4)
						pieces.Insert(cur);
					cur = new array<float>();
					cur.Insert(cx0);
					cur.Insert(cy0);
				}
			}
			cur.Insert(cx1);
			cur.Insert(cy1);

			// The segment got cut on the way out, so the piece ends here.
			if (Math.AbsFloat(cx1 - bx) > 0.01 || Math.AbsFloat(cy1 - by) > 0.01)
			{
				pieces.Insert(cur);
				cur = new array<float>();
			}
		}

		if (cur.Count() >= 4)
			pieces.Insert(cur);
	}

	// Ядро виправлення «шипів»: будуємо draw-команди штриха з екранних точок.
	// Полілінію РІЖЕМО на гострих кутах (щоб LineDrawCommand не давав miter-«шипа» на розворотах),
	// а стики між шматками + обидва кінці закриваємо ЗАЛИТИМИ КОЛАМИ — це дає круглі joins/caps.
	// 1 точка → одне коло (крапка).
	//
	// LOD (продуктивність на 500-1000+ штрихів, коли видно весь малюнок): (1) точки, ближчі за
	// SM_DECIM_PX на ЕКРАНІ, зливаємо — за поточного зуму це суб-піксельна деталізація, її не видно,
	// але вона коштує вершин; (2) коли лінія на екрані тонша за SM_ROUND_MIN_PX — не малюємо кола-стики
	// (їх однаково не видно), лишається ~1 команда на штрих. Обидва LOD залежать від зуму й
	// перебудовуються у ProjectAll; на робочому наближенні (мало штрихів) якість повна.
	//! noClip = the caller already proved the stroke fits the box (AABB test), skip the cutting.
	protected void BuildStrokeCommands(array<float> pRaw, int argb, float widthPx, array<ref CanvasWidgetCommand> outCmds, bool noClip = false)
	{
		outCmds.Clear();
		int nRaw = pRaw.Count() / 2;
		if (nRaw <= 0)
			return;

		if (noClip)
		{
			BuildStrokePiece(pRaw, argb, widthPx, outCmds);
			return;
		}

		float loX, hiX, loY, hiY;
		ClipBox(loX, hiX, loY, hiY);

		if (nRaw == 1)
		{
			float r1 = widthPx * 0.5;
			if (r1 < 0.6)
				r1 = 0.6;
			if (pRaw[0] >= loX && pRaw[0] <= hiX && pRaw[1] >= loY && pRaw[1] <= hiY)
				outCmds.Insert(MakeCircle(pRaw[0], pRaw[1], r1, argb));	// крапка
			return;
		}

		// Zoomed in, a stroke can run hundreds of thousands of pixels off-screen.
		array<ref array<float>> pieces;
		ClipPolylineToPieces(pRaw, loX, hiX, loY, hiY, pieces);
		foreach (array<float> piece : pieces)
			BuildStrokePiece(piece, argb, widthPx, outCmds);
	}

	// Commands for one already-clipped piece. Appends to outCmds rather than clearing it.
	protected void BuildStrokePiece(array<float> pRaw, int argb, float widthPx, array<ref CanvasWidgetCommand> outCmds)
	{
		int nRaw = pRaw.Count() / 2;
		if (nRaw <= 0)
			return;
		float r = widthPx * 0.5;
		if (r < 0.6)
			r = 0.6;

		if (nRaw == 1)
		{
			outCmds.Insert(MakeCircle(pRaw[0], pRaw[1], r, argb));	// крапка
			return;
		}

		array<float> p = DecimateScreen(pRaw, SM_DECIM_PX);
		int n = p.Count() / 2;
		if (n == 1)
		{
			outCmds.Insert(MakeCircle(p[0], p[1], r, argb));
			return;
		}

		bool round = widthPx >= SM_ROUND_MIN_PX;	// малювати круглі стики/шапки?

		int start = 0;
		for (int i = 1; i < n - 1; i++)
		{
			float d1x = p[i * 2]     - p[(i - 1) * 2];
			float d1y = p[i * 2 + 1] - p[(i - 1) * 2 + 1];
			float d2x = p[(i + 1) * 2]     - p[i * 2];
			float d2y = p[(i + 1) * 2 + 1] - p[i * 2 + 1];
			float l1 = Math.Sqrt(d1x * d1x + d1y * d1y);
			float l2 = Math.Sqrt(d2x * d2x + d2y * d2y);

			bool split;
			if (l1 < 0.01 || l2 < 0.01)
				split = true;	// вироджений сегмент (накладання при розвороті) — теж ріжемо
			else
				split = ((d1x * d2x + d1y * d2y) / (l1 * l2)) < SM_SHARP_DOT;

			if (split)
			{
				if (i > start)
					outCmds.Insert(MakeLine(p, start, i, argb, widthPx));
				if (round)
					outCmds.Insert(MakeCircle(p[i * 2], p[i * 2 + 1], r, argb));	// круглий стик
				start = i;
			}
		}
		if (n - 1 > start)
			outCmds.Insert(MakeLine(p, start, n - 1, argb, widthPx));

		// круглі шапки на обох кінцях (лише коли достатньо товсто, щоб їх було видно)
		if (round)
		{
			outCmds.Insert(MakeCircle(p[0], p[1], r, argb));
			outCmds.Insert(MakeCircle(p[(n - 1) * 2], p[(n - 1) * 2 + 1], r, argb));
		}
	}

	// LOD-децимація: лишаємо першу й останню точки та ті, що далі за minPx (екранних) від попередньої
	// лишеної. За малого зуму зливає щільні суб-піксельні точки; за великого — майже нічого не викидає.
	protected array<float> DecimateScreen(array<float> p, float minPx)
	{
		int n = p.Count() / 2;
		array<float> outp = {};
		if (n <= 0)
			return outp;
		float minSq = minPx * minPx;
		float lastX = p[0];
		float lastY = p[1];
		outp.Insert(lastX);
		outp.Insert(lastY);
		for (int i = 1; i < n - 1; i++)
		{
			float dx = p[i * 2]     - lastX;
			float dy = p[i * 2 + 1] - lastY;
			if (dx * dx + dy * dy >= minSq)
			{
				lastX = p[i * 2];
				lastY = p[i * 2 + 1];
				outp.Insert(lastX);
				outp.Insert(lastY);
			}
		}
		if (n >= 2)	// завжди зберігаємо останню точку
		{
			outp.Insert(p[(n - 1) * 2]);
			outp.Insert(p[(n - 1) * 2 + 1]);
		}
		return outp;
	}

	// Лінія по підмножині екранних точок p[a..b] (включно).
	protected LineDrawCommand MakeLine(array<float> p, int a, int b, int argb, float w)
	{
		LineDrawCommand cmd = new LineDrawCommand();
		cmd.m_iColor = argb;
		cmd.m_fWidth = w;
		array<float> v = new array<float>();
		v.Resize((b - a + 1) * 2);
		int k = 0;
		for (int i = a; i <= b; i++)
		{
			v[k]     = p[i * 2];
			v[k + 1] = p[i * 2 + 1];
			k += 2;
		}
		cmd.m_Vertices = v;
		return cmd;
	}

	// Залите коло (PolygonDrawCommand без текстури = заливка) — круглий join/cap/крапка.
	protected PolygonDrawCommand MakeCircle(float cx, float cy, float r, int argb)
	{
		PolygonDrawCommand cmd = new PolygonDrawCommand();
		cmd.m_iColor = argb;
		array<float> v = new array<float>();
		v.Resize(SM_CIRCLE_SEG * 2);
		float step = (Math.PI * 2) / SM_CIRCLE_SEG;
		for (int i = 0; i < SM_CIRCLE_SEG; i++)
		{
			float a = i * step;
			v[i * 2]     = cx + Math.Cos(a) * r;
			v[i * 2 + 1] = cy + Math.Sin(a) * r;
		}
		cmd.m_Vertices = v;
		return cmd;
	}

	// Дешевий зсув кешованих вершин на пан (без проєкцій). Прев'ю не чіпаємо — воно щокадрове.
	protected void TranslateAll(int dx, int dy)
	{
		if (dx == 0 && dy == 0)
			return;
		foreach (int id, SM_RenderStroke rs : m_mStrokes)
			TranslateCmds(rs.m_aCmds, dx, dy);
	}

	// Зсунути вершини всіх команд штриха (ліній і кіл) на дельту.
	protected void TranslateCmds(array<ref CanvasWidgetCommand> cmds, int dx, int dy)
	{
		foreach (CanvasWidgetCommand c : cmds)
		{
			array<float> v = null;
			LineDrawCommand lc = LineDrawCommand.Cast(c);
			if (lc)
				v = lc.m_Vertices;
			else
			{
				PolygonDrawCommand pc = PolygonDrawCommand.Cast(c);
				if (pc)
					v = pc.m_Vertices;
				else
				{
					TriMeshDrawCommand mc = TriMeshDrawCommand.Cast(c);
					if (mc)
						v = mc.m_Vertices;
				}
			}
			if (!v)
				continue;
			int cnt = v.Count();
			for (int i = 0; i < cnt; i += 2)
			{
				v[i]     = v[i] + dx;
				v[i + 1] = v[i + 1] + dy;
			}
		}
	}

	protected void ProjectPreview()
	{
		if (m_aCapture.Count() < 2)
		{
			m_aPreviewCmds.Clear();
			return;
		}
		int n = m_aCapture.Count() / 2;
		array<float> scr = {};
		scr.Resize(n * 2);
		for (int i = 0; i < n; i++)
		{
			int sx, sy;
			WorldToCanvas(m_aCapture[i * 2], m_aCapture[i * 2 + 1], sx, sy);
			scr[i * 2]     = sx;
			scr[i * 2 + 1] = sy;
		}
		BuildStrokeCommands(scr, ApplyOpacity(s_iColor), WidthPxForZoom(s_iWidthIdx, PxPerMeter()), m_aPreviewCmds);
	}

	// Збірка команд закомічених штрихів у головне полотно. Викликається на зум/зміну набору/пан.
	// Дорожча частина — тесселяція в рушії всередині SetDrawCommands; LOD тримає к-сть команд/вершин низькою.
	protected void PushCommitted(float cw, float ch)
	{
		m_wCanvas.SetSizeInUnits(Vector(cw, ch, 0));
		m_wCanvas.SetZoom(1.0);
		m_wCanvas.SetOffsetPx(Vector(0, 0, 0));

		m_aCommands.Clear();
		if (m_bRenderEnabled)	// GM: показ малюнків вимкнено → пустий список (штрихи зникають)
		{
			// Куллінг: пропускаємо штрихи повністю поза видимою областю (виграш при наближенні).
			float vMinX, vMaxX, vMinZ, vMaxZ;
			bool cull = ComputeVisibleWorldRect(vMinX, vMaxX, vMinZ, vMaxZ);
			const int CULL_SLACK = 64;	// м: макс. пів-ширина штриха + запас

			// Два проходи в порядку id (нові поверх старих): СПОЧАТКУ заливки, ПОТІМ штрихи —
			// заливка ніколи не закриває лінії (як розмальовка: фарба під чорнилом).
			for (int pass = 0; pass < 2; pass++)
			{
				bool wantFill = (pass == 0);
				foreach (int id : m_aOrder)
				{
					SM_RenderStroke rs = m_mStrokes.Get(id);
					if (!rs || rs.m_bFill != wantFill)
						continue;
					if (cull && rs.m_bCull
						&& (rs.m_iMaxX + CULL_SLACK < vMinX || rs.m_iMinX - CULL_SLACK > vMaxX
						 || rs.m_iMaxZ + CULL_SLACK < vMinZ || rs.m_iMinZ - CULL_SLACK > vMaxZ))
						continue;	// повністю поза екраном
					foreach (CanvasWidgetCommand c : rs.m_aCmds)
						m_aCommands.Insert(c);
				}
			}
		}

		m_wCanvas.SetDrawCommands(m_aCommands);
	}

	// Прев'ю штриха «в процесі» на окремому полотні (offset=0, проєктується наживо щокадру).
	protected void PushPreview(float cw, float ch)
	{
		if (!m_wPreviewCanvas)
			return;
		m_wPreviewCanvas.SetSizeInUnits(Vector(cw, ch, 0));
		m_wPreviewCanvas.SetZoom(1.0);
		m_wPreviewCanvas.SetOffsetPx(Vector(0, 0, 0));
		m_wPreviewCanvas.SetDrawCommands(m_aPreviewCmds);	// масив-член — лишається живим
	}

	// Видима світова область (метри) = зворотна проєкція протилежних кутів полотна.
	// ScreenToWorld takes MAP-FRAME-local pixels, so the canvas corners go in as frame coords.
	protected bool ComputeVisibleWorldRect(out float minX, out float maxX, out float minZ, out float maxZ)
	{
		if (!m_wCanvas || !m_Map)
			return false;
		float canvasW, canvasH;
		m_wCanvas.GetScreenSize(canvasW, canvasH);
		if (canvasW <= 0 || canvasH <= 0)
			return false;

		float lx0 = m_fCanvasInFrameX;
		float ly0 = m_fCanvasInFrameY;
		float lx1 = lx0 + canvasW;
		float ly1 = ly0 + canvasH;

		// Осі світу не обов'язково збігаються з осями екрана (Z може бути «перевернутим») — нормалізуємо Min/Max.
		float wx0, wz0, wx1, wz1;
		m_Map.ScreenToWorld(lx0, ly0, wx0, wz0);
		m_Map.ScreenToWorld(lx1, ly1, wx1, wz1);
		minX = Math.Min(wx0, wx1);
		maxX = Math.Max(wx0, wx1);
		minZ = Math.Min(wz0, wz1);
		maxZ = Math.Max(wz0, wz1);
		return true;
	}

	//------------------------------------------------------------------------------
	// Геометрія гумки (стандартна: відстань точки до відрізка, в екранних px)
	//! px/py are CANVAS-local physical pixels (FindStrokeAtScreen converts the cursor for us).
	protected float ScreenDistSqToStroke(notnull SM_MapDrawingData d, int px, int py)
	{
		array<int> pts = d.SM_HitPoints();	// shapes hand back their outline here
		int count = pts.Count() / 2;
		if (count < 1)
			return 1e12;
		int prevSx, prevSy;
		WorldToCanvas(pts[0], pts[1], prevSx, prevSy);
		if (count < 2)	// крапка — відстань² до єдиної екранної точки
		{
			float ddx = px - prevSx;
			float ddy = py - prevSy;
			return ddx * ddx + ddy * ddy;
		}

		float best = -1;
		for (int i = 1; i < count; i++)
		{
			int sx, sy;
			WorldToCanvas(pts[i * 2], pts[i * 2 + 1], sx, sy);
			float dsq = PointToSegDistSq(px, py, prevSx, prevSy, sx, sy);
			if (best < 0 || dsq < best)
				best = dsq;
			prevSx = sx;
			prevSy = sy;
		}
		if (best < 0)
			return 1e12;
		return best;
	}

	protected float PointToSegDistSq(float px, float py, float ax, float ay, float bx, float by)
	{
		float dx = bx - ax;
		float dy = by - ay;
		float lenSq = dx * dx + dy * dy;
		if (lenSq < 0.0001)
		{
			float pdx = px - ax;
			float pdy = py - ay;
			return pdx * pdx + pdy * pdy;
		}
		float t = ((px - ax) * dx + (py - ay) * dy) / lenSq;
		if (t < 0) t = 0;
		else if (t > 1) t = 1;
		float cx = ax + t * dx;
		float cy = ay + t * dy;
		float ex = px - cx;
		float ey = py - cy;
		return ex * ex + ey * ey;
	}
}
