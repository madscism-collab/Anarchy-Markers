// Панель малювання — завантажує layout AnarchyMapDrawingBox і привʼязує функціонал.
// Елементи layout (по іменах):
//   PencilButton / EraseButton — інструменти (клік активного — вимикає режим);
//   SizeCombo:  SizeButton (опенер) + SizeDropdown із Size1..Size5 (= 2/5/10/20/40 м, idx 0..4);
//   ColorCombo: ColorButton (опенер) + ColorDropdown із Color0..Color13 (білі кружечки — тонуємо кодом);
//   VisibilityCombo: VisibilityButton (опенер) + VisibilityLocal/Group/Side/Everyone.
// Кнопки — прості ButtonWidget без SCR-компонентів, тож клік ловимо ScriptedWidgetEventHandler-ом.

class SM_DrawCellHandler : ScriptedWidgetEventHandler
{
	protected SM_DrawPanel m_Panel;
	protected int m_iAction;
	protected int m_iParam;

	void Setup(SM_DrawPanel panel, int action, int param)
	{
		m_Panel   = panel;
		m_iAction = action;
		m_iParam  = param;
	}

	override bool OnClick(Widget w, int x, int y, int button)
	{
		if (m_Panel)
			m_Panel.OnAction(m_iAction, m_iParam);
		return true;
	}
}

class SM_DrawPanel
{
	protected const ResourceName PANEL_LAYOUT = "{456A01ACDE43719F}UI/layouts/Map/AnarchyMapDrawingBox.layout";

	// Дії кнопок
	static const int ACT_PENCIL     = 0;
	static const int ACT_ERASER     = 1;
	static const int ACT_WIDTH      = 2;	// param = idx 0..4
	static const int ACT_COLOR      = 3;	// param = idx 0..13
	static const int ACT_CHANNEL    = 4;	// param = SM_EMarkerVisibility
	static const int ACT_OPEN_SIZE  = 5;
	static const int ACT_OPEN_COLOR = 6;
	static const int ACT_OPEN_VIS   = 7;
	// GM-контроли (лише в мапі редактора)
	static const int ACT_OPEN_SIDE  = 8;
	static const int ACT_GMSIDE     = 9;	// param: 0=BLUFOR 1=OPFOR 2=INDFOR
	static const int ACT_GMLOCK     = 10;
	static const int ACT_GMHIDE     = 11;
	static const int ACT_FILL       = 12;	// інструмент «Заливка»
	static const int ACT_TEMPLATES     = 14;	// інструмент «Темплейти» — розмістити й авто-намалювати
	static const int ACT_TEMPLATE_SAVE = 15;	// інструмент «Виділити й зберегти як темплейт»
	static const int ACT_OPEN_TEMPLATES = 16;	// відкрити/закрити список темплейтів
	static const int ACT_TPL_SLOT       = 17;	// клік по слоту (param = індекс слота)
	static const int ACT_TPL_APPLY      = 18;	// «Apply and place»
	static const int ACT_TPL_CANCEL     = 19;	// «Cancel»
	static const int ACT_TPL_ADD        = 20;	// «Add new template»
	static const int ACT_TPL_REMOVE     = 21;	// «Remove template»
	static const int ACT_OPEN_OPACITY = 13;	// відкрити дропдаун прозорості

	// Палітра кольорів малювання (ARGB) — кружечки Color0..ColorN у layout, КОЛОНКАМИ ПО 7.
	// Кожна колонка = родина відтінку, тони від світлого (згори) до темного (знизу);
	// колонки зліва направо: сірі → червоні → помаранчеві → жовті → зелені → бірюзові →
	// сині → фіолетові → рожеві → коричневі. Разом 70 (під Color0..Color69, сітка 10×7).
	// Кількість кнопок у layout може бути МЕНШОЮ за палітру — зайві кольори просто недоступні.
	static ref array<int> COLORS = {
		// колонка 0 — сірі (білий → чорний)
		0xFFFFFFFF, 0xFFE0E0E0, 0xFFBDBDBD, 0xFF9E9E9E, 0xFF757575, 0xFF424242, 0xFF000000,
		// колонка 1 — червоні
		0xFFFFCDD2, 0xFFEF9A9A, 0xFFE57373, 0xFFF44336, 0xFFD32F2F, 0xFFB71C1C, 0xFF7F0000,
		// колонка 2 — помаранчеві
		0xFFFFE0B2, 0xFFFFCC80, 0xFFFFA726, 0xFFFF9800, 0xFFF57C00, 0xFFE65100, 0xFFA33F00,
		// колонка 3 — жовті
		0xFFFFF9C4, 0xFFFFF176, 0xFFFFEE58, 0xFFFDD835, 0xFFFBC02D, 0xFFF9A825, 0xFFC49000,
		// колонка 4 — зелені
		0xFFC8E6C9, 0xFF81C784, 0xFF4CAF50, 0xFF43A047, 0xFF2E7D32, 0xFF1B5E20, 0xFF0E3D12,
		// колонка 5 — бірюзові/теал
		0xFFB2EBF2, 0xFF4DD0E1, 0xFF00BCD4, 0xFF00ACC1, 0xFF0097A7, 0xFF00695C, 0xFF003D33,
		// колонка 6 — сині
		0xFFBBDEFB, 0xFF64B5F6, 0xFF2196F3, 0xFF1E88E5, 0xFF1565C0, 0xFF0D47A1, 0xFF0D2C6E,
		// колонка 7 — фіолетові/індиго
		0xFFD1C4E9, 0xFF9575CD, 0xFF7E57C2, 0xFF673AB7, 0xFF512DA8, 0xFF4527A0, 0xFF1A237E,
		// колонка 8 — рожеві/малинові
		0xFFF8BBD0, 0xFFF06292, 0xFFEC407A, 0xFFD81B60, 0xFFC2185B, 0xFF880E4F, 0xFF560027,
		// колонка 9 — коричневі
		0xFFD7CCC8, 0xFFBCAAA4, 0xFFA1887F, 0xFF8D6E63, 0xFF6D4C41, 0xFF4E342E, 0xFF3E2723
	};

	// Кольори каналів (з layout: Local сірий, Group зелений, Side темно-синій, Everyone червоний)
	protected static int VisColor(int vis)
	{
		switch (vis)
		{
			case SM_EMarkerVisibility.PERSONAL: return 0xFF525252;
			case SM_EMarkerVisibility.GROUP:    return 0xFF129031;
			case SM_EMarkerVisibility.FACTION:  return 0xFF002080;
			case SM_EMarkerVisibility.ALL:      return 0xFF801010;
		}
		return 0xFF525252;
	}

	protected static string VisLabel(int vis)
	{
		switch (vis)
		{
			case SM_EMarkerVisibility.PERSONAL: return "Local";
			case SM_EMarkerVisibility.GROUP:    return "Group";
			case SM_EMarkerVisibility.FACTION:  return "Side";
			case SM_EMarkerVisibility.ALL:      return "Everyone";
		}
		return "?";
	}

	// Текстури кружечків розміру: олівець — заливка, гумка — контур.
	protected const ResourceName TEX_CIRCLE      = "{46DF3F67AC741C13}circle.edds";
	protected const ResourceName TEX_CIRCLE_LINE = "{B72B0A3DAD46568C}circleLine.edds";

	protected SM_DrawCanvas m_Canvas;
	protected Widget m_wRoot;
	protected Widget m_wToolbar;		// ToolbarWrapper — видима смуга (для IsCursorOver)
	protected Widget m_wMapFrame;		// map-frame — під ним живе незалежна від панелі підказка
	protected Widget m_wHintBox;		// стовпчик пад-підказок ліворуч від панелі
	protected Widget m_wHintR1;			// R1 — Drawing Panel / Switch panel/map
	protected Widget m_wHintA;			// A — Select (лише коли в панелі)
	protected Widget m_wHintB;			// B — Back (лише коли в панелі)
	protected int    m_iHintMode = -1;	// 0 приховано; 1 idle (вгорі-центр); 2 канвас+інструмент (ліворуч); 3 у панелі (ліворуч, +A/B)
	protected ref array<ButtonWidget> m_aSizeItems = {};	// Size1..Size6 (Size6 = 100 m, eraser-only; also for the per-tool texture swap)
	protected int m_iSizeTexTool = -1;	// який інструмент відображають кружечки зараз
	protected Widget m_wSizeDropdownBg;	// size dropdown background — grows +50 on Y while Size6 is shown (eraser)
	// Size dropdown background height (from layout: 5 items = 257) and +50 for the 6th item (eraser).
	protected const float SIZE_BG_H_BASE   = 257;
	protected const float SIZE_BG_H_ERASER = 307;

	protected ButtonWidget m_wPencil;
	protected ButtonWidget m_wErase;
	protected ButtonWidget m_wFill;	// опційний: якщо в лейауті нема FillButton — інструмент недоступний з UI
	protected Widget m_wSizeDropdown;
	protected Widget m_wColorDropdown;
	protected int m_iForcedVis = -1;	// host pinned the channel; -1 = the player picks (the normal map)
	protected Widget m_wVisDropdown;
	protected Widget m_wOpacityDropdown;	// опційний (слайдер прозорості)

	// Templates. 8 columns of 10 slots. The slot BUTTONS all share the names Template0..Template9
	// inside their own column, so they can only be resolved per column — FindAnyWidget on the root
	// would hand back the first column's button eight times over.
	protected Widget m_wTemplatesDropdown;
	protected Widget m_wTemplatesOpener;
	protected CanvasWidget m_wTplPreview;					// big preview under the list
	protected Widget m_wTplApply, m_wTplCancel, m_wTplAdd, m_wTplRemove;
	protected Widget m_wTplChrome;	// the dropdown's own background
	protected Widget m_wTplNameOverlay;			// name field, shown only while saving
	protected EditBoxWidget m_wTplNameEdit;		// the input widget inside the WLib_EditBox prefab
	protected SCR_EditBoxComponent m_TplNameComp;	// present if the field carries the SCR wrapper (as the marker dialog does)
	protected bool m_bTplNaming;				// modal: entering the template name, all map input suspended
	protected bool m_bTemplatesFeature = true;	// this map screen asked for the Templates tab (AM_EMapFeature.TEMPLATES)
	protected ref SM_TemplateDeleteDialog m_TplDeleteDialog;	// modal: "really delete?" is up
	protected ref array<Widget> m_aTplGlyphs = {};	// pad glyphs, one to the LEFT of each template button
	protected const int TPL_NAME_MAX = 26;
	protected ref array<ref CanvasWidgetCommand> m_aTplPreviewCmds = {};	// member: the canvas keeps a reference
	protected ref array<Widget> m_aTplCols  = {};	// VerticalLayout0..7
	protected ref array<Widget> m_aTplSlots = {};	// flat, column-major: col0 slots 0..9, col1 10..19, ...
	protected int m_iTplHighlight = -1;				// which slot the player has picked, -1 = none
	protected int m_iTplState = -1;					// change gate for TickTemplateState

	protected const int TPL_COLS      = 8;
	protected const int TPL_PER_COL   = 10;
	protected const float TPL_PREVIEW_PAD     = 10;	// px kept clear of the canvas edge
	protected const float TPL_PREVIEW_LINE_PX = 3;	// px, fixed: see BuildTemplatePreview
	// The box the preview may grow into. It takes the template's OWN proportions inside this, so a
	// wide template gets a wide canvas and a tall one a tall canvas — no letterboxing either way.
	protected const float TPL_PREVIEW_MAX_W = 640;
	protected const float TPL_PREVIEW_MAX_H = 420;
	protected const float TPL_PREVIEW_MIN   = 80;	// a template that is a straight line still needs a rect
	protected ButtonWidget m_wOpacityOpener;
	protected SliderWidget m_wOpacitySlider;
	protected ButtonWidget m_wSizeOpener;
	protected ButtonWidget m_wColorOpener;
	protected ButtonWidget m_wVisOpener;

	// GM-контроли (зевс): вибір сторони для Side-штрихів + чекбокси Locked / Hide info.
	protected bool   m_bEditorMap;
	protected Widget m_wSideCombo;
	protected Widget m_wSideDropdown;	// у layout зветься "VisibilityDropdown" ВСЕРЕДИНІ SideCombo (дубль імені)
	protected ButtonWidget m_wSideOpener;	// так само дубль "VisibilityButton" всередині SideCombo
	protected Widget m_wGmLockRow;
	protected Widget m_wHideInfoRow;
	protected CheckBoxWidget m_wGmLockCheck;
	protected CheckBoxWidget m_wHideInfoCheck;

	protected ref array<ref SM_DrawCellHandler> m_aHandlers = {};	// тримаємо живими
	protected ref array<Widget> m_aWired = {};				// усі клікабельні (для пад-фокуса)
	protected ref SM_OpacitySliderHandler m_OpacityHandler;	// хендлер слайдера прозорості (живий тут)
	// Модель пад-навігації (замкнена в панелі — рушійна просторова вимкнена, бо тікала на ванільні віджети)
	protected ref array<Widget> m_aTopRow    = {};	// Pencil, Erase, Size, Color, Visibility, (Side)
	protected ref array<Widget> m_aColorItems = {};	// Color0..13 (2 колонки по 7)
	protected ref array<Widget> m_aVisItems   = {};	// Local, Group, Side, Everyone
	protected ref array<Widget> m_aSideItems  = {};	// BLUFOR, OPFOR, INDFOR
	protected bool m_bNavL, m_bNavR, m_bNavU, m_bNavD;	// фронти напрямків для власної навігації
	protected bool m_bPadFocus;	// геймпад веде фокус по панелі (не миша)
	protected int   m_iOpacityHeldDir = 0;		// напрямок утримання для швидкого регулювання прозорості (-1/0/1)
	protected float m_fOpacityNextRepeat = 0;	// час наступного авто-повтору
	protected const float SM_OPACITY_REPEAT_DELAY = 0.30;		// пауза перед авто-повтором
	protected const float SM_OPACITY_REPEAT_INTERVAL = 0.045;	// крок авто-повтору (швидко)
	protected ref map<Widget, int> m_mFocusBase = new map<Widget, int>();	// базовий колір віджета (відновлення після фокуса)
	protected Widget m_wLastFocusHl;						// кому зараз намальовано фокус-підсвітку

	// Нейтральний і підсвічений колір фону кнопок-інструментів (Background1 у layout: 0.0123 ≈ #030303)
	protected const int BG_IDLE  = 0xFF030303;
	protected const int BG_ARMED = 0xFFC87818;	// бурштин

	// Підсвітити/зняти підсвітку заокругленого фону кнопки.
	protected void TintBg(Widget btn, bool selected)
	{
		if (!btn)
			return;
		Widget bg = btn.FindAnyWidget("Background1");
		if (!bg)
			return;
		if (selected)
			bg.SetColor(Color.FromInt(BG_ARMED));
		else
			bg.SetColor(Color.FromInt(BG_IDLE));
	}

