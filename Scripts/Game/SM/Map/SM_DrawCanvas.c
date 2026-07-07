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
	protected int   m_iAnchorSx, m_iAnchorSy;	// екранна позиція світової опори (0,0) на момент тесселяції

	// Кешовані коефіцієнти афінної проєкції світ→екран (масштаб+зсув, без обертання): рахуються раз
	// на ProjectAll із 3 референсних WorldToScreen, далі всі точки проєктуємо арифметикою (без нативних викликів).
	protected float m_fPox, m_fPoy;	// екранна проєкція світової (0,0)
	protected float m_fDxx, m_fDyx;	// приріст екрана (x,y) на 1 м світового X
	protected float m_fDxz, m_fDyz;	// приріст екрана (x,y) на 1 м світового Z
	protected float m_fScrW, m_fScrH;	// розмір полотна на момент проєкції (для захисного clamp)
	protected const float SM_CLAMP_MARGIN = 2048;	// запас навколо екрана; далі тиснемо (велетенські координати ламають рендер)

	// Захоплення штриха (світові метри, парами)
	protected ref array<int> m_aCapture = {};
	protected bool m_bCapturing;
	protected int  m_iLastSampleX, m_iLastSampleZ;
	protected int  m_iMaxCapturePts;	// стеля точок штриха (= конфіг); далі не малюємо
	protected const float SAMPLE_MIN_DIST = 3.0;	// мін. світова відстань між семплами (м)
	protected const float ERASE_SLACK_PX  = 2.0;	// невеликий доп. поріг гумки (фіз. px)

	// Гумка (фотошопна): тягнеш — стирає ВСІ штрихи, яких торкається круг гумки.
	// Один RPC на штрих за перетяг (сет захищає від спаму, поки сервер підтверджує).
	protected ref set<int> m_ErasedThisDrag = new set<int>();
	protected bool m_bErasing;
	protected bool m_bLineStroke;	// Shift+олівець: пряма від точки натиску до відпуску

	// Курсор-кружечок пензля/гумки: фізичний розмір штриха на мапі за поточного зуму.
	protected ImageWidget m_wCursor;
	protected int m_iCursorTool = -1;	// яка текстура зараз завантажена (-1 = жодна)
	protected const ResourceName CURSOR_TEX_PENCIL = "{E278DB118260244A}circleWhite.edds";
	protected const ResourceName CURSOR_TEX_ERASER = "{B72B0A3DAD46568C}circleLine.edds";

	// 5 пресетів ширини у СВІТОВИХ метрах (паперова поведінка: товщина прив'язана до землі,
	// тож широка лінія завжди закриває велику ділянку; на екрані товщає при наближенні).
	static float WidthMeters(int idx)
	{
		switch (idx)
		{
			case 0: return 2;
			case 1: return 5;
			case 2: return 10;
			case 3: return 20;
			case 4: return 40;
		}
		return 5;
	}
	static int ClampWidthIdx(int i)
	{
		if (i < 0) return 0;
		if (i > 4) return 4;
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
	void SetActive(bool a)    { m_bActive = a; if (!a) CancelStroke(); }
	void SetTool(int t)       { m_iTool = t; }
	int  GetTool()            { return m_iTool; }
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
		if (!m_wCanvas || !m_Map)
			return;

		float cw, ch;
		m_wCanvas.GetScreenSize(cw, ch);
		if (cw <= 0 || ch <= 0)
			return;	// ще не розкладено

		bool reproject = false;
		bool changed   = false;

		if (m_bMembershipDirty)
		{
			SyncStrokes();
			m_bMembershipDirty = false;
			reproject = true;	// нові штрихи треба спроєктувати
		}

		float zoom = m_Map.GetCurrentZoom();
		int ax, ay;
		m_Map.WorldToScreen(0, 0, ax, ay, true);

		// Зміна виду: зум → повна (але дешева, афінна) проєкція; чистий пан → зсув кешованих вершин.
		if (!m_bViewValid || zoom != m_fLastZoom)
		{
			reproject = true;
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
			ProjectAll();
			m_fLastZoom  = zoom;
			m_iAnchorSx  = ax;
			m_iAnchorSy  = ay;
			m_bViewValid = true;
			changed = true;
		}

		if (changed)
			PushCommitted(cw, ch);

		// Прев'ю штриха в процесі — на окремому легкому полотні, перебудова щокадру під час малювання.
		if (m_bCapturing)
		{
			ProjectPreview();
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

		m_wCursor.SetVisible(true);
		FrameSlot.SetSize(m_wCursor, sizeUnscaled, sizeUnscaled);
		FrameSlot.SetPos(m_wCursor, SCR_MapCursorInfo.x, SCR_MapCursorInfo.y);
	}

	//------------------------------------------------------------------------------
	// Захоплення (кличе перехоплення ЛКМ у шарі зі світовими координатами курсора)

	//! lineMode = true (Shift затиснуто на натиску): пряма від точки натиску до точки відпуску.
	void OnPressDown(int wx, int wz, bool lineMode = false)
	{
		if (m_iTool == 1)
		{
			// Гумка: починаємо «мазок стирання» — стираємо все, чого торкається круг.
			m_ErasedThisDrag.Clear();
			m_bErasing = true;
			EraseHit(wx, wz);
			return;
		}
		if (m_iTool == 2)
		{
			HandleFillClick(wx, wz);	// заливка — одиночний клік, без перетягування
			return;
		}
		m_aCapture.Clear();
		m_aCapture.Insert(wx);
		m_aCapture.Insert(wz);
		m_iLastSampleX = wx;
		m_iLastSampleZ = wz;
		m_bCapturing   = true;
		m_bLineStroke  = lineMode;
		m_iMaxCapturePts = SM_MarkerConfig.GetInstance().m_iDrawMaxPointsPerStroke;
	}

	void OnDrag(int wx, int wz)
	{
		if (m_iTool == 1)
		{
			if (m_bErasing)
				EraseHit(wx, wz);	// тягнеш гумку — стирає по дорозі
			return;
		}
		if (m_iTool == 2)
			return;	// заливка не тягнеться
		if (!m_bCapturing)
			return;
		// Режим прямої: тримаємо рівно 2 точки — старт і поточну (прев'ю «гумова нитка»).
		if (m_bLineStroke)
		{
			m_aCapture.Resize(2);
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

	void OnRelease(int wx, int wz)
	{
		if (m_iTool == 1)
		{
			m_bErasing = false;
			m_ErasedThisDrag.Clear();
			return;
		}
		if (m_iTool == 2)
			return;
		if (!m_bCapturing)
			return;
		m_bCapturing = false;
		if (m_bLineStroke)
		{
			// Пряма: рівно 2 точки — старт + точка відпуску.
			m_aCapture.Resize(2);
			m_aCapture.Insert(wx);
			m_aCapture.Insert(wz);
			m_bLineStroke = false;
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
		m_aCapture.Clear();
		m_aPreviewCmds.Clear();
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
		array<int> meta = tmp.PackMeta();

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (pc)
			pc.SM_DrawRequestAdd(meta, simplified);

		m_aCapture.Clear();
		m_aPreviewCmds.Clear();	// закоммічений штрих прийде по мережі
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

		// 2) Розлив.
		int maxPts = SM_MarkerConfig.GetInstance().m_iDrawMaxPointsPerStroke;
		if (maxPts <= 0)
			maxPts = 200;
		SM_FloodFillResult res;
		int code = SM_MapFloodFill.Compute(wx, wz, all, maxPts, res);

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
		pc.SM_DrawRequestAdd(tmp.PackMeta(), res.m_aContour);
	}

	// Гумка (фотошопна): стирає лише те, ЧОГО ТОРКАЄТЬСЯ круг гумки. Для ВЛАСНИХ штрихів —
	// часткове стирання: вирізаємо накриті кругом вершини/сегменти, а решту шматків штрих
	// розрізається (один RPC «заміни штрих на шматки»). Чужі штрихи не чіпаємо (GM стирає
	// цілком, кліком). Уся геометрія — у СВІТОВИХ метрах: поріг = радіус гумки + пів-ширини штриха.
	protected void EraseHit(int wx, int wz)
	{
		if (!m_Map)
			return;

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!pc)
			return;
		int myId = pc.GetPlayerId();

		array<SM_MapDrawingData> all = {};
		SM_MapDrawingStore.GetInstance().GetAll(all);

		float eraserRad = WidthMeters(s_iWidthIdx) * 0.5;

		foreach (SM_MapDrawingData d : all)
		{
			if (!d || d.GetPointCount() < 1)
				continue;
			if (m_ErasedThisDrag.Contains(d.m_iId))
				continue;
			// Залочений зевсом штрих у не-GM гумка тихо оминає (сервер однаково відхилить; Del покаже чому).
			if (d.m_iGmLocked != 0 && !m_bEditorMap)
				continue;

			float thr = eraserRad + WidthMeters(d.m_iWidthIdx) * 0.5;
			float thrSq = thr * thr;

			if (d.m_iFill != 0)
				continue;	// гумка заливки НЕ чіпає (інакше зачепиш заливку під лініями, які правиш); видалення — Del/X

			if (d.m_iOwnerId != myId)
			{
				// Чужий штрих: власник-не-я → часткове різати не можемо (права в сервера).
				// GM може стерти цілком — шлемо повне видалення, сервер перевірить права.
				if (WorldDistSqToStroke(d, wx, wz) <= thrSq)
				{
					m_ErasedThisDrag.Insert(d.m_iId);
					pc.SM_DrawRequestRemove(d.m_iId);
				}
				continue;
			}

			// Власний штрих: обчислюємо шматки, що лишаються поза кругом гумки.
			array<int> framed = {};
			int change = SplitStrokeByEraser(d, wx, wz, thrSq, framed);
			if (change == 0)
				continue;	// не торкнулись

			m_ErasedThisDrag.Insert(d.m_iId);
			if (framed.IsEmpty())
				pc.SM_DrawRequestRemove(d.m_iId);	// стерли все
			else
				pc.SM_DrawRequestErasePart(d.m_iId, framed);	// заміна штриха на шматки
		}
	}

	// Розрізати полілінію кругом гумки (центр wx,wz; поріг² thrSq, метри).
	// Вершина видаляється, якщо в крузі; додатково рвемо шматок між двома ЗОВНІШНІМИ вершинами,
	// якщо сам сегмент проходить крізь круг (швидкий мах гумкою поперек лінії).
	// out framed: [кількість_шматків, довжина1(точок), x,z,..., довжина2, ...]. Повертає 1 якщо щось зрізано, 0 — ні.
	protected int SplitStrokeByEraser(notnull SM_MapDrawingData d, int ex, int ez, float thrSq, array<int> framed)
	{
		int n = d.GetPointCount();
		array<ref array<int>> pieces = {};
		array<int> cur = {};
		bool changed = false;

		for (int i = 0; i < n; i++)
		{
			int px, pz;
			d.GetPoint(i, px, pz);
			float dx = px - ex;
			float dz = pz - ez;
			bool inside = (dx * dx + dz * dz) <= thrSq;

			if (inside)
			{
				changed = true;
				ClosePiece(cur, pieces);
				continue;
			}

			cur.Insert(px);
			cur.Insert(pz);

			// Обидві вершини зовні, але сегмент i→i+1 проходить крізь круг → розрив.
			if (i < n - 1)
			{
				int qx, qz;
				d.GetPoint(i + 1, qx, qz);
				float qdx = qx - ex;
				float qdz = qz - ez;
				if ((qdx * qdx + qdz * qdz) > thrSq
					&& PointToSegDistSq(ex, ez, px, pz, qx, qz) <= thrSq)
				{
					changed = true;
					ClosePiece(cur, pieces);
				}
			}
		}
		ClosePiece(cur, pieces);

		if (!changed)
			return 0;

		framed.Clear();
		framed.Insert(pieces.Count());
		foreach (array<int> p : pieces)
		{
			framed.Insert(p.Count() / 2);
			for (int k = 0; k < p.Count(); k++)
				framed.Insert(p[k]);
		}
		if (pieces.IsEmpty())
			framed.Clear();	// стерто все — сигнал «повне видалення»
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

		float ppm = PxPerMeter();
		int cwx = curWX;
		int cwz = curWZ;

		// Вибір «з верхнього шару» — як рендер: штрихи малюються НАД заливками, новіші (більший id)
		// над старішими. Тому серед влучань беремо штрих із найбільшим id; якщо жодного штриха
		// не влучили — заливку з найбільшим id.
		int bestStrokeId = -1;
		int bestFillId = -1;
		foreach (SM_MapDrawingData d : all)
		{
			if (!d || d.GetPointCount() < 1)
				continue;
			if (d.m_iFill != 0)
			{
				// Заливка: влучання = курсор всередині полігона.
				if (!haveWorld || !SM_MapFloodFill.PointInPolygon(d.m_aPoints, cwx, cwz))
					continue;
				if (d.m_iId > bestFillId)
					bestFillId = d.m_iId;
			}
			else
			{
				float thr = WidthPxForZoom(d.m_iWidthIdx, ppm) * 0.5 + slackPx;
				if (ScreenDistSqToStroke(d, px, py) > thr * thr)
					continue;
				if (d.m_iId > bestStrokeId)
					bestStrokeId = d.m_iId;
			}
		}
		if (bestStrokeId >= 0)
			return bestStrokeId;
		return bestFillId;
	}

	//! Дані штриха за id (для тултіпа автора).
	static SM_MapDrawingData GetStrokeData(int id)
	{
		return SM_MapDrawingStore.GetInstance().FindById(id);
	}

	// Мін. відстань² від точки до полілінії у СВІТОВИХ метрах (для гумки по чужому штриху).
	protected float WorldDistSqToStroke(notnull SM_MapDrawingData d, int ex, int ez)
	{
		int count = d.GetPointCount();
		int ax, az;
		d.GetPoint(0, ax, az);
		if (count < 2)	// крапка — відстань² до єдиної точки
		{
			float ddx = ex - ax;
			float ddz = ez - az;
			return ddx * ddx + ddz * ddz;
		}
		float best = -1;
		for (int i = 1; i < count; i++)
		{
			int bx, bz;
			d.GetPoint(i, bx, bz);
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
			if (!m_mStrokes.Contains(d2.m_iId))
			{
				SM_RenderStroke nrs = new SM_RenderStroke(d2.m_iId);
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
		}

		// Порядок малювання: за зростанням id (= хронологія створення) → нові штрихи поверх старих.
		m_aOrder.Clear();
		foreach (int oid, SM_RenderStroke ors : m_mStrokes)
			m_aOrder.Insert(oid);
		m_aOrder.Sort();
	}

	// Повна проєкція світ→екран усіх штрихів. Раз на зум/зміну набору. Дешева: спершу рахуємо
	// афінне перетворення з 3 нативних викликів, далі всі точки — арифметикою.
	protected void ProjectAll()
	{
		ComputeAffine();
		float ppm = PxPerMeter();
		array<float> scr = {};
		foreach (int id, SM_RenderStroke rs : m_mStrokes)
		{
			SM_MapDrawingData d = SM_MapDrawingStore.GetInstance().FindById(id);
			if (!d)
				continue;
			ProjectPointsToScreen(d, scr);
			if (rs.m_bFill)
				BuildFillCommands(scr, d.m_iColor, rs);
			else
				BuildStrokeCommands(scr, d.m_iColor, WidthPxForZoom(d.m_iWidthIdx, ppm), rs.m_aCmds);
			rs.m_bCull = d.GetAABB(rs.m_iMinX, rs.m_iMaxX, rs.m_iMinZ, rs.m_iMaxZ);
		}
	}

	// Команди заливки: TriMesh із нашою кешованою тріангуляцією (коректно й для увігнутих контурів).
	// Якщо тріангуляція не вдалась (самоперетин/виродження) — НЕ рендеримо: рушійний PolygonDrawCommand
	// теж не тягне складні полігони й засипає консоль "triangulation failed" щокадру.
	protected void BuildFillCommands(array<float> scr, int argb, notnull SM_RenderStroke rs)
	{
		rs.m_aCmds.Clear();
		if (scr.Count() < 6 || !rs.m_aTriIndices || rs.m_aTriIndices.Count() < 3)
			return;

		TriMeshDrawCommand mesh = new TriMeshDrawCommand();
		mesh.m_iColor = argb;
		array<float> v = new array<float>();
		v.Copy(scr);
		mesh.m_Vertices = v;
		mesh.m_Indices = rs.m_aTriIndices;	// спільний кеш; рендер його не мутує
		rs.m_aCmds.Insert(mesh);
	}

	// Обчислити афінні коефіцієнти світ→екран. КРИТИЧНО: опорні точки мусять бути НА ЕКРАНІ й проєктовані
	// тим самим WorldToScreen(clamp=TRUE), що й вершини штрихів — інакше масштаб не збігається з мапою і
	// малюнок «їздить» при зумі. Тому: беремо 3 кути полотна → ScreenToWorld (світові точки на екрані) →
	// WorldToScreen(clamp=true) (для on-screen точок clamp не спрацьовує → точні значення у просторі вершин).
	protected void ComputeAffine()
	{
		m_wCanvas.GetScreenSize(m_fScrW, m_fScrH);

		float cAbsX, cAbsY, mfAbsX, mfAbsY;
		m_wCanvas.GetScreenPos(cAbsX, cAbsY);
		m_wMapFrame.GetScreenPos(mfAbsX, mfAbsY);
		float bx = cAbsX - mfAbsX;	// frame-local (простір, який приймає ScreenToWorld)
		float by = cAbsY - mfAbsY;

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
		m_fPox = s0x - w0x * m_fDxx;
		m_fPoy = s0y - w0z * m_fDyz;
	}

	// Спроєктувати всі точки штриха арифметикою (без нативних викликів). Захисний clamp навколо екрана
	// з запасом: on-screen точки не чіпає, а далекі тримає в межах (велетенські координати ламають LineDrawCommand).
	protected void ProjectPointsToScreen(notnull SM_MapDrawingData d, array<float> outScr)
	{
		int n = d.GetPointCount();
		outScr.Resize(n * 2);
		float loX = -SM_CLAMP_MARGIN, hiX = m_fScrW + SM_CLAMP_MARGIN;
		float loY = -SM_CLAMP_MARGIN, hiY = m_fScrH + SM_CLAMP_MARGIN;
		for (int i = 0; i < n; i++)
		{
			int wx, wz;
			d.GetPoint(i, wx, wz);
			float sx = m_fPox + wx * m_fDxx + wz * m_fDxz;
			float sy = m_fPoy + wx * m_fDyx + wz * m_fDyz;
			if (sx < loX) sx = loX; else if (sx > hiX) sx = hiX;
			if (sy < loY) sy = loY; else if (sy > hiY) sy = hiY;
			outScr[i * 2]     = sx;
			outScr[i * 2 + 1] = sy;
		}
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
	protected void BuildStrokeCommands(array<float> pRaw, int argb, float widthPx, array<ref CanvasWidgetCommand> outCmds)
	{
		outCmds.Clear();
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
			m_Map.WorldToScreen(m_aCapture[i * 2], m_aCapture[i * 2 + 1], sx, sy, true);
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
	// Canvas — дитина map-frame, тож ScreenToWorld приймає frame-local координати (кути полотна мінус позиція фрейму).
	protected bool ComputeVisibleWorldRect(out float minX, out float maxX, out float minZ, out float maxZ)
	{
		if (!m_wCanvas || !m_wMapFrame || !m_Map)
			return false;
		float canvasAbsX, canvasAbsY;
		m_wCanvas.GetScreenPos(canvasAbsX, canvasAbsY);
		float canvasW, canvasH;
		m_wCanvas.GetScreenSize(canvasW, canvasH);
		if (canvasW <= 0 || canvasH <= 0)
			return false;
		float mfAbsX, mfAbsY;
		m_wMapFrame.GetScreenPos(mfAbsX, mfAbsY);

		float lx0 = canvasAbsX - mfAbsX;
		float ly0 = canvasAbsY - mfAbsY;
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
	protected float ScreenDistSqToStroke(notnull SM_MapDrawingData d, int px, int py)
	{
		int count = d.GetPointCount();
		int x0, z0;
		d.GetPoint(0, x0, z0);
		int prevSx, prevSy;
		m_Map.WorldToScreen(x0, z0, prevSx, prevSy, true);
		if (count < 2)	// крапка — відстань² до єдиної екранної точки
		{
			float ddx = px - prevSx;
			float ddy = py - prevSy;
			return ddx * ddx + ddy * ddy;
		}

		float best = -1;
		for (int i = 1; i < count; i++)
		{
			int wx, wz;
			d.GetPoint(i, wx, wz);
			int sx, sy;
			m_Map.WorldToScreen(wx, wz, sx, sy, true);
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
