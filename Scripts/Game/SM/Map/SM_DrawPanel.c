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
	protected Widget m_wVisDropdown;
	protected Widget m_wOpacityDropdown;	// опційний (слайдер прозорості)
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
	void Build(notnull SM_DrawCanvas canvas, notnull Widget mapFrame, bool editorMap = false)
	{
		m_Canvas = canvas;
		m_bEditorMap = editorMap;
		m_wMapFrame = mapFrame;
		WorkspaceWidget ws = GetGame().GetWorkspace();

		m_wRoot = ws.CreateWidgets(PANEL_LAYOUT, mapFrame);
		if (!m_wRoot)
		{
			Print("[SM] Draw panel layout failed to load", LogLevel.WARNING);
			return;
		}
		m_wRoot.SetZOrder(100);	

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
		m_aTopRow.Insert(m_wVisOpener);
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
	void SetPadFocusMode(bool on)
	{
		m_bPadFocus = on;
		foreach (Widget w : m_aWired)
		{
			if (!w)
				continue;
			if (on)
				w.ClearFlags(WidgetFlags.NOFOCUS);
			else
				w.SetFlags(WidgetFlags.NOFOCUS);
		}
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
		return false;
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

		// (слайдер прозорості обробляє HandleNavInput — там авто-повтор на утриманні)

		// --- вертикальні списки: вгору/вниз із wrap-ом, невидимі пункти пропускаємо ---
		if (NavList(SizeItemsAsWidgets(), f, dy)) return;
		if (NavList(m_aVisItems,  f, dy)) return;
		if (NavList(m_aSideItems, f, dy)) return;

		// --- верхній ряд: вліво/вправо з wrap-ом, пропускаючи приховані (SideCombo поза Side-каналом) ---
		int ti = m_aTopRow.Find(f);
		if (ti != -1 && dx != 0)
		{
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
	void ClosePanelDropdowns()
	{
		CloseDropdowns();
	}

	protected void CloseDropdowns()
	{
		if (m_wSizeDropdown)    m_wSizeDropdown.SetVisible(false);
		if (m_wColorDropdown)   m_wColorDropdown.SetVisible(false);
		if (m_wVisDropdown)     m_wVisDropdown.SetVisible(false);
		if (m_wSideDropdown)    m_wSideDropdown.SetVisible(false);
		if (m_wOpacityDropdown) m_wOpacityDropdown.SetVisible(false);
	}

	protected void ToggleDropdown(Widget dd)
	{
		if (!dd)
			return;
		bool wasOpen = dd.IsVisible();
		CloseDropdowns();	// відкритий лише один
		if (!wasOpen)
			dd.SetVisible(true);
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
				CloseDropdowns();
				if (m_Canvas.IsActive() && m_Canvas.GetTool() == 2)
					m_Canvas.SetActive(false);
				else
				{
					m_Canvas.SetTool(2);
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
				break;

			case ACT_COLOR:
				if (param >= 0 && param < COLORS.Count())
					m_Canvas.SetColor(COLORS[param]);
				CloseDropdowns();
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
		if (action == ACT_OPEN_SIZE || action == ACT_OPEN_COLOR || action == ACT_OPEN_VIS || action == ACT_OPEN_SIDE)
			FocusDropdownFirst(action);
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