	//------------------------------------------------------------------------------
	//! forcedVis >= 0: the host screen owns the audience (an ATAK-style tablet scopes everything to the
	//! player's faction), so the channel picker comes off the panel entirely — leaving a Group/Everyone
	//! switch there would lie to the player about who ends up seeing the drawing.
	void Build(notnull SM_DrawCanvas canvas, notnull Widget mapFrame, bool editorMap = false, int forcedVis = -1, bool templatesFeature = true)
	{
		m_Canvas = canvas;
		m_bEditorMap = editorMap;
		m_wMapFrame = mapFrame;
		m_iForcedVis = forcedVis;
		m_bTemplatesFeature = templatesFeature;
		WorkspaceWidget ws = GetGame().GetWorkspace();

		m_wRoot = ws.CreateWidgets(PANEL_LAYOUT, mapFrame);
		if (!m_wRoot)
		{
			Print("[SM] Draw panel layout failed to load", LogLevel.WARNING);
			return;
		}
		m_wRoot.SetZOrder(100);	// clears the vanilla fullscreen map

		m_wToolbar = m_wRoot.FindAnyWidget("ToolbarWrapper");

		// Інструменти
		m_wPencil = ButtonWidget.Cast(m_wRoot.FindAnyWidget("PencilButton"));
		m_wErase  = ButtonWidget.Cast(m_wRoot.FindAnyWidget("EraseButton"));
		m_wFill   = ButtonWidget.Cast(m_wRoot.FindAnyWidget("FillButton"));
		Wire(m_wPencil, ACT_PENCIL, 0);
		Wire(m_wErase,  ACT_ERASER, 0);
		Wire(m_wFill,   ACT_FILL,   0);	// Wire ігнорує null

		// Width: opener + 6 items (Size1..Size5 = idx 0..4 for all tools; Size6 = idx 5, 100 m, ERASER-ONLY).
		m_wSizeOpener   = ButtonWidget.Cast(m_wRoot.FindAnyWidget("SizeButton"));
		m_wSizeDropdown = m_wRoot.FindAnyWidget("SizeDropdown");
		if (m_wSizeDropdown)
			m_wSizeDropdownBg = m_wSizeDropdown.FindAnyWidget("Background1");
		Wire(m_wSizeOpener, ACT_OPEN_SIZE, 0);
		m_aSizeItems.Clear();
		for (int i = 0; i <= SM_DrawCanvas.WIDTH_IDX_MAX_ERASER; i++)
		{
			ButtonWidget sb = ButtonWidget.Cast(m_wRoot.FindAnyWidget("Size" + (i + 1).ToString()));
			Wire(sb, ACT_WIDTH, i);
			m_aSizeItems.Insert(sb);
		}

		// Колір: опенер + 14 кружечків (тонуємо палітрою)
		m_wColorOpener   = ButtonWidget.Cast(m_wRoot.FindAnyWidget("ColorButton"));
		BuildTemplates();

		m_wColorDropdown = m_wRoot.FindAnyWidget("ColorDropdown");
		Wire(m_wColorOpener, ACT_OPEN_COLOR, 0);
		for (int c = 0; c < COLORS.Count(); c++)
		{
			ButtonWidget cb = ButtonWidget.Cast(m_wRoot.FindAnyWidget("Color" + c.ToString()));
			Wire(cb, ACT_COLOR, c);
			TintChildImage(cb, COLORS[c]);
		}

		// GM-контроли — ШУКАЄМО ПЕРШИМИ, зсередини SideCombo: у layout опенер сторони та її дропдаун
		// мають дубльовані імена ("VisibilityButton"/"VisibilityDropdown"), тож пошук від кореня дав би
		// канальні. Пошук від SideCombo знаходить саме внутрішні.
		m_wSideCombo = m_wRoot.FindAnyWidget("SideCombo");
		if (m_wSideCombo)
		{
			m_wSideDropdown = m_wSideCombo.FindAnyWidget("VisibilityDropdown");
			m_wSideOpener   = ButtonWidget.Cast(m_wSideCombo.FindAnyWidget("VisibilityButton"));
			Wire(m_wSideOpener, ACT_OPEN_SIDE, 0);
			Wire(ButtonWidget.Cast(m_wSideCombo.FindAnyWidget("SideBLUFOR")), ACT_GMSIDE, 0);
			Wire(ButtonWidget.Cast(m_wSideCombo.FindAnyWidget("SideOPFOR")),  ACT_GMSIDE, 1);
			Wire(ButtonWidget.Cast(m_wSideCombo.FindAnyWidget("SideINDFOR")), ACT_GMSIDE, 2);
		}
		m_wGmLockRow    = m_wRoot.FindAnyWidget("GmLockRow");
		m_wHideInfoRow  = m_wRoot.FindAnyWidget("HideInfoRow");
		m_wGmLockCheck  = CheckBoxWidget.Cast(m_wRoot.FindAnyWidget("GmLockCheck"));
		m_wHideInfoCheck = CheckBoxWidget.Cast(m_wRoot.FindAnyWidget("HideInfoCheck"));
		Wire(m_wGmLockCheck,   ACT_GMLOCK, 0);
		Wire(m_wHideInfoCheck, ACT_GMHIDE, 0);
		if (!m_bEditorMap)	// гравцям GM-контролів не видно взагалі
		{
			if (m_wSideCombo)   m_wSideCombo.SetVisible(false);
			if (m_wGmLockRow)   m_wGmLockRow.SetVisible(false);
			if (m_wHideInfoRow) m_wHideInfoRow.SetVisible(false);
		}
		else
		{
			// Зевс: ряди галочок у layout приховані за замовчуванням ("Is Visible" 0) — вмикаємо.
			if (m_wGmLockRow)   m_wGmLockRow.SetVisible(true);
			if (m_wHideInfoRow) m_wHideInfoRow.SetVisible(true);

			// Сторона за замовчуванням = BLUFOR: щоб зевс не малював у Side, «не обравши» сторону
			// (канал -1 бачив би лише він сам).
			EnsureSideDefault();

			// У зевса немає групи — пункт Group у списку каналів ховаємо; якщо пензель
			// запамʼятав Group із гравецької мапи, перемикаємо на Side.
			Widget grpBtn = m_wRoot.FindAnyWidget("VisibilityGroup");
			if (grpBtn)
				grpBtn.SetVisible(false);
			if (m_Canvas.GetVisibility() == SM_EMarkerVisibility.GROUP)
				m_Canvas.SetVisibility(SM_EMarkerVisibility.FACTION);

			// Зсуваємо тулбар нижче — зверху GM-мапи компас/панель редактора (перетиналися).
			Widget bar = m_wRoot.FindAnyWidget("ToolbarWrapper");
			if (bar)
				FrameSlot.SetPos(bar, 0, 130);
		}

		// Канал: опенер + 4 пункти (пошук від кореня знаходить ПЕРШІ в дереві = канальні, вони йдуть раніше SideCombo)
		m_wVisOpener   = ButtonWidget.Cast(m_wRoot.FindAnyWidget("VisibilityButton"));
		m_wVisDropdown = m_wRoot.FindAnyWidget("VisibilityDropdown");
		Wire(m_wVisOpener, ACT_OPEN_VIS, 0);

		// Host pinned the channel: pin the brush to it and take the picker off the panel. Null the
		// opener too, so it never lands in the gamepad row below and nothing can re-open the dropdown.
		if (m_iForcedVis >= 0)
		{
			m_Canvas.SetVisibility(m_iForcedVis);
			Widget visCombo = m_wRoot.FindAnyWidget("VisibilityCombo");
			if (visCombo)
				visCombo.SetVisible(false);
			else if (m_wVisOpener)
				m_wVisOpener.SetVisible(false);
			m_wVisOpener = null;
			m_wVisDropdown = null;
		}

		// Прозорість (опційний блок OpacityCombo: опенер + дропдаун зі слайдером 0..100)
		m_wOpacityOpener   = ButtonWidget.Cast(m_wRoot.FindAnyWidget("OpacityButton"));
		m_wOpacityDropdown = m_wRoot.FindAnyWidget("OpacityDropdown");
		Wire(m_wOpacityOpener, ACT_OPEN_OPACITY, 0);
		if (m_wOpacityDropdown)
		{
			m_wOpacitySlider = SliderWidget.Cast(m_wOpacityDropdown.FindAnyWidget("Slider0"));
			if (m_wOpacitySlider)
			{
				m_wOpacitySlider.SetFlags(WidgetFlags.NOFOCUS);	// пад ним не фокусує (керуємо через опенер);
																// інакше після виходу рушій автофокусив слайдер і діпад крутив його. Миші не заважає.
				m_wOpacitySlider.SetRange(10, 100);	// нижня межа 10% — невидиме малювання = плутанина
				m_wOpacitySlider.SetStep(5);
				if (m_Canvas)
					m_wOpacitySlider.SetCurrent(m_Canvas.GetOpacityPct());	// пензель статичний — відновлюємо
				m_OpacityHandler = new SM_OpacitySliderHandler();
				m_OpacityHandler.Setup(this);
				m_wOpacitySlider.AddHandler(m_OpacityHandler);
			}
		}
		Wire(ButtonWidget.Cast(m_wRoot.FindAnyWidget("VisibilityLocal")),    ACT_CHANNEL, SM_EMarkerVisibility.PERSONAL);
		Wire(ButtonWidget.Cast(m_wRoot.FindAnyWidget("VisibilityGroup")),    ACT_CHANNEL, SM_EMarkerVisibility.GROUP);
		Wire(ButtonWidget.Cast(m_wRoot.FindAnyWidget("VisibilitySide")),     ACT_CHANNEL, SM_EMarkerVisibility.FACTION);
		Wire(ButtonWidget.Cast(m_wRoot.FindAnyWidget("VisibilityEveryone")), ACT_CHANNEL, SM_EMarkerVisibility.ALL);

		// Кнопки тулбара — прозорі: їхній квадратний фон «пробивався» у виїмках кутиків
		// заокругленого Background1 (візуал дає лише SmartPanel, кнопка — тільки клік-зона).
		MakeButtonTransparent(m_wPencil);
		MakeButtonTransparent(m_wErase);
		MakeButtonTransparent(m_wFill);
		MakeButtonTransparent(m_wSizeOpener);
		MakeButtonTransparent(m_wColorOpener);
		MakeButtonTransparent(m_wVisOpener);
		MakeButtonTransparent(m_wOpacityOpener);

		// Модель пад-навігації: верхній ряд + списки. Рушійну просторову навігацію на наших
		// віджетах вимикаємо повністю — інакше вгору/вниз/вліво стрибали на ванільні focusable
		// (меню інструментів мапи тощо); рух веде наша HandleNavInput.
		m_aTopRow.Clear();
		m_aTopRow.Insert(m_wPencil);
		m_aTopRow.Insert(m_wErase);
		if (m_wFill)
			m_aTopRow.Insert(m_wFill);
		m_aTopRow.Insert(m_wSizeOpener);
		m_aTopRow.Insert(m_wColorOpener);
		if (m_wOpacityOpener)
			m_aTopRow.Insert(m_wOpacityOpener);
		if (m_wVisOpener)	// null when the host pinned the channel — the picker isn't on the panel
			m_aTopRow.Insert(m_wVisOpener);
		if (m_wTemplatesOpener)
			m_aTopRow.Insert(m_wTemplatesOpener);	// hidden when the server disallows — NavRowVisible skips it
		if (m_bEditorMap && m_wSideOpener)
			m_aTopRow.Insert(m_wSideOpener);

		m_aColorItems.Clear();
		for (int cc = 0; cc < COLORS.Count(); cc++)
		{
			Widget cw = m_wRoot.FindAnyWidget("Color" + cc.ToString());
			if (cw)	// лише наявні в layout — сітка навігації має бути суцільною
				m_aColorItems.Insert(cw);
		}

		m_aVisItems.Clear();
		m_aVisItems.Insert(m_wRoot.FindAnyWidget("VisibilityLocal"));
		m_aVisItems.Insert(m_wRoot.FindAnyWidget("VisibilityGroup"));
		m_aVisItems.Insert(m_wRoot.FindAnyWidget("VisibilitySide"));
		m_aVisItems.Insert(m_wRoot.FindAnyWidget("VisibilityEveryone"));

		m_aSideItems.Clear();
		if (m_wSideCombo)
		{
			m_aSideItems.Insert(m_wSideCombo.FindAnyWidget("SideBLUFOR"));
			m_aSideItems.Insert(m_wSideCombo.FindAnyWidget("SideOPFOR"));
			m_aSideItems.Insert(m_wSideCombo.FindAnyWidget("SideINDFOR"));
		}

		foreach (Widget wnav : m_aWired)
			DisableEngineNav(wnav);

		// За замовчуванням кнопки НЕфокусні: рушій не може сам поставити на них фокус
		// (стік-меню-навігація з порожнього фокуса блимала підсвіткою і давала A випадково
		// «обирати» елементи без входу в меню). Focusable вони стають лише на час пад-входу (LB).
		SetPadFocusMode(false);

		CloseDropdowns();
		BuildPanelHint();
		UpdateVisuals();
	}

	protected const int SM_HINT_SIZE = 46;	// висота рядка підказки в px (більша за дефолтну)

	// Пад-підказки (R1/A/B). Живуть під map-frame, а не під панеллю, щоб показуватись
	// і коли панель прихована. Чисто візуальні: SM_MakeDisplayOnly прибирає слухачів дій.
	protected void BuildPanelHint()
	{
		if (!m_wMapFrame)
			return;
		WorkspaceWidget ws = GetGame().GetWorkspace();
		m_wHintBox = ws.CreateWidget(WidgetType.VerticalLayoutWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(0x00000000), 0, m_wMapFrame);
		if (!m_wHintBox)
			return;
		m_wHintBox.SetZOrder(120);	// над панеллю
		FrameSlot.SetAnchorMin(m_wHintBox, 0, 0);
		FrameSlot.SetAnchorMax(m_wHintBox, 0, 0);
		FrameSlot.SetSizeToContent(m_wHintBox, true);

		m_wHintR1 = MakeHintRow("AM_PanelFocus", "Drawing Panel");
		m_wHintA  = MakeHintRow("MenuSelect",    "Select");
		m_wHintB  = MakeHintRow("MenuBack",      "Back");
		m_wHintBox.SetVisible(false);
	}

	protected Widget MakeHintRow(string action, string label)
	{
		Widget row = GetGame().GetWorkspace().CreateWidgets(
			"{CB8563509DEF3E0E}UI/layouts/WidgetLibrary/Buttons/WLib_NavigationButtonSmall.layout", m_wHintBox);
		if (!row)
			return null;
		row.SetFlags(WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS);
		LayoutSlot.SetHorizontalAlign(row, LayoutHorizontalAlign.Left);	// гліфи в стовпчик
		SCR_InputButtonComponent c = SCR_InputButtonComponent.Cast(row.FindHandler(SCR_InputButtonComponent));
		if (c)
		{
			c.SetAction(action);
			c.SetLabel(label);
			c.SetSize(SM_HINT_SIZE);
			c.SetClickSoundDisabled(true);
			c.SM_MakeDisplayOnly();
		}
		return row;
	}

	protected void SetHintRowLabel(Widget row, string label)
	{
		if (!row)
			return;
		SCR_InputButtonComponent c = SCR_InputButtonComponent.Cast(row.FindHandler(SCR_InputButtonComponent));
		if (c)
			c.SetLabel(label);
	}

	// Геометрія тулбару відносно map-frame (для позиціонування підказок). false якщо ще не готово.
	protected bool ToolbarRel(out float leftX, out float centerX, out float topY, out float midY)
	{
		if (!m_wToolbar || !m_wMapFrame)
			return false;
		WorkspaceWidget ws = GetGame().GetWorkspace();
		float tx, ty, tw, th, fx, fy;
		m_wToolbar.GetScreenPos(tx, ty);
		m_wToolbar.GetScreenSize(tw, th);
		m_wMapFrame.GetScreenPos(fx, fy);
		leftX   = ws.DPIUnscale(tx - fx);
		centerX = ws.DPIUnscale(tx - fx + tw * 0.5);
		topY    = ws.DPIUnscale(ty - fy);
		midY    = ws.DPIUnscale(ty - fy + th * 0.5);
		return true;
	}

	//! Режим підказок: 0 сховано, 1 idle (вгорі), 2 канвас з інструментом, 3 у панелі (+A/B).
	void SetHintMode(int mode)
	{
		if (!m_wHintBox)
			return;
		if (mode == 0)
		{
			m_wHintBox.SetVisible(false);
			m_iHintMode = 0;
			return;
		}
		if (mode != m_iHintMode)	// текст/склад/вирівнювання міняємо лише на зміні режиму
		{
			m_iHintMode = mode;
			bool inPanel = (mode == 3);
			if (m_wHintA) m_wHintA.SetVisible(inPanel);
			if (m_wHintB) m_wHintB.SetVisible(inPanel);
			if (mode == 1)
			{
				SetHintRowLabel(m_wHintR1, "Drawing Panel");
				FrameSlot.SetAlignment(m_wHintBox, 0.5, 0.5);	// центр стовпчика — на центрі тулбару
			}
			else
			{
				SetHintRowLabel(m_wHintR1, "Switch panel / map");
				FrameSlot.SetAlignment(m_wHintBox, 1, 0);	// правий-верхній кут — на лівому краю тулбару
			}
		}
		float leftX, centerX, topY, midY;
		if (ToolbarRel(leftX, centerX, topY, midY))
		{
			if (mode == 1)
				FrameSlot.SetPos(m_wHintBox, centerX, midY);
			else
				FrameSlot.SetPos(m_wHintBox, leftX - 30, topY);	// трохи лівіше панелі
		}
		m_wHintBox.SetVisible(true);
	}

	//! Прозорість усієї панелі (пад: 1 у панелі, ~0.35 на полотні з інструментом, 0 у idle).
	void SetPanelOpacity(float a)
	{
		if (m_wRoot)
			m_wRoot.SetOpacity(a);
	}

	//! Увімкнути/вимкнути фокусність кнопок панелі (пад-вхід LB / вихід). Кліки миші незалежні від фокуса.
	//! The template ACTION buttons stay unfocusable always: on the pad they are pressed as A/B/Y/X,
	//! never navigated to — a focused Apply would make the same A press mean two things at once.
	void SetPadFocusMode(bool on)
	{
		m_bPadFocus = on;
		foreach (Widget w : m_aWired)
		{
			if (!w)
				continue;
			if (on && !IsTplActionButton(w))
				w.ClearFlags(WidgetFlags.NOFOCUS);
			else
				w.SetFlags(WidgetFlags.NOFOCUS);
		}
	}

	protected bool IsTplActionButton(Widget w)
	{
		return (m_wTplApply && w == m_wTplApply) || (m_wTplCancel && w == m_wTplCancel)
			|| (m_wTplAdd && w == m_wTplAdd) || (m_wTplRemove && w == m_wTplRemove);
	}

	protected void DisableEngineNav(Widget w)
	{
		if (!w)
			return;
		// STOP = фокус у цей бік не рухається взагалі (рух веде наша HandleNavInput).
		w.SetNavigation(WidgetNavigationDirection.UP,    WidgetNavigationRuleType.STOP);
		w.SetNavigation(WidgetNavigationDirection.DOWN,  WidgetNavigationRuleType.STOP);
		w.SetNavigation(WidgetNavigationDirection.LEFT,  WidgetNavigationRuleType.STOP);
		w.SetNavigation(WidgetNavigationDirection.RIGHT, WidgetNavigationRuleType.STOP);
	}

	protected void MakeButtonTransparent(ButtonWidget b)
	{
		if (b)
			b.SetColor(Color.FromInt(0x00000000));
	}

	void Destroy()
	{
		if (m_wHintBox)	// живе під map-frame, а не під m_wRoot — прибираємо окремо (рядки — його діти)
			m_wHintBox.RemoveFromHierarchy();
		foreach (Widget g : m_aTplGlyphs)	// теж під map-frame
		{
			if (g)
				g.RemoveFromHierarchy();
		}
		m_aTplGlyphs.Clear();
		if (m_wRoot)
			m_wRoot.RemoveFromHierarchy();
		m_wRoot = null;
		m_wToolbar = null;
		m_wMapFrame = null;
		m_wPencil = null; m_wErase = null;
		m_wSizeDropdown = null; m_wColorDropdown = null; m_wVisDropdown = null;
		m_wSizeDropdownBg = null;
		m_wSizeOpener = null; m_wColorOpener = null; m_wVisOpener = null;
		m_wOpacityDropdown = null; m_wOpacityOpener = null; m_wOpacitySlider = null;
		m_wHintBox = null; m_wHintR1 = null; m_wHintA = null; m_wHintB = null;
		m_OpacityHandler = null;
		m_aHandlers.Clear();
	}

	void SetVisible(bool v)
	{
		if (m_wRoot)
			m_wRoot.SetVisible(v);
		if (!v)
			CloseDropdowns();
	}

	// Чи курсор над панеллю/відкритим дропдауном (фіз. px). Корінь layout — смуга на всю ширину,
	// тож перевіряємо видимий тулбар і відкриті списки, а не корінь.
	bool IsCursorOver(float px, float py)
	{
		// Надійно: віджет під курсором належить панелі? Покриває ВСІ елементи (пункти
		// розкритих дропдаунів теж), незалежно від їх bounds — FrameWidget із SizeToContent,
		// що росте вгору, повертав хибний GetScreenSize і клік по розміру «провалювався» в малювання.
		Widget under = WidgetManager.GetWidgetUnderCursor();
		if (under && ContainsWidget(under))
			return true;

		if (OverWidget(m_wToolbar, px, py))
			return true;
		if (m_wSizeDropdown && m_wSizeDropdown.IsVisible() && OverWidget(m_wSizeDropdown, px, py))
			return true;
		if (m_wColorDropdown && m_wColorDropdown.IsVisible() && OverWidget(m_wColorDropdown, px, py))
			return true;
		if (m_wVisDropdown && m_wVisDropdown.IsVisible() && OverWidget(m_wVisDropdown, px, py))
			return true;
		if (m_wSideDropdown && m_wSideDropdown.IsVisible() && OverWidget(m_wSideDropdown, px, py))
			return true;
		if (m_wOpacityDropdown && m_wOpacityDropdown.IsVisible() && OverWidget(m_wOpacityDropdown, px, py))
			return true;
		// Leaving this one out let the canvas treat a click on Apply as a click on the MAP: the button
		// confirmed the template and the very same click re-anchored it, wiping the confirmation. From
		// the outside the buttons simply did nothing.
		if (m_wTemplatesDropdown && m_wTemplatesDropdown.IsVisible() && OverWidget(m_wTemplatesDropdown, px, py))
			return true;
		return false;
	}


	//! True while the name modal is up. The layer suspends ALL of its map input polling for this, which
	//! is what lets the edit box keep focus — nothing re-reads the mouse to steal it back.
	bool IsTypingName()
	{
		return m_bTplNaming;
	}

	//! True while ANY template modal is up (naming, or the delete confirmation). Map input polling
	//! stands down for both, for the same reason it does for the name field.
	bool IsModalBusy()
	{
		return m_bTplNaming || m_TplDeleteDialog != null;
	}

	bool IsTemplatesOpen()
	{
		return m_wTemplatesDropdown && m_wTemplatesDropdown.IsVisible();
	}

	bool IsDeleteDialogOpen()
	{
		return m_TplDeleteDialog != null;
	}

	//! A slot is lit. What the pad's hints need to know to advertise Place/Remove. Called every frame
	//! by the hint bar, so no allocation here.
	bool HasTemplatePicked()
	{
		return m_iTplHighlight >= 0 && m_iTplHighlight < SM_TemplateStore.GetInstance().Count();
	}

	//! A pad button (or a rebound key) standing in for a click on one of the template buttons. It may
	//! act exactly when the button itself could be clicked — the button's visibility IS the state
	//! machine, so nothing is decided twice here. Returns true when the press was taken.
	bool PressTemplateButton(int action)
	{
		if (m_TplDeleteDialog)
			return false;	// the dialog owns confirm/cancel now
		if (!IsTemplatesOpen())
			return false;

		Widget b;
		switch (action)
		{
			case ACT_TPL_APPLY:  b = m_wTplApply;  break;
			case ACT_TPL_CANCEL: b = m_wTplCancel; break;
			case ACT_TPL_ADD:    b = m_wTplAdd;    break;
			case ACT_TPL_REMOVE: b = m_wTplRemove; break;
			default: return false;
		}
		if (!b || !b.IsVisible())
			return false;

		// Pad, name entry, screen keyboard up (write mode): the buttons wait. Y confirming here would
		// commit a half-typed name; it means Confirm only once the keyboard has been put away.
		if (m_bTplNaming && m_wTplNameEdit && m_wTplNameEdit.IsInWriteMode())
		{
			InputManager im = GetGame().GetInputManager();
			if (im && !im.IsUsingMouseAndKeyboard())
				return false;
		}

		// Discarding a half-drawn template undoes strokes that are already on the map. That stays a
		// deliberate click on the button that says "Discard", not a B press that usually means "back".
		if (action == ACT_TPL_CANCEL && SM_TemplateSession.GetInstance().IsConfirmed())
			return false;

		// While the ghost is loose the map click IS the placement; Apply has nothing to add yet, and
		// answering it with "pick a template" or a re-select notice would only confuse.
		if (action == ACT_TPL_APPLY && !SM_TemplateSession.GetInstance().IsPlaced()
			&& m_Canvas && m_Canvas.IsActive() && m_Canvas.GetTool() == SM_DrawCanvas.TOOL_TEMPLATE)
			return false;

		// The A that dropped the anchor must not double as the A that confirms it: the map poll and
		// this listener ride the same physical button and their order inside a frame is not ours.
		if (action == ACT_TPL_APPLY && SM_TemplateSession.GetInstance().JustPlaced())
			return false;

		OnAction(action, 0);
		return true;
	}


	//! Called every frame from the layer. The panel lives on the map frame, so nothing holds keyboard
	//! focus for it — the moment anything else is hovered the field drops out of write mode. Hold it
	//! there while the player is typing, the same way the marker dialog holds its own name field.
	//!
	//! KB/M ONLY. On the pad, write mode is what summons the engine's screen keyboard, and it opens
	//! asynchronously: for a few frames the box reads as "not writing", and re-activating it here
	//! knocked the keyboard down before it ever appeared. One activation (OnTemplateAdd) is the whole
	//! job there — the marker dialog types the same way.
	void TickNameField()
	{
		if (!m_bTplNaming || !m_wTplNameEdit)
			return;
		InputManager im = GetGame().GetInputManager();
		if (im && !im.IsUsingMouseAndKeyboard())
			return;
		if (!m_wTplNameEdit.IsInWriteMode())
		{
			if (m_TplNameComp)
				m_TplNameComp.ActivateWriteMode(true);
			else
				m_wTplNameEdit.ActivateWriteMode();
		}
	}

	protected bool OverWidget(Widget w, float px, float py)
	{
		if (!w)
			return false;
		float rx, ry, rw, rh;
		w.GetScreenPos(rx, ry);
		w.GetScreenSize(rw, rh);
		return px >= rx && px <= rx + rw && py >= ry && py <= ry + rh;
	}

	//------------------------------------------------------------------------------
	//! End any template flow in progress and put the UI back to a clean slate: a framed selection, a
	//! ghost on the cursor, an anchor waiting for Apply — all gone. Called when the player picks another
	//! tool or closes the map. A template that is already DRAWING is left alone; its strokes are real
	//! now, and only Discard removes them.
	void AbortTemplateFlow()
	{
		SM_TemplateSession sess = SM_TemplateSession.GetInstance();
		if (!sess.IsConfirmed())
			sess.Clear();

		SM_TemplateStore.GetInstance().Select("");
		m_iTplHighlight = -1;
		m_bTplNaming = false;
		if (m_Canvas.GetShapeMode() != SM_ShapeGeometry.SHAPE_NONE)
		{
			m_Canvas.CancelShapePlacement();
			m_Canvas.SetActive(false);
		}
		if (m_TplDeleteDialog)
		{
			m_TplDeleteDialog.Close();	// the map is going away under it; an orphaned question helps nobody
			m_TplDeleteDialog = null;
		}
		m_Canvas.ClearSelection();
		BuildTemplatePreview(null);
		if (m_wTemplatesDropdown)
			m_wTemplatesDropdown.SetVisible(false);
		RefreshTemplates();
	}

	//! Pick the highlighted template up and arm the tool. The fit check happens HERE, not after the
	//! player has held the button for half an hour: the client knows the server's drawing limits, so a
	//! template that cannot possibly land is refused up front, with the number that explains why.
	protected void OnTemplateApply()
	{
		SM_TemplateSession sess = SM_TemplateSession.GetInstance();

		// Already drawing, and the player has come back from another tool: put the tool back in his
		// hand. Nothing else has to happen — the ghost and the progress were never lost. When the tool
		// is already armed this is the pad's A landing here mid-draw — nothing to do, nothing to say.
		if (sess.IsConfirmed())
		{
			bool alreadyArmed = m_Canvas.IsActive() && m_Canvas.GetTool() == SM_DrawCanvas.TOOL_TEMPLATE;
			m_Canvas.SetTool(SM_DrawCanvas.TOOL_TEMPLATE);
			m_Canvas.SetActive(true);
			if (!alreadyArmed)
			{
				CloseDropdowns();
				TemplateNotice("Hold on the template to carry on drawing it.");
			}
			return;
		}

		// Second press, ghost at rest: this is the confirmation. Only now may it draw.
		if (sess.IsAnchored())
		{
			sess.Confirm();
			RefreshTemplates();
			TemplateNotice("Hold on the template to draw it. Hold Delete on it to throw it away.");
			return;
		}

		SM_DrawTemplate t = TemplateForSlot(m_iTplHighlight);
		if (!t)
		{
			TemplateNotice("Pick a template first.");
			return;
		}

		// A built-in shape: two clicks on the map, one drawing out. No session — a shape is a single
		// record and rides the ordinary limits like any other. Local (PERSONAL) never leaves this
		// machine, so no server cap applies to it.
		if (t.m_iShape != SM_ShapeGeometry.SHAPE_NONE)
		{
			// Grid cap up front: no point arming a placement the server will refuse. Checked against
			// this client's own replica (it sees all of its own grids); the server enforces it for real.
			if (t.m_iShape == SM_ShapeGeometry.SHAPE_GRID
				&& m_Canvas.GetVisibility() != SM_EMarkerVisibility.PERSONAL)
			{
				int gridCap = SM_MarkerConfig.GetInstance().m_iDrawMaxGridsPerPlayer;
				if (gridCap > 0)
				{
					SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
					int myId = -1;
					if (pc)
						myId = pc.GetPlayerId();
					if (SM_MapDrawingStore.GetInstance().CountGridsByOwner(myId) >= gridCap)
					{
						if (gridCap == 1)
							TemplateNotice("You already have a grid. Erase it to place a new one.");
						else
							TemplateNotice(string.Format("You've reached this server's grid limit (%1). Erase one first.", gridCap));
						return;
					}
				}
			}

			SM_TemplateStore.GetInstance().Select(t.m_sId);
			SM_TemplateSession.GetInstance().Clear();
			m_Canvas.StartShapePlacement(t.m_iShape);
			RefreshTemplates();
			if (t.m_iShape == SM_ShapeGeometry.SHAPE_GRID)
				TemplateNotice("Click a grid square — it becomes A-1. A second click sets the size.");
			else if (t.m_iShape == SM_ShapeGeometry.SHAPE_CIRCLE)
				TemplateNotice("Click the centre, then click again to set the radius.");
			else
				TemplateNotice("Click the first corner, then the opposite one.");
			return;
		}

		int detail;
		SM_ETemplateFit fit = SM_TemplateStore.CheckFit(t, m_Canvas.GetVisibility(), detail);
		if (fit != SM_ETemplateFit.FIT)
		{
			TemplateNotice(SM_TemplateStore.FitMessage(fit, detail, t.StrokeCount()));
			return;
		}

		// First press: take it in hand. The dropdown STAYS open — its buttons are the flow from here,
		// and the columns get out of the way on their own.
		SM_TemplateStore.GetInstance().Select(t.m_sId);
		SM_TemplateSession.GetInstance().Clear();

		m_Canvas.SetTool(SM_DrawCanvas.TOOL_TEMPLATE);
		m_Canvas.SetActive(true);
		RefreshTemplates();
		TemplateNotice("Click the map to place it.");
	}

	//! Two presses, no text box. First press arms the selection tool and gets out of the way; drag a
	//! box around your own drawings. Second press saves what the box caught.
	//! Three steps behind one button:
	//!   nothing framed    -> arm the box tool, drag a selection
	//!   box framed         -> open the NAME step (this is the modal — the tool goes off and all our
	//!                         map input is suspended, so the edit box finally holds focus)
	//!   naming             -> save under the typed name
	protected void OnTemplateAdd()
	{
		if (m_bTplNaming)
		{
			CommitTemplateSave();
			return;
		}

		if (m_Canvas.HasSelection())
		{
			// Enter the name step. The tool is switched off and the panel goes into a modal state: from
			// here until Confirm, SM_PollMouse stands down entirely, which is the ONLY way the edit box
			// keeps focus on the map frame — the same thing the marker dialog relies on.
			m_bTplNaming = true;
			m_Canvas.SetActive(false);
			RefreshTemplates();

			if (m_TplNameComp)
			{
				m_TplNameComp.SetValue("");
				m_TplNameComp.ActivateWriteMode(true);
			}
			else if (m_wTplNameEdit)
			{
				m_wTplNameEdit.SetText("");
				GetGame().GetWorkspace().SetFocusedWidget(m_wTplNameEdit);
				m_wTplNameEdit.ActivateWriteMode();
			}
			// The overlay became visible THIS frame; on the pad the screen keyboard sometimes ignores
			// an activation that early. One deferred retry; a no-op when write mode already took.
			GetGame().GetCallqueue().CallLater(NameActivateLate, 120, false);
			TemplateNotice("Type a name, then press Confirm.");
			return;
		}

		m_Canvas.SetTool(SM_DrawCanvas.TOOL_SELECT);
		m_Canvas.SetActive(true);
		RefreshTemplates();
		TemplateNotice("Drag a box around your own drawings, then press Save Template.");
	}

	//! Deferred second shot at write mode (and with it the pad's screen keyboard).
	protected void NameActivateLate()
	{
		if (!m_bTplNaming || !m_wTplNameEdit || m_wTplNameEdit.IsInWriteMode())
			return;
		if (m_TplNameComp)
			m_TplNameComp.ActivateWriteMode(true);
		else
			m_wTplNameEdit.ActivateWriteMode();
	}

	protected void CommitTemplateSave()
	{
		string name;
		if (m_TplNameComp)
			name = m_TplNameComp.GetValue();
		else if (m_wTplNameEdit)
			name = m_wTplNameEdit.GetText();
		name.TrimInPlace();
		if (name.Length() > TPL_NAME_MAX)
			name = name.Substring(0, TPL_NAME_MAX);
		if (name == "")
			name = string.Format("Template %1", SM_TemplateStore.GetInstance().Count() + 1);

		string id = SM_TemplateStore.GetInstance().SaveSelection(m_Canvas, name);
		if (id == "")
		{
			TemplateNotice("Nothing of yours inside the box.");
			return;
		}

		m_bTplNaming = false;
		m_Canvas.SetActive(false);
		m_iTplHighlight = -1;
		if (m_TplNameComp)
			m_TplNameComp.SetValue("");
		else if (m_wTplNameEdit)
			m_wTplNameEdit.SetText("");
		RefreshTemplates();
		TemplateNotice(string.Format("Saved as '%1'.", name));
	}

	//! Cancel walks the flow BACK one step, it does not blow it away. Anchored -> the ghost is loose
	//! again and can be moved; loose -> back to the list. Throwing a CONFIRMED template away is a hold
	//! on the map, not a button: it undoes drawing that already happened.
	protected void OnTemplateCancel()
	{
		SM_TemplateSession sess = SM_TemplateSession.GetInstance();

		// A shape being placed: drop it and put the tool away. Nothing reached the map yet (a shape
		// only exists after its second click), so there is nothing to undo.
		if (m_Canvas.GetShapeMode() != SM_ShapeGeometry.SHAPE_NONE)
		{
			m_Canvas.CancelShapePlacement();
			m_Canvas.SetActive(false);
			SM_TemplateStore.GetInstance().Select("");
			RefreshTemplates();
			return;
		}

		// Naming: one step back to the framed box. The selection is still standing, so Save simply
		// opens the name step again. Without this branch the modal flag survived the reset below and
		// the map stayed deaf with no way out.
		if (m_bTplNaming)
		{
			m_bTplNaming = false;
			m_Canvas.SetTool(SM_DrawCanvas.TOOL_SELECT);
			m_Canvas.SetActive(true);
			RefreshTemplates();
			return;
		}

		// Confirmed: this throws the whole thing away, the strokes it already drew included.
		if (sess.IsConfirmed())
		{
			sess.Discard();
			m_iTplHighlight = -1;
			SM_TemplateStore.GetInstance().Select("");
			m_Canvas.SetActive(false);
			BuildTemplatePreview(null);
			RefreshTemplates();
			TemplateNotice("Template discarded.");
			return;
		}

		if (sess.IsAnchored())
		{
			sess.Unanchor();
			RefreshTemplates();
			TemplateNotice("Click the map to place it somewhere else.");
			return;
		}

		// Take it out of hand, not just off the canvas: the ghost follows whatever is SELECTED, so
		// leaving the selection set kept it glued to the cursor after Cancel. The lit slot stays, so
		// Place and Remove are still one press away.
		SM_TemplateStore.GetInstance().Select("");
		m_Canvas.ClearSelection();	// drops the box if one was being framed
		m_Canvas.SetActive(false);
		sess.Clear();
		RefreshTemplates();
	}

	protected void OnTemplateRemove()
	{
		SM_DrawTemplate t = TemplateForSlot(m_iTplHighlight);
		if (!t)
		{
			TemplateNotice("Pick a template first.");
			return;
		}
		if (t.m_bBuiltIn)
		{
			TemplateNotice("This template is built in and cannot be removed.");
			return;
		}
		if (m_TplDeleteDialog)
			return;

		// Deleting is a file gone from disk, so it gets one explicit question. The vanilla
		// configurable dialog provides the chrome; the preset is just a confirm/cancel pair whose
		// texts are overwritten below.
		SM_TemplateDeleteDialog dlg = new SM_TemplateDeleteDialog();
		dlg.SM_Setup(this, t.m_sId);
		if (!SCR_ConfigurableDialogUi.CreateFromPreset(SCR_CommonDialogs.DIALOGS_CONFIG, "exit_game", dlg))
		{
			ConfirmTemplateDelete(t.m_sId);	// no dialog to ask with — better deleted than a dead button
			return;
		}
		m_TplDeleteDialog = dlg;
		dlg.SetTitle("Delete template");
		dlg.SetMessage(string.Format("Delete '%1'? Its file is removed from disk; this cannot be undone.", t.m_sName));
		SCR_InputButtonComponent confirmBtn = dlg.FindButton("confirm");
		if (confirmBtn)
			confirmBtn.SetLabel("Delete");
	}

	//! The dialog said yes.
	void ConfirmTemplateDelete(string id)
	{
		m_TplDeleteDialog = null;

		SM_DrawTemplate t = SM_TemplateStore.GetInstance().Find(id);
		string name;
		if (t)
			name = t.m_sName;

		if (!SM_TemplateStore.GetInstance().Delete(id))
		{
			TemplateNotice("This template is built in and cannot be removed.");
			return;
		}
		m_iTplHighlight = -1;
		BuildTemplatePreview(null);
		RefreshTemplates();
		TemplateNotice(string.Format("Deleted '%1'.", name));
	}

	//! The dialog said no, or was closed some other way.
	void OnTemplateDeleteDialogClosed()
	{
		m_TplDeleteDialog = null;
	}

	//! Draw the picked template into the preview canvas.
	//!
	//! Fitted by BOUNDING BOX, so a 500 m template and a 50 m one fill the same area — which is the
	//! only thing a preview is for. Stroke width is a FIXED number of pixels, deliberately NOT scaled
	//! with the geometry: our brush widths are metres, and a 5 m brush on a 500 m template would come
	//! out under a pixel and vanish, while a small template would turn into one solid blob.
	//!
	//! Built once per click, never per frame.
	protected void BuildTemplatePreview(SM_DrawTemplate t)
	{
		if (!m_wTplPreview)
			return;

		m_aTplPreviewCmds.Clear();

		if (!t)
		{
			m_wTplPreview.SetDrawCommands(m_aTplPreviewCmds);	// empty list = blank
			return;
		}

		if (t.m_iShape != SM_ShapeGeometry.SHAPE_NONE)
		{
			BuildShapePreview(t.m_iShape);
			return;
		}

		float spanX = t.m_iSpanX;
		float spanZ = t.m_iSpanZ;
		if (spanX < 1) spanX = 1;	// a straight line has no width in one axis
		if (spanZ < 1) spanZ = 1;

		// Size the CANVAS to the template rather than fitting the template into a fixed canvas. A
		// CanvasWidget has no content a layout can measure — SizeToContent gives it nothing and it
		// collapses — so the size has to be handed to it, and the template is what knows the shape.
		float k = Math.Min(TPL_PREVIEW_MAX_W / spanX, TPL_PREVIEW_MAX_H / spanZ);
		float cw = Math.Max(spanX * k, TPL_PREVIEW_MIN);
		float ch = Math.Max(spanZ * k, TPL_PREVIEW_MIN);

		FrameSlot.SetSize(m_wTplPreview, cw, ch);

		// Declare the unit space to BE those pixels. Measuring with GetScreenSize would read the size
		// from BEFORE the line above — the layout has not run yet — and the preview would lag a click
		// behind, drawn to the previous template's shape.
		m_wTplPreview.SetSizeInUnits(Vector(cw, ch, 0));
		m_wTplPreview.SetZoom(1.0);
		m_wTplPreview.SetOffsetPx(Vector(0, 0, 0));

		float availW = cw - TPL_PREVIEW_PAD * 2;
		float availH = ch - TPL_PREVIEW_PAD * 2;
		if (availW <= 0 || availH <= 0)
			return;

		float scale = Math.Min(availW / spanX, availH / spanZ);
		float ox = cw * 0.5;
		float oy = ch * 0.5;

		// Fills first, then strokes: paint under ink, exactly as the map layers them.
		for (int pass = 0; pass < 2; pass++)
		{
			bool wantFill = (pass == 0);
			foreach (SM_DrawTemplateStroke st : t.m_aStrokes)
			{
				if (!st || st.GetPointCount() < 2)
					continue;
				if ((st.m_iFill != 0) != wantFill)
					continue;

				int n = st.GetPointCount();
				array<float> pts = {};
				pts.Resize(n * 2);
				for (int i = 0; i < n; i++)
				{
					// Z grows north, screen Y grows down — hence the minus, or the preview comes out
					// mirrored against what the map shows.
					pts[i * 2]     = ox + st.m_aPoints[i * 2]     * scale;
					pts[i * 2 + 1] = oy - st.m_aPoints[i * 2 + 1] * scale;
				}

				if (wantFill)
				{
					array<int> tri = st.FillIndices();
					if (!tri)
						continue;	// an outline the triangulator cannot handle is skipped, not forced

					TriMeshDrawCommand mesh = new TriMeshDrawCommand();
					mesh.m_iColor   = st.m_iColor;
					mesh.m_Vertices = pts;
					mesh.m_Indices  = tri;
					m_aTplPreviewCmds.Insert(mesh);
				}
				else
				{
					LineDrawCommand line = new LineDrawCommand();
					line.m_iColor   = st.m_iColor;
					line.m_fWidth   = TPL_PREVIEW_LINE_PX;
					line.m_Vertices = pts;
					m_aTplPreviewCmds.Insert(line);
				}
			}
		}

		m_wTplPreview.SetDrawCommands(m_aTplPreviewCmds);
	}

	//! Sample of a built-in shape, drawn with the CURRENT brush colour and opacity — the preview is
	//! how the player checks what he is about to stamp, so it must not lie about the ink.
	protected void BuildShapePreview(int shape)
	{
		m_aTplPreviewCmds.Clear();

		// Sample parameters in their own metre space; the block below fits whatever comes out.
		array<int> p = {};
		float border = 12;
		if (shape == SM_ShapeGeometry.SHAPE_RECT)
		{
			p = {0, 0, 320, 210};
		}
		else if (shape == SM_ShapeGeometry.SHAPE_CIRCLE)
		{
			p = {160, 105, 290, 105};
		}
		else	// grid: 5 columns x 4 rows is enough to read the idea, labels included
		{
			p = {100, 400, 600, 0};
			border = 8;
		}

		array<ref SM_ShapeLine> lines = {};
		SM_ShapeGeometry.Build(shape, p, border, lines);
		if (lines.IsEmpty())
		{
			m_wTplPreview.SetDrawCommands(m_aTplPreviewCmds);
			return;
		}

		// Fit: bounding box of everything built, mapped into the preview canvas.
		bool first = true;
		int loX, hiX, loZ, hiZ;
		foreach (SM_ShapeLine bl : lines)
		{
			for (int i = 0; i + 1 < bl.m_aPts.Count(); i += 2)
			{
				int x = bl.m_aPts[i];
				int z = bl.m_aPts[i + 1];
				if (first) { loX = x; hiX = x; loZ = z; hiZ = z; first = false; continue; }
				if (x < loX) loX = x;
				if (x > hiX) hiX = x;
				if (z < loZ) loZ = z;
				if (z > hiZ) hiZ = z;
			}
		}
		float spanX = Math.Max(hiX - loX, 1);
		float spanZ = Math.Max(hiZ - loZ, 1);

		float k = Math.Min(TPL_PREVIEW_MAX_W / spanX, TPL_PREVIEW_MAX_H / spanZ);
		float cw = Math.Max(spanX * k, TPL_PREVIEW_MIN);
		float ch = Math.Max(spanZ * k, TPL_PREVIEW_MIN);
		FrameSlot.SetSize(m_wTplPreview, cw, ch);
		m_wTplPreview.SetSizeInUnits(Vector(cw, ch, 0));
		m_wTplPreview.SetZoom(1.0);
		m_wTplPreview.SetOffsetPx(Vector(0, 0, 0));

		float availW = cw - TPL_PREVIEW_PAD * 2;
		float availH = ch - TPL_PREVIEW_PAD * 2;
		if (availW <= 0 || availH <= 0)
			return;
		float scale = Math.Min(availW / spanX, availH / spanZ);
		float ox = cw * 0.5 - (loX + hiX) * 0.5 * scale;
		float oy = ch * 0.5 + (loZ + hiZ) * 0.5 * scale;	// Z north-up -> screen y down

		int argb = m_Canvas.BrushColorWithOpacity();
		foreach (SM_ShapeLine l : lines)
		{
			int n = l.m_aPts.Count() / 2;
			if (n < 2)
				continue;
			array<float> pts = {};
			pts.Resize(n * 2);
			for (int j = 0; j < n; j++)
			{
				pts[j * 2]     = ox + l.m_aPts[j * 2] * scale;
				pts[j * 2 + 1] = oy - l.m_aPts[j * 2 + 1] * scale;
			}
			LineDrawCommand line = new LineDrawCommand();
			line.m_iColor   = argb;
			line.m_fWidth   = Math.Max(l.m_fWidthMeters * scale, 1.2);	// keeps the thick/thin contrast
			line.m_Vertices = pts;
			m_aTplPreviewCmds.Insert(line);
		}

		m_wTplPreview.SetDrawCommands(m_aTplPreviewCmds);
	}

	//! The brush changed (colour, width, opacity). A shape preview is painted WITH the brush, so it
	//! has to follow; stroke-template previews carry their own baked colours and don't care.
	protected void RefreshShapePreviewIfAny()
	{
		if (!IsTemplatesOpen())
			return;
		SM_DrawTemplate t = TemplateForSlot(m_iTplHighlight);
		if (t && t.m_iShape != SM_ShapeGeometry.SHAPE_NONE)
			BuildTemplatePreview(t);
	}

	// ---------------------------------------------------------------- templates

	protected void BuildTemplates()
	{
		m_wTemplatesDropdown = m_wRoot.FindAnyWidget("TemplatesDropdown");
		m_wTemplatesOpener   = m_wRoot.FindAnyWidget("TemplatesButton");
		if (!m_wTemplatesDropdown || !m_wTemplatesOpener)
			return;	// panel without the templates combo (an older layout) — everything else still works

		Wire(m_wTemplatesOpener, ACT_OPEN_TEMPLATES, 0);
		m_wTemplatesDropdown.SetVisible(false);

		m_aTplCols.Clear();
		m_aTplSlots.Clear();

		for (int c = 0; c < TPL_COLS; c++)
		{
			Widget col = m_wTemplatesDropdown.FindAnyWidget("VerticalLayout" + c.ToString());
			if (!col)
				break;
			m_aTplCols.Insert(col);

			// The slot names repeat in every column, so search INSIDE the column, never from the root.
			for (int i = 0; i < TPL_PER_COL; i++)
			{
				ButtonWidget slot = ButtonWidget.Cast(col.FindAnyWidget("Template" + i.ToString()));
				if (!slot)
					continue;
				Wire(slot, ACT_TPL_SLOT, m_aTplSlots.Count());
				m_aTplSlots.Insert(slot);
			}
		}

		m_wTplPreview = CanvasWidget.Cast(m_wTemplatesDropdown.FindAnyWidget("PreviewCanvas"));
		Widget overlay = m_wTemplatesDropdown.FindAnyWidget("TmpDrpOverlay");
		if (overlay)
			m_wTplChrome = overlay.FindAnyWidget("Background1");

		m_wTplNameOverlay = m_wTemplatesDropdown.FindAnyWidget("TemplateNameEdit");
		if (m_wTplNameOverlay)
		{
			// The edit widget lives inside the WLib_EditBox prefab and has no name of its own, so it is
			// found by TYPE. Reaching into the prefab like this is why nothing has to be wired by hand
			// in Workbench beyond dropping the field in.
			m_TplNameComp = SCR_EditBoxComponent.Cast(m_wTplNameOverlay.FindHandler(SCR_EditBoxComponent));
			m_wTplNameEdit = FindEditBox(m_wTplNameOverlay);
			if (m_wTplNameEdit)
				m_wTplNameEdit.SetPlaceholderText("Name");	// the prefab ships a "Write..." placeholder

			// The WLib_EditBox prefab also carries a static label ("EditBox") baked in. It lives in the
			// prefab, so it cannot be removed in Workbench — blank every stray text widget in there,
			// leaving only the edit widget itself.
			HidePrefabLabels(m_wTplNameOverlay);

			m_wTplNameOverlay.SetVisible(false);	// hidden until the player is framing a save
		}

		m_wTplApply  = m_wRoot.FindAnyWidget("ApplyAndPlaceButton");
		m_wTplCancel = m_wRoot.FindAnyWidget("CancelButton");
		m_wTplAdd    = m_wRoot.FindAnyWidget("AddNewTemplateButton");
		m_wTplRemove = m_wRoot.FindAnyWidget("RemoveTemplateButton");

		Wire(m_wTplApply,  ACT_TPL_APPLY,  0);
		Wire(m_wTplCancel, ACT_TPL_CANCEL, 0);
		Wire(m_wTplAdd,    ACT_TPL_ADD,    0);
		Wire(m_wTplRemove, ACT_TPL_REMOVE, 0);

		BuildTemplateGlyphs();

		// Two gates: this map screen must ASK for the tab (feature), and the server must ALLOW it
		// (config, which can arrive after a late sync — TickTemplatesAllowed re-checks).
		if (!TemplatesAllowed())
			m_wTemplatesOpener.SetVisible(false);

		RefreshTemplates();
	}

	//! May the Templates tab exist here? Feature (this screen opted in) AND server config.
	protected bool TemplatesAllowed()
	{
		return m_bTemplatesFeature && SM_MarkerConfig.GetInstance().m_bAllowTemplates;
	}

	//! One pad glyph per template button, floating just LEFT of it: the button carries no room of its
	//! own for a glyph, and the layout is not ours to grow. They live under the map frame and follow
	//! the buttons every frame; on mouse and keyboard they stay hidden.
	protected void BuildTemplateGlyphs()
	{
		m_aTplGlyphs.Clear();
		if (!m_wMapFrame)
			return;

		m_aTplGlyphs.Insert(MakeTplGlyph("AM_TplApply"));
		m_aTplGlyphs.Insert(MakeTplGlyph("AM_TplCancel"));
		m_aTplGlyphs.Insert(MakeTplGlyph("AM_TplAdd"));
		m_aTplGlyphs.Insert(MakeTplGlyph("AM_TplRemove"));
	}

	protected Widget MakeTplGlyph(string action)
	{
		Widget row = GetGame().GetWorkspace().CreateWidgets(
			"{CB8563509DEF3E0E}UI/layouts/WidgetLibrary/Buttons/WLib_NavigationButtonSmall.layout", m_wMapFrame);
		if (!row)
			return null;
		row.SetFlags(WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS);
		row.SetZOrder(130);	// above the dropdown
		FrameSlot.SetAnchorMin(row, 0, 0);
		FrameSlot.SetAnchorMax(row, 0, 0);
		FrameSlot.SetAlignment(row, 1, 0.5);	// pivot on the right edge: SetPos points at the button's left
		FrameSlot.SetSizeToContent(row, true);

		SCR_InputButtonComponent c = SCR_InputButtonComponent.Cast(row.FindHandler(SCR_InputButtonComponent));
		if (c)
		{
			c.SetAction(action);
			c.SetLabel("");	// the button next to it is the label
			c.SetClickSoundDisabled(true);
			c.SM_MakeDisplayOnly();
		}
		row.SetVisible(false);
		return row;
	}

	protected Widget TplButtonForGlyph(int idx)
	{
		switch (idx)
		{
			case 0: return m_wTplApply;
			case 1: return m_wTplCancel;
			case 2: return m_wTplAdd;
			case 3: return m_wTplRemove;
		}
		return null;
	}

	//! Follow the buttons. Cheap — four widgets — and only while the dropdown is up on a pad.
	protected void TickTemplateGlyphs()
	{
		if (m_aTplGlyphs.IsEmpty() || !m_wMapFrame)
			return;

		InputManager im = GetGame().GetInputManager();
		bool pad = im && !im.IsUsingMouseAndKeyboard();
		// The glyphs are separate widgets under the map frame, so the panel's opacity does not carry to
		// them: on the idle pad the panel is transparent but the tab may still be technically "open",
		// which left an Add(Y) glyph floating over the map after a save. Gate on the panel being
		// actually shown (opacity from last frame — this runs before the opacity is set this frame).
		bool panelShown = m_wRoot && m_wRoot.GetOpacity() > 0.1;
		bool open = IsTemplatesOpen() && pad && !m_TplDeleteDialog && panelShown;

		WorkspaceWidget ws = GetGame().GetWorkspace();
		float fx, fy;
		m_wMapFrame.GetScreenPos(fx, fy);

		foreach (int i, Widget g : m_aTplGlyphs)
		{
			if (!g)
				continue;
			Widget b = TplButtonForGlyph(i);
			bool show = open && b && b.IsVisible();
			g.SetVisible(show);
			if (!show)
				continue;

			float bx, by, bw, bh;
			b.GetScreenPos(bx, by);
			b.GetScreenSize(bw, bh);
			FrameSlot.SetPos(g, ws.DPIUnscale(bx - fx) - 6, ws.DPIUnscale(by - fy + bh * 0.5));
		}
	}

	//! The server's allowTemplates switch can arrive after the panel was built (the config sync is a
	//! round trip). Watched per frame; flipping it off aborts whatever the flow was doing.
	protected void TickTemplatesAllowed()
	{
		if (!m_wTemplatesOpener)
			return;
		bool allowed = TemplatesAllowed();
		if (m_wTemplatesOpener.IsVisible() == allowed)
			return;
		m_wTemplatesOpener.SetVisible(allowed);
		if (!allowed)
			AbortTemplateFlow();	// hides the dropdown too
	}

	//! Fill the slots, and show only as many columns as there is something to put in.
	//!
	//! A column appears once the one before it is FULL — so an empty list is one column of "Empty"
	//! rather than eight, and the dropdown grows with the collection instead of standing there mostly
	//! blank. The first empty slot of the last shown column is where the next template will land.
	void RefreshTemplates()
	{
		if (m_aTplSlots.IsEmpty())
			return;

		array<SM_DrawTemplate> list = {};
		SM_TemplateStore.GetInstance().GetAll(list);

		foreach (int i, Widget slot : m_aTplSlots)
		{
			if (!slot)
				continue;

			string label = "Empty";
			if (i < list.Count() && list[i])
				label = list[i].m_sName;

			SetSlotText(slot, label);
			TintSlot(slot, i == m_iTplHighlight);
		}

		// Column N is shown when column N-1 is full. The first is always shown.
		int shown = 1 + list.Count() / TPL_PER_COL;
		if (shown > m_aTplCols.Count())
			shown = m_aTplCols.Count();

		SM_TemplateSession sess = SM_TemplateSession.GetInstance();
		// The columns are what give TmpDrpOverlay its size — it sizes to content, and ButtonsLayout
		// hangs outside it on a negative padding. Hide the columns and the overlay collapses, taking
		// the buttons out of its rectangle with it: the hit test stops descending and every press on
		// Apply falls straight through to the map. That is exactly what happened.
		//
		// So the list stays up. To hide it during placement, ButtonsLayout has to stop depending on
		// the overlay's size — put it and the columns side by side in a horizontal row inside the
		// overlay, with no negative padding, and this becomes safe again.
		foreach (int c, Widget col : m_aTplCols)
		{
			if (col)
				col.SetVisible(c < shown);
		}

		RefreshTemplateButtons();
	}

	//! Which buttons exist depends on where the player is, and there is only ever one sensible next
	//! step showing.
	//!
	//!   browsing                    Add
	//!   picked (a slot is lit)      Place, Remove
	//!   armed, ghost on the cursor  Place, Cancel     (Place reminds you: the MAP is what you click)
	//!   anchored, ghost at rest     Apply, Cancel     (Cancel un-anchors, so you can move it again)
	//!   confirmed, drawing          none              (the map's hints take over: draw, or discard)
	//!   framing a new template      Save, Cancel      (Save is greyed out until the box holds something)
	void RefreshTemplateButtons()
	{
		// These four live OUTSIDE TemplatesDropdown's own rectangle — ButtonsLayout hangs off it on a
		// negative padding (see the note in RefreshTemplates), which is what lets them stay clickable
		// through a click that also has to reach the map underneath. But nothing here ever gated them
		// on the dropdown being OPEN: "Add New Template" is visible at rest by the flow-state checks
		// below regardless. Closed, they were still sitting there, invisible and hit-testable, wherever
		// the toolbar happened to place them — harmless on the player's narrower row, but landing right
		// on top of Pencil/Eraser/Color for the GM, whose row is wider (it also carries SideCombo) and
		// centered, so the whole row — and this offset with it — shifts left. Every click on those
		// tools was being eaten by an invisible "Add" floating over them. The tab genuinely stays open
		// for the whole armed/anchored/confirmed flow (per the comment above), so gating on it here
		// costs nothing that flow needs.
		if (!IsTemplatesOpen())
		{
			if (m_wTplAdd)    m_wTplAdd.SetVisible(false);
			if (m_wTplRemove) m_wTplRemove.SetVisible(false);
			if (m_wTplApply)  m_wTplApply.SetVisible(false);
			if (m_wTplCancel) m_wTplCancel.SetVisible(false);
			if (m_wTplNameOverlay) m_wTplNameOverlay.SetVisible(false);
			return;
		}

		SM_TemplateSession sess = SM_TemplateSession.GetInstance();

		// "picked" is a lit SLOT, not the store's selection: the store only learns about a template
		// once it is taken in hand, and requiring that here made Place a button you had to press in
		// order for it to appear.
		bool picked    = TemplateForSlot(m_iTplHighlight) != null;
		bool framing   = m_Canvas.GetTool() == SM_DrawCanvas.TOOL_SELECT && m_Canvas.IsActive();
		bool armed     = SM_TemplateStore.GetInstance().Selected() != null
			&& m_Canvas.GetTool() == SM_DrawCanvas.TOOL_TEMPLATE && m_Canvas.IsActive();
		bool anchored  = sess.IsAnchored();
		bool confirmed = sess.IsConfirmed();

		// A confirmed template needs a way BACK. Hiding every button left the player stranded: switch
		// to the pencil and there was nothing left anywhere that could re-arm the tool.
		if (m_wTplAdd)
		{
			m_wTplAdd.SetVisible((!picked && !armed && !confirmed) || framing || m_bTplNaming);
			SetSlotText(m_wTplAdd, AddLabel(framing, m_bTplNaming));
		}
		if (m_wTplRemove)
			m_wTplRemove.SetVisible(picked && !armed && !confirmed && !framing && !m_bTplNaming);
		if (m_wTplApply)
		{
			m_wTplApply.SetVisible((picked && !framing && !m_bTplNaming) || confirmed);
			SetSlotText(m_wTplApply, ApplyLabel(anchored, confirmed));
		}
		if (m_wTplCancel)
		{
			m_wTplCancel.SetVisible(armed || framing || confirmed || m_bTplNaming);
			SetSlotText(m_wTplCancel, CancelLabel(confirmed));
		}

		// The name field belongs to the save step: it shows exactly while a box is being framed —
		// alongside the Save button — and nowhere else.
		if (m_wTplNameOverlay)
			m_wTplNameOverlay.SetVisible(m_bTplNaming);
	}

	//! Place while the ghost is loose, Apply once it has come to rest, Resume once it is drawing and
	//! the player has wandered off to another tool. Same button; the word says what the press will do.
	protected string ApplyLabel(bool anchored, bool confirmed)
	{
		if (confirmed)
			return "Resume";
		if (anchored)
			return "Apply";
		return "Place";
	}

	//! Cancel steps back — except on a confirmed template, where stepping back means undoing drawing
	//! that already happened. The word has to say so.
	protected string CancelLabel(bool confirmed)
	{
		if (confirmed)
			return "Discard";
		return "Cancel";
	}

	//! Same trick for the other one: it starts the box, then it saves what the box caught.
	protected string AddLabel(bool framing, bool naming)
	{
		if (naming)
			return "Confirm";
		if (framing)
			return "Save Template";
		return "Add New Template";
	}

	//! The anchor is dropped by clicking the MAP, so the panel cannot know the state has moved on
	//! unless it looks. Called every frame from the layer; change-gated, so the steady state costs one
	//! integer compare.
	void TickTemplateState()
	{
		TickTemplatesAllowed();
		TickTemplateGlyphs();	// before the early-out: they must also HIDE when the dropdown closes

		// A shape just landed: the placement flow is over, so close the menu and clear the pick — the
		// player asked for one shape, and here is the clean slate to choose the next thing from.
		if (m_Canvas && m_Canvas.ConsumeShapePlaced())
		{
			AbortTemplateFlow();
			return;
		}

		if (!m_wTemplatesDropdown || !m_wTemplatesDropdown.IsVisible())
			return;

		// Pad navigation: FOCUSING a slot is picking it. A separate confirm press cannot exist — A is
		// Apply here — so the highlight follows the focus, exactly like a mouse hover that commits.
		if (m_bPadFocus)
		{
			WorkspaceWidget fws = GetGame().GetWorkspace();
			if (fws)
			{
				int fsi = m_aTplSlots.Find(fws.GetFocusedWidget());
				if (fsi != -1 && fsi != m_iTplHighlight)
				{
					m_iTplHighlight = fsi;
					RefreshTemplates();
					BuildTemplatePreview(TemplateForSlot(fsi));
				}
			}
		}

		SM_TemplateSession sess = SM_TemplateSession.GetInstance();
		int state = 0;
		if (SM_TemplateStore.GetInstance().Selected())
			state |= 1;
		if (m_Canvas && m_Canvas.GetTool() == SM_DrawCanvas.TOOL_TEMPLATE && m_Canvas.IsActive())
			state |= 2;
		if (m_Canvas && m_Canvas.GetTool() == SM_DrawCanvas.TOOL_SELECT && m_Canvas.IsActive())
			state |= 16;
		if (m_Canvas && m_Canvas.HasSelection())
			state |= 32;
		if (m_iTplHighlight >= 0)
			state |= 64;
		if (sess.IsAnchored())
			state |= 4;
		if (sess.IsConfirmed())
			state |= 8;
		if (m_bTplNaming)
			state |= 128;
		if (m_Canvas && m_Canvas.GetShapeMode() != SM_ShapeGeometry.SHAPE_NONE)
			state |= 256;
		if (m_Canvas && m_Canvas.ShapeFirstSet())
			state |= 512;

		if (state == m_iTplState)
			return;
		m_iTplState = state;
		RefreshTemplates();
	}

	//! Mark the picked slot. The template slots carry no Background1 of their own — unlike the toolbar
	//! buttons — so with one there we tint it like everything else, and without one we fall back to the
	//! label. Tinting the BUTTON itself is not an option: TickFocusHighlight caches a button's colour to
	//! restore after a pad highlight, and changing it underneath would leave the cache stale.
	protected void TintSlot(notnull Widget slot, bool picked)
	{
		Widget bg = slot.FindAnyWidget("Background1");
		if (bg)
		{
			if (picked)
				bg.SetColor(Color.FromInt(BG_ARMED));
			else
				bg.SetColor(Color.FromInt(BG_IDLE));
			return;
		}

		TextWidget t = TextWidget.Cast(slot.FindAnyWidget("Text0"));
		if (!t)
			return;
		if (picked)
			t.SetColor(Color.FromInt(BG_ARMED));	// amber, the same "this one is on" the tools use
		else
			t.SetColor(Color.FromInt(0xFFFFFFFF));
	}

	//! Blank the decorative TextWidgets the edit-box prefab drags along. The typed text is an
	//! EditBoxWidget, never a TextWidget, so clearing every TextWidget cannot touch the input.
	protected void HidePrefabLabels(Widget w)
	{
		for (Widget c = w.GetChildren(); c; c = c.GetSibling())
		{
			TextWidget t = TextWidget.Cast(c);
			if (t)
				t.SetText("");
			HidePrefabLabels(c);
		}
	}

	//! First EditBoxWidget anywhere under w. The prefab buries it several levels down and unnamed.
	protected EditBoxWidget FindEditBox(Widget w)
	{
		for (Widget c = w.GetChildren(); c; c = c.GetSibling())
		{
			EditBoxWidget eb = EditBoxWidget.Cast(c);
			if (eb)
				return eb;
			eb = FindEditBox(c);
			if (eb)
				return eb;
		}
		return null;
	}

	protected void SetSlotText(notnull Widget slot, string text)
	{
		TextWidget t = TextWidget.Cast(slot.FindAnyWidget("Text0"));
		if (t)
			t.SetText(text);
	}

	//! The template a slot points at, or null for an empty slot.
	protected SM_DrawTemplate TemplateForSlot(int idx)
	{
		array<SM_DrawTemplate> list = {};
		SM_TemplateStore.GetInstance().GetAll(list);
		if (idx < 0 || idx >= list.Count())
			return null;
		return list[idx];
	}

	protected void TemplateNotice(string msg)
	{
		SCR_HintManagerComponent hm = SCR_HintManagerComponent.GetInstance();
		if (hm)
			hm.ShowCustom(msg, "Templates", 6, false);
	}

	protected void Wire(Widget b, int action, int param)
	{
		if (!b)
			return;
		SM_DrawCellHandler h = new SM_DrawCellHandler();
		h.Setup(this, action, param);
		b.AddHandler(h);
		m_aHandlers.Insert(h);
		m_aWired.Insert(b);
	}

	// --- Геймпад: фокусна навігація панеллю ---

	//! Перша ціль фокуса (хрестовина вправо «заходить» у панель сюди).
	Widget GetFirstFocusTarget()
	{
		return m_wPencil;
	}

	//! Чи віджет належить панелі (коли фокус усередині, пад-дії мапи мовчать).
	bool ContainsWidget(Widget w)
	{
		while (w)
		{
			if (w == m_wRoot)
				return true;
			w = w.GetParent();
		}
		return false;
	}

	//! Вхід у панель відбувся натиском pad_right — позначаємо його «вже натиснутим»,
	//! щоб той самий натиск не зчитався HandleNavInput як негайний рух вправо.
	void NotifyPadEntered()
	{
		m_bNavR = true;
		m_bNavL = false;
		m_bNavU = false;
		m_bNavD = false;
	}

	//! Власна пад-навігація панеллю (кличе шар, коли фокус усередині панелі).
	void HandleNavInput(notnull InputManager im)
	{
		// Напрямок береться і з хрестовини (AM_Nav*), і з лівого стіка (Menu*).
		bool l = im.GetActionValue("AM_NavLeft")  > 0.5 || im.GetActionValue("MenuLeft")  > 0.5;
		bool r = im.GetActionValue("AM_NavRight") > 0.5 || im.GetActionValue("MenuRight") > 0.5;
		bool u = im.GetActionValue("AM_NavUp")    > 0.5 || im.GetActionValue("MenuUp")    > 0.5;
		bool d = im.GetActionValue("AM_NavDown")  > 0.5 || im.GetActionValue("MenuDown")  > 0.5;

		// Відкритий слайдер прозорості: утримання ліво/право регулює з авто-повтором.
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if ((l || r) && m_wOpacitySlider && m_wOpacityDropdown && m_wOpacityDropdown.IsVisible()
			&& ws && ws.GetFocusedWidget() == m_wOpacityOpener)
		{
			int dir = 1;
			if (l) dir = -1;
			float now = System.GetTickCount() / 1000.0;
			bool fire = false;
			if (dir != m_iOpacityHeldDir)
			{
				// перший натиск: крок одразу, потім пауза перед повтором
				fire = true;
				m_fOpacityNextRepeat = now + SM_OPACITY_REPEAT_DELAY;
			}
			else if (now >= m_fOpacityNextRepeat)
			{
				fire = true;
				m_fOpacityNextRepeat = now + SM_OPACITY_REPEAT_INTERVAL;
			}
			m_iOpacityHeldDir = dir;
			if (fire)
			{
				float cur = Math.Clamp(m_wOpacitySlider.GetCurrent() + dir * 5, 10, 100);
				m_wOpacitySlider.SetCurrent(cur);
				OnOpacitySlider(cur);
			}
			m_bNavL = l; m_bNavR = r; m_bNavU = u; m_bNavD = d;
			return;
		}
		m_iOpacityHeldDir = 0;

		// Звичайний рух фокуса: один крок на натиск.
		int dx = 0;
		int dy = 0;
		if (l && !m_bNavL) dx = -1;
		else if (r && !m_bNavR) dx = 1;
		else if (u && !m_bNavU) dy = -1;
		else if (d && !m_bNavD) dy = 1;

		m_bNavL = l; m_bNavR = r; m_bNavU = u; m_bNavD = d;

		if (dx != 0 || dy != 0)
			NavMove(dx, dy);
	}

	// Пад-B у панелі: якщо відкритий дропдаун — закрити його й повернути фокус на його опенер
	// (назад у верхній ряд). true = лишились у панелі; false = ми вже в ряду, хай шар виходить на мапу.
	bool HandleBack()
	{
		Widget opener = null;
		if      (m_wSizeDropdown    && m_wSizeDropdown.IsVisible())    opener = m_wSizeOpener;
		else if (m_wColorDropdown   && m_wColorDropdown.IsVisible())   opener = m_wColorOpener;
		else if (m_wVisDropdown     && m_wVisDropdown.IsVisible())     opener = m_wVisOpener;
		else if (m_wSideDropdown    && m_wSideDropdown.IsVisible())    opener = m_wSideOpener;
		else if (m_wOpacityDropdown && m_wOpacityDropdown.IsVisible()) opener = m_wOpacityOpener;

		if (!opener && m_wTemplatesDropdown && m_wTemplatesDropdown.IsVisible())
		{
			AbortTemplateFlow();	// closing the tab drops a framing/placement, exactly as the opener does
			FocusW(m_wTemplatesOpener);
			return true;
		}

		if (!opener)
			return false;

		CloseDropdowns();
		FocusW(opener);
		return true;
	}

	protected void NavMove(int dx, int dy)
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;
		Widget f = ws.GetFocusedWidget();
		if (!f)
			return;

		// --- колірна сітка (колонки по 7, кількість колонок довільна): вгору/вниз у колонці
		// (wrap у межах колонки; остання може бути коротшою), вліво/вправо між колонками ---
		int ci = m_aColorItems.Find(f);
		if (ci != -1)
		{
			int nCol = m_aColorItems.Count();
			if (dy != 0)
			{
				int col = ci / 7;
				int rowCount = nCol - col * 7;
				if (rowCount > 7)
					rowCount = 7;
				int row = (ci % 7 + dy + rowCount) % rowCount;
				FocusW(m_aColorItems[col * 7 + row]);
			}
			else if (dx != 0)
			{
				int ni;
				if (dx > 0)
					ni = (ci + 7) % nCol;
				else
					ni = (ci - 7 + nCol) % nCol;
				FocusW(m_aColorItems[ni]);
			}
			return;
		}

		// --- сітка слотів темплейтів (колонки по 10, показуються в міру заповнення):
		// вгору/вниз у колонці з wrap-ом, вліво/вправо між ВИДИМИМИ колонками ---
		int si = m_aTplSlots.Find(f);
		if (si != -1)
		{
			int visCols = 0;
			foreach (Widget col : m_aTplCols)
			{
				if (col && col.IsVisible())
					visCols++;
			}
			if (visCols < 1)
				visCols = 1;
			int maxIdx = visCols * TPL_PER_COL;
			if (maxIdx > m_aTplSlots.Count())
				maxIdx = m_aTplSlots.Count();

			if (dy != 0)
			{
				int scol = si / TPL_PER_COL;
				int rows = maxIdx - scol * TPL_PER_COL;
				if (rows > TPL_PER_COL)
					rows = TPL_PER_COL;
				if (rows > 0)
				{
					int srow = (si % TPL_PER_COL + dy + rows) % rows;
					FocusW(m_aTplSlots[scol * TPL_PER_COL + srow]);
				}
			}
			else if (dx != 0 && maxIdx > 0)
			{
				FocusW(m_aTplSlots[(si + dx * TPL_PER_COL + maxIdx) % maxIdx]);
			}
			return;
		}

		// (слайдер прозорості обробляє HandleNavInput — там авто-повтор на утриманні)

		// --- вертикальні списки: вгору/вниз із wrap-ом, невидимі пункти пропускаємо ---
		if (NavList(SizeItemsAsWidgets(), f, dy)) return;
		if (NavList(m_aVisItems,  f, dy)) return;
		if (NavList(m_aSideItems, f, dy)) return;

		// --- верхній ряд: вліво/вправо з wrap-ом, пропускаючи приховані (SideCombo поза Side-каналом) ---
		int ti = m_aTopRow.Find(f);
		if (ti != -1 && dx != 0)
		{
			SortTopRowByX();	// the row's build order is fixed in code; the layout order is the user's
			ti = m_aTopRow.Find(f);
			int n = m_aTopRow.Count();
			int i = ti;
			for (int guard = 0; guard < n; guard++)
			{
				i = (i + dx + n) % n;
				Widget cand = m_aTopRow[i];
				if (cand && NavRowVisible(cand))
				{
					FocusW(cand);
					return;
				}
			}
		}
	}

	//! Order m_aTopRow by where the buttons actually SIT on screen, left to right. The row is filled in
	//! a fixed code order, but the layout is the user's to rearrange (Templates was moved left of Side),
	//! and left/right navigation has to follow the eye, not the build order. Insertion sort — a handful
	//! of widgets, and the order is nearly always already correct so it costs almost nothing.
	protected void SortTopRowByX()
	{
		for (int i = 1; i < m_aTopRow.Count(); i++)
		{
			Widget cur = m_aTopRow[i];
			if (!cur)
				continue;
			float cx, cy;
			cur.GetScreenPos(cx, cy);
			int j = i - 1;
			while (j >= 0 && m_aTopRow[j])
			{
				float jx, jy;
				m_aTopRow[j].GetScreenPos(jx, jy);
				if (jx <= cx)
					break;
				m_aTopRow[j + 1] = m_aTopRow[j];
				j--;
			}
			m_aTopRow[j + 1] = cur;
		}
	}

	// Size-пункти зберігаються як ButtonWidget — приводимо до Widget-списку для спільного NavList.
	protected array<Widget> SizeItemsAsWidgets()
	{
		array<Widget> outw = {};
		foreach (ButtonWidget b : m_aSizeItems)
			outw.Insert(b);
		return outw;
	}

	protected bool NavList(array<Widget> list, Widget f, int dy)
	{
		int i = list.Find(f);
		if (i == -1)
			return false;
		if (dy == 0)
			return true;	// у списку, але рух горизонтальний — ковтаємо (не тікаємо з панелі)
		int n = list.Count();
		int k = i;
		for (int guard = 0; guard < n; guard++)
		{
			k = (k + dy + n) % n;
			Widget cand = list[k];
			if (cand && cand.IsVisible())
			{
				FocusW(cand);
				return true;
			}
		}
		return true;
	}

	protected bool NavRowVisible(notnull Widget w)
	{
		if (!w.IsVisible())
			return false;
		if (w == m_wSideOpener)
			return m_wSideCombo && m_wSideCombo.IsVisible();
		return true;
	}

	protected void FocusW(Widget w)
	{
		if (!w)
			return;
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (ws)
			ws.SetFocusedWidget(w);
	}

	//! Видима підсвітка сфокусованої кнопки (щокадру з шару). Рушійна фокус-рамка на наших
	//! прозорих/темних кнопках непомітна, тож тонуємо сам ButtonWidget і повертаємо базовий
	//! колір, коли фокус пішов.
	void TickFocusHighlight()
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		Widget f = null;
		if (ws)
			f = ws.GetFocusedWidget();

		Widget target = null;
		if (f && m_aWired.Find(f) != -1)
			target = f;

		// Підсвітка — ЛИШЕ для геймпада (фокусна навігація). На KB/M клік/ховер теж дає кнопці
		// фокус на кадр — білий тінт квадратної кнопки «блимав кутиками» довкола круглого фону.
		InputManager im = GetGame().GetInputManager();
		if (im && im.IsUsingMouseAndKeyboard())
			target = null;

		if (target == m_wLastFocusHl)
			return;	// нічого не змінилось

		// зняти стару підсвітку
		if (m_wLastFocusHl)
		{
			int base;
			if (m_mFocusBase.Find(m_wLastFocusHl, base))
				m_wLastFocusHl.SetColor(Color.FromInt(base));
		}
		m_wLastFocusHl = target;
		if (!target)
			return;

		// запамʼятати базовий колір (раз) і підсвітити
		if (!m_mFocusBase.Contains(target))
			m_mFocusBase.Set(target, target.GetColor().PackToInt());
		target.SetColor(Color.FromInt(0x90FFFFFF));	// напівпрозорий білий — видно і на прозорих, і на темних
	}

	// Публічний вихід для шару: закрити всі відкриті дропдауни (напр. при виході з панелі падом).
	// The templates tab is exempt: on the pad the flow CONTINUES on the map after leaving the panel
	// (place the ghost, A to apply), and the tab is that flow's UI. Its own opener/B closes it.
	void ClosePanelDropdowns()
	{
		bool tplOpen = IsTemplatesOpen();
		CloseDropdowns();
		if (tplOpen)
			m_wTemplatesDropdown.SetVisible(true);
	}

	protected void CloseDropdowns()
	{
		if (m_wSizeDropdown)    m_wSizeDropdown.SetVisible(false);
		if (m_wColorDropdown)   m_wColorDropdown.SetVisible(false);
		if (m_wVisDropdown)     m_wVisDropdown.SetVisible(false);
		if (m_wSideDropdown)    m_wSideDropdown.SetVisible(false);
		if (m_wOpacityDropdown) m_wOpacityDropdown.SetVisible(false);
		if (m_wTemplatesDropdown) m_wTemplatesDropdown.SetVisible(false);
	}

	protected void ToggleDropdown(Widget dd)
	{
		if (!dd)
		{
			Print("[SM-DD] toggle: dropdown widget is NULL (layout name not found at Build time)", LogLevel.NORMAL);
			return;
		}
		bool wasOpen = dd.IsVisible();
		CloseDropdowns();	// відкритий лише один
		if (!wasOpen)
			dd.SetVisible(true);

		// TEMP DIAG (host-screen dropdowns): says whether the widget opened at all, where it landed and
		// how big it is. Remove once the ATAK tablet case is understood.
		float sx, sy, sw, sh;
		dd.GetScreenPos(sx, sy);
		dd.GetScreenSize(sw, sh);
		Print(string.Format("[SM-DD] '%1' vis=%2 visHier=%3 pos=(%4,%5) size=(%6,%7)",
			dd.GetName(), dd.IsVisible(), dd.IsVisibleInHierarchy(), sx, sy, sw, sh), LogLevel.NORMAL);

		Widget bg = dd.FindAnyWidget("Background1");
		if (!bg)
			bg = dd.FindAnyWidget("Background");
		if (bg)
		{
			float bx, by, bw, bh;
			bg.GetScreenPos(bx, by);
			bg.GetScreenSize(bw, bh);
			Print(string.Format("[SM-DD]   bg vis=%1 pos=(%2,%3) size=(%4,%5)",
				bg.IsVisibleInHierarchy(), bx, by, bw, bh), LogLevel.NORMAL);
		}

		// Walk the whole parent chain: the dropdown reports a real size and vis=1 yet nothing shows, so
		// the culprit is an ancestor — one of them is clipping us, or hiding, or sitting at a Z that
		// buries us. Printing every link says which, instead of guessing one at a time.
		int depth = 0;
		for (Widget p = dd; p && depth < 12; p = p.GetParent())
		{
			float px, py, pw, ph;
			p.GetScreenPos(px, py);
			p.GetScreenSize(pw, ph);
			Print(string.Format("[SM-DD]   ^%1 '%2' vis=%3 z=%4 pos=(%5,%6) size=(%7,%8)",
				depth, p.GetName(), p.IsVisible(), p.GetZOrder(), px, py, pw, ph), LogLevel.NORMAL);
			depth++;
		}
	}

	//------------------------------------------------------------------------------
	void OnAction(int action, int param)
	{
		if (!m_Canvas)
			return;

		SCR_UISoundEntity.SoundEvent(SCR_SoundEvent.CLICK);	// звук натискання (ванільний клік UI)

		switch (action)
		{
			case ACT_PENCIL:
				AbortTemplateFlow();
				CloseDropdowns();
				if (m_Canvas.IsActive() && m_Canvas.GetTool() == 0)
					m_Canvas.SetActive(false);
				else
				{
					m_Canvas.SetTool(0);
					m_Canvas.SetActive(true);
				}
				break;

			case ACT_ERASER:
				AbortTemplateFlow();
				CloseDropdowns();
				if (m_Canvas.IsActive() && m_Canvas.GetTool() == 1)
					m_Canvas.SetActive(false);
				else
				{
					m_Canvas.SetTool(1);
					m_Canvas.SetActive(true);
				}
				break;

			case ACT_FILL:
				AbortTemplateFlow();
				CloseDropdowns();
				if (m_Canvas.IsActive() && m_Canvas.GetTool() == 2)
					m_Canvas.SetActive(false);
				else
				{
					m_Canvas.SetTool(2);
					m_Canvas.SetActive(true);
				}
				break;

			case ACT_TEMPLATES:
				CloseDropdowns();
				if (m_Canvas.IsActive() && m_Canvas.GetTool() == SM_DrawCanvas.TOOL_TEMPLATE)
					m_Canvas.SetActive(false);
				else
				{
					m_Canvas.SetTool(SM_DrawCanvas.TOOL_TEMPLATE);
					m_Canvas.SetActive(true);
				}
				break;

			case ACT_OPEN_TEMPLATES:
				ToggleDropdown(m_wTemplatesDropdown);
				if (m_wTemplatesDropdown && m_wTemplatesDropdown.IsVisible())
				{
					SM_TemplateStore.GetInstance().Reload();	// picks up anything dropped in the folder
					RefreshTemplates();
				}
				else
				{
					AbortTemplateFlow();	// closing the tab drops a framing/placement in progress
				}
				break;

			case ACT_TPL_SLOT:
				m_iTplHighlight = param;
				RefreshTemplates();				// re-tints; the pick is not acted on until Apply
				BuildTemplatePreview(TemplateForSlot(param));
				break;

			case ACT_TPL_APPLY:
				OnTemplateApply();
				break;

			case ACT_TPL_CANCEL:
				OnTemplateCancel();
				break;

			case ACT_TPL_ADD:
				OnTemplateAdd();
				break;

			case ACT_TPL_REMOVE:
				OnTemplateRemove();
				break;

			case ACT_TEMPLATE_SAVE:
				CloseDropdowns();
				if (m_Canvas.IsActive() && m_Canvas.GetTool() == SM_DrawCanvas.TOOL_SELECT)
				{
					m_Canvas.ClearSelection();	// leaving the tool drops the box
					m_Canvas.SetActive(false);
				}
				else
				{
					m_Canvas.SetTool(SM_DrawCanvas.TOOL_SELECT);
					m_Canvas.SetActive(true);
				}
				break;

			case ACT_OPEN_SIZE:
				ToggleDropdown(m_wSizeDropdown);
				break;

			case ACT_OPEN_COLOR:
				ToggleDropdown(m_wColorDropdown);
				break;

			case ACT_OPEN_VIS:
				ToggleDropdown(m_wVisDropdown);
				break;

			case ACT_OPEN_OPACITY:
				ToggleDropdown(m_wOpacityDropdown);
				break;

			case ACT_WIDTH:
				m_Canvas.SetWidthIdx(param);
				CloseDropdowns();
				RefreshShapePreviewIfAny();
				break;

			case ACT_COLOR:
				if (param >= 0 && param < COLORS.Count())
					m_Canvas.SetColor(COLORS[param]);
				CloseDropdowns();
				RefreshShapePreviewIfAny();
				break;

			case ACT_CHANNEL:
				m_Canvas.SetVisibility(param);
				CloseDropdowns();
				break;

			case ACT_OPEN_SIDE:
				ToggleDropdown(m_wSideDropdown);
				break;

			case ACT_GMSIDE:
				SM_GmState.s_iDrawSideChannel = FactionIndexForSide(param);
				CloseDropdowns();
				break;

			case ACT_GMLOCK:
				// Джерело істини — наш прапорець: інвертуємо і виставляємо чекбоксу (без гадань,
				// у якому порядку двигун сам тоглить стан на клік).
				SM_GmState.s_bDrawGmLock = !SM_GmState.s_bDrawGmLock;
				if (m_wGmLockCheck)
					m_wGmLockCheck.SetChecked(SM_GmState.s_bDrawGmLock);
				break;

			case ACT_GMHIDE:
				SM_GmState.s_bDrawHideInfo = !SM_GmState.s_bDrawHideInfo;
				if (m_wHideInfoCheck)
					m_wHideInfoCheck.SetChecked(SM_GmState.s_bDrawHideInfo);
				break;
		}

		// Пад-UX: відкриття дропдауна — фокус на перший пункт списку. Прозорість — окремо: у неї
		// слайдер, а не список, тож ЛИШАЄМО фокус на опенері (ліво/право крутить слайдер у NavMove).
		// Вибір ЗНАЧЕННЯ (колір/розмір/видимість/сторона) — лишаємось у панелі, повертаємо фокус на опенер
		// комбо. Інструмент/чекбокс — знімаємо фокус, повертаючи керування мапі для малювання.
		if (action == ACT_OPEN_SIZE || action == ACT_OPEN_COLOR || action == ACT_OPEN_VIS || action == ACT_OPEN_SIDE || action == ACT_OPEN_TEMPLATES)
			FocusDropdownFirst(action);
		else if (m_bPadFocus && action == ACT_TPL_SLOT)
		{
			// Picking a slot is browsing, not leaving: the pad stays on it so Y/X/A read naturally.
		}
		else if (action == ACT_TPL_ADD && m_bTplNaming)
		{
			// The name step just focused the edit box and write mode is summoning the (pad) screen
			// keyboard. ClearFocus here killed both on the very frame they were born.
		}
		else if (action == ACT_OPEN_OPACITY)
		{
			if (m_bPadFocus && m_wOpacityOpener)
				FocusW(m_wOpacityOpener);	// тримаємо фокус на опенері — слайдером керуємо звідси
		}
		else if (m_bPadFocus && (action == ACT_WIDTH || action == ACT_COLOR || action == ACT_CHANNEL || action == ACT_GMSIDE))
			RefocusOpener(action);
		else
			ClearFocus();

		UpdateVisuals();
	}

	protected void ClearFocus()
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;
		Widget f = ws.GetFocusedWidget();
		if (f && ContainsWidget(f))
			ws.SetFocusedWidget(null);
	}

	// Після вибору значення на паді — повернути фокус на опенер відповідного комбо, щоб гравець
	// лишався в панелі й міг навігувати далі (а не тиснути RB заново).
	protected void RefocusOpener(int selectAction)
	{
		Widget opener = null;
		switch (selectAction)
		{
			case ACT_WIDTH:   opener = m_wSizeOpener;  break;
			case ACT_COLOR:   opener = m_wColorOpener; break;
			case ACT_CHANNEL: opener = m_wVisOpener;   break;
			case ACT_GMSIDE:  opener = m_wSideOpener;  break;
		}
		if (opener)
			FocusW(opener);
	}

	// Після відкриття дропдауна — сфокусувати його перший пункт (якщо список реально відкрито).
	protected void FocusDropdownFirst(int openAction)
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;

		Widget first = null;
		if (openAction == ACT_OPEN_SIZE && m_wSizeDropdown && m_wSizeDropdown.IsVisible())
			first = m_wRoot.FindAnyWidget("Size1");
		else if (openAction == ACT_OPEN_COLOR && m_wColorDropdown && m_wColorDropdown.IsVisible())
			first = m_wRoot.FindAnyWidget("Color0");
		else if (openAction == ACT_OPEN_VIS && m_wVisDropdown && m_wVisDropdown.IsVisible())
			first = m_wRoot.FindAnyWidget("VisibilityLocal");
		else if (openAction == ACT_OPEN_SIDE && m_wSideDropdown && m_wSideDropdown.IsVisible() && m_wSideCombo)
			first = m_wSideCombo.FindAnyWidget("SideBLUFOR");
		else if (openAction == ACT_OPEN_TEMPLATES && m_wTemplatesDropdown && m_wTemplatesDropdown.IsVisible() && !m_aTplSlots.IsEmpty())
			first = m_aTplSlots[0];

		if (first)
			ws.SetFocusedWidget(first);
	}

	// Індекс фракції за стороною (0=BLUFOR/US, 1=OPFOR/USSR, 2=INDFOR/FIA). -1 якщо не знайдено.
	protected int FactionIndexForSide(int side)
	{
		string key = "US";
		if (side == 1)
			key = "USSR";
		else if (side == 2)
			key = "FIA";

		SCR_FactionManager fm = SCR_FactionManager.Cast(GetGame().GetFactionManager());
		if (!fm)
			return -1;
		array<Faction> all = {};
		fm.GetFactionsList(all);
		foreach (Faction f : all)
		{
			if (f && f.GetFactionKey() == key)
				return fm.GetFactionIndex(f);
		}
		return -1;
	}

	// Дефолт сторони для зевса: BLUFOR (US); якщо в сценарії нема US — перша не-цивільна фракція.
	// Кличемо з Build і (про запас) з UpdateVisuals — раптом FactionManager був ще не готовий.
	protected void EnsureSideDefault()
	{
		if (SM_GmState.s_iDrawSideChannel >= 0)
			return;	// вже обрано/виставлено

		int idx = FactionIndexForSide(0);	// BLUFOR (US)
		if (idx < 0)
		{
			// Fallback: перша не-цивільна фракція сценарію.
			SCR_FactionManager fm = SCR_FactionManager.Cast(GetGame().GetFactionManager());
			if (fm)
			{
				array<Faction> all = {};
				fm.GetFactionsList(all);
				foreach (Faction f : all)
				{
					if (!f)
						continue;
					string key = f.GetFactionKey();
					if (key == "" || key == "CIV")
						continue;
					idx = fm.GetFactionIndex(f);
					break;
				}
			}
		}
		if (idx >= 0)
			SM_GmState.s_iDrawSideChannel = idx;
	}

	// Розмір кружечка-превʼю на опенері ширини — той самий, що в пунктах списку (з layout).
	protected float SizePreviewPx(int idx)
	{
		switch (idx)
		{
			case 0: return 8;
			case 1: return 15;
			case 2: return 20;
			case 3: return 25;
			case 4: return 34;
			case 5: return 34;	// Size6 (100 m, eraser): the opener cell caps at 34px (the dropdown circle is 45 in layout)
		}
		return 34;
	}

	// Колір фону опенера сторони. Не обрано ("") — нейтральний сірий.
	protected int SideColorForName(string side)
	{
		if (side == "BLUFOR") return 0xFF0050A0;	// синій
		if (side == "OPFOR")  return 0xFF801010;	// червоний
		if (side == "INDFOR") return 0xFF128F31;	// зелений
		if (side == "")       return 0xFF525252;	// «Pick side» — сірий
		return 0xFF6D4C41;	// модова фракція — нейтральний коричневий
	}

	// Назва сторони за індексом фракції (для опенера). "" якщо не обрано/невідомо.
	protected string SideNameForFaction(int factionIndex)
	{
		if (factionIndex < 0)
			return "";
		SCR_FactionManager fm = SCR_FactionManager.Cast(GetGame().GetFactionManager());
		if (!fm)
			return "";
		Faction f = fm.GetFactionByIndex(factionIndex);
		if (!f)
			return "";
		string key = f.GetFactionKey();
		if (key == "US")   return "BLUFOR";
		if (key == "USSR") return "OPFOR";
		if (key == "FIA")  return "INDFOR";
		string n = f.GetFactionName();
		if (n != "")
			return n;
		return key;
	}

	//------------------------------------------------------------------------------
	// Відобразити поточний стан пензля на панелі.
	protected void UpdateVisuals()
	{
		if (!m_Canvas)
			return;

		bool active = m_Canvas.IsActive();
		int  tool   = m_Canvas.GetTool();

		// Підсвітка активного інструмента — через заокруглений фон Background1 (не колір кнопки,
		// бо кнопка прямокутна і світилась лише кутиками навколо заокругленого фону).
		TintBg(m_wPencil, active && tool == 0);
		TintBg(m_wErase,  active && tool == 1);
		TintBg(m_wFill,   active && tool == 2);

		// Опенер ширини: підпис = метри поточного пресета, кружечок = розмір як у списку.
		if (m_wSizeOpener)
		{
			TextWidget t = TextWidget.Cast(m_wSizeOpener.FindAnyWidget("Text0"));
			if (t)
			{
				int wm = Math.Round(SM_DrawCanvas.WidthMeters(m_Canvas.GetWidthIdx()));
				t.SetText(wm.ToString());
			}
			ImageWidget si = ImageWidget.Cast(m_wSizeOpener.FindAnyWidget("Image0"));
			if (si)
			{
				// Кружечок меншає, але його місце в ряду лишається 34px (паддінгом добираємо різницю),
				// інакше комірка стискається і текст зʼїжджає вліво.
				float px = SizePreviewPx(m_Canvas.GetWidthIdx());
				si.SetSize(px, px);
				float pad = (34 - px) * 0.5;
				AlignableSlot.SetPadding(si, 5 + pad, pad, 5 + pad, pad);	// базовий боковий паддінг 5 з layout
			}
		}

		// Кружечки розміру відображають інструмент: олівець — заливка (circle), гумка — контур (circleLine).
		if (tool != m_iSizeTexTool)
		{
			bool eraser = (tool == 1);

			ResourceName tex = TEX_CIRCLE;
			if (eraser)
				tex = TEX_CIRCLE_LINE;
			SwapChildTexture(m_wSizeOpener, tex);
			foreach (ButtonWidget sb : m_aSizeItems)
				SwapChildTexture(sb, tex);

			// The 100 m item (Size6) shows for the eraser only; the dropdown background grows +50 on Y.
			if (m_aSizeItems.Count() > SM_DrawCanvas.WIDTH_IDX_MAX_ERASER && m_aSizeItems[SM_DrawCanvas.WIDTH_IDX_MAX_ERASER])
				m_aSizeItems[SM_DrawCanvas.WIDTH_IDX_MAX_ERASER].SetVisible(eraser);
			if (m_wSizeDropdownBg)
			{
				if (eraser)
					FrameSlot.SetSizeY(m_wSizeDropdownBg, SIZE_BG_H_ERASER);
				else
					FrameSlot.SetSizeY(m_wSizeDropdownBg, SIZE_BG_H_BASE);
			}

			m_iSizeTexTool = tool;
		}

		// Опенер кольору: тонуємо кружечок поточним кольором.
		TintChildImage(m_wColorOpener, m_Canvas.GetColor());

		// Опенер прозорості: динамічний підпис «N%».
		if (m_wOpacityOpener)
		{
			TextWidget ot = TextWidget.Cast(m_wOpacityOpener.FindAnyWidget("Text0"));
			if (ot)
				ot.SetText(m_Canvas.GetOpacityPct().ToString() + "%");
		}

		// Опенер каналу: підпис + колір фону.
		if (m_wVisOpener)
		{
			int vis = m_Canvas.GetVisibility();
			TextWidget vt = TextWidget.Cast(m_wVisOpener.FindAnyWidget("Text0"));
			if (vt)
				vt.SetText(VisLabel(vis));
			Widget bg = m_wVisOpener.FindAnyWidget("Background1");
			if (bg)
				bg.SetColor(Color.FromInt(VisColor(vis)));
		}

		// GM-контроли (лише зевс): вибір сторони видно тільки коли канал = Side; чекбокси зі статиків.
		if (m_bEditorMap)
		{
			if (m_wSideCombo)
			{
				bool sideMode = (m_Canvas.GetVisibility() == SM_EMarkerVisibility.FACTION);
				m_wSideCombo.SetVisible(sideMode);
				if (!sideMode && m_wSideDropdown)
					m_wSideDropdown.SetVisible(false);
			}
			if (m_wSideOpener)
			{
				EnsureSideDefault();	// про запас: якщо FactionManager не був готовий у Build
				string sn = SideNameForFaction(SM_GmState.s_iDrawSideChannel);
				TextWidget st = TextWidget.Cast(m_wSideOpener.FindAnyWidget("Text0"));
				if (st)
				{
					if (sn == "")
						st.SetText("Pick side");	// сторона ще не обрана — Side-штрих бачитиме лише зевс
					else
						st.SetText(sn);
				}
				// Фон опенера — у колір обраної сторони (BLUFOR синій / OPFOR червоний / INDFOR зелений).
				Widget sbg = m_wSideOpener.FindAnyWidget("Background1");
				if (sbg)
					sbg.SetColor(Color.FromInt(SideColorForName(sn)));
			}
			if (m_wGmLockCheck)
				m_wGmLockCheck.SetChecked(SM_GmState.s_bDrawGmLock);
			if (m_wHideInfoCheck)
				m_wHideInfoCheck.SetChecked(SM_GmState.s_bDrawHideInfo);
		}
	}

	// Пофарбувати кружечок Image0 всередині кнопки.
	protected void TintChildImage(Widget btn, int argb)
	{
		if (!btn)
			return;
		ImageWidget img = ImageWidget.Cast(btn.FindAnyWidget("Image0"));
		if (img)
			img.SetColor(Color.FromInt(argb));
	}

	// Замінити текстуру кружечка Image0 всередині кнопки (вибір розміру: заливка/контур).
	protected void SwapChildTexture(Widget btn, ResourceName tex)
	{
		if (!btn)
			return;
		ImageWidget img = ImageWidget.Cast(btn.FindAnyWidget("Image0"));
		if (img)
			img.LoadImageTexture(0, tex);
	}

	//! Слайдер прозорості посунувся — передати пензлю (значення застосується до наступних малюнків).
	void OnOpacitySlider(float value)
	{
		if (!m_Canvas)
			return;
		m_Canvas.SetOpacityPct(Math.Round(value));
		// Живе оновлення підпису «N%» просто під час перетягування.
		if (m_wOpacityOpener)
		{
			TextWidget ot = TextWidget.Cast(m_wOpacityOpener.FindAnyWidget("Text0"));
			if (ot)
				ot.SetText(m_Canvas.GetOpacityPct().ToString() + "%");
		}
		RefreshShapePreviewIfAny();	// a shape preview is painted with the brush — follow it live
	}
}

//! "Really delete this template?" — the vanilla configurable dialog with our texts. Confirm deletes
//! the file; Cancel (and Esc, which lands in OnCancel) just puts the question away.
class SM_TemplateDeleteDialog : SCR_ConfigurableDialogUi
{
	protected SM_DrawPanel m_Panel;	// weak on purpose: the panel owns the dialog, not the other way round
	protected string m_sTemplateId;

	void SM_Setup(SM_DrawPanel panel, string id)
	{
		m_Panel = panel;
		m_sTemplateId = id;
	}

	override protected void OnConfirm()
	{
		if (m_Panel)
			m_Panel.ConfirmTemplateDelete(m_sTemplateId);
		super.OnConfirm();
	}

	override protected void OnCancel()
	{
		if (m_Panel)
			m_Panel.OnTemplateDeleteDialogClosed();
		super.OnCancel();
	}
}

//! Обробник слайдера прозорості: OnChange (тягнеться і при відпусканні).
class SM_OpacitySliderHandler : ScriptedWidgetEventHandler
{
	protected SM_DrawPanel m_Panel;

	void Setup(SM_DrawPanel panel)
	{
		m_Panel = panel;
	}

	override bool OnChange(Widget w, bool finished)
	{
		SliderWidget s = SliderWidget.Cast(w);
		if (s && m_Panel)
			m_Panel.OnOpacitySlider(s.GetCurrent());
		return true;
	}
}
