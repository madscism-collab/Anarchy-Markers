// Адаптер мапи — єдине місце, що торкається ванільного фреймворку мапи. Якщо BI щось у мапі
// змінить, правити доведеться лише цей файл; ядро (дані/мережа/збереження) не зачіпається.
// Що тут робиться:
//   1. Глушимо ванільне розміщення міток (OnRadialMenuInit / OnInputQuickMarkerMenu стають no-op).
//      Ліву панель (компас/лінійка/годинник) не чіпаємо — це окремий SCR_MapToolMenuUI.
//   2. Малюємо наші мітки зі сховища власними віджетами, прикріпленими до MapFrame. Самі рахуємо:
//        - позицію: WorldToScreen щокадру, центр мітки точно на світовій точці;
//        - розмір: "як на папері" — base*factor, на макс. наближенні 1.0, при віддаленні менше;
//        - колір/поворот; картинку беремо з ванільного конфігу іконок (без хардкоду).
//      Хіт-тест точний, бо віджет збігається з видимою іконкою.
//   3. Ввід: подвійний клік — створити/редагувати, утримання ЛКМ на мітці — переміщення.

// Те, чим мітка намальована на мапі: іконка (або військовий символ) + підпис + позначка часу.
// Підпис це окремий віджет, не дитина іконки, щоб він не зсував саму мітку, а просто стояв під нею.
class SM_MarkerVisual
{
	ref SM_MapMarkerData m_Data;	// сильне посилання: для прев'ю дані живуть локально й мають
									// пережити SM_BeginPreview (без ref їх прибрав би GC і був би краш)
	ImageWidget      m_wIcon;		// цивільна іконка
	Widget           m_wSymbol;		// військовий APP-6 overlay
	SCR_MilitarySymbolUIComponent m_SymbolComp;	// малює символ на m_wSymbol
	TextWidget       m_wLabel;		// підпис під міткою
	TextWidget       m_wTime;		// позначка часу, окремим удвічі меншим шрифтом

	void SM_MarkerVisual(SM_MapMarkerData data)
	{
		m_Data = data;
	}

	// Головний віджет мітки — іконка або символ. По ньому рахуємо позицію й розмір.
	Widget GetMainWidget()
	{
		if (m_wIcon)
			return m_wIcon;
		return m_wSymbol;
	}

	void Destroy()
	{
		if (m_wIcon)
			m_wIcon.RemoveFromHierarchy();
		if (m_wSymbol)
			m_wSymbol.RemoveFromHierarchy();
		if (m_wLabel)
			m_wLabel.RemoveFromHierarchy();
		if (m_wTime)
			m_wTime.RemoveFromHierarchy();
		m_wIcon = null;
		m_wSymbol = null;
		m_SymbolComp = null;
		m_wLabel = null;
		m_wTime = null;
	}
}

// Візуал тимчасового «вказівника»: жовто-помаранчева точка (з текстури з альфою) + ім'я гравця.
class SM_PointerVisual
{
	ImageWidget m_wDot;
	TextWidget  m_wName;

	void Destroy()
	{
		if (m_wDot)  m_wDot.RemoveFromHierarchy();
		if (m_wName) m_wName.RemoveFromHierarchy();
		m_wDot = null;
		m_wName = null;
	}
}

modded class SCR_MapMarkersUI
{
	protected ref map<int, ref SM_MarkerVisual> m_mSMVisuals = new map<int, ref SM_MarkerVisual>();
	protected bool m_bSMMapOpen = false;
	protected bool m_bSMEditorMap = false;	// кеш: чи поточна мапа — режим редактора (GM)
	protected bool m_bSMPadConfirmDown = false;	// фронт кнопки A (AM_Confirm) на геймпаді
	protected bool m_bSMPadPlaceDown = false;	// фронт кнопки Y (AM_Place) на МАПІ — взяти/покласти мітку
	protected bool m_bSMPadDeleteDown = false;	// фронт кнопки X (AM_Delete) на МАПІ — видалити мітку
	// Консольна навігація діалогу (секційна модель).
	protected bool m_bSMNavActive = false;		// контролер активний (діалог відкрито на геймпаді)
	protected int  m_iSMNavLevel = 0;			// 0 = вибір секції, 1 = всередині секції
	protected int  m_iSMNavSection = 0;			// індекс поточної секції
	protected ref array<string> m_aSMNavContainers = {};	// контейнер кожної ВИДИМОЇ секції (по порядку)
	protected ref array<Widget> m_aSMNavItems = {};	// елементи поточної горизонтальної секції (наша навігація)
	protected int m_iSMNavItem = 0;			// індекс поточного елемента в горизонтальній секції
	protected ref array<ref array<Widget>> m_aSMNavGrid = {};	// рядки сітки (секція іконок)
	protected int m_iSMNavRow = 0;				// поточний рядок сітки
	protected int m_iSMNavCol = 0;				// поточна колонка сітки
	protected bool m_bSMNavGrid = false;		// поточна секція — сітка (іконки)
	protected bool m_bSMNavOnCheck = false;		// рівень секцій: підсвічена «бічна» галочка Text color (праворуч від Color)
	protected bool m_bSMNavTyping = false;		// активний режим вводу тексту (екранна клавіатура) — контролер на паузі
	protected bool m_bSMNavWriteSub = false;	// чи підписані на m_OnWriteModeLeave едітбокса
	protected bool m_bSMTextLock = false;		// KB/M: поле назви в режимі вводу — тримаємо фокус на ньому до Enter/Esc
	protected int  m_iSMTabIndex = 0;			// поточна «дозволена» вкладка іконок (для відкату випадкового LB/RB на паді)
	protected bool m_bSMNavParking = false;		// true під час «паркування» фокуса на кольорі (анти-пресет) — НЕ реальний вибір кольору
	protected bool m_bSMSuppressIconSelect = false;	// тимчасово ігнорувати авто-вибір іконки при зміні сторінки падом
	protected bool m_bSMPlaceDown = false;		// фронт кнопки Y (AM_Place) у діалозі
	protected bool m_bSMDeleteDown = false;		// фронт кнопки X (AM_Delete) — видалити сфокусований пресет
	protected ImageWidget m_wSMNavHL;			// жовто-оранжевий оверлей підсвітки поточної секції
	protected const int SM_NAV_HL_COLOR = 0x24FFA000;	// ледь помітний жовто-оранжевий (низька альфа)
	protected bool m_bSMSubscribed = false;
	protected Widget m_wSMMapFrame;		// MapFrame, до нього чіпляємо наші іконки
	protected bool   m_bSMOwnFrame = false;	// true якщо m_wSMMapFrame — наш створений оверлей (GM-мапа), треба прибрати

	// AM_EMapFeature mask for the map screen currently open. The player's map and the GM editor get
	// everything; tablets, terminals and anything else default to view-only, so our hotkey listeners
	// and panels stay out of other mods' map screens. See AM_MapFeatures.
	protected int m_iSMFeatures = 31;	// AM_MapFeatures.FULL

	protected bool SM_HasFeature(int f)
	{
		return (m_iSMFeatures & f) != 0;
	}

	// Render budget for always-on screens (AM_MapRenderPolicy). Radius 0 = no policy, current
	// behavior: a visual exists for every marker in the store. With a radius set, visuals exist
	// only near the view centre and the set is refreshed on a timer — cheap enough for a tablet
	// that never closes. Positions of what IS shown stay exact every frame either way.
	protected float m_fSMPolicyRadius;
	protected int   m_iSMPolicyMembershipMs;
	protected float m_fSMNextMembershipCheck;

	// Малювання на мапі (полотно + панель параметрів). Лише на звичайній мапі гравця.
	protected ref SM_DrawCanvas m_DrawCanvas;
	protected ref SM_DrawPanel  m_DrawPanel;
	protected bool m_bSMDrawDown;	// стан ЛКМ для захоплення штриха
	protected bool m_bSMDrawCursorHidden;	// ванільний курсор схований, бо активний інструмент малювання
	protected bool m_bSMPanelPadNav;	// гравець СВІДОМО зайшов падом у панель (хрестовина вправо)
	protected float m_fSMLastPanelBack;	// час останнього пад-B у панелі (дебаунс подвійних дій)
	protected bool m_bSMPadDrawDown;	// стан A при активному інструменті — пад-малювання затиснутою A
	protected bool m_bSMPadCancelDown;	// фронт B при активному інструменті — скасувати інструмент
	protected bool m_bSMPadUiGuard;	// «щит» від ванільних мапних дій пада (HandleDialog), поки панель/інструмент
	protected TextWidget m_wSMPlacePrompt;	// підказка «оберіть точку» біля курсора (режим Create Marker зевса)
	protected Widget m_wSMMapCursor;	// віджет курсора карти (ховаємо під час вказування пальцем)

	// Оптимізація рендера: масово переставляти мітки треба лише коли змінився вид (пан/зум) або набір міток
	protected bool  m_bSMNeedReposition = true;	// прапорець "треба перерахувати всі"
	protected float m_fSMLastZoom = -1;			// зум минулого кадру (щоб зловити зміну виду)
	protected int   m_iSMLastRefX = -99999;		// екранна позиція світового (0,0) — щоб зловити пан
	protected int   m_iSMLastRefY = -99999;

	// Базовий розмір іконки (px) на макс. наближенні при 100%; масштаб "папір" зменшує його при віддаленні.
	// 720 — це попередні 288 × 2.5, бо старі мітки виходили замалі.
	protected const float SM_BASE_SIZE = 720;
	// «Паперовий» масштаб має бути ЧИСТО пропорційним (currentPPU/maxPPU): мітка зменшується
	// разом зі світом без плато, як будівлі/поля. Тримаємо лише мікроскопічний епсилон, щоб розмір
	// не став 0 (не «приклеюється» до мапи, як було з 0.02 — тоді на дальніх зумах мітка застигала ~14px).
	protected const float SM_MIN_ZOOM_SCALE = 0.0005;
	protected const float SM_TEXT_RATIO = 0.22;		// шрифт підпису = розмір іконки * це
	protected const float SM_LABEL_OFFSET = 0.32;	// зсув ВЕРХУ підпису вниз від ЦЕНТРУ іконки = розмір * це
													// (іконки мають прозорі поля, тож кладемо ближче за нижній край віджета)
	protected const int   SM_TEXT_CHAR_LIMIT = 128;	// ліміт тексту мітки в байтах (ваніль = 16)
	// Наші іконки-серця: зарезервовані індекси (поза ванільним списком) + власний imageset.
	// Додаємо їх як власні кнопки в кінець вкладки General (конфіг-класи не модуємо — це валить .conf).
	protected const int   SM_HEART_ICON_BASE = 9000;	// 9000 = anarchyHeart1, 9001 = anarchyHeart2
	protected const ResourceName SM_HEART_IMAGESET = "{85C4164C04E12DB8}AnarchyHeart.imageset";

	// Ввід для переміщення: окремої клавіші нема — утримав ЛКМ на мітці, підняв, клікнув куди поставити.
	protected int   m_iSMCarryId = -1;		// мітка, яку зараз тягнемо (-1 = жодної)
	static bool s_bSMCarrying = false;		// читає SM_DisableRadial, щоб не відкривати радіалку під час перенесення
	protected float m_fSMLastSelectTime = 0;	// для детекту подвійного кліку
	protected bool  m_bSMLmbDown = false;	// стан ЛКМ минулого кадру (ловимо натиск/відпуск)
	protected bool  m_bSMPickedThisPress = false;	// підняли на цьому ж утриманні (щоб відпуск не поставив одразу)
	protected float m_fSMPressTime = 0;		// коли натиснули ЛКМ (для утримання-підняття)
	protected int   m_iSMPressMarkerId = -1;	// мітка під курсором у мить натиску
	protected float m_fSMPressX = 0;		// позиція курсора в мить натиску
	protected float m_fSMPressY = 0;
	// Шаблон останньої розміщеної/редагованої гравцем мітки (Alt+ЛКМ ставить її копію). Живе всю сесію.
	protected ref SM_MapMarkerData m_SMLastTemplate;
	// Стан режиму розміщення зевса (Create Marker): відстежуємо, щоб клік по самій кнопці не зарахувався.
	protected bool m_bSMWasCreatePending = false;
	protected bool m_bSMCreateSawRelease = false;
	protected bool m_bSMEditorUIHidden = false;	// чи ми сховали UI редактора на час нашого діалогу
	protected bool m_bSMEditorUISub = false;	// чи підписані на подію зміни видимості UI редактора
	protected const float SM_DOUBLECLICK_SEC = 0.3;
	protected const float SM_HOLD_SEC = 0.2;	// скільки тримати ЛКМ/A на мітці, щоб підняти (с)
	protected const float SM_POINT_HOLD_SEC = 0.3;	// скільки тримати на пустому місці, щоб почати вказувати пальцем (с)
	protected const float SM_MOVE_THRESHOLD = 40;	// на скільки px курсор може зрушити й це ще "на місці"
	// Вказівник (показати пальцем): утримання ЛКМ на пустому місці водить тимчасову точку.
	protected bool  m_bSMPointing = false;
	protected float m_fSMLastPointSend = 0;
	protected ref map<int, ref SM_PointerVisual> m_mSMPointerVis = new map<int, ref SM_PointerVisual>();	// ключ = ownerId
	protected const float SM_POINT_SEND    = 0.08;	// інтервал відправки позиції (~12/сек)
	protected const float SM_POINT_TIMEOUT = 1.5;	// без апдейту довше — точка зникає
	protected const int   SM_POINT_SIZE    = 112;	// розмір точки у відсотках
	protected const ResourceName SM_FINGER_TEX = "{172AA8C9D3BE7D0E}reforgerFinger.edds";
	protected const int   SM_FINGER_COLOR  = 0xFFFF6A00;	// насичений помаранчевий (краще видно)
	// Підказки керування — рядки з гліфами, як ванільні Pan/Zoom, внизу-зліва над ними.
	protected Widget m_wSMHintBox;			// контейнер рядків підказки
	protected ref array<Widget> m_aSMHintRows = {};	// рядки WLib_NavigationButtonSmall (гліф + підпис)
	protected int   m_iSMHintState = -1;	// 0 = звичайний, 1 = перенесення (щоб не смикати SetAction щокадру)
	protected const float SM_HINT_X = 14;	// відступ зліва (px)
	protected const float SM_HINT_Y = -184;	// наскільки підняти над ванільними Pan/Zoom (px)
	protected TextWidget m_wSMTooltip;		// тултіп "Edited by: <нік>" при наведенні на мітку
	protected TextWidget m_wSMTooltipVis;	// рядок видимості мітки під "Edited by", колір за областю
	protected int m_iSMTipWX = -999999;	// світова точка під курсором минулого кадру: тултіп оновлюємо
	protected int m_iSMTipWY = -999999;	// на її зміну (рух курсора АБО пан мапи стіком)

	// Діалог: перевикористовуємо ванільний CreateMarkerEditDialog, а підтвердження ловимо самі.
	protected int m_iSMEditId = -1;		// id мітки, яку редагуємо (-1 = створюємо нову)
	protected int m_iSMPlaceX;			// світова позиція нової/редагованої мітки
	protected int m_iSMPlaceY;
	protected int m_iSMHiddenMarkerId = -1;	// реальну мітку ховаємо на час редагування, щоб не двоїлась із прев'ю
	protected ref SM_MarkerVisual m_PreviewVisual;	// живе прев'ю на мапі, поки відкритий діалог

	// Наші контроли (розмір + видимість) у власному layout діалогу.
	protected int m_iSMSelectedSize = 200;	// розмір у відсотках (стандартно 200%)
	protected int m_iSMSelectedVis  = SM_EMarkerVisibility.ALL;
	protected int m_iSMMinVis        = SM_EMarkerVisibility.PERSONAL;	// при редагуванні не можна звузити видимість нижче поточної
	protected SCR_SliderComponent m_SMSizeSlider;					// повзунок "Size"
	protected ref array<Widget> m_aSMVisWidgets = {};	// кнопки видимості (індекс = SM_EMarkerVisibility)
	protected const float SM_VIS_OPACITY_SEL   = 1.0;	// вибрана кнопка — яскрава
	protected const float SM_VIS_OPACITY_UNSEL = 0.4;	// невибрана — приглушена (колір кнопки лишається)
	protected const float SM_VIS_OPACITY_LOCKED = 0.15;	// заблокована (звуження заборонено) — ледь видима

	// ВІЙСЬКОВА СЕКЦІЯ (stage 2) — Faction/Dimension/Type 1-2; тип мітки в діалозі (civ↔mil)
	protected int m_iSMSelKind = SM_EMarkerKind.CIVILIAN;	// поточний тип у діалозі
	protected int m_iSMSelIdentity;		// EMilitarySymbolIdentity (фракція/форма рамки)
	protected int m_iSMSelDimension;	// EMilitarySymbolDimension
	protected int m_iSMTypeAFlags;		// icons від ComboBox1
	protected int m_iSMTypeBFlags;		// icons від ComboBox2
	protected int m_iSMMilColor = 0xFFFFFFFF;	// колір вибраної фракції (дефолт кольору військової мітки)
	protected bool m_bSMMilColorUser;	// true → гравець клікнув колір у палітрі (перекриває колір фракції)

	protected bool m_bSMTextColored;	// галочка "Text": підпис у колір мітки (інакше чорний)
	protected CheckBoxWidget m_wSMTextCheck;	// рушійний чекбокс TextColorCheck (читаємо стан напряму)
	// GM-контроли (лише для зевса): кнопка «Side» циклічно обирає фракцію + галочка Locked.
	protected ref array<int> m_aSMGmFactionIdx = {};	// ігрові індекси фракцій сценарію (для циклу)
	protected int m_iSMGmFactionChosen = -1;	// обраний ігровий індекс фракції (для Side-мітки зевса)
	protected CheckBoxWidget m_wSMGmLockCheck;	// галочка Locked
	protected bool m_bSMGmLocked = false;		// стан Locked (передзаповнення при редагуванні)
	protected CheckBoxWidget m_wSMHideInfoCheck;	// галочка Hide info (ховати тултіп для гравців)
	protected bool m_bSMHideInfo = false;		// стан Hide info (передзаповнення при редагуванні)

	protected bool m_bSMTimestamp;	// показувати дату+час під міткою (Timestamp Yes/No)
	protected bool m_bSMBlackColor;	// вибрано нашу чорну кнопку (поза конфігом палітри)
	protected SCR_ButtonImageComponent m_SMBlackBtn;
	protected const int SM_BLACK_ARGB = 0xFF0A0A0A;	// майже чорний (трохи м'якший за чистий 0,0,0)
	protected int m_iSMSelFactionIdx = -1;
	protected int m_iSMSelDimensionIdx = -1;
	protected ref array<SCR_ButtonImageComponent> m_aSMFactionBtns = {};
	protected ref array<SCR_ButtonImageComponent> m_aSMDimensionBtns = {};

	// ВЛАСНИЙ список фракцій (замість ванільних). Рядок рендериться у ЗВОРОТНОМУ порядку,
	// тож індекс 0 = найправіша кнопка (і дефолт). Видимий порядок L→R: Unknown, Enemy, Enemy 2, Allied, Friendly.
	//   Friendly — синій прямокутник (BLUFOR): наша факція/союзники
	//   Allied   — зелений квадрат (INDFOR): дружня НЕ-наша фракція
	//   Enemy 2  — оранжевий ромб (OPFOR + оранжевий): другий ворог
	//   Enemy    — червоний ромб (OPFOR): ворог
	//   Unknown  — жовта конюшина (UNKNOWN): неопізнане
	protected ref array<int>    m_aSMFactIdentity = {EMilitarySymbolIdentity.BLUFOR, EMilitarySymbolIdentity.INDFOR, EMilitarySymbolIdentity.OPFOR, EMilitarySymbolIdentity.OPFOR, EMilitarySymbolIdentity.UNKNOWN};
	protected ref array<int>    m_aSMFactColor    = {0xFF2E6FE6, 0xFF3DA63D, 0xFFE88A2A, 0xFFD83A3A, 0xFFE8D24A};
	protected ref array<string> m_aSMFactLabel    = {"Friendly", "Allied", "Enemy 2", "Enemy", "Unknown"};
	// Ванільні назви (вмикаються конфігом). "Enemy 2" лишається — це наша додана фракція, ванільного аналога нема.
	protected ref array<string> m_aSMFactLabelVanilla = {"BLUFOR", "INDFOR", "Enemy 2", "OPFOR", "Unknown"};
	protected SCR_ComboBoxComponent m_SMComboA;
	protected SCR_ComboBoxComponent m_SMComboB;
	protected int m_iSMComboAIdx = 0;	// поточний індекс ComboBox1
	protected int m_iSMComboBIdx = 0;	// поточний індекс ComboBox2
	protected SCR_ComboBoxComponent m_SMNavOpenCombo;	// відкритий дропдаун комбобокса (контролер на паузі)

	// ПРЕСЕТИ (шаблони міток у профілі; див. SM_MapMarkerPresets)
	protected ref array<SCR_ButtonImageComponent> m_aSMMilPresetBtns = {};	// індекс = індекс військового пресета
	protected SCR_ButtonImageComponent m_SMMilAddBtn;	// кнопка «+» (зберегти поточну як військовий пресет)
	protected int  m_iSMHoveredMilPreset = -1;	// військовий пресет під курсором (для ПКМ-видалення)
	protected bool m_bSMRmbDown = false;		// стан ПКМ минулого кадру (детект фронту)
	protected ref array<SCR_ButtonImageComponent> m_aSMGenPresetBtns = {};	// загальні пресети
	protected SCR_ButtonImageComponent m_SMGenAddBtn;	// «+» загальних
	protected int  m_iSMHoveredGenPreset = -1;	// загальний пресет під курсором

	// Куди центрувати мапу при розміщенні (частка екрана). Діалог займає правий-нижній кут,
	// тож мітку ставимо у вільну верхньо-ліву зону, щоб гравець її бачив.
	protected const float SM_FREE_X = 0.30;
	protected const float SM_FREE_Y = 0.40;

	// 1. ГЛУШІННЯ ВАНІЛЬНИХ МІТОК
	override protected void OnRadialMenuInit()
	{
	}

	override protected void OnInputQuickMarkerMenu(float value, EActionTrigger reason)
	{
	}

	// 2. ЖИТТЄВИЙ ЦИКЛ + РЕНДЕР
	override void Init()
	{
		super.Init();
		SM_SubscribeStore();

		// Наше об'єднане вікно (зроблене у Workbench) замість ванільного діалогу.
		m_sEditBoxLayout = "{0386163FCAB81778}UI/layouts/Map/AnarchyMapMarkerEditBox.layout";

		// Задаємо layout-и селекторів у коді (ті самі, що й ванільні дефолти конфігу), щоб інстанс,
		// створений через 'new' (ін'єкція в мапу Game Master, див. SM_EditorMapConfig.c), був
		// самодостатнім і не залежав від атрибутів конфігу. Геймплейному інстансу це не шкодить —
		// значення ідентичні тим, що задає ванільний MapFullscreen.conf.
		m_sSelectorIconEntry      = "{DEA2D3B788CDCB4F}UI/layouts/Map/MapIconSelectorEntry.layout";
		m_sSelectorColorEntry     = "{8A5D43FC8AC6C171}UI/layouts/Map/MapColorSelectorEntry.layout";
		m_sSelectorDimensionEntry = "{4B6A50B3D8200779}UI/layouts/Map/MapDimensionSelectorEntry.layout";
		m_sMilitaryEditBoxLayout  = "{DF5BCE91F8A59977}UI/layouts/Map/MapMilitaryMarkerEditBox.layout";
		// Не layout, але теж конфіг-атрибути: для 'new'-інстанса (GM) їх треба задати вручну, інакше
		// m_iIconsPerLine=0 → кожна іконка лягає на нову лінію (іконки «стовпчиком»).
		m_iIconsPerLine    = 20;
		m_sIconImageset    = "{3262679C50EF4F01}UI/Textures/Icons/icons_wrapperUI.imageset";
		m_sCategoryIconName = "scenarios";
		m_sDeleteIconName  = "cancel";
	}

	protected void SM_SubscribeStore()
	{
		if (m_bSMSubscribed)
			return;

		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
		store.GetOnMarkerAdded().Insert(SM_OnMarkerAdded);
		store.GetOnMarkerChanged().Insert(SM_OnMarkerChanged);
		store.GetOnMarkerRemoved().Insert(SM_OnMarkerRemoved);
		m_bSMSubscribed = true;
	}

	override void OnMapOpen(MapConfiguration config)
	{
		super.OnMapOpen(config);
		m_bSMMapOpen = true;
		m_bSMEditorMap = (config && config.MapEntityMode == EMapEntityMode.EDITOR);	// кеш: режим не змінюється за час відкриття
		m_iSMFeatures = AM_MapFeatures.ResolveForOpen(config);	// what we attach to this map screen

		m_fSMPolicyRadius = 0;
		m_iSMPolicyMembershipMs = 0;
		m_fSMNextMembershipCheck = 0;
		AM_MapRenderPolicy policy = AM_MapFeatures.ResolvePolicyForOpen(config);
		if (policy)
		{
			m_fSMPolicyRadius = policy.m_fRadiusMeters;
			m_iSMPolicyMembershipMs = policy.m_iMembershipMs;
		}
		m_bSMNeedReposition = true;	// перший кадр — спозиціонувати все
		m_fSMLastZoom = -1;
		m_iSMLastRefX = -99999;
		m_iSMLastRefY = -99999;

		Widget mapRoot = m_MapEntity.GetMapMenuRoot();
		if (mapRoot)
		{
			m_wSMMapFrame = mapRoot.FindAnyWidget(SCR_MapConstants.MAP_FRAME_NAME);
		}
		// Мапа Game Master не має MapFrame (інший layout) — створюємо власний повноекранний оверлей,
		// бо наші мітки позиціонуються в екранних координатах.
		if (!m_wSMMapFrame)
			SM_CreateOwnMapFrame();
		// курсор (CursorImage) шукаємо ліниво в SM_SetMapCursorHidden — він на корені workspace, не під mapRoot

		// Полотно малювання + панель: на звичайній мапі гравця завжди (якщо дозволено конфігом);
		// у GM-мапі теж створюємо — рендер керується кнопкою «Player drawings», панель — «Drawing tools».
		// A view-only screen gets the canvas but no panel: drawings render, no tool can be armed.
		if (m_wSMMapFrame && SM_MarkerConfig.GetInstance().m_bAllowDrawing
			&& SM_HasFeature(AM_EMapFeature.DRAWINGS | AM_EMapFeature.DRAWING_TOOLS))
		{
			CanvasWidget cv = CanvasWidget.Cast(GetGame().GetWorkspace().CreateWidget(
				WidgetType.CanvasWidgetTypeID,
				WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
				Color.FromInt(0x00000000), 0, m_wSMMapFrame));	// z=0: штрихи під мітками й під панеллю
			if (cv)
			{
				FrameSlot.SetAnchorMin(cv, 0, 0);
				FrameSlot.SetAnchorMax(cv, 1, 1);
				m_DrawCanvas = new SM_DrawCanvas();
				m_DrawCanvas.Init(cv, m_MapEntity, m_wSMMapFrame, m_bSMEditorMap);
				m_DrawCanvas.SetRenderRadius(m_fSMPolicyRadius);
				if (SM_HasFeature(AM_EMapFeature.DRAWING_TOOLS))
				{
					m_DrawPanel = new SM_DrawPanel();
					m_DrawPanel.Build(m_DrawCanvas, m_wSMMapFrame, m_bSMEditorMap);
				}
			}
		}

		SM_GmState.s_OnMarkerViewChanged.Insert(SM_OnGmViewChanged);	// перебудова при перемиканні видимості зевса

		SM_RebuildAllVisuals();

		// ЛКМ (натиск/утримання/відпуск) обробляємо ПОЛЛІНГОМ сирого MouseLeft у Update —
		// MapSelect дає повторні/миттєві DOWN/UP і непридатний для детекту утримання.
		// Listeners go in only for the features this screen actually has. Hiding a panel is not
		// enough: a registered listener keeps swallowing the key while the map is open.
		InputManager im = GetGame().GetInputManager();
		bool featMarkerTools = SM_HasFeature(AM_EMapFeature.MARKER_TOOLS);
		bool featDrawTools = (m_DrawPanel != null);
		if (featMarkerTools || featDrawTools)
		{
			im.AddActionListener("MapContextualMenu", EActionTrigger.DOWN, SM_OnContext);
			im.AddActionListener("MapMarkerDelete",   EActionTrigger.DOWN, SM_OnDelete);
		}
		if (featDrawTools)
		{
			im.AddActionListener("AM_PanelFocus", EActionTrigger.DOWN, SM_OnPanelFocus);	// пад: LB → панель малювання

			// Пад-режим панелі: ванільні шорткати інструментів мапи (подвійний тап хрестовини = компас/
			// лінійка) не заглушити конфігом — тож відкочуємо: щойно інструмент увімкнувся в цей час,
			// одразу вимикаємо його назад.
			SCR_MapToolEntry.GetOnEntryToggledInvoker().Insert(SM_OnToolToggledGuard);
		}
		if (featMarkerTools)
		{
			// Консольна навігація діалогу (секційна модель): слухаємо Menu* DOWN, діємо лише коли активний контролер.
			im.AddActionListener("MenuUp",     EActionTrigger.DOWN, SM_NavUp);
			im.AddActionListener("MenuDown",   EActionTrigger.DOWN, SM_NavDown);
			im.AddActionListener("MenuLeft",   EActionTrigger.DOWN, SM_NavLeft);
			im.AddActionListener("MenuRight",  EActionTrigger.DOWN, SM_NavRight);
			im.AddActionListener("MenuSelect", EActionTrigger.DOWN, SM_NavSelect);
			im.AddActionListener("MenuBack",   EActionTrigger.DOWN, SM_NavBack);
			im.AddActionListener("MenuTabLeft",  EActionTrigger.DOWN, SM_NavTab);	// LB — перемикання вкладки іконок
			im.AddActionListener("MenuTabRight", EActionTrigger.DOWN, SM_NavTab);	// RB
			// Place (Y / AM_Place) опитуємо в SM_NavTick (надійніше за DOWN-слухача з фільтром Pressed).
		}

		if (featMarkerTools || featDrawTools)
			SM_BuildHint();		// no point advertising controls that aren't wired up
		if (SM_HasFeature(AM_EMapFeature.MARKERS))
			SM_BuildTooltip();	// тултіп при наведенні — у будь-якій мапі (зокрема GM)

		// Просимо актуальні мітки при кожному відкритті мапи (на хості SM_RequestSync сам no-op).
		// Гарантує мітки навіть якщо мапу відкрили з deploy-екрана ще до спавну (перший синк по
		// OnControlledEntityChanged тоді ще не спрацював). Ідемпотентно: сервер дошле лише дозволене, дублів нема по id.
		SCR_PlayerController syncPc = SM_LocalPC();
		if (syncPc)
		{
			syncPc.SM_RequestSync();
			syncPc.SM_RequestHostLocalSync();	// listen-host/SP: server sync is a no-op — activate own Locals from the persistence code
		}
	}

	override void OnMapClose(MapConfiguration config)
	{
		if (m_DrawCanvas)
			m_DrawCanvas.FinishLineChain();	// map closed mid-polyline; keep what was drawn

		SM_DrawOutbox.Flush();	// the map is closing and Tick stops — send whatever draw ops are still buffered

		SM_GmState.s_OnMarkerViewChanged.Remove(SM_OnGmViewChanged);
		if (m_bSMEditorUIHidden)
			SM_SetEditorUIHidden(false);	// мапа закрилась із відкритим діалогом — відписатись/повернути UI

		InputManager im = GetGame().GetInputManager();
		im.RemoveActionListener("MapContextualMenu", EActionTrigger.DOWN, SM_OnContext);
		im.RemoveActionListener("MapMarkerDelete",   EActionTrigger.DOWN, SM_OnDelete);
		im.RemoveActionListener("AM_PanelFocus",     EActionTrigger.DOWN, SM_OnPanelFocus);
		SCR_MapToolEntry.GetOnEntryToggledInvoker().Remove(SM_OnToolToggledGuard);
		im.RemoveActionListener("MenuUp",     EActionTrigger.DOWN, SM_NavUp);
		im.RemoveActionListener("MenuDown",   EActionTrigger.DOWN, SM_NavDown);
		im.RemoveActionListener("MenuLeft",   EActionTrigger.DOWN, SM_NavLeft);
		im.RemoveActionListener("MenuRight",  EActionTrigger.DOWN, SM_NavRight);
		im.RemoveActionListener("MenuSelect", EActionTrigger.DOWN, SM_NavSelect);
		im.RemoveActionListener("MenuBack",   EActionTrigger.DOWN, SM_NavBack);
		im.RemoveActionListener("MenuTabLeft",  EActionTrigger.DOWN, SM_NavTab);
		im.RemoveActionListener("MenuTabRight", EActionTrigger.DOWN, SM_NavTab);
		m_bSMNavActive = false;
		m_bSMPlaceDown = false;
		SM_NavDestroyHL();

		if (m_wSMHintBox)
		{
			m_wSMHintBox.RemoveFromHierarchy();
			m_wSMHintBox = null;
		}
		m_aSMHintRows.Clear();
		m_iSMHintState = -1;
		if (m_wSMTooltip)
		{
			m_wSMTooltip.RemoveFromHierarchy();
			m_wSMTooltip = null;
		}
		if (m_wSMTooltipVis)
		{
			m_wSMTooltipVis.RemoveFromHierarchy();
			m_wSMTooltipVis = null;
		}
		m_iSMTipWX = -999999;
		m_iSMTipWY = -999999;
		m_iSMCarryId = -1;
		s_bSMCarrying = false;
		m_bSMLmbDown = false;
		m_bSMPickedThisPress = false;
		m_iSMPressMarkerId = -1;

		// якщо закрили мапу під час вказування — коректно зупинити й прибрати точки
		if (m_bSMPointing)
		{
			m_bSMPointing = false;
			if (m_CursorModule)
				m_CursorModule.HandleDialog(false);	// повернути інфо-текст, якщо вказували при закритті
			SM_SetMapCursorHidden(false);
			SCR_PlayerController lpc = SM_LocalPC();
			if (lpc)
				lpc.SM_RequestPointStop();
		}
		foreach (int pid, SM_PointerVisual pv : m_mSMPointerVis)
		{
			if (pv)
				pv.Destroy();
		}
		m_mSMPointerVis.Clear();
		SM_PointerHub.GetInstance().Clear();

		m_bSMMapOpen = false;
		SM_EndPreview();
		SM_ClearAllVisuals();
		if (m_wSMPlacePrompt)
		{
			m_wSMPlacePrompt.RemoveFromHierarchy();
			m_wSMPlacePrompt = null;
		}
		if (m_DrawPanel)
		{
			m_DrawPanel.Destroy();
			m_DrawPanel = null;
		}
		if (m_DrawCanvas)
		{
			m_DrawCanvas.Destroy();
			m_DrawCanvas = null;
		}
		m_bSMDrawDown = false;
		if (m_bSMDrawCursorHidden)
		{
			// Курсор ховали під активний інструмент малювання. Віджет CursorImage живе на корені
			// workspace й переживає закриття мапи — тож ОБОВ'ЯЗКОВО вертаємо його видимість, інакше
			// при повторному відкритті ванільного курсора не буде (а наш кружечок ще не активний).
			SM_SetMapCursorHidden(false);
			m_bSMDrawCursorHidden = false;
		}
		if (m_bSMPadUiGuard)
		{
			m_bSMPadUiGuard = false;	// зняти «щит», якщо мапу закрили з активним інструментом/панеллю
			if (m_CursorModule)
				m_CursorModule.HandleDialog(false);
		}
		m_bSMPanelPadNav = false;

		if (m_bSMOwnFrame && m_wSMMapFrame)
			m_wSMMapFrame.RemoveFromHierarchy();	// наш оверлей GM-мапи — прибрати
		m_bSMOwnFrame = false;
		m_wSMMapFrame = null;
		m_wSMMapCursor = null;

		super.OnMapClose(config);
	}

	// Щокадрове позиціонування+масштаб. Тягнута мітка слідує за курсором (локально).
	override void Update(float timeSlice)
	{
		super.Update(timeSlice);

		if (!m_bSMMapOpen)
			return;

		// Консольний контролер навігації діалогу: на рівні секцій «пришпилюємо» фокус до поточної
		// секції щокадру, щоб вбудована геометрична навігація рушія не блукала між секціями.
		SM_NavTick();

		// Масштаб «папір»: 1.0 на макс. наближенні, менше при віддаленні.
		float maxZoom = m_MapEntity.GetMaxZoom();
		float factor = 1.0;
		if (maxZoom > 0)
			factor = Math.Clamp(m_MapEntity.GetCurrentZoom() / maxZoom, SM_MIN_ZOOM_SCALE, 1.0);

		WorkspaceWidget ws = GetGame().GetWorkspace();

		SM_PollMouse();		// натиск/утримання/відпуск ЛКМ (до позиціонування цього кадру)
		s_bSMCarrying = (m_iSMCarryId != -1);	// для SM_DisableRadial: ПКМ під час перенесення не відкриває радіалку

		if (m_fSMPolicyRadius > 0)
			SM_TickPolicyMembership();
		SM_UpdateHint();
		SM_UpdateTooltip();	// «Edited by» при наведенні
		if (SM_IsEditorMap())
			SM_UpdatePlacePrompt();	// підказка «оберіть точку» біля курсора (режим Create Marker)

		// Тягнута мітка слідує за курсором — оновлюємо ЩОКАДРУ (це одна мітка).
		if (m_iSMCarryId != -1)
		{
			int curWX, curWY;
			if (SM_GetCursorWorld(curWX, curWY))
			{
				SM_MarkerVisual cv = m_mSMVisuals.Get(m_iSMCarryId);
				if (cv)
					SM_PositionVisual(cv, curWX, curWY, factor, ws);
			}
		}

		// Решту репозиціонуємо ЛИШЕ коли змінився вид (пан/зум) або набір міток — інакше пропускаємо.
		// (Розміри/масштаб не страждають: SM_PositionVisual рахує їх щоразу, а зум = «вид змінено».)
		if (SM_DetectViewChange() || m_bSMNeedReposition)
		{
			float fw, fh;
			SM_GetFrameSizeUnscaled(fw, fh, ws);

			foreach (int id, SM_MarkerVisual vis : m_mSMVisuals)
			{
				if (!vis || id == m_iSMCarryId || id == m_iSMHiddenMarkerId)
					continue;	// тягнута — окремо вище; прихована при редагуванні — не чіпаємо

				int sx, sy;
				m_MapEntity.WorldToScreen(vis.m_Data.m_iPosX, vis.m_Data.m_iPosY, sx, sy, true);
				float usx = ws.DPIUnscale(sx);
				float usy = ws.DPIUnscale(sy);
				float margin = SM_BASE_SIZE * SM_SizeFactor(vis.m_Data.m_iSize) * factor;

				Widget mw = vis.GetMainWidget();
				if (usx < -margin || usx > fw + margin || usy < -margin || usy > fh + margin)
				{
					// КУЛЛІНГ: поза екраном — ховаємо й не позиціонуємо
					if (mw) mw.SetVisible(false);
					if (vis.m_wLabel) vis.m_wLabel.SetVisible(false);
					if (vis.m_wTime) vis.m_wTime.SetVisible(false);
					continue;
				}

				if (mw) mw.SetVisible(true);
				if (vis.m_wLabel) vis.m_wLabel.SetVisible(true);
				if (vis.m_wTime) vis.m_wTime.SetVisible(vis.m_Data.m_iDate != 0);	// позначка часу — за датою
				SM_PositionVisual(vis, vis.m_Data.m_iPosX, vis.m_Data.m_iPosY, factor, ws);
			}
			m_bSMNeedReposition = false;
		}

		// Живе прев'ю в діалозі: оновлюємо вигляд із поточних виборів і показуємо в точці розміщення.
		// Сторож від «привидів»: якщо діалог закрився в обхід нашого CleanupMarkerEditWidget (напр. конфлікт
		// іншого мапного мода перехопив закриття) — самотужки прибираємо залишки наступним кадром:
		// знищуємо «зависле» живе прев'ю та повертаємо видимість схованої на час редагування реальної мітки.
		if (!m_MarkerEditRoot)
		{
			if (m_PreviewVisual)
				SM_EndPreview();
			if (m_iSMHiddenMarkerId != -1)
			{
				SM_SetMarkerVisible(m_iSMHiddenMarkerId, true);
				SM_MarkerVisual hv = m_mSMVisuals.Get(m_iSMHiddenMarkerId);
				if (hv && hv.m_Data)
					SM_PositionVisual(hv, hv.m_Data.m_iPosX, hv.m_Data.m_iPosY, factor, ws);
				m_iSMHiddenMarkerId = -1;
			}
		}

		if (m_MarkerEditRoot && m_PreviewVisual)
		{
			SM_UpdatePreviewData();
			SM_ApplyVisualData(m_PreviewVisual);
			SM_PositionVisual(m_PreviewVisual, m_iSMPlaceX, m_iSMPlaceY, factor, ws);
		}

		// Підсвітку вибраної кнопки видимості тримаємо щокадру — інакше hover-анімація WLib-кнопки
		// скине нашу прозорість і вибір «згубиться».
		if (m_MarkerEditRoot)
			SM_UpdateVisHighlight();

		// KB/M фокус-лок: поки поле назви в режимі вводу, не даємо ховеру сусідньої секції перехопити
		// клавіатуру — якщо write-mode злетів, одразу повертаємо його на поле. Лок знімають лише Enter/Esc.
		if (m_bSMTextLock && m_MarkerEditRoot && m_EditBoxComp && SM_NavOnKBM())
		{
			EditBoxWidget eb = EditBoxWidget.Cast(m_EditBoxComp.m_wEditBox);
			if (eb && !eb.IsInWriteMode())
				m_EditBoxComp.ActivateWriteMode(true);
		}

		SM_UpdatePointers(factor, ws);	// тимчасові вказівники (показати пальцем)

		// Малювання: перемальовка полотна за потреби; панель ховаємо під час діалогу редагування мітки.
		// GM-мапа: рендер штрихів — за кнопкою «Player drawings», панель — за кнопкою «Drawing tools».
		if (m_DrawCanvas)
		{
			if (m_bSMEditorMap)
				m_DrawCanvas.SetRenderEnabled(SM_GmState.s_bDrawView);
			m_DrawCanvas.Tick();
			// Поки інструмент активний — ванільний курсор мапи ховаємо (його заміняє наш кружечок).
			// Ховаємо ЩОКАДРУ: під час пану ваніль інакше вертає курсор з іконкою перетягування.
			bool drawCur = m_DrawCanvas.IsActive();
			if (drawCur)
				SM_SetMapCursorHidden(true);
			else if (m_bSMDrawCursorHidden)
				SM_SetMapCursorHidden(false);
			m_bSMDrawCursorHidden = drawCur;
		}
		if (m_DrawPanel)
		{
			bool baseAllowed = !m_MarkerEditRoot;	// ховаємо під час діалогу мітки / вимкнення зевсом
			if (m_bSMEditorMap)
				baseAllowed = baseAllowed && SM_GmState.s_bDrawPanel;
			m_DrawPanel.SetVisible(baseAllowed);
			m_DrawPanel.TickFocusHighlight();

			// Анти-автофокус: рушій сам фокусить перший focusable-віджет при відкритті мапи.
			// Якщо фокус на панелі без свідомого входу — знімаємо, інакше пад залипає в меню.
			WorkspaceWidget fws = GetGame().GetWorkspace();
			bool focusInPanel = false;
			if (fws)
			{
				Widget pf = fws.GetFocusedWidget();
				if (pf && m_DrawPanel.ContainsWidget(pf))
				{
					if (!m_bSMPanelPadNav)
						fws.SetFocusedWidget(null);
					else
						focusInPanel = true;
				}
				else if (m_bSMPanelPadNav)
				{
					// Фокус пішов із панелі (вибір значення зняв його) — виходимо з пад-режиму.
					SM_PanelExit();
				}
			}

			InputManager gim = GetGame().GetInputManager();
			bool onPad = gim && !gim.IsUsingMouseAndKeyboard();

			// Той самий контекст, що й діалог мітки: тримає мапу відкритою на B
			// (B тоді фаєрить MenuBack, його ловить SM_NavBack). Поновлюється щокадру.
			if (m_bSMPanelPadNav && onPad)
				gim.ActivateContext("MapMarkerEditContext");

			if (focusInPanel && onPad)
				m_DrawPanel.HandleNavInput(gim);

			// Щит від ванільних пад-дій мапи (d-pad = меню інструментів тощо), лише поки
			// гравець падом у панелі. Активний інструмент мапу не глушить — стік має панорамувати.
			bool wantGuard = onPad && !m_MarkerEditRoot && !m_bSMPointing && focusInPanel;
			if (wantGuard != m_bSMPadUiGuard)
			{
				m_bSMPadUiGuard = wantGuard;
				if (m_CursorModule)
					m_CursorModule.HandleDialog(wantGuard);
			}
			// Панель закрили (зевс вимкнув кнопку) — інструмент теж вимикаємо, щоб ЛКМ повернувся редактору.
			if (m_bSMEditorMap && !SM_GmState.s_bDrawPanel && m_DrawCanvas && m_DrawCanvas.IsActive())
				m_DrawCanvas.SetActive(false);

			// Вигляд панелі + підказка R1 залежно від того, де зараз гравець (лише геймпад).
			if (!baseAllowed)
			{
				m_DrawPanel.SetHintMode(0);
			}
			else if (!onPad)
			{
				m_DrawPanel.SetPanelOpacity(1.0);
				m_DrawPanel.SetHintMode(0);
			}
			else if (m_bSMPanelPadNav)
			{
				m_DrawPanel.SetPanelOpacity(1.0);
				m_DrawPanel.SetHintMode(3);
			}
			else if (m_DrawCanvas && m_DrawCanvas.IsActive())
			{
				m_DrawPanel.SetPanelOpacity(0.35);
				m_DrawPanel.SetHintMode(2);
			}
			else
			{
				m_DrawPanel.SetPanelOpacity(0.0);
				m_DrawPanel.SetHintMode(1);
			}
		}
	}

	// Рендер тимчасових вказівників із хабу: створюємо/оновлюємо точку+ім'я, прибираємо застарілі.
	protected void SM_UpdatePointers(float factor, WorkspaceWidget ws)
	{
		float now = System.GetTickCount() / 1000.0;
		SM_PointerHub hub = SM_PointerHub.GetInstance();
		hub.PruneStale(now, SM_POINT_TIMEOUT);
		map<int, ref SM_PointerData> pts = hub.GetAll();

		// прибрати візуали тих, кого вже немає
		array<int> gone = {};
		foreach (int id, SM_PointerVisual pv : m_mSMPointerVis)
		{
			if (!pts.Contains(id))
				gone.Insert(id);
		}
		foreach (int id : gone)
		{
			if (m_mSMPointerVis[id])
				m_mSMPointerVis[id].Destroy();
			m_mSMPointerVis.Remove(id);
		}

		// створити/оновити та спозиціонувати
		foreach (int id, SM_PointerData p : pts)
		{
			if (!p)
				continue;
			SM_PointerVisual pv = m_mSMPointerVis.Get(id);
			if (!pv)
			{
				pv = SM_BuildPointerVisual(id);
				if (!pv)
					continue;
				m_mSMPointerVis.Set(id, pv);
			}
			SM_PositionPointer(pv, p.m_iPosX, p.m_iPosY, factor, ws);
		}
	}

	protected SM_PointerVisual SM_BuildPointerVisual(int ownerId)
	{
		if (!m_wSMMapFrame)
			return null;
		WorkspaceWidget ws = GetGame().GetWorkspace();

		// BLEND обов'язковий — вмикає альфа-змішування (без нього текстура малюється непрозорим квадратом).
		ImageWidget dot = ImageWidget.Cast(ws.CreateWidget(
			WidgetType.ImageWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.BLEND | WidgetFlags.STRETCH | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(SM_FINGER_COLOR), 0, m_wSMMapFrame));
		if (!dot)
			return null;
		dot.LoadImageTexture(0, SM_FINGER_TEX);
		dot.SetColor(Color.FromInt(SM_FINGER_COLOR));	// тонуємо білу текстуру (форма/розмиття — в альфі)
		FrameSlot.SetAnchorMin(dot, 0, 0);
		FrameSlot.SetAnchorMax(dot, 0, 0);
		FrameSlot.SetAlignment(dot, 0.5, 0.5);	// пивот центр → SetPos позиціонує центр
		FrameSlot.SetSizeToContent(dot, false);

		TextWidget name = SM_BuildLabel();	// підпис під точкою (той самий стиль, що й мітки)
		if (name)
		{
			string n = "";
			PlayerManager pm = GetGame().GetPlayerManager();
			if (pm)
				n = pm.GetPlayerName(ownerId);
			name.SetText(n);
			name.SetColor(Color.FromInt(SM_FINGER_COLOR));
		}

		SM_PointerVisual pv = new SM_PointerVisual();
		pv.m_wDot = dot;
		pv.m_wName = name;
		return pv;
	}

	protected void SM_PositionPointer(SM_PointerVisual pv, int wx, int wy, float factor, WorkspaceWidget ws)
	{
		if (!pv || !pv.m_wDot)
			return;
		int sx, sy;
		m_MapEntity.WorldToScreen(wx, wy, sx, sy, true);
		float usx = ws.DPIUnscale(sx);
		float usy = ws.DPIUnscale(sy);

		float size = SM_BASE_SIZE * SM_SizeFactor(SM_POINT_SIZE) * factor;
		FrameSlot.SetSize(pv.m_wDot, size, size);
		FrameSlot.SetPos(pv.m_wDot, usx, usy);	// пивот центр → точка центрована на курсорі

		if (pv.m_wName)
		{
			float font = size * SM_TEXT_RATIO;
			if (font < 2.0)
				pv.m_wName.SetVisible(false);
			else
			{
				pv.m_wName.SetVisible(true);
				pv.m_wName.SetExactFontSize(Math.Round(font));
				FrameSlot.SetPos(pv.m_wName, usx, usy + size * SM_LABEL_OFFSET);
			}
		}
	}

	// Ховає/повертає віджет курсора карти (тановий вказівник) поки показуємо пальцем.
	// SCR_CursorCustom створює свій лейаут на корені WORKSPACE (не під mapRoot), а зображення
	// зветься "CursorImage". Кешуємо ліниво: на момент старту вказування він точно існує.
	protected void SM_SetMapCursorHidden(bool hidden)
	{
		if (!m_wSMMapCursor)
		{
			WorkspaceWidget ws = GetGame().GetWorkspace();
			if (ws)
				m_wSMMapCursor = ws.FindAnyWidget("CursorImage");
		}
		if (m_wSMMapCursor)
			m_wSMMapCursor.SetVisible(!hidden);
	}

	// Поставити копію останньої розміщеної/редагованої мітки в точці курсора (Alt+ЛКМ).
	protected void SM_PlaceCopyAtCursor()
	{
		if (!m_SMLastTemplate)
			return;
		int cwx, cwy;
		if (!SM_GetCursorWorld(cwx, cwy))
			return;
		SCR_PlayerController cpc = SM_LocalPC();
		if (!cpc)
			return;
		SM_MapMarkerData copy = m_SMLastTemplate.SM_Clone();
		copy.m_iId = -1;		// нова мітка — id призначить сервер (або LocalCreate для Local)
		copy.m_iPosX = cwx;
		copy.m_iPosY = cwy;

		// A Local copy stays client-side; anything else goes to the server.
		if (copy.m_iVisibility == SM_EMarkerVisibility.PERSONAL)
		{
			copy.m_iOwnerId = cpc.GetPlayerId();
			SM_LocalMarkerPersistence.GetInstance().AddLocal(copy);
		}
		else
			cpc.SM_RequestPlace(copy.PackInts(), copy.m_sText);
	}

	// Commit a marker move: Local (id <= -2) moves in the client file, server ones via RPC.
	protected void SM_DoMoveMarker(int id, int wx, int wy)
	{
		if (SM_MapMarkerStore.IsLocalId(id))
		{
			SM_LocalMarkerPersistence.GetInstance().MoveLocal(id, wx, wy);
			return;
		}
		SCR_PlayerController pc = SM_LocalPC();
		if (pc)
			pc.SM_RequestMove(id, wx, wy);
	}

	// Delete a marker: Local (id <= -2) from the client file, server ones via RPC. (Called after the GM-lock check.)
	protected void SM_DeleteMarkerById(int id)
	{
		if (SM_MapMarkerStore.IsLocalId(id))
		{
			SM_LocalMarkerPersistence.GetInstance().RemoveLocal(id);
			return;
		}
		SCR_PlayerController pc = SM_LocalPC();
		if (pc)
			pc.SM_RequestRemove(id);
	}

	// Модифікатор копії «останньої мітки» (Alt). Власний input-екшен AM_CopyModifier (фільтр
	// InputFilterPressed → значення 1, поки клавішу затиснуто) доданий у ПЕРЕКРИТОМУ
	// chimeraInputCommon.conf і прив'язаний до наявних контекстів: MapContext (мапа редактора/зевса)
	// і GadgetMapContext (ігрова мапа гравця). Власний контекст НЕ працює — додані в override
	// контексти не активуються рушієм, тож чіпляємо дію до вбудованих.
	protected bool SM_CopyModifierDown()
	{
		InputManager im = GetGame().GetInputManager();
		return im && im.GetActionValue("AM_CopyModifier") > 0.5;
	}

	// Чи дія гравця по мітці заблокована (мітка залочена зевсом, а ми не в редакторі).
	// Якщо так — одразу показуємо локальне повідомлення + звук і повертаємо true (дію скасувати).
	protected bool SM_BlockedByLock(SM_MapMarkerData d)
	{
		if (!d || d.m_iGmLocked == 0 || SM_IsEditorMap())
			return false;
		SCR_PlayerController pc = SM_LocalPC();
		if (pc)
			pc.SM_ShowPlaceDenied(SM_EPlaceDenyReason.MARKER_LOCKED, 0);
		return true;
	}

	// Чи це мапа Game Master (редактор). Кешується в OnMapOpen (режим не змінюється під час відкриття).
	protected bool SM_IsEditorMap()
	{
		return m_bSMEditorMap;
	}

	// РЕАКЦІЯ НА ЗМІНИ СХОВИЩА
	protected void SM_OnMarkerAdded(SM_MapMarkerData data)
	{
		if (m_bSMMapOpen && data)
			SM_CreateVisual(data);
	}

	protected void SM_OnMarkerChanged(SM_MapMarkerData data)
	{
		if (!m_bSMMapOpen || !data)
			return;

		SM_OnMarkerRemoved(data.m_iId);
		SM_CreateVisual(data);
	}

	protected void SM_OnMarkerRemoved(int markerId)
	{
		// Якщо саме цю мітку хтось видалив, поки ми її «несли» — скидаємо перенесення,
		// інакше лишилися б у режимі carry з неіснуючою міткою (на drop сервер усе одно зробить no-op за id).
		if (markerId == m_iSMCarryId)
		{
			m_iSMCarryId = -1;
			m_bSMPickedThisPress = false;
		}

		SM_MarkerVisual vis = m_mSMVisuals.Get(markerId);
		if (vis)
		{
			vis.Destroy();
			m_mSMVisuals.Remove(markerId);
		}
	}

	// ПОБУДОВА ВІЗУАЛІВ
	protected void SM_RebuildAllVisuals()
	{
		SM_ClearAllVisuals();

		array<SM_MapMarkerData> all = {};
		SM_MapMarkerStore.GetInstance().GetAll(all);
		foreach (SM_MapMarkerData data : all)
		{
			if (data)
				SM_CreateVisual(data);
		}
	}

	protected void SM_ClearAllVisuals()
	{
		foreach (int id, SM_MarkerVisual vis : m_mSMVisuals)
		{
			if (vis)
				vis.Destroy();
		}
		m_mSMVisuals.Clear();
	}

	// Створює власний повноекранний FrameWidget-оверлей на workspace для мап без MapFrame (GM-мапа).
	// Мітки чіпляються до нього як FrameSlot-діти; позиції в нас екранні, тож оверлей анкоримо 0..1.
	protected void SM_CreateOwnMapFrame()
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;

		// Батько — корінь самої мапи (її шар), щоб оверлей був ПІД панелями редактора, а не поверх UI.
		// Резерв — корінь workspace, якщо раптом нема кореня мапи.
		Widget host = m_MapEntity.GetMapMenuRoot();
		if (!host)
			host = ws;

		Widget overlay = ws.CreateWidget(
			WidgetType.FrameWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(0x00000000), 0, host);
		if (!overlay)
			return;

		FrameSlot.SetAnchorMin(overlay, 0, 0);
		FrameSlot.SetAnchorMax(overlay, 1, 1);
		FrameSlot.SetOffsets(overlay, 0, 0, 0, 0);

		m_wSMMapFrame = overlay;
		m_bSMOwnFrame = true;
	}

	// Ховає/повертає UI редактора на час нашого діалогу створення мітки.
	// SCR_MenuEditorComponent.SetVisible ховає HUD + «hideable» частину, але НЕ «always shown» (тулбар),
	// і кліки повз діалог протікають у редактор (він знову показує панелі). Тому додатково кладемо
	// повноекранний бекдроп, що поглинає ввід (НЕ IGNORE_CURSOR) і затемнює залишки UI редактора.
	// ВАЖЛИВО: кликати ДО створення діалогу, щоб бекдроп був ПІД ним.
	// Ховає/повертає UI редактора на час нашого діалогу. Редактор САМ показує свій UI на будь-який клік
	// (авто-поведінка через m_aShowActions). Тому підписуємось на його подію зміни видимості й, щойно він
	// проявляється, ховаємо назад — подієво, без полінгу. Без бекдропа: мапу/передперегляд не затемнює.
	protected void SM_SetEditorUIHidden(bool hidden)
	{
		SCR_MenuEditorComponent menuEd = SCR_MenuEditorComponent.Cast(SCR_MenuEditorComponent.GetInstance(SCR_MenuEditorComponent));
		if (!menuEd)
		{
			m_bSMEditorUIHidden = hidden;
			return;
		}

		if (hidden)
		{
			if (!m_bSMEditorUISub)
			{
				menuEd.GetOnVisibilityChange().Insert(SM_OnEditorUIVisibilityChanged);
				m_bSMEditorUISub = true;
			}
			menuEd.SetVisible(false, true);
		}
		else
		{
			if (m_bSMEditorUISub)
			{
				menuEd.GetOnVisibilityChange().Remove(SM_OnEditorUIVisibilityChanged);
				m_bSMEditorUISub = false;
			}
			GetGame().GetCallqueue().Remove(SM_RehideEditorUI);	// прибрати відкладене ре-ховання, якщо є
			menuEd.SetVisible(true, true);
		}
		m_bSMEditorUIHidden = hidden;
	}

	// Редактор сам показав свій UI (клік тощо) — ховаємо назад, поки наш діалог відкритий.
	// Цей обробник викликається ВСЕРЕДИНІ Invoke події SetVisible, тож SetVisible(false) тут напряму
	// дав би рекурсивний Invoke (краш). Тому відкладаємо ре-ховання на наступний кадр через CallLater.
	protected void SM_OnEditorUIVisibilityChanged(bool visible)
	{
		if (!visible || !m_bSMEditorUIHidden)
			return;
		GetGame().GetCallqueue().Remove(SM_RehideEditorUI);
		GetGame().GetCallqueue().CallLater(SM_RehideEditorUI, 1, false);
	}

	protected void SM_RehideEditorUI()
	{
		if (!m_bSMEditorUIHidden)
			return;
		SCR_MenuEditorComponent menuEd = SCR_MenuEditorComponent.Cast(SCR_MenuEditorComponent.GetInstance(SCR_MenuEditorComponent));
		if (menuEd)
			menuEd.SetVisible(false, true);
	}

	// Назва сторони за ІГРОВИМ індексом фракції: BLUFOR/OPFOR/INDFOR для стандартних US/USSR/FIA,
	// інакше — назва фракції з гри. "" якщо невідомо. Для тултіпа сторони в GM-мапі.
	// (Не плутати з SM_FactionLabel — той за індексом селектора військового діалогу.)
	// Колір сторони за ІГРОВИМ індексом фракції: BLUFOR — темно-синій, OPFOR — темно-червоний,
	// INDFOR — темно-зелений; інакше дефолтний синій (як стандартний Side).
	protected int SM_FactionSideColor(int factionIndex)
	{
		int def = 0xFF2E6FE6;	// стандартний Side-синій
		if (factionIndex < 0)
			return def;
		SCR_FactionManager fm = SCR_FactionManager.Cast(GetGame().GetFactionManager());
		if (!fm)
			return def;
		Faction f = fm.GetFactionByIndex(factionIndex);
		if (!f)
			return def;
		string key = f.GetFactionKey();
		if (key == "US")
			return 0xFF0A1228;	// майже чорно-синій (BLUFOR) — щоб читалось на синьому фоні кнопки
		if (key == "USSR")
			return 0xFF4E0C0C;	// темно-червоний (OPFOR)
		if (key == "FIA")
			return 0xFF14571F;	// темно-зелений (INDFOR)
		return def;
	}

	protected string SM_FactionSideName(int factionIndex)
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
		if (key == "US")
			return "BLUFOR";
		if (key == "USSR")
			return "OPFOR";
		if (key == "FIA")
			return "INDFOR";

		// Невідома (модова) фракція — показуємо її власну назву, інакше ключ.
		string name = f.GetFactionName();
		if (name != "")
			return name;
		return key;
	}

	// Підказка «оберіть точку» біля курсора, поки активний режим Create Marker зевса.
	protected void SM_UpdatePlacePrompt()
	{
		bool show = SM_GmState.s_bCreatePending && !m_MarkerEditRoot && m_wSMMapFrame != null;
		if (show)
		{
			if (!m_wSMPlacePrompt)
			{
				WorkspaceWidget ws = GetGame().GetWorkspace();
				TextWidget t = TextWidget.Cast(ws.CreateWidget(
					WidgetType.TextWidgetTypeID,
					WidgetFlags.VISIBLE | WidgetFlags.NOWRAP | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
					Color.FromInt(0xFFF0A020), 0, m_wSMMapFrame));	// помаранчевий
				if (t)
				{
					t.SetFont("{3E7733BAC8C831F6}UI/Fonts/RobotoCondensed/RobotoCondensed_Regular.fnt");
					t.SetExactFontSize(20);
					FrameSlot.SetAnchorMin(t, 0, 0);
					FrameSlot.SetAnchorMax(t, 0, 0);
					FrameSlot.SetSizeToContent(t, true);
					t.SetText("Click a point to place the marker");
					m_wSMPlacePrompt = t;
				}
			}
			if (m_wSMPlacePrompt)
				FrameSlot.SetPos(m_wSMPlacePrompt, SCR_MapCursorInfo.x + 22, SCR_MapCursorInfo.y + 52);	// нижче, щоб не лізло на висоту/координати
		}
		else if (m_wSMPlacePrompt)
		{
			m_wSMPlacePrompt.RemoveFromHierarchy();
			m_wSMPlacePrompt = null;
		}
	}

	// GM-фільтр: у мапі Game Master показуємо мітки лише коли увімкнено перемикач видимості,
	// і лише канали Side (FACTION) та Global (ALL) — не Group/Personal. У звичайній мапі фільтра нема.
	protected bool SM_PassesGmFilter(SM_MapMarkerData d)
	{
		if (!SM_IsEditorMap())
			return true;
		if (!SM_GmState.s_bMarkerView)
			return false;
		if (d.m_iVisibility == SM_EMarkerVisibility.ALL || d.m_iVisibility == SM_EMarkerVisibility.FACTION)
			return true;
		// Власні мітки зевса (будь-який канал, навіть Local/Group) — щоб міг ними керувати й переканалити.
		SCR_PlayerController pc = SM_LocalPC();
		return pc && d.m_iOwnerId == pc.GetPlayerId();
	}

	// Реакція на перемикач видимості зевса — перебудувати видимі мітки.
	protected void SM_OnGmViewChanged(bool v)
	{
		if (m_bSMMapOpen)
			SM_RebuildAllVisuals();
	}

	// --- Render policy (always-on screens) ---

	//! World position under the middle of the map widget. false while the layout isn't ready.
	protected bool SM_GetViewCenterWorld(out int wx, out int wy)
	{
		if (!m_wSMMapFrame)
			return false;
		CanvasWidget mapW = m_MapEntity.GetMapWidget();
		if (!mapW)
			return false;

		float mx, my, mw, mh;
		mapW.GetScreenPos(mx, my);
		mapW.GetScreenSize(mw, mh);
		if (mw <= 0 || mh <= 0)
			return false;

		float fx, fy;
		m_wSMMapFrame.GetScreenPos(fx, fy);

		float cwx, cwy;
		m_MapEntity.ScreenToWorld(mx - fx + mw * 0.5, my - fy + mh * 0.5, cwx, cwy);
		wx = cwx;
		wy = cwy;
		return true;
	}

	//! slackMul > 1 gives the keep-alive hysteresis so markers at the edge don't churn.
	protected bool SM_WithinPolicyRadius(int wx, int wy, float slackMul)
	{
		int cx, cy;
		if (!SM_GetViewCenterWorld(cx, cy))
			return true;	// can't tell yet — better to show than to drop

		float r = m_fSMPolicyRadius * slackMul;
		float dx = wx - cx;
		float dy = wy - cy;
		return dx * dx + dy * dy <= r * r;
	}

	// Throttled visible-set maintenance: create visuals for markers that entered the radius, drop
	// the ones that left it. Runs at the policy interval, so a tablet gliding across the map costs
	// one pass over the store every few seconds instead of every frame. Distance checks only when
	// nothing changed.
	protected void SM_TickPolicyMembership()
	{
		float now = System.GetTickCount();
		if (now < m_fSMNextMembershipCheck)
			return;
		m_fSMNextMembershipCheck = now + m_iSMPolicyMembershipMs;

		array<SM_MapMarkerData> all = {};
		SM_MapMarkerStore.GetInstance().GetAll(all);
		foreach (SM_MapMarkerData d : all)
		{
			if (!d)
				continue;
			if (d.m_iId == m_iSMCarryId || d.m_iId == m_iSMHiddenMarkerId)
				continue;	// mid-interaction, hands off

			if (m_mSMVisuals.Contains(d.m_iId))
			{
				if (!SM_WithinPolicyRadius(d.m_iPosX, d.m_iPosY, 1.25))
					SM_OnMarkerRemoved(d.m_iId);	// drops the visual; the data stays in the store
			}
			else if (SM_WithinPolicyRadius(d.m_iPosX, d.m_iPosY, 1.0))
			{
				SM_CreateVisual(d);
				m_bSMNeedReposition = true;
			}
		}
	}

	protected void SM_CreateVisual(notnull SM_MapMarkerData data)
	{
		if (!m_bSMMapOpen || !m_wSMMapFrame)
			return;
		if (!SM_HasFeature(AM_EMapFeature.MARKERS))	// marker rendering disabled on this map screen
			return;
		if (m_fSMPolicyRadius > 0 && !SM_WithinPolicyRadius(data.m_iPosX, data.m_iPosY, 1.0))
			return;	// out of budget; SM_TickPolicyMembership picks it up once the view gets close
		if (!SM_PassesGmFilter(data))
			return;
		SM_CreateVisualInner(data);
	}

	protected void SM_CreateVisualInner(notnull SM_MapMarkerData data)
	{
		if (!m_bSMMapOpen || !m_wSMMapFrame)
			return;

		if (m_mSMVisuals.Contains(data.m_iId))
			SM_OnMarkerRemoved(data.m_iId);

		SM_MarkerVisual vis = new SM_MarkerVisual(data);
		SM_BuildVisualWidgets(vis);
		if (!vis.GetMainWidget())
			return;

		SM_ApplyVisualData(vis);
		m_mSMVisuals.Set(data.m_iId, vis);

		// Спозиціонувати одразу (інакше до наступної зміни виду висіла б у (0,0)); + позначити «брудно»
		// для куллінгу/перерахунку наступного кадру.
		SM_PositionVisual(vis, data.m_iPosX, data.m_iPosY, SM_ZoomFactor(), GetGame().GetWorkspace());
		m_bSMNeedReposition = true;
	}

	// Створює віджети візуала залежно від типу: CIVILIAN → ImageWidget; MILITARY → APP-6 overlay. + підпис.
	protected void SM_BuildVisualWidgets(SM_MarkerVisual vis)
	{
		if (!vis || !vis.m_Data)
			return;
		SM_RebuildMain(vis);
		vis.m_wLabel = SM_BuildLabel();
		vis.m_wTime = SM_BuildLabel();	// окремий віджет позначки часу (менший шрифт)
	}

	// (Пере)будовує головний віджет (цивільна іконка / військовий символ) за поточним типом.
	// Підпис не чіпає — використовується і при початковій побудові, і при civ↔mil у прев'ю.
	protected void SM_RebuildMain(SM_MarkerVisual vis)
	{
		if (!vis || !vis.m_Data)
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
			vis.m_wSymbol = SM_BuildSymbol(vis);
		else
			vis.m_wIcon = SM_BuildIcon();
	}

	// Створює OverlayWidget + SCR_MilitarySymbolUIComponent для рендеру APP-6 символа.
	protected Widget SM_BuildSymbol(SM_MarkerVisual vis)
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		Widget overlay = ws.CreateWidget(WidgetType.OverlayWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(Color.WHITE), 0, m_wSMMapFrame);
		if (!overlay)
			return null;

		FrameSlot.SetAnchorMin(overlay, 0, 0);
		FrameSlot.SetAnchorMax(overlay, 0, 0);
		FrameSlot.SetAlignment(overlay, 0.5, 0.5);	// пивот центр → SetPos позиціонує центр
		FrameSlot.SetSize(overlay, SM_BASE_SIZE, SM_BASE_SIZE);
		FrameSlot.SetPos(overlay, 0, 0);

		SCR_MilitarySymbolUIComponent comp = new SCR_MilitarySymbolUIComponent();
		overlay.AddHandler(comp);	// HandlerAttached встановить m_Widget = overlay
		vis.m_SymbolComp = comp;
		return overlay;
	}

	// Збирає SCR_MilitarySymbol з наших полів (identity/dimension/icons).
	protected SCR_MilitarySymbol SM_BuildMilitarySymbol(SM_MapMarkerData d)
	{
		SCR_MilitarySymbol sym = new SCR_MilitarySymbol();
		sym.SetIdentity(d.m_iIdentity);
		sym.SetDimension(d.m_iDimension);
		sym.SetIcons(d.m_iSymbolFlags);
		return sym;
	}

	// Рекурсивно повертає всі дочірні ImageWidget на кут (для повороту військового символа-композита).
	protected void SM_RotateChildren(Widget root, float angle)
	{
		if (!root)
			return;
		Widget child = root.GetChildren();
		while (child)
		{
			ImageWidget img = ImageWidget.Cast(child);
			if (img)
				img.SetRotation(angle);
			SM_RotateChildren(child, angle);	// у глибину (вкладені шари)
			child = child.GetSibling();
		}
	}

	// Створює БАЗОВУ ImageWidget мітки (без даних). Пивот центр → SetPos = центр на точці.
	protected ImageWidget SM_BuildIcon()
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		ImageWidget icon = ImageWidget.Cast(ws.CreateWidget(
			WidgetType.ImageWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.BLEND | WidgetFlags.STRETCH,
			Color.FromInt(Color.WHITE), 0, m_wSMMapFrame));
		if (!icon)
			return null;

		// Розмір — через СЛОТ (не ImageWidget.SetSize), інакше віджет заповнює MapFrame і STRETCH розтягує.
		FrameSlot.SetAnchorMin(icon, 0, 0);
		FrameSlot.SetAnchorMax(icon, 0, 0);
		FrameSlot.SetAlignment(icon, 0.5, 0.5);		// пивот центр → SetPos позиціонує центр
		FrameSlot.SetSize(icon, SM_BASE_SIZE, SM_BASE_SIZE);
		FrameSlot.SetPos(icon, 0, 0);

		return icon;
	}

	// Створює БАЗОВИЙ підпис (без даних; пивот верх-центр → центрований під іконкою; IGNORE_CURSOR).
	protected TextWidget SM_BuildLabel()
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		TextWidget label = TextWidget.Cast(ws.CreateWidget(
			WidgetType.TextWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.NOWRAP | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(Color.WHITE), 0, m_wSMMapFrame));
		if (!label)
			return null;

		label.SetFont("{3E7733BAC8C831F6}UI/Fonts/RobotoCondensed/RobotoCondensed_Regular.fnt");
		FrameSlot.SetAnchorMin(label, 0, 0);
		FrameSlot.SetAnchorMax(label, 0, 0);
		FrameSlot.SetAlignment(label, 0.5, 0.0);	// верх-центр → текст центрований під іконкою
		FrameSlot.SetSizeToContent(label, true);	// слот обтягує текст (інакше заповнює MapFrame і «їздить»)

		return label;
	}

	// Підказка керування внизу-зліва (над ванільними Pan/Zoom). Glyph-рядки у ванільному стилі:
	// гліф клавіші/кнопки + підпис через SCR_InputButtonComponent (як ванільні підказки керування).
	protected void SM_BuildHint()
	{
		if (!m_wSMMapFrame)
			return;
		WorkspaceWidget ws = GetGame().GetWorkspace();

		// Контейнер: вертикальний стек рядків, прикріплений до нижнього-лівого кута карти
		Widget box = ws.CreateWidget(
			WidgetType.VerticalLayoutWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(0x00000000), 0, m_wSMMapFrame);
		if (!box)
			return;
		FrameSlot.SetAnchorMin(box, 0, 1);
		FrameSlot.SetAnchorMax(box, 0, 1);
		FrameSlot.SetAlignment(box, 0, 1);	// нижній-лівий півот блока на якорі
		FrameSlot.SetSizeToContent(box, true);
		// У GM-мапі підказки вище (над ванільними Pan/Zoom редактора) і трохи правіше, щоб не накладались.
		float hintX = SM_HINT_X;
		float hintY = SM_HINT_Y;
		if (SM_IsEditorMap())
		{
			hintX = 200;	// справа від ванільних підказок редактора (Pan/Zoom), щоб не накладатись
			hintY = -340;	// на рівні Pan/Zoom (не з'їжджати на нижні панелі)
		}
		FrameSlot.SetPos(box, hintX, hintY);
		m_wSMHintBox = box;

		// 5 рядків-підказок (макс. для будь-якого стану). Переюзаємо ванільний WLib_NavigationButtonSmall.
		m_aSMHintRows.Clear();
		for (int i = 0; i < 5; i++)
		{
			Widget row = ws.CreateWidgets("{CB8563509DEF3E0E}UI/layouts/WidgetLibrary/Buttons/WLib_NavigationButtonSmall.layout", box);
			if (!row)
				continue;
			row.SetFlags(WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS);	// пасивна підказка — без наведення/фокусу
			LayoutSlot.SetHorizontalAlign(row, LayoutHorizontalAlign.Left);	// по лівому краю, як ванільний текст знизу
			m_aSMHintRows.Insert(row);
		}

		m_iSMHintState = -1;	// форсуємо перше наповнення
		SM_UpdateHint();
	}

	// Тултіп при наведенні на мітку («Edited by» + рядок видимості). Окремо від хінт-боксу, бо в
	// GM-мапі ми хінт-бокс не будуємо, а тултіп лишаємо.
	protected void SM_BuildTooltip()
	{
		if (!m_wSMMapFrame)
			return;
		WorkspaceWidget ws = GetGame().GetWorkspace();

		// Тултіп «Edited by» (біля курсора; з'являється при наведенні на мітку)
		TextWidget tip = TextWidget.Cast(ws.CreateWidget(
			WidgetType.TextWidgetTypeID,
			WidgetFlags.NOWRAP | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(0xFF000000), 0, m_wSMMapFrame));	// чорний
		if (tip)
		{
			tip.SetFont("{3E7733BAC8C831F6}UI/Fonts/RobotoCondensed/RobotoCondensed_Regular.fnt");
			tip.SetExactFontSize(16);
			FrameSlot.SetAnchorMin(tip, 0, 0);
			FrameSlot.SetAnchorMax(tip, 0, 0);
			FrameSlot.SetSizeToContent(tip, true);
			tip.SetVisible(false);
			m_wSMTooltip = tip;
		}

		// Рядок видимості мітки (під «Edited by»), колір за областю: Local-сірий/Group-зелений/Side-синій/Global-червоний
		TextWidget tipv = TextWidget.Cast(ws.CreateWidget(
			WidgetType.TextWidgetTypeID,
			WidgetFlags.NOWRAP | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(0xFFFFFFFF), 0, m_wSMMapFrame));
		if (tipv)
		{
			tipv.SetFont("{3E7733BAC8C831F6}UI/Fonts/RobotoCondensed/RobotoCondensed_Regular.fnt");
			tipv.SetExactFontSize(16);
			FrameSlot.SetAnchorMin(tipv, 0, 0);
			FrameSlot.SetAnchorMax(tipv, 0, 0);
			FrameSlot.SetSizeToContent(tipv, true);
			tipv.SetVisible(false);
			m_wSMTooltipVis = tipv;
		}
	}

	// Наповнення рядків підказки за поточним станом (тягнемо мітку чи ні). SetAction лише при зміні
	// стану — не щокадру (інакше зайвий перерахунок гліфів).
	protected void SM_UpdateHint()
	{
		if (m_aSMHintRows.IsEmpty())
			return;

		if (SM_IsEditorMap())
		{
			SM_UpdateHintEditor();
			return;
		}

		bool allowPtr  = SM_MarkerConfig.GetInstance().m_bAllowPointer;
		bool allowCopy = SM_MarkerConfig.GetInstance().m_bAllowCopyLast;

		InputManager him = GetGame().GetInputManager();
		bool pad = him && !him.IsUsingMouseAndKeyboard();	// геймпад → показуємо наші AM-кнопки (A/Y/X/B)

		int baseState;
		if (m_MarkerEditRoot)
			baseState = 2;	// відкритий діалог редагування/створення
		else if (m_bSMPanelPadNav)
			baseState = 5;	// пад: фокус у панелі малювання (навігація меню)
		else if (m_bSMPointing)
			baseState = 6;
		else if (m_iSMCarryId != -1)
			baseState = 1;	// перенесення мітки
		else if (m_DrawCanvas && m_DrawCanvas.IsActive() && m_DrawCanvas.GetTool() == 0)
			baseState = 3;	// малювання: олівець
		else if (m_DrawCanvas && m_DrawCanvas.IsActive() && m_DrawCanvas.GetTool() == 1)
			baseState = 4;	// малювання: гумка
		else
			baseState = 0;	// звичайний

		// Ключ стану з прапорцями конфігу + девайсом — щоб їх зміна перебудувала рядки.
		int state = baseState;
		if (!allowPtr)
			state += 100;
		if (!allowCopy)
			state += 200;
		if (pad)
			state += 500;

		if (state == m_iSMHintState)
			return;
		m_iSMHintState = state;

		if (baseState == 2)	// діалог: на паді X видаляє пресет, на KB/M — ПКМ
		{
			if (pad)
				SM_SetHintRow(0, "AM_Delete", "Delete preset");
			else
				SM_SetHintRow(0, "MapContextualMenu", "Delete preset");
			SM_SetHintRowVisible(1, false);
			SM_SetHintRowVisible(2, false);
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else if (baseState == 1)	// несемо мітку
		{
			if (pad)
			{
				SM_SetHintRow(0, "AM_Confirm",        "Release — place");	// відпустив A — поклав
				SM_SetHintRow(1, "MapContextualMenu", "Cancel");			// B
			}
			else
			{
				SM_SetHintRow(0, "MapSelect",         "Place");
				SM_SetHintRow(1, "MapContextualMenu", "Cancel");
			}
			SM_SetHintRowVisible(2, false);
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else if (baseState == 5)	// пад: навігація панеллю малювання — підказки показує лівий стовпчик біля панелі
		{
			SM_SetHintRowVisible(0, false);
			SM_SetHintRowVisible(1, false);
			SM_SetHintRowVisible(2, false);
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else if (baseState == 6)	// вказуємо пальцем (палець водиться стіком/паном, відпуск — стоп)
		{
			if (pad)
				SM_SetHintRow(0, "AM_Confirm", "Release — stop pointing");
			else
				SM_SetHintRow(0, "MapSelect", "Release — stop pointing");
			SM_SetHintRowVisible(1, false);
			SM_SetHintRowVisible(2, false);
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else if (baseState == 3)	// малювання: олівець
		{
			if (pad)
			{
				SM_SetHintRow(0, "AM_Confirm",        "Hold — draw");
				SM_SetHintRow(1, "MapContextualMenu", "Cancel tool");
				SM_SetHintRow(2, "AM_Delete",         "Remove stroke");
			}
			else
			{
				SM_SetHintRow(0, "MapSelect",       "Draw");
				SM_SetHintRow(1, "MapSelect",       "Shift + drag — straight line");
				SM_SetHintRow(2, "MapMarkerDelete", "Remove stroke");
			}
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else if (baseState == 4)	// малювання: гумка
		{
			if (pad)
			{
				SM_SetHintRow(0, "AM_Confirm",        "Hold — erase");
				SM_SetHintRow(1, "MapContextualMenu", "Cancel tool");
				SM_SetHintRow(2, "AM_Delete",         "Remove stroke");
			}
			else
			{
				SM_SetHintRow(0, "MapSelect",       "Erase — hold & drag");
				SM_SetHintRow(1, "MapMarkerDelete", "Remove stroke");
				SM_SetHintRowVisible(2, false);
			}
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else if (pad)	// звичайний — геймпад (A: тап=редаг/створ, утримання=нести/вказувати)
		{
			SM_SetHintRow(0, "AM_Confirm", "Tap — edit / create");
			SM_SetHintRow(1, "AM_Confirm", "Hold — move");
			SM_SetHintRow(2, "AM_Delete",  "Remove marker");
			if (allowPtr)
				SM_SetHintRow(3, "AM_Confirm", "Hold empty — point");
			else
				SM_SetHintRowVisible(3, false);
			if (allowCopy)
				SM_SetHintRow(4, "AM_Place",   "Copy last marker");
			else
				SM_SetHintRowVisible(4, false);
		}
		else	// звичайний — KB/M
		{
			SM_SetHintRow(0, "MapSelect",       "Hold — move");
			SM_SetHintRow(1, "MapSelect",       "Double-click — edit / create");
			SM_SetHintRow(2, "MapMarkerDelete", "Remove marker / stroke");
			if (allowPtr)
				SM_SetHintRow(3, "MapSelect",   "Hold on empty — point at map");
			else
				SM_SetHintRowVisible(3, false);	// вказування вимкнено — без підказки
			if (allowCopy)
				SM_SetHintRow(4, "MapSelect",   "Alt + click — copy last marker");
			else
				SM_SetHintRowVisible(4, false);	// копіювання вимкнено — без підказки
		}
	}

	// Підказки керування в мапі Game Master (видимість ON): рух/редагування/видалення/копія.
	protected void SM_UpdateHintEditor()
	{
		InputManager him = GetGame().GetInputManager();
		bool pad = him && !him.IsUsingMouseAndKeyboard();

		int est;
		if (m_MarkerEditRoot)
			est = 1000;	// відкритий діалог — без підказок
		else if (!SM_GmState.s_bMarkerView)
			est = 1001;	// видимість вимкнена — без підказок
		else if (m_iSMCarryId != -1)
			est = 1002;	// несемо мітку
		else
			est = 1003;	// звичайний GM-перегляд

		if (pad)
			est += 5000;	// девайс у ключ стану — щоб перемикання пад/KB/M перебудувало рядки

		if (est == m_iSMHintState)
			return;
		m_iSMHintState = est;

		if (est == 1002 || est == 6002)	// несемо мітку
		{
			if (pad)
			{
				SM_SetHintRow(0, "AM_Confirm",        "Release — place");
				SM_SetHintRow(1, "MapContextualMenu", "Cancel");	// B
			}
			else
			{
				SM_SetHintRow(0, "MapSelect", "Place");
				SM_SetHintRowVisible(1, false);
			}
			SM_SetHintRowVisible(2, false);
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else if (est == 6003)	// звичайний GM — геймпад
		{
			SM_SetHintRow(0, "AM_Confirm", "Tap — edit / create");
			SM_SetHintRow(1, "AM_Confirm", "Hold — move");
			SM_SetHintRow(2, "AM_Delete",  "Remove");
			if (SM_MarkerConfig.GetInstance().m_bAllowCopyLast)
				SM_SetHintRow(3, "AM_Place", "Copy last marker");
			else
				SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else if (est == 1003)	// звичайний GM — KB/M
		{
			SM_SetHintRow(0, "MapSelect",       "Hold — move");
			SM_SetHintRow(1, "MapSelect",       "Double-click — edit");
			SM_SetHintRow(2, "MapMarkerDelete", "Remove");
			if (SM_MarkerConfig.GetInstance().m_bAllowCopyLast)
				SM_SetHintRow(3, "MapSelect",   "Alt + click — copy last marker");
			else
				SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else	// 1000 діалог / 1001 видимість off — ховаємо всі
		{
			SM_SetHintRowVisible(0, false);
			SM_SetHintRowVisible(1, false);
			SM_SetHintRowVisible(2, false);
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
	}

	// Налаштувати рядок підказки: гліф дії + підпис. Пасивний (без звуку/анімації/реакції на дію).
	protected void SM_SetHintRow(int idx, string action, string label)
	{
		if (idx < 0 || idx >= m_aSMHintRows.Count() || !m_aSMHintRows[idx])
			return;
		m_aSMHintRows[idx].SetVisible(true);
		SCR_InputButtonComponent comp = SCR_InputButtonComponent.Cast(m_aSMHintRows[idx].FindHandler(SCR_InputButtonComponent));
		if (!comp)
			return;
		comp.SetAction(action);
		comp.SetLabel(label);
		comp.SetClickSoundDisabled(true);
		comp.SM_MakeDisplayOnly();	// прибрати слухачів дії — підказка не блимає при кліках на мапі
	}

	protected void SM_SetHintRowVisible(int idx, bool vis)
	{
		if (idx < 0 || idx >= m_aSMHintRows.Count() || !m_aSMHintRows[idx])
			return;
		m_aSMHintRows[idx].SetVisible(vis);
	}

	// Тултіп «Edited by: <нік>» при наведенні на мітку. Перерахунок лише коли курсор рухається.
	protected void SM_UpdateTooltip()
	{
		if (!m_wSMTooltip)
			return;

		// Не показуємо під час діалогу/перенесення
		if (m_MarkerEditRoot || m_iSMCarryId != -1)
		{
			SM_HideTooltip();
			return;
		}

		// Троттл по СВІТОВІЙ точці під курсором: змінюється і коли рухаємо курсор, і коли панорамуємо
		// (лівий стік — курсор на екрані нерухомий, а світова точка під ним інша).
		int twx, twy;
		SM_GetCursorWorld(twx, twy);
		if (twx == m_iSMTipWX && twy == m_iSMTipWY)
			return;
		m_iSMTipWX = twx;
		m_iSMTipWY = twy;

		SM_MarkerVisual vis = SM_FindMarkerUnderCursor();
		if (!vis || !vis.m_Data)
		{
			SM_UpdateStrokeTooltip();	// мітки нема — може, під курсором штрих малюнка (автор + видимість)
			return;
		}
		SM_MapMarkerData d = vis.m_Data;

		// Зевс позначив мітку «Hide info» — для гравців (поза редактором) не показуємо тултіп взагалі.
		// У GM-мапі зевс інфо однаково бачить.
		if (d.m_iHideInfo != 0 && !SM_IsEditorMap())
		{
			SM_HideTooltip();
			return;
		}

		float x = SCR_MapCursorInfo.x + 18;
		float y = SCR_MapCursorInfo.y + 34;

		// Рядок «хто» — ім'я зберігається в JSON, тож показуємо й після рестарту. Якщо невідоме — ховаємо лише цей рядок.
		if (d.m_sLastEditor != "")
		{
			m_wSMTooltip.SetText("Edited by: " + d.m_sLastEditor);
			m_wSMTooltip.SetVisible(true);
			FrameSlot.SetPos(m_wSMTooltip, x, y);
			y += 20;	// видимість піде нижче
		}
		else
		{
			m_wSMTooltip.SetVisible(false);
		}

		// Рядок видимості — показуємо ЗАВЖДИ (m_iVisibility зберігається), колір за областю.
		if (m_wSMTooltipVis)
		{
			string visLabel;
			int visColor;
			SM_VisibilityLabel(d.m_iVisibility, visLabel, visColor);
			// У GM-мапі для Side-міток дописуємо назву сторони (BLUFOR/OPFOR/INDFOR), щоб зевс її розрізняв.
			if (SM_IsEditorMap() && d.m_iVisibility == SM_EMarkerVisibility.FACTION)
			{
				string side = SM_FactionSideName(d.m_iChannel);
				if (side != "")
				{
					visLabel = visLabel + " · " + side;
					visColor = SM_FactionSideColor(d.m_iChannel);	// колір за стороною
				}
			}
			m_wSMTooltipVis.SetText(visLabel);
			m_wSMTooltipVis.SetColor(Color.FromInt(visColor));
			m_wSMTooltipVis.SetVisible(true);
			FrameSlot.SetPos(m_wSMTooltipVis, x, y);
		}
	}

	protected void SM_HideTooltip()
	{
		if (m_wSMTooltip)
			m_wSMTooltip.SetVisible(false);
		if (m_wSMTooltipVis)
			m_wSMTooltipVis.SetVisible(false);
	}

	// Тултіп для ШТРИХА малюнка під курсором: «Drawn by» (за конфігом showLastEditor) + видимість.
	// Викликається з SM_UpdateTooltip, коли мітки під курсором нема (троттл по руху курсора вже пройдено).
	protected void SM_UpdateStrokeTooltip()
	{
		if (!m_DrawCanvas)
		{
			SM_HideTooltip();
			return;
		}
		// GM-мапа з ВИМКНЕНИМ показом малюнків: штрихів не видно — тултіп по «невидимому» не показуємо.
		if (SM_IsEditorMap() && !SM_GmState.s_bDrawView)
		{
			SM_HideTooltip();
			return;
		}
		// Під час малювання/стирання тултіп не заважає.
		if (m_DrawCanvas.IsActive() && m_bSMDrawDown)
		{
			SM_HideTooltip();
			return;
		}

		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
		{
			SM_HideTooltip();
			return;
		}
		int ttwx, ttwy;
		bool ttHaveW = SM_GetCursorWorld(ttwx, ttwy);
		int strokeId = m_DrawCanvas.FindStrokeAtScreen(
			ws.DPIScale(SCR_MapCursorInfo.x), ws.DPIScale(SCR_MapCursorInfo.y), ttwx, ttwy, ttHaveW);
		if (strokeId == -1)	// -1 = нічого не влучили (Local-штрихи мають негативні id <= -2 — теж валідні)
		{
			SM_HideTooltip();
			return;
		}
		SM_MapDrawingData sd = SM_DrawCanvas.GetStrokeData(strokeId);
		if (!sd)
		{
			SM_HideTooltip();
			return;
		}

		// Зевс позначив штрих «Hide info» — гравцям (поза редактором) тултіп не показуємо взагалі.
		if (sd.m_iHideInfo != 0 && !SM_IsEditorMap())
		{
			SM_HideTooltip();
			return;
		}

		float x = SCR_MapCursorInfo.x + 18;
		float y = SCR_MapCursorInfo.y + 34;

		if (sd.m_sOwnerName != "" && SM_MarkerConfig.GetInstance().m_bShowLastEditor)
		{
			m_wSMTooltip.SetText("Drawn by: " + sd.m_sOwnerName);
			m_wSMTooltip.SetVisible(true);
			FrameSlot.SetPos(m_wSMTooltip, x, y);
			y += 20;
		}
		else
		{
			m_wSMTooltip.SetVisible(false);
		}

		if (m_wSMTooltipVis)
		{
			string visLabel;
			int visColor;
			SM_VisibilityLabel(sd.m_iVisibility, visLabel, visColor);
			// У GM-мапі для Side-штрихів дописуємо сторону (BLUFOR/OPFOR/INDFOR) — як у міток.
			if (SM_IsEditorMap() && sd.m_iVisibility == SM_EMarkerVisibility.FACTION)
			{
				string side = SM_FactionSideName(sd.m_iChannel);
				if (side != "")
				{
					visLabel = visLabel + " · " + side;
					visColor = SM_FactionSideColor(sd.m_iChannel);
				}
			}
			m_wSMTooltipVis.SetText(visLabel);
			m_wSMTooltipVis.SetColor(Color.FromInt(visColor));
			m_wSMTooltipVis.SetVisible(true);
			FrameSlot.SetPos(m_wSMTooltipVis, x, y);
		}
	}

	// Підпис і колір рядка видимості: Local-сірий / Group-зелений / Side-синій / Global-червоний.
	protected void SM_VisibilityLabel(int vis, out string label, out int color)
	{
		switch (vis)
		{
			case SM_EMarkerVisibility.PERSONAL: label = "Local";  color = 0xFFAAAAAA; return;	// сірий
			case SM_EMarkerVisibility.GROUP:    label = "Group";  color = 0xFF49C24A; return;	// зелений
			case SM_EMarkerVisibility.FACTION:  label = "Side";   color = 0xFF2E6FE6; return;	// синій
			case SM_EMarkerVisibility.ALL:      label = "Global"; color = 0xFFD83A3A; return;	// червоний
		}
		label = "";
		color = 0xFFFFFFFF;
	}

	// Застосовує дані (іконка/колір/поворот/текст) до віджетів візуала.
	protected void SM_ApplyVisualData(SM_MarkerVisual vis)
	{
		if (!vis)
			return;
		SM_MapMarkerData d = vis.m_Data;

		if (vis.m_wIcon)	// CIVILIAN
		{
			ResourceName imageset;
			string quad;
			if (SM_ResolveCivIcon(d.m_iIconEntry, imageset, quad) && imageset != "" && quad != "")
				vis.m_wIcon.LoadImageFromSet(0, imageset, quad);
			vis.m_wIcon.SetColor(Color.FromInt(d.m_iColor));
			vis.m_wIcon.SetRotation(d.m_iRotation);
		}
		else if (vis.m_wSymbol && vis.m_SymbolComp)	// MILITARY (APP-6)
		{
			vis.m_SymbolComp.Update(SM_BuildMilitarySymbol(d));
			vis.m_wSymbol.SetColor(Color.FromInt(d.m_iColor));
			// OverlayWidget не має SetRotation — крутимо кожен шар-ImageWidget на той самий кут
			// (шари символа накладені по центру, тож це повертає весь символ).
			SM_RotateChildren(vis.m_wSymbol, d.m_iRotation);
		}
		int labelColor = 0xFF000000;	// чорний за замовчуванням
		if (d.m_iTextColored != 0)
			labelColor = d.m_iColor;	// або у колір мітки (галочка Text)

		if (vis.m_wLabel)
		{
			vis.m_wLabel.SetText(d.m_sText);
			vis.m_wLabel.SetColor(Color.FromInt(labelColor));
		}
		if (vis.m_wTime)	// позначка часу — окремий менший віджет (дата+час сценарію)
		{
			if (d.m_iDate != 0)
			{
				vis.m_wTime.SetText(SM_DateTimeString(d.m_iDate, d.m_iTime));
				vis.m_wTime.SetColor(Color.FromInt(labelColor));
				vis.m_wTime.SetVisible(true);
			}
			else
			{
				vis.m_wTime.SetVisible(false);
			}
		}
	}

	// Позиціонує+масштабує візуал у світовій точці (спільно для міток і прев'ю).
	protected void SM_PositionVisual(SM_MarkerVisual vis, int wx, int wy, float factor, WorkspaceWidget ws)
	{
		if (!vis || !vis.m_Data)
			return;
		Widget main = vis.GetMainWidget();	// цивільна іконка АБО військовий символ
		if (!main)
			return;

		float size = SM_BASE_SIZE * SM_SizeFactor(vis.m_Data.m_iSize) * factor;

		int sx, sy;
		m_MapEntity.WorldToScreen(wx, wy, sx, sy, true);
		float usx = ws.DPIUnscale(sx);
		float usy = ws.DPIUnscale(sy);

		FrameSlot.SetSize(main, size, size);
		FrameSlot.SetPos(main, usx, usy);

		float mainFont = size * SM_TEXT_RATIO;
		float labelY = usy + size * SM_LABEL_OFFSET;	// під видимою іконкою (а не під краєм віджета)
		// На дуже дальніх зумах шрифт стає суб-піксельним — ховаємо підписи, щоб не виставляти ~0px.
		bool tinyText = (mainFont < 2.0);
		if (vis.m_wLabel)
		{
			if (tinyText)
				vis.m_wLabel.SetVisible(false);
			else
			{
				vis.m_wLabel.SetExactFontSize(mainFont);
				FrameSlot.SetPos(vis.m_wLabel, usx, labelY);
			}
		}
		if (vis.m_wTime)	// позначка часу — удвічі менший шрифт, під підписом
		{
			if (tinyText)
			{
				vis.m_wTime.SetVisible(false);
				return;
			}
			vis.m_wTime.SetExactFontSize(mainFont * 0.5);
			float timeY = labelY;
			if (vis.m_Data.m_sText != "")
				timeY = labelY + mainFont * 1.1;	// нижче рядка підпису
			FrameSlot.SetPos(vis.m_wTime, usx, timeY);
		}
	}

	// Множник «папір»-масштабу за поточним зумом (1.0 на макс. наближенні, менше при віддаленні).
	protected float SM_ZoomFactor()
	{
		float maxZoom = m_MapEntity.GetMaxZoom();
		if (maxZoom > 0)
			return Math.Clamp(m_MapEntity.GetCurrentZoom() / maxZoom, SM_MIN_ZOOM_SCALE, 1.0);
		return 1.0;
	}

	// Чи змінився вид мапи (зум або пан) з минулого кадру. Дешево: 1 WorldToScreen опорної точки.
	protected bool SM_DetectViewChange()
	{
		float zoom = m_MapEntity.GetCurrentZoom();
		int rx, ry;
		m_MapEntity.WorldToScreen(0, 0, rx, ry, true);	// опорна світова точка (0,0)
		bool changed = (zoom != m_fSMLastZoom) || (rx != m_iSMLastRefX) || (ry != m_iSMLastRefY);
		m_fSMLastZoom = zoom;
		m_iSMLastRefX = rx;
		m_iSMLastRefY = ry;
		return changed;
	}

	// Розмір рамки мапи у НЕмасштабованих px (для куллінгу — у тій самій системі, що й позиції слотів).
	protected void SM_GetFrameSizeUnscaled(out float w, out float h, WorkspaceWidget ws)
	{
		w = 99999;
		h = 99999;
		CanvasWidget mapW = m_MapEntity.GetMapWidget();
		if (!mapW)
			return;
		float sw, sh;
		mapW.GetScreenSize(sw, sh);
		w = ws.DPIUnscale(sw);
		h = ws.DPIUnscale(sh);
	}

	// Множник базового розміру з ВІДСОТКА (100 = базові 720). Старі збереження мали 0..3
	// (enum SM_EMarkerSize) — трактуємо < 10 як 100% (зворотна сумісність, інакше були б невидимі).
	protected float SM_SizeFactor(int sizePercent)
	{
		if (sizePercent < 10)
			return 1.0;
		return sizePercent * 0.01;
	}

	// Після того, як ванільний грід наповнив вкладку, дописуємо у вкладку "General" дві
	// наші кнопки-серця. Вони стають повноцінними кнопками гріда (лягають у m_mIconIDs із
	// зарезервованими індексами, клік іде через ванільний OnIconEntryClicked), тож вибір і
	// підсвітка працюють штатно. Конфіг при цьому не чіпаємо.
	override protected void InitCategoryIcons(SCR_TabViewContent tabContent)
	{
		super.InitCategoryIcons(tabContent);

		if (!tabContent || tabContent.m_sTabIdentifier != "general" || !m_IconSelector)
			return;

		WorkspaceWidget ws = GetGame().GetWorkspace();
		SM_AddHeartButton(ws, SM_HEART_ICON_BASE,     "anarchyHeart1");
		SM_AddHeartButton(ws, SM_HEART_ICON_BASE + 1, "anarchyHeart2");
	}

	protected void SM_AddHeartButton(WorkspaceWidget ws, int reservedId, string quad)
	{
		if (!ws || !m_IconSelector)
			return;

		// продовжуємо нумерацію/рядки так само, як ванільний грід
		m_iIconEntryCount++;
		Widget line;
		if (m_iIconEntryCount > m_iIconsPerLine * m_iIconLines)
		{
			line = ws.CreateWidgets(SELECTOR_LINE, m_IconSelector);
			m_iIconLines++;
		}
		else
			line = SM_LastChild(m_IconSelector);
		if (!line)
			return;

		Widget button = ws.CreateWidgets(m_sSelectorIconEntry, line);
		if (!button)
			return;
		button.SetName(ICON_ENTRY + m_iIconEntryCount.ToString());

		SCR_ButtonImageComponent comp = SCR_ButtonImageComponent.Cast(button.FindHandler(SCR_ButtonImageComponent));
		if (!comp)
			return;

		m_mIconIDs.Insert(comp, reservedId);
		comp.SetImage(SM_HEART_IMAGESET, quad);
		comp.m_OnClicked.Insert(OnIconEntryClicked);
		comp.m_OnFocus.Insert(OnIconEntryFocused);

		// якщо редагуємо мітку-серце — одразу виділяємо потрібну кнопку (super її ще не мав)
		if (reservedId == m_iWantedIconEntry)
			OnIconEntryClicked(comp);
	}

	protected Widget SM_LastChild(Widget parent)
	{
		Widget c = parent.GetChildren();
		if (!c)
			return null;
		Widget n = c.GetSibling();
		while (n)
		{
			c = n;
			n = c.GetSibling();
		}
		return c;
	}

	// Резолвить (imageset, quad) для індексу цивільної іконки.
	// Зарезервовані індекси (>= SM_HEART_ICON_BASE) — наші серця з власного imageset,
	// конфіг не чіпаємо. Решта — звичайний ванільний конфіг PLACED_CUSTOM.
	protected bool SM_ResolveCivIcon(int iconEntry, out ResourceName imageset, out string quad)
	{
		if (iconEntry >= SM_HEART_ICON_BASE)
		{
			imageset = SM_HEART_IMAGESET;
			if (iconEntry == SM_HEART_ICON_BASE)
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

	// 3. ВВІД
	// Гейтинг: мапа готова прийняти наш клік (не сфокусована панель, не popup-режим).
	protected bool SM_CanAcceptMapClick()
	{
		if (m_MarkerEditRoot)		// відкритий діалог — кліки по мапі ігноруємо
			return false;

		if (IsToolMenuFocused())
			return false;

		// CS_PAN виключаємо з маски: на паді ліво-стіком панорамуєш мапу й водночас хочеш
		// наводитись/видаляти. Решту popup-обмежень (radial/draw/rotate/команди) лишаємо.
		if (m_CursorModule && (m_CursorModule.GetCursorState() & (SCR_MapCursorModule.STATE_POPUP_RESTRICTED & ~EMapCursorState.CS_PAN)) != 0)
			return false;

		return true;
	}

	// Повний поллінг ЛКМ через сирий MouseLeft (фронти сигналу). Імунний до повторних/миттєвих
	// MapSelect DOWN/UP. Натиск → запам'ятати; утримання на мітці → підняти; відпуск → поставити
	// (якщо тягнемо) АБО подвійний клік (редагувати/створити).
	// Розміщення мітки зевсом у GM-мапі: після кнопки Create Marker наступний клік по мапі задає
	// точку й відкриває наш діалог. Повний ввід (переміщення/редагування) тут поки не обробляємо.
	protected void SM_PollEditorPlacement(InputManager im)
	{
		bool down = im.GetActionValue("MouseLeft") > 0.5;
		float now = System.GetTickCount() / 1000.0;

		if (SM_GmState.s_bCreatePending)
		{
			if (!m_bSMWasCreatePending)	// щойно увійшли в режим (натиснули кнопку)
			{
				m_bSMWasCreatePending = true;
				m_bSMCreateSawRelease = false;	// спершу дочекаємось відпускання кліку по кнопці
			}

			if (!m_MarkerEditRoot)
			{
				if (!m_bSMCreateSawRelease)
				{
					if (!down)
						m_bSMCreateSawRelease = true;	// кнопку відпущено — далі ловимо клік по мапі
				}
				else if (!down && m_bSMLmbDown)	// справжній клік по мапі завершено
				{
					int wx, wy;
					if (SM_GetCursorWorld(wx, wy))
					{
						SM_GmState.s_bCreatePending = false;
						SM_OpenCreate(wx, wy);
					}
				}
			}
		}
		else if (SM_GmState.s_bMarkerView && !m_MarkerEditRoot)
		{
			m_bSMWasCreatePending = false;

			// --- Alt+ЛКМ — копія останньої мітки зевса в точці курсора (Ctrl у редакторі зайнятий мапою) ---
			if (down && !m_bSMLmbDown && m_iSMCarryId == -1
				&& SM_CopyModifierDown() && m_SMLastTemplate
				&& SM_MarkerConfig.GetInstance().m_bAllowCopyLast)
			{
				SM_PlaceCopyAtCursor();
				m_fSMPressTime = now;
				m_bSMPickedThisPress = true;
				m_iSMPressMarkerId = -1;
				m_fSMLastSelectTime = 0;
				m_bSMLmbDown = down;
				return;
			}

			// --- ФРОНТ ВНИЗ: запам'ятати мітку під курсором ---
			if (down && !m_bSMLmbDown)
			{
				m_fSMPressTime = now;
				m_fSMPressX = SCR_MapCursorInfo.x;
				m_fSMPressY = SCR_MapCursorInfo.y;
				m_bSMPickedThisPress = false;
				m_iSMPressMarkerId = -1;
				SM_MarkerVisual u = SM_FindMarkerUnderCursor();
				if (u && u.m_Data)
					m_iSMPressMarkerId = u.m_Data.m_iId;
			}

			// --- УТРИМАННЯ на мітці ≥ HOLD → підняти (тягнути) ---
			if (down && m_iSMCarryId == -1 && m_iSMPressMarkerId != -1
				&& (now - m_fSMPressTime) >= SM_HOLD_SEC && SM_CursorNearPress())
			{
				m_iSMCarryId = m_iSMPressMarkerId;	// у редакторі зевс — lock його не стримує
				m_bSMPickedThisPress = true;		// відпуск ЦЬОГО утримання не рахуємо як клік
				m_fSMLastSelectTime = 0;
			}

			// --- ФРОНТ ВГОРУ ---
			if (!down && m_bSMLmbDown)
			{
				if (m_bSMPickedThisPress)
				{
					m_bSMPickedThisPress = false;	// це відпуск підняття — мітка лишається "в руці"
				}
				else if (m_iSMCarryId != -1)
				{
					int wx, wy;
					if (SM_GetCursorWorld(wx, wy))
						SM_DoMoveMarker(m_iSMCarryId, wx, wy);	// commit the move (Local or server)
					m_iSMCarryId = -1;
					m_iSMPressMarkerId = -1;
				}
				else if ((now - m_fSMPressTime) < SM_HOLD_SEC && SM_CursorNearPress())
				{
					// швидкий клік на місці → детект подвійного (редагувати)
					SM_MarkerVisual u = SM_FindMarkerUnderCursor();
					if (u && u.m_Data)
					{
						if (m_fSMLastSelectTime > 0 && (now - m_fSMLastSelectTime) <= SM_DOUBLECLICK_SEC)
						{
							m_fSMLastSelectTime = 0;
							SM_OpenEdit(u.m_Data);
						}
						else
							m_fSMLastSelectTime = now;
					}
					else
						m_fSMLastSelectTime = 0;
				}
			}
		}
		else
		{
			m_bSMWasCreatePending = false;
		}

		m_bSMLmbDown = down;
	}

	// Захоплення штриха в режимі малювання: натиск/перетяг/відпуск ЛКМ → полотно.
	// Клік над панеллю параметрів не починає штрих.
	protected void SM_PollDraw(InputManager im)
	{
		bool down = im.GetActionValue("MouseLeft") > 0.5;

		// Курсор над панеллю? (фіз. px = DPI-scale від unscaled-курсора мапи)
		WorkspaceWidget ws = GetGame().GetWorkspace();
		bool overPanel = false;
		if (m_DrawPanel && ws)
			overPanel = m_DrawPanel.IsCursorOver(ws.DPIScale(SCR_MapCursorInfo.x), ws.DPIScale(SCR_MapCursorInfo.y));

		int wx, wy;
		bool haveWorld = SM_GetCursorWorld(wx, wy);

		// Shift chains straight segments into one polyline: each click continues from the end of the
		// previous one, and letting Shift go commits the whole thing as a single stroke.
		bool lineMode = im.GetActionValue("AM_LineModifier") > 0.5;

		if (down && !m_bSMDrawDown)
		{
			if (haveWorld && !overPanel)
				m_DrawCanvas.OnPressDown(wx, wy, lineMode);
		}
		else if (down && m_bSMDrawDown)
		{
			if (haveWorld)
				m_DrawCanvas.OnDrag(wx, wy);
		}
		else if (!down && m_bSMDrawDown)
		{
			if (haveWorld)
				m_DrawCanvas.OnRelease(wx, wy, lineMode);
		}
		else if (m_DrawCanvas.HasLineChain())
		{
			// LMB up with a chain open: trail the rubber band until Shift is released.
			if (!lineMode)
				m_DrawCanvas.FinishLineChain();
			else if (haveWorld)
				m_DrawCanvas.OnLineChainHover(wx, wy);
		}
		m_bSMDrawDown = down;
	}

	protected void SM_PollMouse()
	{
		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;

		// Режим малювання перехоплює ЛКМ повністю (без логіки міток). ПЕРЕД editor-гілкою:
		// зевс теж малює (інструмент вмикається лише з відкритою панеллю «Drawing tools»).
		if (m_DrawCanvas && m_DrawCanvas.IsActive() && im.IsUsingMouseAndKeyboard())
		{
			SM_PollDraw(im);
			return;
		}

		// У GM-мапі повний клік-ввід не обробляємо (щоб не заважати редактору), лише режим розміщення
		// мітки після кнопки Create Marker.
		if (SM_IsEditorMap())
		{
			SM_PollEditorPlacement(im);
			return;
		}

		// Геймпад (консоль): окрема дискретна модель — мишачі жести (подвійний клік, hold-drag) не
		// переносяться на стік. Курсор той самий (SCR_MapCursorInfo стік-керований), кнопки — наші екшени.
		if (!im.IsUsingMouseAndKeyboard())
		{
			SM_PollGamepad(im);
			return;
		}

		// Курсор над панеллю малювання (чи її відкритим списком) — кліки належать панелі:
		// не даємо марковській логіці (подвійний клік create / hold / вказівник) спрацювати «крізь» неї.
		if (m_DrawPanel)
		{
			WorkspaceWidget pws = GetGame().GetWorkspace();
			if (pws && m_DrawPanel.IsCursorOver(pws.DPIScale(SCR_MapCursorInfo.x), pws.DPIScale(SCR_MapCursorInfo.y)))
			{
				m_bSMLmbDown = im.GetActionValue("MouseLeft") > 0.5;	// тримаємо стан ЛКМ актуальним
				m_iSMPressMarkerId = -1;
				m_bSMPickedThisPress = false;
				return;
			}
		}

		bool down = im.GetActionValue("MouseLeft") > 0.5;
		float now = System.GetTickCount() / 1000.0;

		if (SM_CanAcceptMapClick())
		{
			// --- Alt+ЛКМ (фронт натиску) — поставити копію останньої мітки гравця і спожити натиск ---
			if (down && !m_bSMLmbDown && m_iSMCarryId == -1 && !m_bSMPointing
				&& SM_HasFeature(AM_EMapFeature.MARKER_TOOLS)
				&& SM_CopyModifierDown() && m_SMLastTemplate
				&& SM_MarkerConfig.GetInstance().m_bAllowCopyLast)
			{
				SM_PlaceCopyAtCursor();
				// споживаємо натиск: без утримання-переміщення/вказівника/подвійного кліку
				m_fSMPressTime = now;
				m_bSMPickedThisPress = true;
				m_iSMPressMarkerId = -1;
				m_fSMLastSelectTime = 0;
				m_bSMLmbDown = down;
				return;
			}

			// --- ФРОНТ ВНИЗ (натиск) — запам'ятати позицію/мітку ---
			if (down && !m_bSMLmbDown)
			{
				m_fSMPressTime = now;
				m_fSMPressX = SCR_MapCursorInfo.x;
				m_fSMPressY = SCR_MapCursorInfo.y;
				m_bSMPickedThisPress = false;
				m_iSMPressMarkerId = -1;
				if (SM_HasFeature(AM_EMapFeature.MARKER_TOOLS))	// without tools a marker is not a pick target
				{
					SM_MarkerVisual u = SM_FindMarkerUnderCursor();
					if (u && u.m_Data)
						m_iSMPressMarkerId = u.m_Data.m_iId;
				}
			}

			// --- УТРИМАННЯ на мітці (на місці) ≥ SM_HOLD_SEC → підняти ---
			if (down && m_iSMCarryId == -1 && m_iSMPressMarkerId != -1
				&& (now - m_fSMPressTime) >= SM_HOLD_SEC && SM_CursorNearPress())
			{
				SM_MapMarkerData pm = SM_MapMarkerStore.GetInstance().FindById(m_iSMPressMarkerId);
				if (SM_BlockedByLock(pm))
				{
					m_bSMPickedThisPress = true;	// споживаємо утримання (не піднімаємо й не ставимо)
					m_iSMPressMarkerId = -1;		// щоб повідомлення не повторювалось щокадру
				}
				else
				{
					m_iSMCarryId = m_iSMPressMarkerId;
					m_bSMPickedThisPress = true;	// відпуск ЦЬОГО утримання НЕ ставитиме мітку
					m_fSMLastSelectTime = 0;	// скасувати арм подвійного кліку
				}
			}

			// --- УТРИМАННЯ на ПУСТОМУ місці ≥ SM_POINT_HOLD_SEC → режим «вказівник» (показати пальцем) ---
			if (down && m_iSMCarryId == -1 && !m_bSMPointing && m_iSMPressMarkerId == -1
				&& (now - m_fSMPressTime) >= SM_POINT_HOLD_SEC
				&& SM_HasFeature(AM_EMapFeature.POINTER)
				&& SM_MarkerConfig.GetInstance().m_bAllowPointer)	// вимкнено в конфізі — не входимо в режим
			{
				m_bSMPointing = true;
				m_bSMPickedThisPress = true;	// відпуск НЕ створюватиме мітку
				m_fSMLastSelectTime = 0;
				if (m_CursorModule)
					m_CursorModule.HandleDialog(true);	// ховає інфо-текст координат/висоти
				SM_SetMapCursorHidden(true);			// ховає сам віджет курсора карти
			}

			// --- поки водимо вказівником: своя точка локально + потік позиції на сервер (троттл) ---
			if (down && m_bSMPointing)
			{
				SM_SetMapCursorHidden(true);	// тримаємо курсор схованим, якщо модуль його повертає
				int px, py;
				if (SM_GetCursorWorld(px, py))
				{
					SCR_PlayerController lpc = SM_LocalPC();
					if (lpc)
					{
						SM_PointerHub.GetInstance().Show(lpc.GetPlayerId(), px, py, now);
						if (now - m_fSMLastPointSend >= SM_POINT_SEND)
						{
							lpc.SM_RequestPointUpdate(px, py);
							m_fSMLastPointSend = now;
						}
					}
				}
			}

			// --- ФРОНТ ВГОРУ (відпуск) ---
			if (!down && m_bSMLmbDown)
			{
				float heldDur = now - m_fSMPressTime;
				if (m_bSMPointing)
				{
					// завершуємо «вказівник»
					m_bSMPointing = false;
					m_bSMPickedThisPress = false;
					if (m_CursorModule)
						m_CursorModule.HandleDialog(false);	// повертаємо інфо-текст
					SM_SetMapCursorHidden(false);			// повертаємо віджет курсора
					SCR_PlayerController lpc = SM_LocalPC();
					if (lpc)
					{
						SM_PointerHub.GetInstance().Hide(lpc.GetPlayerId());
						lpc.SM_RequestPointStop();
					}
				}
				else if (m_bSMPickedThisPress)
				{
					// це відпуск утримання-підняття → мітка лишається «в руці», ставимо наступним кліком
					m_bSMPickedThisPress = false;
				}
				else if (m_iSMCarryId != -1)
				{
					// поставити (підтвердити переміщення)
					int wx, wy;
					if (SM_GetCursorWorld(wx, wy))
						SM_DoMoveMarker(m_iSMCarryId, wx, wy);	// Local or server
					m_iSMCarryId = -1;
					m_iSMPressMarkerId = -1;
				}
				else if (heldDur < SM_HOLD_SEC && SM_CursorNearPress()
					&& SM_HasFeature(AM_EMapFeature.MARKER_TOOLS))
				{
					// швидкий клік на місці → детект подвійного (редагувати/створити)
					if (m_fSMLastSelectTime > 0 && (now - m_fSMLastSelectTime) <= SM_DOUBLECLICK_SEC)
					{
						m_fSMLastSelectTime = 0;
						SM_MarkerVisual u = SM_FindMarkerUnderCursor();
						if (u && u.m_Data)
						{
							SM_OpenEdit(u.m_Data);
						}
						else
						{
							int wx, wy;
							if (SM_GetCursorWorld(wx, wy))
								SM_OpenCreate(wx, wy);
						}
					}
					else
					{
						m_fSMLastSelectTime = now;
					}
				}
			}
		}

		m_bSMLmbDown = down;	// стан для наступного кадру (фронти)

		// --- ПКМ по військовому пресету (коли діалог відкритий) → видалити (вбудовані захищені) ---
		bool rdown = im.GetActionValue("MouseRight") > 0.5;
		if (rdown && !m_bSMRmbDown && m_MarkerEditRoot)
		{
			if (m_iSMHoveredMilPreset >= 0)
			{
				SM_MapMarkerPresets.GetInstance().RemoveMilitary(m_iSMHoveredMilPreset);
				m_iSMHoveredMilPreset = -1;
				SM_WirePresets();
			}
			else if (m_iSMHoveredGenPreset >= 0)
			{
				SM_MapMarkerPresets.GetInstance().RemoveGeneral(m_iSMHoveredGenPreset);
				m_iSMHoveredGenPreset = -1;
				SM_WirePresets();
			}
		}
		m_bSMRmbDown = rdown;
	}

	// Поллінг вводу з геймпада (консоль). Дискретна модель замість мишачих жестів.
	// Інкремент 1: A (AM_Confirm) контекстна — на мітці відкриває редагування, на пустому місці створює.
	// Курсор той самий (SCR_MapCursorInfo). Move/copy/вказівник додамо наступними кроками.
	protected void SM_PollGamepad(InputManager im)
	{
		// Фокус на БУДЬ-ЯКОМУ UI (ванільне меню інструментів мапи зліва, наша панель малювання,
		// будь-що фокусне) → пад взаємодіє з тим UI, а не з мапою: A/Y/X тут мовчать. Інакше вибір
		// елемента меню кнопкою A одночасно ставив би мітку (баг «компас + мітка»).
		WorkspaceWidget gws = GetGame().GetWorkspace();
		if (gws && gws.GetFocusedWidget())
		{
			// «Проковтнути» поточні натиски: стани = down, щоб після зняття фокуса (вибір у меню
			// кнопкою A) той САМИЙ натиск не спрацював фронтом як тап/дія на мапі. Додатково
			// позначаємо прес «спожитим»: release-гілка A інакше зробила б create/edit на відпусканні.
			m_bSMPadConfirmDown = im.GetActionValue("AM_Confirm") > 0.5;
			if (m_bSMPadConfirmDown)
				m_bSMPickedThisPress = true;
			m_bSMPadPlaceDown   = im.GetActionValue("AM_Place")   > 0.5;
			m_bSMPadDeleteDown  = im.GetActionValue("AM_Delete")  > 0.5;
			return;
		}

		// Пад-малювання: інструмент (олівець/гумка) активний → A затиснутим малює/стирає по курсору
		// (стік веде), відпускання — коміт. B — скасувати інструмент (вийти в звичайний режим),
		// X — видалити штрих під курсором. Мітко-дії A/Y недоступні, поки інструмент активний.
		if (m_DrawCanvas && m_DrawCanvas.IsActive())
		{
			bool draw = im.GetActionValue("AM_Confirm") > 0.5;	// A
			int dwx, dwy;
			bool haveW = SM_GetCursorWorld(dwx, dwy);
			if (draw && !m_bSMPadDrawDown)
			{
				if (haveW)
					m_DrawCanvas.OnPressDown(dwx, dwy);
			}
			else if (draw && m_bSMPadDrawDown)
			{
				if (haveW)
					m_DrawCanvas.OnDrag(dwx, dwy);
			}
			else if (!draw && m_bSMPadDrawDown)
			{
				if (haveW)
					m_DrawCanvas.OnRelease(dwx, dwy);
			}
			m_bSMPadDrawDown = draw;

			// B — скасувати інструмент
			bool cancel = im.GetActionValue("MapContextualMenu") > 0.5 || im.GetActionValue("MenuBack") > 0.5;
			if (cancel && !m_bSMPadCancelDown)
				m_DrawCanvas.SetActive(false);
			m_bSMPadCancelDown = cancel;

			// X — видалити штрих під курсором
			bool delx = im.GetActionValue("AM_Delete") > 0.5;
			if (delx && !m_bSMPadDeleteDown)
				SM_TryDeleteStrokeAtCursor();
			m_bSMPadDeleteDown = delx;

			// Мітко-стан A тримаємо «спожитим», щоб після B-скасування відпуск A не створив мітку.
			m_bSMPadConfirmDown = draw;
			if (draw)
				m_bSMPickedThisPress = true;
			return;
		}
		else
		{
			m_bSMPadDrawDown = false;
			m_bSMPadCancelDown = false;
		}

		bool confirm = im.GetActionValue("AM_Confirm") > 0.5;	// A — тап: редаг/створ; утримання: нести/вказувати
		bool place   = im.GetActionValue("AM_Place")   > 0.5;	// Y — копія останньої мітки
		bool del     = im.GetActionValue("AM_Delete")  > 0.5;	// X — видалити мітку під курсором
		float now = System.GetTickCount() / 1000.0;

		bool placeEdge = place && !m_bSMPadPlaceDown;
		bool delEdge   = del   && !m_bSMPadDeleteDown;

		if (SM_CanAcceptMapClick())
		{
			// --- Y: копія останньої мітки в точку курсора ---
			if (placeEdge && m_iSMCarryId == -1 && !m_bSMPointing
				&& SM_HasFeature(AM_EMapFeature.MARKER_TOOLS)
				&& m_SMLastTemplate && SM_MarkerConfig.GetInstance().m_bAllowCopyLast)
			{
				SM_PlaceCopyAtCursor();
			}
			// --- X: видалити мітку під курсором (залочену зевсом не чіпаємо); нема мітки — штрих малюнка ---
			else if (delEdge && m_iSMCarryId == -1 && !m_bSMPointing)
			{
				SM_MarkerVisual du = null;
				if (SM_HasFeature(AM_EMapFeature.MARKER_TOOLS))
					du = SM_FindMarkerUnderCursor();
				if (du && du.m_Data && !SM_BlockedByLock(du.m_Data))
				{
					SM_DeleteMarkerById(du.m_Data.m_iId);	// Local or server
				}
				else if ((!du || !du.m_Data) && m_DrawPanel)	// stroke erase = drawing tools feature
				{
					SM_TryDeleteStrokeAtCursor();
				}
			}

			// --- A фронт ВНИЗ: запам'ятати позицію/мітку під курсором ---
			if (confirm && !m_bSMPadConfirmDown)
			{
				m_fSMPressTime = now;
				m_fSMPressX = SCR_MapCursorInfo.x;
				m_fSMPressY = SCR_MapCursorInfo.y;
				m_bSMPickedThisPress = false;
				m_iSMPressMarkerId = -1;
				if (SM_HasFeature(AM_EMapFeature.MARKER_TOOLS))	// without tools a marker is not a pick target
				{
					SM_MarkerVisual pu = SM_FindMarkerUnderCursor();
					if (pu && pu.m_Data)
						m_iSMPressMarkerId = pu.m_Data.m_iId;
				}
			}

			// --- A УТРИМАННЯ на мітці ≥ HOLD → підняти (далі слідує за курсором; відпуск поставить) ---
			if (confirm && m_iSMCarryId == -1 && m_iSMPressMarkerId != -1
				&& (now - m_fSMPressTime) >= SM_HOLD_SEC && SM_CursorNearPress())
			{
				SM_MapMarkerData pm = SM_MapMarkerStore.GetInstance().FindById(m_iSMPressMarkerId);
				if (SM_BlockedByLock(pm))
				{
					m_bSMPickedThisPress = true;	// залочена — споживаємо утримання, не піднімаємо
					m_iSMPressMarkerId = -1;
				}
				else
				{
					m_iSMCarryId = m_iSMPressMarkerId;
					m_bSMPickedThisPress = true;
				}
			}

			// --- A УТРИМАННЯ на ПУСТОМУ ≥ SM_POINT_HOLD_SEC → вказівник (показати пальцем) ---
			if (confirm && m_iSMCarryId == -1 && !m_bSMPointing && m_iSMPressMarkerId == -1
				&& (now - m_fSMPressTime) >= SM_POINT_HOLD_SEC
				&& SM_HasFeature(AM_EMapFeature.POINTER)
				&& SM_MarkerConfig.GetInstance().m_bAllowPointer)
			{
				m_bSMPointing = true;
				m_bSMPickedThisPress = true;
				// НЕ кличемо HandleDialog(true): він ставить CS_DIALOG, що блокує пан — а нам треба,
				// щоб лівий стік панорамував і так водив пальцем. Ванільний курсор ховаємо власним методом.
				SM_SetMapCursorHidden(true);
			}

			// --- поки вказуємо: своя точка локально + потік позиції на сервер (троттл) ---
			if (confirm && m_bSMPointing)
			{
				SM_SetMapCursorHidden(true);
				int px, py;
				if (SM_GetCursorWorld(px, py))
				{
					SCR_PlayerController lpc = SM_LocalPC();
					if (lpc)
					{
						SM_PointerHub.GetInstance().Show(lpc.GetPlayerId(), px, py, now);
						if (now - m_fSMLastPointSend >= SM_POINT_SEND)
						{
							lpc.SM_RequestPointUpdate(px, py);
							m_fSMLastPointSend = now;
						}
					}
				}
			}

			// --- A фронт ВГОРУ: завершити вказівник / поставити перенесену / швидкий тап = редаг/створ ---
			if (!confirm && m_bSMPadConfirmDown)
			{
				if (m_bSMPointing)
				{
					m_bSMPointing = false;
					SM_SetMapCursorHidden(false);
					SCR_PlayerController lpc = SM_LocalPC();
					if (lpc)
					{
						SM_PointerHub.GetInstance().Hide(lpc.GetPlayerId());
						lpc.SM_RequestPointStop();
					}
				}
				else if (m_iSMCarryId != -1)
				{
					int wx, wy;
					if (SM_GetCursorWorld(wx, wy))
					{
						SCR_PlayerController mpc = SM_LocalPC();
						if (mpc)
							SM_DoMoveMarker(m_iSMCarryId, wx, wy);	// commit the move (Local or server)
					}
					m_iSMCarryId = -1;
				}
				else if (!m_bSMPickedThisPress && SM_HasFeature(AM_EMapFeature.MARKER_TOOLS))
				{
					// швидкий тап (без утримання) → на мітці редагувати, на пустому створити (відкриваємо на UP)
					SM_MarkerVisual u = SM_FindMarkerUnderCursor();
					if (u && u.m_Data)
						SM_OpenEdit(u.m_Data);
					else
					{
						int wx, wy;
						if (SM_GetCursorWorld(wx, wy))
							SM_OpenCreate(wx, wy);
					}
				}
				m_bSMPickedThisPress = false;
				m_iSMPressMarkerId = -1;
			}
		}

		m_bSMPadConfirmDown = confirm;
		m_bSMPadPlaceDown   = place;
		m_bSMPadDeleteDown  = del;
	}

	// ПКМ: скасувати переміщення (мітка повертається на місце; на сервер нічого не шлемо).
	protected void SM_OnContext(float value, EActionTrigger reason)
	{
		if (SM_TryPanelBack())	// пад-B у панелі часто приходить як MapContextualMenu (фокус на нашій STOP-кнопці)
			return;
		if (m_iSMCarryId != -1)
			m_iSMCarryId = -1;	// наступний кадр позиціонує з m_Data (оригінал)
	}

	// Пад-B у панелі малювання: дворівневий вихід (дропдаун -> ряд -> мапа). true = оброблено.
	// Залежно від фокуса B приходить то як MenuBack, то як MapContextualMenu, тому кличеться
	// з обох слухачів, а дебаунс не дає одним натиском перескочити рівень.
	protected bool SM_TryPanelBack()
	{
		if (!m_bSMPanelPadNav || !m_DrawPanel)
			return false;
		float now = System.GetTickCount() / 1000.0;
		if (now - m_fSMLastPanelBack < 0.15)
			return true;
		m_fSMLastPanelBack = now;

		if (!m_DrawPanel.HandleBack())
			SM_PanelExit();
		InputManager im = GetGame().GetInputManager();
		im.ResetAction("MenuBack");
		im.ResetAction("MapContextualMenu");
		return true;
	}

	// Пад: хрестовина вправо — зайти фокусом у панель малювання (перший елемент) / вийти з неї.
	// Поки фокус усередині панелі, пад-дії мапи (A/Y/X) мовчать (гейт у SM_PollGamepad),
	// тож вибір елемента НЕ ставить мітку. Зевсу поки не даємо (окрема задача).
	protected void SM_OnPanelFocus(float value, EActionTrigger reason)
	{
		if (!m_bSMMapOpen || m_MarkerEditRoot || !m_DrawPanel || SM_IsEditorMap())
			return;
		InputManager pim = GetGame().GetInputManager();
		if (!pim || pim.IsUsingMouseAndKeyboard())	// лише геймпад
			return;

		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;

		Widget f = ws.GetFocusedWidget();
		if (f && m_DrawPanel.ContainsWidget(f))
		{
			SM_PanelExit();	// повторний LB — вихід із меню
			return;
		}
		Widget first = m_DrawPanel.GetFirstFocusTarget();
		if (first)
		{
			m_bSMPanelPadNav = true;	// свідомий вхід (щоб анти-автофокус в Update не скинув)
			m_DrawPanel.SetPadFocusMode(true);	// зробити кнопки фокусними на час входу
			ws.SetFocusedWidget(first);	// зайти в панель: фокус + підсвітка першого елемента
			m_DrawPanel.NotifyPadEntered();	// «проковтнути» цей натиск для навігації
		}
	}

	// Видалити штрих малюнка під курсором (спільне для Del/пад-X): залочений зевсом —
	// локальне повідомлення без RPC, інакше запит на сервер (він перевірить права).
	protected void SM_TryDeleteStrokeAtCursor()
	{
		if (!m_DrawCanvas)
			return;
		WorkspaceWidget xws = GetGame().GetWorkspace();
		if (!xws)
			return;
		int dwx, dwy;
		bool dHaveW = SM_GetCursorWorld(dwx, dwy);
		int strokeId = m_DrawCanvas.FindStrokeAtScreen(
			xws.DPIScale(SCR_MapCursorInfo.x), xws.DPIScale(SCR_MapCursorInfo.y), dwx, dwy, dHaveW);
		if (strokeId == -1)
			return;
		SCR_PlayerController spc = SM_LocalPC();
		if (!spc)
			return;
		SM_DeleteStrokeById(strokeId, spc);
	}

	// Delete a whole stroke by id: Local (id <= -2) from the client file; server ones via the
	// outbox (GM-locked -> local message, no RPC).
	protected void SM_DeleteStrokeById(int strokeId, notnull SCR_PlayerController pc)
	{
		// Local CHANNEL (not an optimistic server temp) -> client file.
		if (SM_MapDrawingStore.IsLocalId(strokeId) && !SM_DrawOutbox.IsServerTemp(strokeId))
		{
			SM_LocalDrawingPersistence.GetInstance().RemoveLocal(strokeId);
			return;
		}
		SM_MapDrawingData sdel = SM_DrawCanvas.GetStrokeData(strokeId);
		if (sdel && sdel.m_iGmLocked != 0 && !SM_IsEditorMap())
			pc.SM_ShowPlaceDenied(SM_EPlaceDenyReason.DRAW_LOCKED, 0);	// GM-locked — hands off (no optimistic removal)
		else
			SM_DrawOutbox.SubmitRemove(strokeId);	// batched/optimistic (a temp just gets cancelled)
	}

	// Пад-режим панелі: інструмент мапи щойно тогльнувся (напр. подвійний тап хрестовини =
	// компас/лінійка) — відкочуємо, щоб гортання нашого меню не вмикало ванільні інструменти.
	protected bool m_bSMToolGuardBusy;	// захист від повторного входу (SetActive сам фаєрить інвокер)
	protected void SM_OnToolToggledGuard(SCR_MapToolEntry entry)
	{
		if (!m_bSMPanelPadNav || !entry || m_bSMToolGuardBusy)
			return;
		m_bSMToolGuardBusy = true;
		entry.SetActive(false);
		m_bSMToolGuardBusy = false;
	}

	// Вихід із пад-режиму панелі: фокус мапі, кнопки знову НЕфокусні.
	protected void SM_PanelExit()
	{
		m_bSMPanelPadNav = false;
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (ws)
			ws.SetFocusedWidget(null);
		if (m_DrawPanel)
		{
			m_DrawPanel.ClosePanelDropdowns();	// не лишати відкритий дропдаун (напр. слайдер прозорості) після виходу
			m_DrawPanel.SetPadFocusMode(false);
		}
	}

	// Delete: видалити мітку під курсором.
	protected void SM_OnDelete(float value, EActionTrigger reason)
	{
		if (!m_bSMMapOpen || !SM_CanAcceptMapClick())
			return;

		SM_MarkerVisual vis = null;
		if (SM_HasFeature(AM_EMapFeature.MARKER_TOOLS))
			vis = SM_FindMarkerUnderCursor();
		if (!vis || !vis.m_Data)
		{
			// Мітки під курсором нема — може, там штрих малюнка? Del стирає його цілком
			// (права перевіряє сервер: власник/GM).
			if (m_DrawCanvas && m_DrawPanel)	// stroke erase needs the drawing-tools feature
			{
				WorkspaceWidget dws = GetGame().GetWorkspace();
				if (dws)
				{
					int pdwx, pdwy;
					bool pdHaveW = SM_GetCursorWorld(pdwx, pdwy);
					int strokeId = m_DrawCanvas.FindStrokeAtScreen(
						dws.DPIScale(SCR_MapCursorInfo.x), dws.DPIScale(SCR_MapCursorInfo.y), pdwx, pdwy, pdHaveW);
					if (strokeId != -1)
					{
						SCR_PlayerController dpc = SM_LocalPC();
						if (dpc)
							SM_DeleteStrokeById(strokeId, dpc);	// Local or server (with the GM-lock check)
					}
				}
			}
			return;
		}

		if (SM_BlockedByLock(vis.m_Data))	// залочена зевсом — гравець не видаляє (повідомлення + звук)
			return;

		int delId = vis.m_Data.m_iId;
		if (SM_MapMarkerStore.IsLocalId(delId))
		{
			SM_LocalMarkerPersistence.GetInstance().RemoveLocal(delId);	// Local — erase from the client file
			return;
		}
		SCR_PlayerController pc = SM_LocalPC();
		if (pc)
			pc.SM_RequestRemove(delId);	// сервер видаляє по ID і синкає всім
	}

	// Чи курсор біля точки натискання (в межах порога) — щоб відрізнити клік/утримання від перетягування.
	protected bool SM_CursorNearPress()
	{
		float dx = SCR_MapCursorInfo.x - m_fSMPressX;
		float dy = SCR_MapCursorInfo.y - m_fSMPressY;
		return (dx * dx + dy * dy) <= SM_MOVE_THRESHOLD * SM_MOVE_THRESHOLD;
	}

	// Світова позиція під курсором (канонічний ванільний патерн; координати курсора статичні).
	protected bool SM_GetCursorWorld(out int worldX, out int worldY)
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return false;

		float wx, wy;
		m_MapEntity.ScreenToWorld(ws.DPIScale(SCR_MapCursorInfo.x), ws.DPIScale(SCR_MapCursorInfo.y), wx, wy);
		worldX = wx;
		worldY = wy;
		return true;
	}

	// Наша мітка під курсором — власний екранний хіт-тест (надійніше за трасування віджетів;
	// працює для цивільних і військових однаково). Бере найближчу мітку в межах її радіуса.
	protected SM_MarkerVisual SM_FindMarkerUnderCursor()
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return null;

		// Той самий «папір»-масштаб, що й у позиціонуванні (мітка зменшується при віддаленні).
		float maxZoom = m_MapEntity.GetMaxZoom();
		float factor = 1.0;
		if (maxZoom > 0)
			factor = Math.Clamp(m_MapEntity.GetCurrentZoom() / maxZoom, SM_MIN_ZOOM_SCALE, 1.0);

		float cx = SCR_MapCursorInfo.x;	// екранна позиція курсора (DPI-unscaled, як і слоти міток)
		float cy = SCR_MapCursorInfo.y;

		SM_MarkerVisual best = null;
		float bestDist2 = -1;

		foreach (int id, SM_MarkerVisual vis : m_mSMVisuals)
		{
			if (!vis || !vis.m_Data)
				continue;

			int sx, sy;
			m_MapEntity.WorldToScreen(vis.m_Data.m_iPosX, vis.m_Data.m_iPosY, sx, sy, true);
			float mx = ws.DPIUnscale(sx);
			float my = ws.DPIUnscale(sy);

			float half = SM_BASE_SIZE * SM_SizeFactor(vis.m_Data.m_iSize) * factor * 0.5;
			if (half < 18)	// мінімальний радіус кліку (дрібні мітки на віддаленні)
				half = 18;

			float dx = cx - mx;
			float dy = cy - my;
			if (dx < -half || dx > half || dy < -half || dy > half)
				continue;

			float dist2 = dx * dx + dy * dy;
			if (bestDist2 < 0 || dist2 < bestDist2)
			{
				bestDist2 = dist2;
				best = vis;
			}
		}
		return best;
	}

	protected SCR_PlayerController SM_LocalPC()
	{
		return SCR_PlayerController.Cast(GetGame().GetPlayerController());
	}

	// ДІАЛОГ — перевикористовуємо ванільний CreateMarkerEditDialog (іконки/колір/текст/поворот),
	// але перенаправляємо підтвердження (OnInsertMarker) на НАШУ систему.
	// Подвійний клік по порожньому — відкрити діалог створення в позиції кліку.
	protected void SM_OpenCreate(int worldX, int worldY)
	{
		// У GM-мапі спершу ховаємо UI редактора + кладемо бекдроп, ПОТІМ створюємо діалог (щоб він був
		// над бекдропом). Бекдроп поглинає кліки повз діалог, тож редактор не «оживає» від них.
		if (SM_IsEditorMap())
			SM_SetEditorUIHidden(true);

		m_iSMEditId = -1;
		m_iSMPlaceX = worldX;
		m_iSMPlaceY = worldY;
		m_iSMSelectedSize = 200;	// стандартно 200%
		m_iSMSelectedVis  = SM_DrawCanvas.GetBrushVisibility();	// дефолт — канал із панелі малювання (запамʼятований вибір гравця)
		m_iSMMinVis       = SM_EMarkerVisibility.PERSONAL;	// нова мітка — будь-яка видимість дозволена
		m_iSMSelectedVis  = SM_FirstAllowedVis(m_iSMSelectedVis);	// якщо канал заборонено конфігом — беремо перший дозволений
		m_iSMSelKind = SM_EMarkerKind.CIVILIAN;	// стартуємо як цивільна (поки не чіпнули військові контроли)
		m_bSMTextColored = false;
		m_bSMTimestamp = false;
		m_bSMGmLocked = false;			// GM: нова мітка не залочена
		m_bSMHideInfo = false;			// GM: нова мітка — інфо при наведенні видиме
		m_iSMGmFactionChosen = -1;		// GM: дефолт — перша фракція у дропдауні
		// У GM-мапі діалог за замовчуванням чіпляється під UI редактора (m_RootWidget = корінь мапи).
		// Тимчасово робимо батьком корінь workspace, щоб діалог був НАД UI редактора й отримував кліки.
		Widget savedRoot = null;
		bool editorMap = SM_IsEditorMap();
		if (editorMap)
		{
			savedRoot = m_RootWidget;
			m_RootWidget = GetGame().GetWorkspace();
		}
		CreateMarkerEditDialog(false);
		if (editorMap)
			m_RootWidget = savedRoot;	// повертаємо (наші мітки мають лишатись у шарі мапи)
		m_bSMMilColorUser = false;	// ПІСЛЯ діалогу: ванільний InitColorIcons клацає OnColorEntryClicked
		m_bSMBlackColor = false;
		SM_AddBlackColorButton();
		SM_PositionDialog();
		SM_OverrideCharLimit();
		SM_HideVanillaPreview();
		SM_WireControls();
		SM_WireMilitary();
		SM_WirePresets();
		SM_PresetBrushColor();	// колір нової мітки = колір пензля з панелі малювання (лише цивільна)
		SM_BeginPreview();
		SM_CenterOnPlacement(worldX, worldY);
		SM_NavBegin();	// геймпад: запустити секційний контролер навігації
	}

	// Подвійний клік по мітці — відкрити діалог редагування, ЗАПОВНЕНИЙ її даними.
	protected void SM_OpenEdit(notnull SM_MapMarkerData data)
	{
		if (SM_BlockedByLock(data))	// залочена зевсом мітка — гравець не редагує (повідомлення + звук уже показано)
			return;

		if (SM_IsEditorMap())
			SM_SetEditorUIHidden(true);	// ховаємо UI редактора на час нашого діалогу (як у SM_OpenCreate)

		m_iSMEditId = data.m_iId;
		m_iSMPlaceX = data.m_iPosX;
		m_iSMPlaceY = data.m_iPosY;
		m_iSMSelectedSize = data.m_iSize;
		if (m_iSMSelectedSize < 10)		// легасі-збереження (enum 0..3) → 100%
			m_iSMSelectedSize = 100;
		m_iSMSelectedVis  = data.m_iVisibility;
		m_iSMMinVis       = data.m_iVisibility;	// widen-only: звужувати нижче поточної не можна (кнопки заблокуються)
		m_bSMTextColored  = (data.m_iTextColored != 0);	// до SM_WireControls (виставить галочку)
		m_bSMTimestamp    = (data.m_iDate != 0);	// до SM_WireControls (виставить SpinBox)
		m_bSMGmLocked     = (data.m_iGmLocked != 0);	// GM: передзаповнити Locked
		m_bSMHideInfo     = (data.m_iHideInfo != 0);	// GM: передзаповнити Hide info
		if (data.m_iVisibility == SM_EMarkerVisibility.FACTION)
			m_iSMGmFactionChosen = data.m_iChannel;		// GM: передвибрати фракцію в дропдауні
		else
			m_iSMGmFactionChosen = -1;

		int catID = 0;
		int colorIdx = -1;
		if (m_PlacedMarkerConfig)
		{
			catID = m_PlacedMarkerConfig.GetIconCategoryID(data.m_iIconEntry);
			colorIdx = SM_FindColorIndex(data.m_iColor);	// ARGB → індекс палітри (передвибрати колір)
		}

		// isEditing=false навмисно: ванільний isEditing=true розіменовує m_EditedMarker (у нас його нема).
		// У GM-мапі діалог чіпляємо на корінь workspace (над UI редактора), щоб він отримував кліки.
		Widget savedRoot = null;
		bool editorMap = SM_IsEditorMap();
		if (editorMap)
		{
			savedRoot = m_RootWidget;
			m_RootWidget = GetGame().GetWorkspace();
		}
		CreateMarkerEditDialog(false, catID, data.m_iIconEntry, colorIdx);
		if (editorMap)
			m_RootWidget = savedRoot;
		SM_PositionDialog();
		SM_OverrideCharLimit();

		// Передзаповнюємо поля наявними параметрами (текст у editbox, поворот у повзунок),
		// ніби гравець уже їх вибрав. (OnEditBoxTextChanged ставить лише прев'ю-текст — треба SetValue.)
		if (m_EditBoxComp)
			m_EditBoxComp.SetValue(data.m_sText);
		if (m_SliderComp)
			m_SliderComp.SetValue(data.m_iRotation);

		SM_HideVanillaPreview();
		m_bSMMilColorUser = false;	// ПІСЛЯ діалогу (InitColorIcons клацає OnColorEntryClicked)
		m_bSMBlackColor = false;
		SM_AddBlackColorButton();
		if (data.m_iColor == SM_BLACK_ARGB)	// мітку було збережено з нашим чорним → відновити вибір
			SM_SelectBlack();
		SM_WireControls();	// підхопить m_iSMSelectedSize/Vis (виставить повзунок + підсвітку)
		SM_WireMilitary();
		SM_WirePresets();

		// Тип мітки з даних; для військової — відновлюємо вибір (підсвітка фракції/виміру + текст).
		m_iSMSelKind = data.m_iKind;
		if (data.m_iKind == SM_EMarkerKind.MILITARY)
		{
			int fIdx = SM_FindFactionIndex(data.m_iIdentity, data.m_iColor);
			if (fIdx >= 0)
				SM_SelectFaction(fIdx, false);	// підсвітить фракцію + перебудує виміри
			SCR_MapMarkerEntryMilitary mcfg = SM_MilConfig();
			if (mcfg)
			{
				int dIdx = mcfg.GetDimensionEntryID(data.m_iDimension);
				if (dIdx >= 0)
					SM_SelectDimension(dIdx, false);	// підсвітить збережений вимір
			}
			// Стан символа з даних (перекриває дефолти від SM_SelectFaction вище).
			m_iSMSelIdentity  = data.m_iIdentity;
			m_iSMSelDimension = data.m_iDimension;
			SM_PrefillMilitaryTypes(data.m_iSymbolFlags);	// виставить Type 1/Type 2 комбобокси
			m_iSMMilColor     = data.m_iColor;	// SM_MilColor() поверне його (m_bSMMilColorUser=false)
		}

		// Ховаємо реальну мітку, яку редагуємо — інакше вона дублюється з живим прев'ю.
		m_iSMHiddenMarkerId = data.m_iId;
		SM_SetMarkerVisible(data.m_iId, false);

		SM_BeginPreview();
		SM_CenterOnPlacement(data.m_iPosX, data.m_iPosY);
		SM_NavBegin();	// геймпад: запустити секційний контролер навігації
	}

	// === КОНСОЛЬНА НАВІГАЦІЯ ДІАЛОГУ (секційна, дворівнева) ===
	// Рівень 0 (вибір секції): фокус рушія очищено, поточну секцію лишаємо яскравою, решту притемнюємо.
	//   MenuUp/Down — перемикання секції, A — зайти, B — скасувати діалог.
	// Рівень 1 (всередині): рух по елементах веде рушій, але УТРИМУЄМО фокус у межах секції (повертаємо,
	//   якщо рушій вискочив назовні). A — активувати елемент (рушій), B — назад на рівень секцій.
	// Place — окремий екшен AM_Place (кнопка Y), прив'язаний до ButtonPublic у layout (ставить звідусіль).

	// Список ВИДИМИХ секцій (контейнер для підсвітки/меж). TextColorCheck — окрема секція-галочка.
	protected void SM_NavBuildEntries()
	{
		m_aSMNavContainers.Clear();
		array<string> conts = {"UserPresets", "MilitaryPresets", "FactionSelector", "DimensionSelector", "ComboBox1", "ComboBox2", "IconSelector", "ColorSelector", "EditBoxRoot", "SliderRoot", "SliderSize", "TimestampSpinBox", "Visibility"};
		foreach (string n : conts)
		{
			if (m_MarkerEditRoot && m_MarkerEditRoot.FindAnyWidget(n))
				m_aSMNavContainers.Insert(n);
		}
	}

	// Запуск контролера при відкритті діалогу (тільки геймпад). На KB/M — не активуємо (працює мишею).
	// true, якщо гравець зараз на миші+клавіатурі. Наш падовий контролер мусить повністю мовчати на KB/M,
	// інакше клавіші навігації (стрілки тощо) керують секціями замість вводу тексту в полі назви.
	protected bool SM_NavOnKBM()
	{
		InputManager im = GetGame().GetInputManager();
		return im && im.IsUsingMouseAndKeyboard();
	}

	// «Паркування» фокуса на поточному кольорі (анти-пресет): тримаємо фокус не-null, щоб A не активував
	// перший пресет, але прапор m_bSMNavParking каже OnColorEntryClicked НЕ вважати це вибором кольору
	// (інакше військовому маркеру біліє колір через m_bSMMilColorUser).
	protected void SM_NavParkColor()
	{
		if (!m_SelectedColorButton || !m_SelectedColorButton.GetRootWidget())
			return;
		m_bSMNavParking = true;
		GetGame().GetWorkspace().SetFocusedWidget(m_SelectedColorButton.GetRootWidget());
		m_bSMNavParking = false;
	}

	protected void SM_NavBegin()
	{
		InputManager im = GetGame().GetInputManager();
		m_bSMNavActive = false;
		m_iSMNavLevel = 0;
		m_iSMNavSection = 0;
		m_iSMNavItem = 0;
		m_bSMNavOnCheck = false;
		m_aSMNavItems.Clear();
		if (!im || im.IsUsingMouseAndKeyboard() || !m_MarkerEditRoot)
			return;

		SM_NavBuildEntries();
		if (m_aSMNavContainers.IsEmpty())
			return;
		m_bSMNavActive = true;

		// запам'ятати поточну вкладку іконок — щоб OnTabChanged відкочував випадковий LB/RB саме на неї
		if (m_TabComponent)
			m_iSMTabIndex = m_TabComponent.GetShownTab();

		// Геймпад: робимо нижні кнопки лише-показовими (гліф+підпис лишаються), щоб A/B їх НЕ тригерили
		// (A ставив би мітку, B закривав діалог). Place робимо опитуванням Y, Cancel — нашим B.
		// На KB/M цей метод не викликається (контролер не активний), тож там кнопки працюють штатно.
		SM_NavSetPlaceGlyph();				// Place: гліф = AM_Place (Y на паді), не лише-показовий MenuSelect (A)
		SM_NavMakeDisplayOnly("ButtonCancel");

		// Текст: підписуємось на завершення вводу (екранна клавіатура), щоб зняти паузу контролера.
		m_bSMNavTyping = false;
		m_bSMNavWriteSub = false;
		if (m_EditBoxComp)
		{
			m_EditBoxComp.m_OnWriteModeLeave.Insert(SM_NavOnWriteLeave);
			m_bSMNavWriteSub = true;
		}
	}

	// Користувач закрив екранну клавіатуру — знімаємо паузу контролера.
	protected void SM_NavOnWriteLeave(string text)
	{
		m_bSMNavTyping = false;
	}

	// KB/M фокус-лок поля назви: вмикається при вході в режим вводу, знімається ЛИШЕ на Enter/Esc
	// (а не на втраті фокуса від ховера — тоді Update поверне режим вводу назад).
	protected void SM_OnEditWriteEnter()
	{
		m_bSMTextLock = true;
	}

	protected void SM_OnEditConfirmLock(SCR_EditBoxComponent comp, string val)
	{
		m_bSMTextLock = false;
	}

	protected void SM_OnEditCancelLock()
	{
		m_bSMTextLock = false;
	}

	// Place-кнопка (ButtonPublic): у layout вона "MenuSelect" (на паді гліф = A) для правильного гліфа на KB/M.
	// Але ставимо ми падом через AM_Place (Y). Тож на паді підміняємо дію кнопки на AM_Place → гліф стає Y,
	// і робимо її лише-показовою (ставимо опитуванням AM_Place у SM_NavTick, а не слухачем кнопки).
	protected void SM_NavSetPlaceGlyph()
	{
		if (!m_MarkerEditRoot)
			return;
		Widget w = m_MarkerEditRoot.FindAnyWidget("ButtonPublic");
		if (!w)
			return;
		SCR_InputButtonComponent c = SCR_InputButtonComponent.Cast(w.FindHandler(SCR_InputButtonComponent));
		if (!c)
			return;
		c.SetAction("AM_Place");	// гліф → Y (бренд-залежний: Xbox Y / PS △), оновлює візуал
		c.SM_MakeDisplayOnly();		// прибрати слухачів AM_Place, щоб кнопка не дублювала розміщення
	}

	protected void SM_NavMakeDisplayOnly(string name)
	{
		if (!m_MarkerEditRoot)
			return;
		Widget w = m_MarkerEditRoot.FindAnyWidget(name);
		if (!w)
			return;
		SCR_InputButtonComponent c = SCR_InputButtonComponent.Cast(w.FindHandler(SCR_InputButtonComponent));
		if (c)
			c.SM_MakeDisplayOnly();
	}

	protected Widget SM_NavContainer(int idx)
	{
		if (!m_MarkerEditRoot || idx < 0 || idx >= m_aSMNavContainers.Count())
			return null;
		return m_MarkerEditRoot.FindAnyWidget(m_aSMNavContainers[idx]);
	}

	// Віджет для ПІДСВІТКИ секції (може бути вужчим за контейнер логіки). Color → лише рядок кольорів
	// (ColorSelector охоплює й мітку, й галочку — підсвічувати весь рядок збиває з пантелику).
	protected Widget SM_NavHighlightTarget(int idx)
	{
		if (!m_MarkerEditRoot || idx < 0 || idx >= m_aSMNavContainers.Count())
			return null;
		if (m_aSMNavContainers[idx] == "ColorSelector")
		{
			// ВАЖЛИВО: іконкові рядки теж звуться "ColorSelectorLine" (той самий layout), тож шукаємо
			// саме ВСЕРЕДИНІ контейнера ColorSelector, а не по всьому діалогу.
			Widget cs = m_MarkerEditRoot.FindAnyWidget("ColorSelector");
			if (cs)
			{
				Widget line = cs.FindAnyWidget("ColorSelectorLine");
				if (line)
					return line;
			}
		}
		return SM_NavContainer(idx);
	}

	// Оверлей підсвітки (жовто-оранжевий) — ліниво на корені workspace. ImageWidget+SetColor малює
	// суцільний колір без текстури (як кольорові свотчі палітри).
	protected void SM_NavEnsureHL()
	{
		if (m_wSMNavHL)
			return;
		WorkspaceWidget ws = GetGame().GetWorkspace();
		m_wSMNavHL = ImageWidget.Cast(ws.CreateWidget(WidgetType.ImageWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.BLEND | WidgetFlags.STRETCH | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(SM_NAV_HL_COLOR), 0, ws));
		if (m_wSMNavHL)
		{
			m_wSMNavHL.SetColor(Color.FromInt(SM_NAV_HL_COLOR));
			FrameSlot.SetAnchorMin(m_wSMNavHL, 0, 0);
			FrameSlot.SetAnchorMax(m_wSMNavHL, 0, 0);
			FrameSlot.SetAlignment(m_wSMNavHL, 0, 0);
			FrameSlot.SetSizeToContent(m_wSMNavHL, false);
		}
	}

	protected void SM_NavHideHL()
	{
		if (m_wSMNavHL)
			m_wSMNavHL.SetVisible(false);
	}

	protected void SM_NavDestroyHL()
	{
		if (m_wSMNavHL)
		{
			m_wSMNavHL.RemoveFromHierarchy();
			m_wSMNavHL = null;
		}
	}

	// Накласти оверлей на контейнер секції (екранні px → юніти workspace).
	protected void SM_NavPositionHL(Widget container)
	{
		if (!container)
		{
			SM_NavHideHL();
			return;
		}
		SM_NavEnsureHL();
		if (!m_wSMNavHL)
			return;
		WorkspaceWidget ws = GetGame().GetWorkspace();
		float sx, sy, sw, sh;
		container.GetScreenPos(sx, sy);
		container.GetScreenSize(sw, sh);
		FrameSlot.SetPos(m_wSMNavHL, ws.DPIUnscale(sx), ws.DPIUnscale(sy));
		FrameSlot.SetSize(m_wSMNavHL, ws.DPIUnscale(sw), ws.DPIUnscale(sh));
		m_wSMNavHL.SetVisible(true);
	}

	// Підсвітка галочки: позиція від TextColorCheck, але ФІКСОВАНИЙ розмір видимого квадрата (28×28).
	// (GetScreenSize кнопки більший за квадрат → зсув; TextColorBox зміщений padding-ом.)
	protected void SM_NavPositionCheckHL()
	{
		if (!m_MarkerEditRoot)
		{
			SM_NavHideHL();
			return;
		}
		Widget cb = m_MarkerEditRoot.FindAnyWidget("TextColorCheck");
		if (!cb)
		{
			SM_NavHideHL();
			return;
		}
		SM_NavEnsureHL();
		if (!m_wSMNavHL)
			return;
		WorkspaceWidget ws = GetGame().GetWorkspace();
		float sx, sy;
		cb.GetScreenPos(sx, sy);
		FrameSlot.SetPos(m_wSMNavHL, ws.DPIUnscale(sx), ws.DPIUnscale(sy));
		FrameSlot.SetSize(m_wSMNavHL, 28, 28);	// розмір видимого квадрата (TextColorBox override)
		m_wSMNavHL.SetVisible(true);
	}

	// Зібрати елементи ГОРИЗОНТАЛЬНОЇ секції (Color / Visibility) у m_aSMNavItems.
	protected void SM_NavCollectItems()
	{
		m_aSMNavItems.Clear();
		m_iSMNavItem = 0;
		if (!m_MarkerEditRoot || m_iSMNavSection < 0 || m_iSMNavSection >= m_aSMNavContainers.Count())
			return;
		string c = m_aSMNavContainers[m_iSMNavSection];
		if (c == "Visibility")
		{
			array<string> btns = {"ButtonLocal", "ButtonGroup", "ButtonSide", "ButtonAll"};
			foreach (string b : btns)
			{
				Widget w = m_MarkerEditRoot.FindAnyWidget(b);
				if (w)
					m_aSMNavItems.Insert(w);
			}
			return;
		}
		if (c == "UserPresets")
		{
			foreach (SCR_ButtonImageComponent bc : m_aSMGenPresetBtns)
				if (bc && bc.GetRootWidget())
					m_aSMNavItems.Insert(bc.GetRootWidget());
			if (m_SMGenAddBtn && m_SMGenAddBtn.GetRootWidget())
				m_aSMNavItems.Insert(m_SMGenAddBtn.GetRootWidget());	// «+» зберегти поточну
			return;
		}
		if (c == "MilitaryPresets")
		{
			foreach (SCR_ButtonImageComponent bc : m_aSMMilPresetBtns)
				if (bc && bc.GetRootWidget())
					m_aSMNavItems.Insert(bc.GetRootWidget());
			if (m_SMMilAddBtn && m_SMMilAddBtn.GetRootWidget())
				m_aSMNavItems.Insert(m_SMMilAddBtn.GetRootWidget());
			return;
		}
		if (c == "FactionSelector")	// військова фракція — ряд кнопок (вибір робить мітку військовою)
		{
			foreach (SCR_ButtonImageComponent bc : m_aSMFactionBtns)
				if (bc && bc.GetRootWidget())
					m_aSMNavItems.Insert(bc.GetRootWidget());
			return;
		}
		if (c == "DimensionSelector")	// категорія (вимір)
		{
			foreach (SCR_ButtonImageComponent bc : m_aSMDimensionBtns)
				if (bc && bc.GetRootWidget())
					m_aSMNavItems.Insert(bc.GetRootWidget());
			return;
		}
		if (c == "ColorSelector")
		{
			for (int i = 0; i < 400; i++)
			{
				Widget w = m_MarkerEditRoot.FindAnyWidget("ColorEntry" + i.ToString());
				if (w)
					m_aSMNavItems.Insert(w);
			}
			// наша чорна кнопка (без імені ColorEntryN)
			if (m_SMBlackBtn && m_SMBlackBtn.GetRootWidget())
				m_aSMNavItems.Insert(m_SMBlackBtn.GetRootWidget());
		}
	}

	// Чи поточна горизонтальна секція має ОБЕРНЕНИЙ напрямок (індекси справа наліво).
	protected bool SM_NavReversed()
	{
		string c = SM_NavName();
		return c == "ColorSelector" || c == "FactionSelector" || c == "DimensionSelector";
	}

	// Назва поточної секції (зручність).
	protected string SM_NavName()
	{
		if (m_iSMNavSection < 0 || m_iSMNavSection >= m_aSMNavContainers.Count())
			return "";
		return m_aSMNavContainers[m_iSMNavSection];
	}

	// Секції-«значення»: керуються Вліво/Вправо прямо на рівні секцій (без заходу). A на них нічого не робить.
	protected bool SM_NavIsValueSection()
	{
		string c = SM_NavName();
		return c == "SliderRoot" || c == "SliderSize" || c == "TimestampSpinBox";
	}

	// Секції, де A виконує дію (не «заходить»): текст (клавіатура) і Type-комбобокси (дропдаун).
	protected bool SM_NavIsActionSection()
	{
		string c = SM_NavName();
		return c == "EditBoxRoot" || c == "ComboBox1" || c == "ComboBox2";
	}

	protected const float SM_NAV_ROT_STEP  = 15;	// крок повороту, °
	protected const float SM_NAV_SIZE_STEP = 10;	// крок розміру, %

	// Слайдер поточної секції (Rotation → m_SliderComp, Size → m_SMSizeSlider).
	protected SCR_SliderComponent SM_NavSlider()
	{
		string c = SM_NavName();
		if (c == "SliderRoot")
			return m_SliderComp;
		if (c == "SliderSize")
			return m_SMSizeSlider;
		return null;
	}

	protected void SM_NavSliderAdjust(float delta)
	{
		SCR_SliderComponent s = SM_NavSlider();
		if (!s)
			return;
		float v = s.GetValue() + delta;
		float mn = s.GetMin();
		float mx = s.GetMax();
		if (v < mn) v = mn;
		if (v > mx) v = mx;
		s.SetValue(v);	// SetValue сам кидає OnValueChanged → оновлює rotation/size + прев'ю
	}

	// Timestamp (спінбокс No/Yes): індекс 1 = Yes, 0 = No.
	protected void SM_NavSetTimestamp(bool yes)
	{
		if (!m_MarkerEditRoot)
			return;
		Widget tsw = m_MarkerEditRoot.FindAnyWidget("TimestampSpinBox");
		if (!tsw)
			return;
		SCR_SpinBoxComponent sb = SCR_SpinBoxComponent.Cast(tsw.FindHandler(SCR_SpinBoxComponent));
		if (!sb)
			return;
		int idx = 0;
		if (yes)
			idx = 1;
		sb.SetCurrentItem(idx, true, true);	// notify → SM_OnTimestampChanged оновить m_bSMTimestamp
		m_bSMTimestamp = yes;				// страховка (на випадок, якщо notify не спрацює)
	}

	// Зібрати СІТКУ іконок: рядки = діти m_IconSelector, у кожному кнопки IconEntry (візуальний порядок).
	protected void SM_NavBuildGrid()
	{
		m_aSMNavGrid.Clear();
		m_iSMNavRow = 0;
		m_iSMNavCol = 0;
		if (!m_IconSelector)
			return;
		Widget line = m_IconSelector.GetChildren();
		while (line)
		{
			array<Widget> row = {};
			Widget e = line.GetChildren();
			while (e)
			{
				if (e.GetName().Contains("IconEntry"))
					row.Insert(e);
				e = e.GetSibling();
			}
			if (!row.IsEmpty())
				m_aSMNavGrid.Insert(row);
			line = line.GetSibling();
		}
	}

	// Сфокусувати поточну клітинку сітки (пришпилення).
	protected void SM_NavFocusGrid()
	{
		if (m_aSMNavGrid.IsEmpty())
			return;
		if (m_iSMNavRow < 0)
			m_iSMNavRow = 0;
		if (m_iSMNavRow >= m_aSMNavGrid.Count())
			m_iSMNavRow = m_aSMNavGrid.Count() - 1;
		array<Widget> row = m_aSMNavGrid[m_iSMNavRow];
		if (row.IsEmpty())
			return;
		if (m_iSMNavCol < 0)
			m_iSMNavCol = 0;
		if (m_iSMNavCol >= row.Count())
			m_iSMNavCol = row.Count() - 1;
		Widget w = row[m_iSMNavCol];
		if (w)
			GetGame().GetWorkspace().SetFocusedWidget(w);
	}

	// Секції, чиї елементи НЕ приймають фокус (кнопки Visibility тощо) — навігуємо НАШИМ оверлеєм,
	// а фокус тримаємо на кольорі (анти-пресет). Кольори/іконки натомість фокусуємо (для live-прев'ю).
	protected bool SM_NavIsOverlaySection()
	{
		// Лише Visibility (її кнопки SCR_InputButtonComponent не фокусуються). Presets/Military —
		// SCR_ButtonImageComponent (як кольори) → ФОКУС-навігація, без пришпилення кольору (інакше
		// застосований пресетом колір щокадру перезаписувався б старим).
		return SM_NavName() == "Visibility";
	}

	// Сфокусувати/підсвітити поточний елемент горизонтальної секції (пришпилення).
	protected void SM_NavFocusItem()
	{
		if (m_aSMNavItems.IsEmpty())
			return;
		if (m_iSMNavItem < 0)
			m_iSMNavItem = 0;
		if (m_iSMNavItem >= m_aSMNavItems.Count())
			m_iSMNavItem = m_aSMNavItems.Count() - 1;
		Widget w = m_aSMNavItems[m_iSMNavItem];
		if (!w)
			return;
		if (SM_NavIsOverlaySection())
		{
			// елемент може бути не фокусованим → підсвічуємо оверлеєм, фокус «паркуємо» на кольорі (анти-пресет)
			SM_NavParkColor();
			SM_NavPositionHL(w);
		}
		else
		{
			SM_NavHideHL();
			GetGame().GetWorkspace().SetFocusedWidget(w);
		}
	}

	// Щокадрово: рівень секцій — підсвітка + очищений фокус; усередині — тримаємо фокус на нашому елементі.
	protected void SM_NavTick()
	{
		if (!m_bSMNavActive)
			return;
		if (!m_MarkerEditRoot)	// діалог закрився — деактивуємо
		{
			m_bSMNavActive = false;
			m_bSMPlaceDown = false;
			SM_NavHideHL();
			return;
		}

		// Гравець перейшов на KB/M (пад від'єднали/перемкнувся після відкриття діалогу) — повністю
		// деактивуємо контролер. Інакше щокадрове пришпилення фокуса крало б фокус у поля вводу тексту
		// (текст переставав вводитися при русі миші). m_bSMNavActive=false робить інертними і NavTick,
		// і всі nav-слухачі (вони гейтнуті по ньому). Знову увімкнеться при наступному відкритті на паді.
		InputManager kbmCheck = GetGame().GetInputManager();
		if (kbmCheck && kbmCheck.IsUsingMouseAndKeyboard())
		{
			m_bSMNavActive = false;
			m_bSMNavTyping = false;
			m_bSMPlaceDown = false;
			SM_NavHideHL();
			return;
		}

		// Дропдаун Type-комбобокса закрився — відновлюємо контролер.
		if (m_SMNavOpenCombo && !m_SMNavOpenCombo.IsOpened())
		{
			m_SMNavOpenCombo = null;
			m_bSMNavTyping = false;
		}
		// Пауза: ввід тексту (клавіатура) або відкритий дропдаун комбобокса — не чіпаємо фокус/підсвітку.
		if (m_bSMNavTyping)
		{
			SM_NavHideHL();
			return;
		}

		// Place (Y / AM_Place) — опитування з детектом фронту. Працює на будь-якому рівні.
		InputManager pim = GetGame().GetInputManager();
		bool placeNow = pim && pim.GetActionValue("AM_Place") > 0.5;
		if (placeNow && !m_bSMPlaceDown)
		{
			m_bSMPlaceDown = placeNow;
			m_bSMPadPlaceDown = true;	// «з'їсти» цей Y для мапного поллера, інакше мітку одразу почне переносити
				OnInsertMarker(false);	// поставити мітку (діалог закриється)
			return;
		}
		m_bSMPlaceDown = placeNow;

		// Delete (X / AM_Delete) — видалити сфокусований пресет у секції пресетів (консольний аналог ПКМ).
		bool delNow = pim && pim.GetActionValue("AM_Delete") > 0.5;
		if (delNow && !m_bSMDeleteDown && m_iSMNavLevel == 1)
		{
			string delSec = SM_NavName();
			bool deleted = false;
			if (delSec == "MilitaryPresets" && m_iSMHoveredMilPreset >= 0)
			{
				SM_MapMarkerPresets.GetInstance().RemoveMilitary(m_iSMHoveredMilPreset);
				m_iSMHoveredMilPreset = -1;
				deleted = true;
			}
			else if (delSec == "UserPresets" && m_iSMHoveredGenPreset >= 0)
			{
				SM_MapMarkerPresets.GetInstance().RemoveGeneral(m_iSMHoveredGenPreset);
				m_iSMHoveredGenPreset = -1;
				deleted = true;
			}
			if (deleted)
			{
				SM_WirePresets();	// перебудувати ряд (знищить сфокусовану кнопку)
				// анти-пресет: щоб engine-confirm/фокус не зачепив інший пресет — паркуємо колір, вихід на рівень секцій
				SM_NavParkColor();
				m_iSMNavLevel = 0;
				m_bSMNavGrid = false;
				m_aSMNavItems.Clear();
				m_bSMDeleteDown = delNow;
				return;
			}
		}
		m_bSMDeleteDown = delNow;

		if (m_iSMNavLevel == 0)
		{
			if (m_bSMNavOnCheck)
			{
				// Фокус «паркуємо» на ПОТОЧНОМУ КОЛЬОРІ (не null), інакше A активує перший фокусований
				// віджет (перший пресет). Підсвічуємо лише галочку нашим оверлеєм.
				SM_NavParkColor();
				SM_NavPositionCheckHL();	// позиція TextColorCheck + фіксований розмір квадрата
			}
			else
			{
				// Секції-значення (повзунки/timestamp) керуються Вліво/Вправо; A на них — no-op, тож паркуємо
				// фокус на кольорі (анти-пресет на випадок випадкового A). Решта секцій — фокус null (A заходить).
				if (SM_NavIsValueSection() || SM_NavIsActionSection())
					SM_NavParkColor();
				else
					GetGame().GetWorkspace().SetFocusedWidget(null);
				SM_NavPositionHL(SM_NavHighlightTarget(m_iSMNavSection));	// Color — лише рядок кольорів
			}
			return;
		}

		SM_NavHideHL();
		if (m_bSMNavGrid)
			SM_NavFocusGrid();	// сітка іконок
		else
			SM_NavFocusItem();	// горизонтальна секція
	}

	protected void SM_NavSetSection(int idx)
	{
		int n = m_aSMNavContainers.Count();
		if (n == 0)
			return;
		m_iSMNavSection = (idx % n + n) % n;
		SM_NavPositionHL(SM_NavContainer(m_iSMNavSection));
	}

	// Вгору/вниз: на рівні секцій — перемикання секції (скидає бічну галочку); усередині сітки — рядок.
	protected void SM_NavUp(float value, EActionTrigger reason)
	{
		if (!m_bSMNavActive || !m_MarkerEditRoot || m_bSMNavTyping || SM_NavOnKBM())
			return;
		if (m_iSMNavLevel == 0)
		{
			m_bSMNavOnCheck = false;
			SM_NavSetSection(m_iSMNavSection - 1);
		}
		else if (m_bSMNavGrid)
		{
			m_iSMNavRow--;
			SM_NavFocusGrid();
		}
	}

	protected void SM_NavDown(float value, EActionTrigger reason)
	{
		if (!m_bSMNavActive || !m_MarkerEditRoot || m_bSMNavTyping || SM_NavOnKBM())
			return;
		if (m_iSMNavLevel == 0)
		{
			m_bSMNavOnCheck = false;
			SM_NavSetSection(m_iSMNavSection + 1);
		}
		else if (m_bSMNavGrid)
		{
			m_iSMNavRow++;
			SM_NavFocusGrid();
		}
	}

	// Вліво/вправо. Рівень секцій: Right на секції Color → бічна галочка; Left з галочки → назад на Color.
	// Усередині секції: сітка — по колонках; горизонтальна — по індексу (Color обернений).
	protected void SM_NavLeft(float value, EActionTrigger reason)
	{
		if (!m_bSMNavActive || !m_MarkerEditRoot || m_bSMNavTyping || SM_NavOnKBM())
			return;
		if (m_iSMNavLevel == 0)
		{
			if (m_bSMNavOnCheck)
			{
				m_bSMNavOnCheck = false;	// з галочки назад на секцію Color
				return;
			}
			// секції-значення: Вліво зменшує
			string c = SM_NavName();
			if (c == "SliderRoot")
				SM_NavSliderAdjust(-SM_NAV_ROT_STEP);
			else if (c == "SliderSize")
				SM_NavSliderAdjust(-SM_NAV_SIZE_STEP);
			else if (c == "TimestampSpinBox")
				SM_NavSetTimestamp(false);
			return;
		}
		if (m_bSMNavGrid)
		{
			m_iSMNavCol--;
			SM_NavFocusGrid();
			return;
		}
		if (m_aSMNavItems.IsEmpty())
			return;
		if (SM_NavReversed())
			m_iSMNavItem++;
		else
			m_iSMNavItem--;
		SM_NavFocusItem();
	}

	protected void SM_NavRight(float value, EActionTrigger reason)
	{
		if (!m_bSMNavActive || !m_MarkerEditRoot || m_bSMNavTyping || SM_NavOnKBM())
			return;
		if (m_iSMNavLevel == 0)
		{
			string c = SM_NavName();
			// на секції Color → перейти на бічну галочку (праворуч), якщо вона є
			if (!m_bSMNavOnCheck && c == "ColorSelector" && m_MarkerEditRoot.FindAnyWidget("TextColorCheck"))
			{
				m_bSMNavOnCheck = true;
				return;
			}
			// секції-значення: Вправо збільшує
			if (c == "SliderRoot")
				SM_NavSliderAdjust(SM_NAV_ROT_STEP);
			else if (c == "SliderSize")
				SM_NavSliderAdjust(SM_NAV_SIZE_STEP);
			else if (c == "TimestampSpinBox")
				SM_NavSetTimestamp(true);
			return;
		}
		if (m_bSMNavGrid)
		{
			m_iSMNavCol++;
			SM_NavFocusGrid();
			return;
		}
		if (m_aSMNavItems.IsEmpty())
			return;
		if (SM_NavReversed())
			m_iSMNavItem--;
		else
			m_iSMNavItem++;
		SM_NavFocusItem();
	}

	// A: рівень секцій — галочку перемкнути НА МІСЦІ, інакше зайти в секцію (сітка/горизонтальна);
	// усередині секції — підтвердити сфокусований елемент (клік) і вийти на рівень секцій.
	protected void SM_NavSelect(float value, EActionTrigger reason)
	{
		if (!m_bSMNavActive || !m_MarkerEditRoot || m_bSMNavTyping || SM_NavOnKBM())
			return;
		if (m_iSMNavLevel == 0)
		{
			if (m_bSMNavOnCheck)	// на бічній галочці Text color — перемкнути й лишитись
			{
				CheckBoxWidget cb = CheckBoxWidget.Cast(m_MarkerEditRoot.FindAnyWidget("TextColorCheck"));
				if (cb)
					cb.SetChecked(!cb.IsChecked());
				return;
			}
			if (SM_NavIsValueSection())	// повзунки/timestamp — керуються Вліво/Вправо, A нічого не робить
				return;
			if (SM_NavName() == "EditBoxRoot")	// текст — A відкриває екранну клавіатуру (контролер на паузу)
			{
				if (m_EditBoxComp)
				{
					m_bSMNavTyping = true;
					m_EditBoxComp.ActivateWriteMode(true);
				}
				return;
			}
			if (SM_NavName() == "ComboBox1" || SM_NavName() == "ComboBox2")	// Type — A відкриває дропдаун (як ваніла)
			{
				SCR_ComboBoxComponent combo = m_SMComboA;
				if (SM_NavName() == "ComboBox2")
					combo = m_SMComboB;
				if (combo)
				{
					m_SMNavOpenCombo = combo;
					m_bSMNavTyping = true;	// пауза контролера, поки відкритий список (рушій навігує його сам)
					combo.OpenList();
				}
				return;
			}
			m_iSMNavLevel = 1;
			SM_NavHideHL();
			if (m_aSMNavContainers[m_iSMNavSection] == "IconSelector")
			{
				m_bSMNavGrid = true;
				SM_NavBuildGrid();
				SM_NavFocusGrid();		// фокус на першу іконку
			}
			else
			{
				m_bSMNavGrid = false;
				SM_NavCollectItems();
				m_iSMNavItem = 0;
				// Стати на ПОТОЧНИЙ обраний елемент.
				if (m_aSMNavContainers[m_iSMNavSection] == "Visibility")
				{
					m_iSMNavItem = m_iSMSelectedVis;	// індекси кнопок = SM_EMarkerVisibility
					if (m_iSMNavItem < 0 || m_iSMNavItem >= m_aSMNavItems.Count())
						m_iSMNavItem = 0;
				}
				else if (m_SelectedColorButton)		// Color — поточний колір мітки
				{
					int sel = m_aSMNavItems.Find(m_SelectedColorButton.GetRootWidget());
					if (sel >= 0)
						m_iSMNavItem = sel;
				}
				SM_NavFocusItem();
			}
		}
		else
		{
			// Поточний елемент (сітка або горизонтальний список).
			Widget cur;
			if (m_bSMNavGrid)
			{
				if (m_iSMNavRow >= 0 && m_iSMNavRow < m_aSMNavGrid.Count())
				{
					array<Widget> row = m_aSMNavGrid[m_iSMNavRow];
					if (m_iSMNavCol >= 0 && m_iSMNavCol < row.Count())
						cur = row[m_iSMNavCol];
				}
			}
			else if (m_iSMNavItem >= 0 && m_iSMNavItem < m_aSMNavItems.Count())
			{
				cur = m_aSMNavItems[m_iSMNavItem];
			}

			// Галочка — перемкнути й ЛИШИТИСЬ у секції (зручно вмикати/вимикати).
			CheckBoxWidget cb = CheckBoxWidget.Cast(cur);
			if (cb)
			{
				cb.SetChecked(!cb.IsChecked());
				return;
			}
			// Інакше — клік (вибір) і вихід на рівень секцій.
			if (cur)
			{
				// «+» (додати пресет) перебудовує список і знищує цю сфокусовану кнопку — після чого рушій
				// сам активував би перший пресет. Тому для «+» одразу переводимо фокус на інертний поточний
				// колір (повторний клік = той самий колір, без змін), щоб engine-confirm не зачепив пресет.
				bool isAddBtn = (m_SMGenAddBtn && cur == m_SMGenAddBtn.GetRootWidget())
					|| (m_SMMilAddBtn && cur == m_SMMilAddBtn.GetRootWidget());

				SCR_ButtonBaseComponent comp = SCR_ButtonBaseComponent.Cast(cur.FindHandler(SCR_ButtonBaseComponent));
				if (comp)
					comp.m_OnClicked.Invoke(comp);

				if (isAddBtn)
					SM_NavParkColor();
			}
			m_iSMNavLevel = 0;
			m_bSMNavGrid = false;
			m_aSMNavItems.Clear();
			m_aSMNavGrid.Clear();
		}
	}

	// B: усередині — назад на рівень секцій; на рівні секцій — скасувати діалог (без розміщення).
	protected void SM_NavBack(float value, EActionTrigger reason)
	{
		if (SM_TryPanelBack())	// панель малювання: дворівневий вихід, не закриваючи мапу
			return;

		if (!m_bSMNavActive || !m_MarkerEditRoot || m_bSMNavTyping || SM_NavOnKBM())
			return;
		if (m_iSMNavLevel == 0 && m_bSMNavOnCheck)	// з галочки — назад на секцію Color (не скасовуємо)
		{
			m_bSMNavOnCheck = false;
			return;
		}
		if (m_iSMNavLevel == 1)
		{
			m_iSMNavLevel = 0;
			m_bSMNavGrid = false;
			m_aSMNavItems.Clear();
			m_aSMNavGrid.Clear();
			// підсвітку поверне SM_NavTick
		}
		else
		{
			m_iSMEditId = -1;
			SM_NavDestroyHL();
			CleanupMarkerEditWidget();
		}
	}

	// LB/RB перемкнули вкладку іконок (вкладку міняє ваніла) — старі IconEntry зникають, нові з'являються.
	// Затримка перед перебудовою, щоб ваніла встигла наповнити вкладку.
	protected void SM_NavTab(float value, EActionTrigger reason)
	{
		if (!m_bSMNavActive || !m_MarkerEditRoot || m_bSMNavTyping || SM_NavOnKBM())
			return;
		GetGame().GetCallqueue().Remove(SM_NavRegrid);
		GetGame().GetCallqueue().CallLater(SM_NavRegrid, 60, false);
	}

	protected void SM_NavRegrid()
	{
		if (!m_bSMNavActive || !m_MarkerEditRoot)
			return;
		if (m_iSMNavLevel == 1 && m_bSMNavGrid)	// у сітці — оновлюємо її, лишаємось усередині
		{
			SM_NavBuildGrid();
			SM_NavFocusGrid();
			return;
		}
		// Поза сіткою — підсвічуємо секцію іконок (рівень 0), не вибираючи сам маркер.
		int iconIdx = m_aSMNavContainers.Find("IconSelector");
		if (iconIdx < 0)
			return;
		m_iSMNavLevel = 0;
		m_bSMNavGrid = false;
		m_bSMNavOnCheck = false;
		m_iSMNavSection = iconIdx;
		GetGame().GetWorkspace().SetFocusedWidget(null);
		SM_NavPositionHL(SM_NavHighlightTarget(iconIdx));
	}


	// --- Придушення ванільних обробників вводу, поки активний наш секційний контролер ---
	// Інакше MenuSelect (A) ставить мітку (OnPlaceMarkerConfirmed), MenuBack (B) закриває діалог,
	// а ванільна меню-навігація конфліктує з нашою. Поза контролером (KB/M) — ванільна поведінка.
	override protected void OnInputMenuConfirm(float value, EActionTrigger reason)
	{
		if (m_bSMNavActive)
			return;
		super.OnInputMenuConfirm(value, reason);
	}

	override protected void OnInputMenuConfirmAlter(float value, EActionTrigger reason)
	{
		if (m_bSMNavActive)
			return;
		super.OnInputMenuConfirmAlter(value, reason);
	}

	override protected void OnInputMenuBack(float value, EActionTrigger reason)
	{
		if (m_bSMNavActive)
			return;
		super.OnInputMenuBack(value, reason);
	}

	override protected void OnInputMenuDown(float value, EActionTrigger reason)
	{
		if (m_bSMNavActive)
			return;
		super.OnInputMenuDown(value, reason);
	}

	override protected void OnInputMenuUp(float value, EActionTrigger reason)
	{
		if (m_bSMNavActive)
			return;
		super.OnInputMenuUp(value, reason);
	}

	override protected void OnInputMenuLeft(float value, EActionTrigger reason)
	{
		if (m_bSMNavActive)
			return;
		super.OnInputMenuLeft(value, reason);
	}

	override protected void OnInputMenuRight(float value, EActionTrigger reason)
	{
		if (m_bSMNavActive)
			return;
		super.OnInputMenuRight(value, reason);
	}

	override protected void OnInputMapSelect(float value, EActionTrigger reason)
	{
		if (m_bSMNavActive)
			return;
		super.OnInputMapSelect(value, reason);
	}
	// OnInputQuickMarkerMenu уже повний no-op вище (рядок ~231) — ванільне розміщення глушиться завжди.

	// LB/RB перемикає сторінку іконок через ванільний SCR_TabViewComponent. Ваніль на зміні авто-обирає
	// першу іконку (OnIconEntryClicked) → мітка стає цивільною. Дозволяємо перемикати сторінку будь-коли,
	// але глушимо цей авто-вибір, щоб випадковий LB/RB не збив налаштування військової мітки.
	override protected void OnTabChanged(SCR_TabViewComponent tabView, Widget widget, int index)
	{
		if (m_bSMNavActive)
			m_bSMSuppressIconSelect = true;
		super.OnTabChanged(tabView, widget, index);
		m_bSMSuppressIconSelect = false;
		m_iSMTabIndex = index;
	}

	// Центрує мапу так, щоб мітка опинилась у вільній зоні (не під діалогом).
	protected void SM_CenterOnPlacement(int worldX, int worldY)
	{
		CanvasWidget mapW = m_MapEntity.GetMapWidget();
		if (!mapW)
			return;

		float sw, sh;
		mapW.GetScreenSize(sw, sh);		// scaled px

		// ВАЖЛИВО: PanSmooth очікує координати БЕЗ панорами (withPan=false), як ванільне центрування.
		int msx, msy;
		m_MapEntity.WorldToScreen(worldX, worldY, msx, msy);

		// PanSmooth(px,py): світова точка з НЕ-панорамованою екранною позицією (px,py) переїде в центр.
		// Щоб мітка опинилась у (SM_FREE_X, SM_FREE_Y): target = rawPos + центр − вільна_точка.
		float panX = msx + sw * 0.5 - sw * SM_FREE_X;
		float panY = msy + sh * 0.5 - sh * SM_FREE_Y;
		m_MapEntity.PanSmooth(panX, panY, 0.25);
	}

	// Прикріплює видиму панель діалогу (SizeFrame) до ПРАВОГО краю екрана.
	// Корінь MarkerEditRoot має Anchor 0 0 1 1 (розтягнутий прозорий контейнер на весь
	// map-frame), тож ванільний SetPos кореня нічого не зсуває — рухаємо саме контент-фрейм.
	protected void SM_PositionDialog()
	{
		if (!m_MarkerEditRoot)
			return;

		Widget panel = m_MarkerEditRoot.FindAnyWidget("SizeFrame");
		if (!panel)
			return;

		// Точковий якір у правому-верхньому куті контейнера + вирівнювання box'а правим краєм
		// до якоря → панель «прилипає» до правого краю, тримаючи власну (SizeToContent) ширину.
		FrameSlot.SetAnchorMin(panel, 1, 0);
		FrameSlot.SetAnchorMax(panel, 1, 0);
		FrameSlot.SetAlignment(panel, 1, 0);
		FrameSlot.SetPos(panel, 0, 0);
	}

	// Піднімає ліміт довжини тексту мітки (ванільний дефолт у layout = 16 байт).
	protected void SM_OverrideCharLimit()
	{
		if (!m_EditBoxComp || !m_EditBoxComp.m_wEditBox)
			return;

		EditBoxFilterComponent filter = EditBoxFilterComponent.Cast(m_EditBoxComp.m_wEditBox.FindHandler(EditBoxFilterComponent));
		if (filter)
			filter.SM_SetCharacterLimit(SM_TEXT_CHAR_LIMIT);
	}

	// Підключає контроли з НАШОГО layout діалогу: повзунок "Size" + 4 кнопки видимості.
	// Викликати ПІСЛЯ CreateMarkerEditDialog (віджети вже існують у m_MarkerEditRoot).
	protected void SM_WireControls()
	{
		if (!m_MarkerEditRoot)
			return;

		// KB/M: поки поле назви в режимі вводу — тримаємо на ньому фокус (інакше ховер сусідньої секції
		// перехоплює клавіатуру). Знімається лише на Enter (Confirm) чи Esc (Cancel). На паді не діє.
		m_bSMTextLock = false;
		if (m_EditBoxComp)
		{
			m_EditBoxComp.m_OnWriteModeEnter.Insert(SM_OnEditWriteEnter);
			m_EditBoxComp.m_OnConfirm.Insert(SM_OnEditConfirmLock);
			m_EditBoxComp.m_OnCancel.Insert(SM_OnEditCancelLock);
		}

		// --- РОЗМІР: повзунок "Size" (відсоток 25..1000, крок 10 — з layout) ---
		m_SMSizeSlider = null;
		Widget sizeW = m_MarkerEditRoot.FindAnyWidget("SliderSize");
		if (sizeW)
			m_SMSizeSlider = SCR_SliderComponent.Cast(sizeW.FindHandler(SCR_SliderComponent));
		if (m_SMSizeSlider)
		{
			m_SMSizeSlider.SetValue(m_iSMSelectedSize);	// відобразити поточний (100 або з мітки)
			m_SMSizeSlider.m_OnChanged.Insert(SM_OnSizeSliderChanged);
		}

		// --- ГАЛОЧКА "Text" (підпис у колір мітки) — рушійний CheckBoxWidget "TextColorCheck" ---
		m_wSMTextCheck = CheckBoxWidget.Cast(m_MarkerEditRoot.FindAnyWidget("TextColorCheck"));
		if (m_wSMTextCheck)
			m_wSMTextCheck.SetChecked(m_bSMTextColored);	// клік перемикає сам; стан читаємо в SM_IsTextColored

		// --- TIMESTAMP (дата) — SpinBox Yes/No (0=No, 1=Yes) ---
		Widget tsw = m_MarkerEditRoot.FindAnyWidget("TimestampSpinBox");
		if (tsw)
		{
			SCR_SpinBoxComponent sb = SCR_SpinBoxComponent.Cast(tsw.FindHandler(SCR_SpinBoxComponent));
			if (sb)
			{
				int tsIdx = 0;
				if (m_bSMTimestamp)
					tsIdx = 1;
				sb.SetCurrentItem(tsIdx, false, false);	// до Insert — щоб ініціалізація не клацнула обробник
				sb.m_OnChanged.Insert(SM_OnTimestampChanged);
			}
		}

		// --- ВИДИМІСТЬ: 4 кнопки (індекс у масиві = значення SM_EMarkerVisibility) ---
		// Підписи/кольори беремо з layout (Local/Group/Side/Everyone) — НЕ перезатираємо.
		m_aSMVisWidgets.Clear();

		Widget wPersonal, wGroup, wFaction, wAll;
		SCR_InputButtonComponent cPersonal = SM_GetVisButton("ButtonLocal", wPersonal);
		SCR_InputButtonComponent cGroup    = SM_GetVisButton("ButtonGroup", wGroup);
		SCR_InputButtonComponent cFaction  = SM_GetVisButton("ButtonSide",  wFaction);
		SCR_InputButtonComponent cAll      = SM_GetVisButton("ButtonAll",   wAll);

		if (cPersonal) cPersonal.m_OnClicked.Insert(SM_OnVisPersonal);
		if (cGroup)    cGroup.m_OnClicked.Insert(SM_OnVisGroup);
		if (cFaction)  cFaction.m_OnClicked.Insert(SM_OnVisFaction);
		if (cAll)      cAll.m_OnClicked.Insert(SM_OnVisAll);

		// Не сіріти під час набору тексту (видимість + Place/Cancel/Private).
		if (cPersonal) cPersonal.m_bSMNoInteractionDisable = true;
		if (cGroup)    cGroup.m_bSMNoInteractionDisable = true;
		if (cFaction)  cFaction.m_bSMNoInteractionDisable = true;
		if (cAll)      cAll.m_bSMNoInteractionDisable = true;
		SM_KeepButtonActive("ButtonPublic");
		SM_KeepButtonActive("ButtonPrivate");
		SM_KeepButtonActive("ButtonCancel");

		m_aSMVisWidgets.Insert(wPersonal);	// 0 = PERSONAL
		m_aSMVisWidgets.Insert(wGroup);		// 1 = GROUP
		m_aSMVisWidgets.Insert(wFaction);	// 2 = FACTION
		m_aSMVisWidgets.Insert(wAll);		// 3 = ALL

		// Блокуємо кнопки: звуження видимості (widen-only) АБО заборонений канал (конфіг сервера).
		if (cPersonal) cPersonal.SetEnabled(SM_IsVisSelectable(SM_EMarkerVisibility.PERSONAL));
		if (cGroup)    cGroup.SetEnabled(SM_IsVisSelectable(SM_EMarkerVisibility.GROUP));
		if (cFaction)  cFaction.SetEnabled(SM_IsVisSelectable(SM_EMarkerVisibility.FACTION));
		if (cAll)      cAll.SetEnabled(SM_IsVisSelectable(SM_EMarkerVisibility.ALL));

		SM_UpdateVisHighlight();
		SM_WireGmControls();	// GM-only: дропдаун фракції + галочка Locked
	}

	// Підключає GM-контроли в діалозі (лише в мапі Game Master). Віджети додано в layout
	// фракцію зевс обирає циклічно кнопкою «Side»; галочка Locked = віджет GmLockCheck (для гравців схований).
	protected void SM_WireGmControls()
	{
		if (!m_MarkerEditRoot)
			return;
		bool isGm = SM_IsEditorMap();

		// Вибір фракції для зевса: кнопка «Side» показує назву фракції й циклічно перемикає її
		// (BLUFOR/OPFOR/INDFOR). Жодних нових віджетів — кнопка лишається в стилі решти.
		if (isGm)
		{
			SM_BuildGmFactionList();
			if (m_iSMGmFactionChosen < 0 && !m_aSMGmFactionIdx.IsEmpty())
				m_iSMGmFactionChosen = m_aSMGmFactionIdx[0];	// дефолт — перша фракція
			SM_UpdateGmFactionLabel();
		}

		// Галочка Locked (показуємо лише зевсу).
		m_wSMGmLockCheck = CheckBoxWidget.Cast(m_MarkerEditRoot.FindAnyWidget("GmLockCheck"));
		SM_SetGmRowVisible("GmLockRow", m_wSMGmLockCheck, isGm);
		if (m_wSMGmLockCheck)
			m_wSMGmLockCheck.SetChecked(m_bSMGmLocked);

		// Галочка Hide info (показуємо лише зевсу): ховає тултіп «Edited by»/«Side» для гравців.
		m_wSMHideInfoCheck = CheckBoxWidget.Cast(m_MarkerEditRoot.FindAnyWidget("HideInfoCheck"));
		SM_SetGmRowVisible("HideInfoRow", m_wSMHideInfoCheck, isGm);
		if (m_wSMHideInfoCheck)
			m_wSMHideInfoCheck.SetChecked(m_bSMHideInfo);
	}

	// Показати/сховати GM-контрол разом із його підписом. Підпис — окремий віджет, тож ховаємо
	// КОНТЕЙНЕР: спершу іменований рядок (rowName), інакше батьківський віджет чекбокса, інакше сам чекбокс.
	protected void SM_SetGmRowVisible(string rowName, CheckBoxWidget check, bool visible)
	{
		Widget w = null;
		if (rowName != "")
			w = m_MarkerEditRoot.FindAnyWidget(rowName);
		if (!w && check)
			w = check.GetParent();
		if (!w)
			w = check;
		if (w)
			w.SetVisible(visible);
	}

	// Список ігрових індексів фракцій сценарію (без цивільних) — для циклічного перемикання.
	protected void SM_BuildGmFactionList()
	{
		m_aSMGmFactionIdx.Clear();
		SCR_FactionManager fm = SCR_FactionManager.Cast(GetGame().GetFactionManager());
		if (!fm)
			return;

		array<Faction> all = {};
		fm.GetFactionsList(all);
		foreach (Faction f : all)
		{
			if (!f)
				continue;
			string key = f.GetFactionKey();
			if (key == "" || key == "CIV")	// цивільних пропускаємо — у них нема сторони
				continue;
			m_aSMGmFactionIdx.Insert(fm.GetFactionIndex(f));
		}
	}

	// Наступна фракція у списку (циклічно).
	protected void SM_CycleGmFaction()
	{
		if (m_aSMGmFactionIdx.IsEmpty())
			return;
		int pos = m_aSMGmFactionIdx.Find(m_iSMGmFactionChosen) + 1;
		if (pos < 0 || pos >= m_aSMGmFactionIdx.Count())
			pos = 0;
		m_iSMGmFactionChosen = m_aSMGmFactionIdx[pos];
	}

	// Підпис кнопки «Side» = назва обраної фракції (для зевса).
	protected void SM_UpdateGmFactionLabel()
	{
		if (!m_MarkerEditRoot)
			return;
		Widget sideBtn = m_MarkerEditRoot.FindAnyWidget("ButtonSide");
		if (!sideBtn)
			return;
		SCR_InputButtonComponent c = SCR_InputButtonComponent.Cast(sideBtn.FindHandler(SCR_InputButtonComponent));
		if (!c)
			return;
		string lbl = SM_FactionSideName(m_iSMGmFactionChosen);
		if (lbl == "")
			lbl = "Side";
		c.SetLabel(lbl);
		c.SetLabelColor(Color.FromInt(SM_FactionSideColor(m_iSMGmFactionChosen)));	// колір за стороною
	}

	// Чи можна вибрати цю видимість: не вужче за поточну (widen-only) І дозволено конфігом сервера.
	protected bool SM_IsVisSelectable(int vis)
	{
		if (vis < m_iSMMinVis)
			return false;
		return SM_MarkerConfig.GetInstance().IsVisibilityAllowed(vis);
	}

	// Бажана видимість, якщо дозволена конфігом; інакше перший дозволений канал.
	protected int SM_FirstAllowedVis(int preferred)
	{
		SM_MarkerConfig cfg = SM_MarkerConfig.GetInstance();
		if (cfg.IsVisibilityAllowed(preferred))
			return preferred;
		for (int v = SM_EMarkerVisibility.PERSONAL; v <= SM_EMarkerVisibility.ALL; v++)
		{
			if (cfg.IsVisibilityAllowed(v))
				return v;
		}
		return preferred;	// усе заборонено (малоймовірно) — лишаємо як є
	}

	// Знімає з кнопки ванільне «сіріння під час вводу тексту» (наш прапорець у modded-класі).
	protected void SM_KeepButtonActive(string widgetName)
	{
		Widget w = m_MarkerEditRoot.FindAnyWidget(widgetName);
		if (!w)
			return;
		SCR_InputButtonComponent comp = SCR_InputButtonComponent.Cast(w.FindHandler(SCR_InputButtonComponent));
		if (comp)
			comp.m_bSMNoInteractionDisable = true;
	}

	// Знаходить кнопку видимості за іменем; повертає її компонент і (out) кореневий віджет.
	protected SCR_InputButtonComponent SM_GetVisButton(string widgetName, out Widget outWidget)
	{
		outWidget = m_MarkerEditRoot.FindAnyWidget(widgetName);
		if (!outWidget)
			return null;
		return SCR_InputButtonComponent.Cast(outWidget.FindHandler(SCR_InputButtonComponent));
	}

	// Обробники кліку по кнопках видимості (m_OnClicked → SCR_InputButtonComponent button).
	protected void SM_OnVisPersonal(SCR_InputButtonComponent button) { SM_SetVisibility(SM_EMarkerVisibility.PERSONAL); }
	protected void SM_OnVisGroup(SCR_InputButtonComponent button)    { SM_SetVisibility(SM_EMarkerVisibility.GROUP); }
	protected void SM_OnVisFaction(SCR_InputButtonComponent button)
	{
		// Зевс: клік по «Side» обирає Side; повторний клік циклічно перемикає фракцію (BLUFOR/OPFOR/INDFOR).
		if (SM_IsEditorMap() && !m_aSMGmFactionIdx.IsEmpty())
		{
			if (m_iSMSelectedVis == SM_EMarkerVisibility.FACTION)
				SM_CycleGmFaction();
			else
				SM_SetVisibility(SM_EMarkerVisibility.FACTION);
			SM_UpdateGmFactionLabel();
			return;
		}
		SM_SetVisibility(SM_EMarkerVisibility.FACTION);
	}
	protected void SM_OnVisAll(SCR_InputButtonComponent button)      { SM_SetVisibility(SM_EMarkerVisibility.ALL); }

	protected void SM_SetVisibility(int vis)
	{
		if (!SM_IsVisSelectable(vis))	// звуження або заборонений канал — ігноруємо (страховка попри блокування)
			return;
		m_iSMSelectedVis = vis;
		SM_UpdateVisHighlight();
	}

	// Вибрана кнопка видимості — яскрава (opacity 1.0), решта приглушені (їхній колір лишається).
	protected void SM_UpdateVisHighlight()
	{
		foreach (int i, Widget w : m_aSMVisWidgets)
		{
			if (!w)
				continue;
			if (!SM_IsVisSelectable(i))
				w.SetOpacity(SM_VIS_OPACITY_LOCKED);	// заблокована (звуження або заборонений канал)
			else if (i == m_iSMSelectedVis)
				w.SetOpacity(SM_VIS_OPACITY_SEL);
			else
				w.SetOpacity(SM_VIS_OPACITY_UNSEL);
		}
	}

	// Зміна повзунка "Size" → відсоток розміру; миттєво оновлює живе прев'ю.
	protected void SM_OnSizeSliderChanged(SCR_SliderComponent slider, float value)
	{
		m_iSMSelectedSize = Math.Round(value);
		SM_UpdatePreviewData();
	}

	// ВІЙСЬКОВА СЕКЦІЯ (stage 2) — заповнення Faction/Dimension/Type 1-2 + перемикання civ↔mil.
	// Не використовуємо ванільні Init*-методи: вони лізуть у відсутнє в нас прев'ю (SymbolOverlay).
	// Лінива ініціалізація конфіга військових міток (PLACED_MILITARY).
	protected SCR_MapMarkerEntryMilitary SM_MilConfig()
	{
		if (!m_MilitaryMarkerConfig)
		{
			SCR_MapMarkerManagerComponent mgr = SCR_MapMarkerManagerComponent.GetInstance();
			if (!mgr)
				return null;
			SCR_MapMarkerConfig cfg = mgr.GetMarkerConfig();
			if (!cfg)
				return null;
			m_MilitaryMarkerConfig = SCR_MapMarkerEntryMilitary.Cast(cfg.GetMarkerEntryConfigByType(SCR_EMapMarkerType.PLACED_MILITARY));
		}
		return m_MilitaryMarkerConfig;
	}

	// Підключає військову секцію діалогу (виклик ПІСЛЯ CreateMarkerEditDialog).
	protected void SM_WireMilitary()
	{
		SCR_MapMarkerEntryMilitary cfg = SM_MilConfig();
		if (!cfg || !m_MarkerEditRoot)
			return;

		// ComboBox1/2 — типи символа (A/B). Спершу наповнюємо, тоді SetCurrentItem, ПОТІМ вішаємо
		// обробник — щоб ініціалізація не «клацнула» kind=MILITARY при відкритті.
		m_SMComboA = null;
		m_SMComboB = null;
		Widget cb1w = m_MarkerEditRoot.FindAnyWidget("ComboBox1");
		Widget cb2w = m_MarkerEditRoot.FindAnyWidget("ComboBox2");
		if (cb1w)
			m_SMComboA = SCR_ComboBoxComponent.Cast(cb1w.FindHandler(SCR_ComboBoxComponent));
		if (cb2w)
			m_SMComboB = SCR_ComboBoxComponent.Cast(cb2w.FindHandler(SCR_ComboBoxComponent));

		array<ref SCR_MarkerMilitaryType> types = cfg.GetMilitaryTypes();
		if (m_SMComboA)
			m_SMComboA.AddItem("");
		if (m_SMComboB)
			m_SMComboB.AddItem("");
		foreach (SCR_MarkerMilitaryType t : types)
		{
			if (m_SMComboA)
				m_SMComboA.AddItem(t.GetTranslation(), false, t);
			if (m_SMComboB)
				m_SMComboB.AddItem(t.GetTranslation(), false, t);
		}
		if (m_SMComboA)
		{
			m_SMComboA.SetCurrentItem(0);
			m_SMComboA.m_OnChanged.Insert(SM_OnComboA);
		}
		if (m_SMComboB)
		{
			m_SMComboB.SetCurrentItem(0);
			m_SMComboB.m_OnChanged.Insert(SM_OnComboB);
		}

		SM_BuildFactionButtons(cfg);
	}

	protected void SM_BuildFactionButtons(SCR_MapMarkerEntryMilitary cfg)
	{
		Widget fs = m_MarkerEditRoot.FindAnyWidget("FactionSelector");
		if (!fs)
			return;
		Widget line = fs.FindAnyWidget("FactionSelectorLine");
		if (!line)
			return;

		SM_ClearChildren(line);
		m_aSMFactionBtns.Clear();

		// Власний список: кнопка = міні-символ (форма рамки за identity, у кольорі фракції).
		// Використовуємо dimension-entry layout (має OverlaySymbol + SCR_ButtonImageComponent).
		foreach (int i, int identity : m_aSMFactIdentity)
		{
			Widget b = GetGame().GetWorkspace().CreateWidgets(m_sSelectorDimensionEntry, line);
			SCR_ButtonImageComponent bc = null;
			if (b)
			{
				Widget overlay = b.FindAnyWidget("OverlaySymbol");
				if (overlay)
				{
					overlay.SetColor(Color.FromInt(m_aSMFactColor[i]));
					SCR_MilitarySymbolUIComponent sc = SCR_MilitarySymbolUIComponent.Cast(overlay.FindHandler(SCR_MilitarySymbolUIComponent));
					if (sc)
					{
						SCR_MilitarySymbol s = new SCR_MilitarySymbol();
						s.SetIdentity(identity);
						s.SetDimension(EMilitarySymbolDimension.LAND);
						sc.Update(s);
					}
				}
				bc = SCR_ButtonImageComponent.Cast(b.FindHandler(SCR_ButtonImageComponent));
				if (bc)
					bc.m_OnClicked.Insert(SM_OnFactionClicked);
			}
			m_aSMFactionBtns.Insert(bc);	// індекс = індекс фракції (навіть якщо null)
		}

		// Дефолт — перша фракція (лише стан, без переходу в MILITARY).
		if (!m_aSMFactionBtns.IsEmpty())
			SM_SelectFaction(0, false);
	}

	protected void SM_BuildDimensionButtons(SCR_MapMarkerEntryMilitary cfg)
	{
		Widget ds = m_MarkerEditRoot.FindAnyWidget("DimensionSelector");
		if (!ds)
			return;
		Widget line = ds.FindAnyWidget("DimensionSelectorLine");
		if (!line)
			return;

		SM_ClearChildren(line);
		m_aSMDimensionBtns.Clear();

		// Форма/колір рамки беремо з ПОТОЧНОЇ вибраної фракції (наш список), не з ванільного конфіга.
		array<ref SCR_MarkerMilitaryDimension> dims = cfg.GetMilitaryDimensions();
		foreach (int i, SCR_MarkerMilitaryDimension dim : dims)
		{
			Widget b = GetGame().GetWorkspace().CreateWidgets(m_sSelectorDimensionEntry, line);
			SCR_ButtonImageComponent bc = null;
			if (b)
			{
				Widget overlay = b.FindAnyWidget("OverlaySymbol");
				if (overlay)
				{
					overlay.SetColor(Color.FromInt(m_iSMMilColor));
					SCR_MilitarySymbolUIComponent sc = SCR_MilitarySymbolUIComponent.Cast(overlay.FindHandler(SCR_MilitarySymbolUIComponent));
					if (sc)
					{
						SCR_MilitarySymbol s = new SCR_MilitarySymbol();
						s.SetIdentity(m_iSMSelIdentity);
						s.SetDimension(dim.GetDimension());
						sc.Update(s);
					}
				}
				bc = SCR_ButtonImageComponent.Cast(b.FindHandler(SCR_ButtonImageComponent));
				if (bc)
					bc.m_OnClicked.Insert(SM_OnDimensionClicked);
			}
			m_aSMDimensionBtns.Insert(bc);
		}

		// Дефолт — перший вимір.
		if (!m_aSMDimensionBtns.IsEmpty())
			SM_SelectDimension(0, false);
	}

	// Підпис фракції: ванільний (BLUFOR/OPFOR/INDFOR) чи наш (Friendly/Enemy…) — за конфігом сервера.
	protected string SM_FactionLabel(int idx)
	{
		if (idx < 0 || idx >= m_aSMFactLabel.Count())
			return "";
		if (SM_MarkerConfig.GetInstance().m_bVanillaFactionNames && idx < m_aSMFactLabelVanilla.Count())
			return m_aSMFactLabelVanilla[idx];
		return m_aSMFactLabel[idx];
	}

	protected void SM_SelectFaction(int idx, bool userAction)
	{
		if (idx < 0 || idx >= m_aSMFactIdentity.Count())
			return;

		m_iSMSelFactionIdx = idx;
		m_iSMSelIdentity = m_aSMFactIdentity[idx];
		m_iSMMilColor = m_aSMFactColor[idx];
		if (userAction)
			m_bSMMilColorUser = false;	// нова фракція → дефолтний колір = її колір

		SM_HighlightButtons(m_aSMFactionBtns, idx);
		SM_SetSelText("FactionSelector", SM_FactionLabel(idx));

		SCR_MapMarkerEntryMilitary cfg = SM_MilConfig();
		if (cfg)
			SM_BuildDimensionButtons(cfg);	// форма/колір рамок вимірів = поточна фракція

		if (userAction)
			m_iSMSelKind = SM_EMarkerKind.MILITARY;
	}

	// Індекс нашої фракції за identity+кольором (колір відрізняє Enemy від Enemy 2 — обидва OPFOR).
	protected int SM_FindFactionIndex(int identity, int color)
	{
		int fallback = -1;
		foreach (int i, int id : m_aSMFactIdentity)
		{
			if (id != identity)
				continue;
			if (m_aSMFactColor[i] == color)
				return i;	// точний збіг identity+колір
			if (fallback < 0)
				fallback = i;
		}
		return fallback;
	}

	protected void SM_OnFactionClicked(SCR_ButtonBaseComponent component)
	{
		int idx = m_aSMFactionBtns.Find(SCR_ButtonImageComponent.Cast(component));
		if (idx >= 0)
			SM_SelectFaction(idx, true);
	}

	protected void SM_OnDimensionClicked(SCR_ButtonBaseComponent component)
	{
		int idx = m_aSMDimensionBtns.Find(SCR_ButtonImageComponent.Cast(component));
		if (idx >= 0)
			SM_SelectDimension(idx, true);
	}

	protected void SM_SelectDimension(int idx, bool userAction)
	{
		SCR_MapMarkerEntryMilitary cfg = SM_MilConfig();
		if (!cfg)
			return;
		SCR_MarkerMilitaryDimension d = cfg.GetDimensionEntry(idx);
		if (!d)
			return;
		m_iSMSelDimensionIdx = idx;
		m_iSMSelDimension = d.GetDimension();
		SM_HighlightButtons(m_aSMDimensionBtns, idx);
		SM_SetSelText("DimensionSelector", d.GetTranslation());
		if (userAction)
			m_iSMSelKind = SM_EMarkerKind.MILITARY;
	}

	// Підсвічує вибрану кнопку (opacity 1.0), решту приглушує (0.45). Спільне для фракцій/вимірів.
	protected void SM_HighlightButtons(array<SCR_ButtonImageComponent> btns, int selIdx)
	{
		foreach (int i, SCR_ButtonImageComponent b : btns)
		{
			if (!b)
				continue;
			Widget r = b.GetRootWidget();
			if (!r)
				continue;
			if (i == selIdx)
				r.SetOpacity(1.0);
			else
				r.SetOpacity(0.45);
		}
	}

	// Виставляє підпис вибору ("TextSelection") у заданому селекторі (Faction/Dimension).
	protected void SM_SetSelText(string selectorName, string text)
	{
		if (!m_MarkerEditRoot)
			return;
		Widget sel = m_MarkerEditRoot.FindAnyWidget(selectorName);
		if (!sel)
			return;
		TextWidget t = TextWidget.Cast(sel.FindAnyWidget("TextSelection"));
		if (t)
			t.SetText(text);
	}

	// Клік по кольору в палітрі → запам'ятовуємо, що колір перекрито вручну (стосується військових).
	override protected void OnColorEntryClicked(SCR_ButtonBaseComponent component)
	{
		// Це лише «паркування» фокуса на кольорі (анти-пресет на секціях Type/повзунки/тощо), а не реальний
		// вибір — ігноруємо, інакше військовому маркеру вмикався б m_bSMMilColorUser і колір біліє.
		if (m_bSMNavParking)
			return;
		super.OnColorEntryClicked(component);
		m_bSMMilColorUser = true;
		m_bSMBlackColor = false;	// вибрано колір із конфіг-палітри → чорний скинуто
	}

	// Реверс symbolFlags → елементи Type 1/Type 2 (при редагуванні). Бере перші два типи, чиї біти присутні.
	protected void SM_PrefillMilitaryTypes(int symbolFlags)
	{
		m_iSMTypeAFlags = 0;
		m_iSMTypeBFlags = 0;
		SCR_MapMarkerEntryMilitary cfg = SM_MilConfig();
		if (!cfg)
			return;

		array<ref SCR_MarkerMilitaryType> types = cfg.GetMilitaryTypes();
		int slotA = -1;
		int slotB = -1;
		foreach (int i, SCR_MarkerMilitaryType t : types)
		{
			int tflag = t.GetType();
			if (tflag == 0)
				continue;
			if ((symbolFlags & tflag) != tflag)	// усі біти цього типу мають бути присутні
				continue;
			if (slotA < 0)
			{
				slotA = i;
				m_iSMTypeAFlags = tflag;
			}
			else if (slotB < 0 && tflag != m_iSMTypeAFlags)
			{
				slotB = i;
				m_iSMTypeBFlags = tflag;
			}
		}

		// Індекс елемента комбобокса = позиція в масиві + 1 (0 = порожній елемент).
		if (m_SMComboA && slotA >= 0)
			m_SMComboA.SetCurrentItem(slotA + 1);
		if (m_SMComboB && slotB >= 0)
			m_SMComboB.SetCurrentItem(slotB + 1);
	}

	// ПРЕСЕТИ (крок 1: військові — рендер + застосування. «+»/видалення/загальні — наступним кроком).
	// Заповнює лінію військових пресетів (MilitaryIconSelectorLine0) символ-кнопками.
	protected void SM_WirePresets()
	{
		if (!m_MarkerEditRoot)
			return;
		SM_BuildMilPresetLine();
		SM_BuildGenPresetLine();
	}

	protected void SM_BuildMilPresetLine()
	{
		m_aSMMilPresetBtns.Clear();
		Widget line = m_MarkerEditRoot.FindAnyWidget("MilitaryIconSelectorLine0");
		if (!line)
			return;
		SM_ClearChildren(line);

		array<ref SM_MapMarkerData> mil = SM_MapMarkerPresets.GetInstance().GetMilitary();
		foreach (SM_MapMarkerData p : mil)
		{
			Widget b = GetGame().GetWorkspace().CreateWidgets(m_sSelectorDimensionEntry, line);
			SCR_ButtonImageComponent bc = null;
			if (b)
			{
				SM_RenderPresetSymbol(b, p);
				bc = SCR_ButtonImageComponent.Cast(b.FindHandler(SCR_ButtonImageComponent));
				if (bc)
				{
					bc.m_OnClicked.Insert(SM_OnMilPresetClicked);
					bc.m_OnFocus.Insert(SM_OnMilPresetFocus);		// наведення (для ПКМ-видалення)
					bc.m_OnFocusLost.Insert(SM_OnMilPresetFocusLost);
				}
			}
			m_aSMMilPresetBtns.Insert(bc);	// індекс = індекс пресета
		}
		m_iSMHoveredMilPreset = -1;

		// «+» в кінці — зберегти поточну військову мітку як пресет
		m_SMMilAddBtn = null;
		Widget plus = GetGame().GetWorkspace().CreateWidgets(m_sSelectorDimensionEntry, line);
		if (plus)
		{
			SM_RenderPlus(plus);
			m_SMMilAddBtn = SCR_ButtonImageComponent.Cast(plus.FindHandler(SCR_ButtonImageComponent));
			if (m_SMMilAddBtn)
				m_SMMilAddBtn.m_OnClicked.Insert(SM_OnAddMilPreset);
		}
	}

	// Малює «+» по центру кнопки. Текст у FrameWidget (заповнює overlay) + FrameSlot-центрування —
	// той самий надійний патерн, що й підписи міток (пивот 0.5,0.5 + SizeToContent).
	protected void SM_RenderPlus(Widget b)
	{
		Widget overlay = b.FindAnyWidget("OverlaySymbol");
		if (!overlay)
			return;
		WorkspaceWidget ws = GetGame().GetWorkspace();

		Widget frame = ws.CreateWidget(WidgetType.FrameWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(0x00000000), 0, overlay);
		if (!frame)
			return;
		AlignableSlot.SetHorizontalAlign(frame, LayoutHorizontalAlign.Stretch);
		AlignableSlot.SetVerticalAlign(frame, LayoutVerticalAlign.Stretch);

		TextWidget t = TextWidget.Cast(ws.CreateWidget(WidgetType.TextWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(0xFFFFFFFF), 0, frame));
		if (!t)
			return;
		t.SetText("+");
		t.SetFont("{3E7733BAC8C831F6}UI/Fonts/RobotoCondensed/RobotoCondensed_Regular.fnt");
		t.SetExactFontSize(32);
		FrameSlot.SetAnchorMin(t, 0.5, 0.5);
		FrameSlot.SetAnchorMax(t, 0.5, 0.5);
		FrameSlot.SetAlignment(t, 0.5, 0.5);
		FrameSlot.SetSizeToContent(t, true);
		FrameSlot.SetPos(t, 0, 0);
	}

	// Наведення на військовий пресет (focus = hover) — запам'ятати для ПКМ-видалення.
	protected void SM_OnMilPresetFocus(Widget w)
	{
		foreach (int i, SCR_ButtonImageComponent bc : m_aSMMilPresetBtns)
		{
			if (bc && bc.GetRootWidget() == w)
			{
				m_iSMHoveredMilPreset = i;
				return;
			}
		}
	}

	protected void SM_OnMilPresetFocusLost(Widget w)
	{
		foreach (int i, SCR_ButtonImageComponent bc : m_aSMMilPresetBtns)
		{
			if (bc && bc.GetRootWidget() == w && m_iSMHoveredMilPreset == i)
			{
				m_iSMHoveredMilPreset = -1;
				return;
			}
		}
	}

	// Клік «+» → зберегти поточні військові параметри як новий пресет.
	protected void SM_OnAddMilPreset(SCR_ButtonBaseComponent component)
	{
		SM_MapMarkerData d = new SM_MapMarkerData();
		d.m_iKind        = SM_EMarkerKind.MILITARY;
		d.m_iIdentity    = m_iSMSelIdentity;
		d.m_iDimension   = m_iSMSelDimension;
		d.m_iSymbolFlags = m_iSMTypeAFlags | m_iSMTypeBFlags;
		d.m_iColor       = SM_MilColor();
		SM_MapMarkerPresets.GetInstance().AddMilitary(d);
		SM_WirePresets();	// перебудувати лінію — показати новий пресет
	}

	// ЗАГАЛЬНІ ПРЕСЕТИ (UserPresets) — усі параметри; рендер civ-іконка АБО символ.
	protected void SM_BuildGenPresetLine()
	{
		m_aSMGenPresetBtns.Clear();
		Widget line = m_MarkerEditRoot.FindAnyWidget("UserIconSelectorLine0");
		if (!line)
			return;
		SM_ClearChildren(line);

		array<ref SM_MapMarkerData> gen = SM_MapMarkerPresets.GetInstance().GetGeneral();
		foreach (SM_MapMarkerData p : gen)
		{
			Widget b;
			if (p.m_iKind == SM_EMarkerKind.MILITARY)
			{
				b = GetGame().GetWorkspace().CreateWidgets(m_sSelectorDimensionEntry, line);
				if (b)
					SM_RenderPresetSymbol(b, p);
			}
			else
			{
				b = GetGame().GetWorkspace().CreateWidgets(m_sSelectorIconEntry, line);
				if (b)
					SM_RenderPresetCivIcon(b, p);
			}

			SCR_ButtonImageComponent bc = null;
			if (b)
			{
				bc = SCR_ButtonImageComponent.Cast(b.FindHandler(SCR_ButtonImageComponent));
				if (bc)
				{
					bc.m_OnClicked.Insert(SM_OnGenPresetClicked);
					bc.m_OnFocus.Insert(SM_OnGenPresetFocus);
					bc.m_OnFocusLost.Insert(SM_OnGenPresetFocusLost);
				}
			}
			m_aSMGenPresetBtns.Insert(bc);
		}
		m_iSMHoveredGenPreset = -1;

		// «+» — зберегти поточну мітку (усі параметри) як загальний пресет
		m_SMGenAddBtn = null;
		Widget plus = GetGame().GetWorkspace().CreateWidgets(m_sSelectorDimensionEntry, line);
		if (plus)
		{
			SM_RenderPlus(plus);
			m_SMGenAddBtn = SCR_ButtonImageComponent.Cast(plus.FindHandler(SCR_ButtonImageComponent));
			if (m_SMGenAddBtn)
				m_SMGenAddBtn.m_OnClicked.Insert(SM_OnAddGenPreset);
		}
	}

	// Рендерить цивільну іконку пресета на кнопці (m_sSelectorIconEntry).
	protected void SM_RenderPresetCivIcon(Widget b, SM_MapMarkerData p)
	{
		SCR_ButtonImageComponent bc = SCR_ButtonImageComponent.Cast(b.FindHandler(SCR_ButtonImageComponent));
		if (!bc || !bc.GetImageWidget())
			return;
		ResourceName imageset;
		string quad;
		if (SM_ResolveCivIcon(p.m_iIconEntry, imageset, quad) && imageset != "" && quad != "")
			bc.SetImage(imageset, quad);
		bc.GetImageWidget().SetColor(Color.FromInt(p.m_iColor));
	}

	protected void SM_OnGenPresetClicked(SCR_ButtonBaseComponent component)
	{
		int idx = m_aSMGenPresetBtns.Find(SCR_ButtonImageComponent.Cast(component));
		if (idx < 0)
			return;
		array<ref SM_MapMarkerData> gen = SM_MapMarkerPresets.GetInstance().GetGeneral();
		if (idx >= gen.Count())
			return;
		SM_ApplyGeneralPreset(gen[idx]);
	}

	// Автозаповнення діалогу УСІМА параметрами (тип/іконка-символ/колір/розмір/видимість/текст/поворот/галочка).
	protected void SM_ApplyGeneralPreset(notnull SM_MapMarkerData p)
	{
		m_iSMSelKind = p.m_iKind;
		if (p.m_iKind == SM_EMarkerKind.MILITARY)
		{
			SM_ApplyMilitaryPreset(p);	// фракція/вимір/типи/колір (виставить kind=MILITARY)
		}
		else
		{
			m_iSMSelKind = SM_EMarkerKind.CIVILIAN;
			m_iSelectedIconID = p.m_iIconEntry;
			if (p.m_iColor == SM_BLACK_ARGB)
			{
				SM_SelectBlack();
			}
			else
			{
				m_bSMBlackColor = false;
				int ci = SM_FindColorIndex(p.m_iColor);
				if (ci >= 0)
				{
					m_iSelectedColorID = ci;
					// Повноцінно ОБИРАЄМО колір (оновити m_SelectedColorButton + підсвітку), інакше при
					// геймпад-навігації пришпилений фокус на старій кнопці перезапише колір назад.
					Widget cw = m_MarkerEditRoot.FindAnyWidget("ColorEntry" + ci.ToString());
					if (cw)
					{
						SCR_ButtonBaseComponent cc = SCR_ButtonBaseComponent.Cast(cw.FindHandler(SCR_ButtonBaseComponent));
						if (cc)
							OnColorEntryClicked(cc);
					}
				}
			}
		}

		// спільні параметри
		m_iSMSelectedSize = p.m_iSize;
		if (m_SMSizeSlider)
			m_SMSizeSlider.SetValue(p.m_iSize);

		SM_SetVisibility(p.m_iVisibility);

		m_fRotation = p.m_iRotation;
		if (m_SliderComp)
			m_SliderComp.SetValue(p.m_iRotation);

		m_bSMTextColored = (p.m_iTextColored != 0);
		if (m_wSMTextCheck)
			m_wSMTextCheck.SetChecked(m_bSMTextColored);

		if (m_EditBoxComp)
			m_EditBoxComp.SetValue(p.m_sText);
	}

	// Клік «+» загальних → зберегти ВСІ поточні параметри як пресет.
	protected void SM_OnAddGenPreset(SCR_ButtonBaseComponent component)
	{
		SM_MapMarkerData d = new SM_MapMarkerData();
		d.m_iKind = m_iSMSelKind;
		if (m_iSMSelKind == SM_EMarkerKind.MILITARY)
		{
			d.m_iIdentity    = m_iSMSelIdentity;
			d.m_iDimension   = m_iSMSelDimension;
			d.m_iSymbolFlags = m_iSMTypeAFlags | m_iSMTypeBFlags;
			d.m_iColor       = SM_MilColor();
		}
		else
		{
			d.m_iIconEntry = m_iSelectedIconID;
			if (d.m_iIconEntry < 0)
				d.m_iIconEntry = 0;
			d.m_iColor = SM_SelectedColorARGB();
		}
		d.m_iSize       = m_iSMSelectedSize;
		d.m_iVisibility = m_iSMSelectedVis;
		d.m_iRotation   = m_fRotation;
		if (SM_IsTextColored())
			d.m_iTextColored = 1;
		if (m_EditBoxComp)
			d.m_sText = m_EditBoxComp.GetValue();

		SM_MapMarkerPresets.GetInstance().AddGeneral(d);
		SM_WirePresets();
	}

	protected void SM_OnGenPresetFocus(Widget w)
	{
		foreach (int i, SCR_ButtonImageComponent bc : m_aSMGenPresetBtns)
		{
			if (bc && bc.GetRootWidget() == w)
			{
				m_iSMHoveredGenPreset = i;
				return;
			}
		}
	}

	protected void SM_OnGenPresetFocusLost(Widget w)
	{
		foreach (int i, SCR_ButtonImageComponent bc : m_aSMGenPresetBtns)
		{
			if (bc && bc.GetRootWidget() == w && m_iSMHoveredGenPreset == i)
			{
				m_iSMHoveredGenPreset = -1;
				return;
			}
		}
	}

	// Рендерить APP-6 символ пресета на кнопці (OverlaySymbol у dimension-entry layout).
	protected void SM_RenderPresetSymbol(Widget b, SM_MapMarkerData p)
	{
		Widget overlay = b.FindAnyWidget("OverlaySymbol");
		if (!overlay)
			return;
		overlay.SetColor(Color.FromInt(p.m_iColor));
		SCR_MilitarySymbolUIComponent sc = SCR_MilitarySymbolUIComponent.Cast(overlay.FindHandler(SCR_MilitarySymbolUIComponent));
		if (sc)
			sc.Update(SM_BuildMilitarySymbol(p));
	}

	protected void SM_OnMilPresetClicked(SCR_ButtonBaseComponent component)
	{
		int idx = m_aSMMilPresetBtns.Find(SCR_ButtonImageComponent.Cast(component));
		if (idx < 0)
			return;
		array<ref SM_MapMarkerData> mil = SM_MapMarkerPresets.GetInstance().GetMilitary();
		if (idx >= mil.Count())
			return;
		SM_ApplyMilitaryPreset(mil[idx]);
	}

	// Автозаповнення діалогу військовими параметрами пресета (далі гравець сам ставить мітку).
	protected void SM_ApplyMilitaryPreset(notnull SM_MapMarkerData p)
	{
		m_iSMSelKind = SM_EMarkerKind.MILITARY;

		int f = SM_FindFactionIndex(p.m_iIdentity, p.m_iColor);
		if (f >= 0)
			SM_SelectFaction(f, true);	// підсвітить фракцію + перебудує виміри

		SCR_MapMarkerEntryMilitary cfg = SM_MilConfig();
		if (cfg)
		{
			int dIdx = cfg.GetDimensionEntryID(p.m_iDimension);
			if (dIdx >= 0)
				SM_SelectDimension(dIdx, true);
		}

		SM_PrefillMilitaryTypes(p.m_iSymbolFlags);	// виставить Type 1/Type 2

		// стан символа з пресета (перекриває дефолти від SM_SelectFaction/Dimension)
		m_iSMSelIdentity  = p.m_iIdentity;
		m_iSMSelDimension = p.m_iDimension;
		m_iSMMilColor     = p.m_iColor;	// SM_MilColor() поверне його (m_bSMMilColorUser лишається false)
	}

	protected void SM_OnComboA(SCR_ComboBoxComponent comp, int value)
	{
		m_iSMComboAIdx = value;	// для циклу геймпадом
		SCR_MarkerMilitaryType t = SCR_MarkerMilitaryType.Cast(comp.GetItemData(value));
		m_iSMTypeAFlags = 0;
		if (t)
			m_iSMTypeAFlags = t.GetType();
		m_iSMSelKind = SM_EMarkerKind.MILITARY;
	}

	protected void SM_OnComboB(SCR_ComboBoxComponent comp, int value)
	{
		m_iSMComboBIdx = value;
		SCR_MarkerMilitaryType t = SCR_MarkerMilitaryType.Cast(comp.GetItemData(value));
		m_iSMTypeBFlags = 0;
		if (t)
			m_iSMTypeBFlags = t.GetType();
		m_iSMSelKind = SM_EMarkerKind.MILITARY;
	}

	// Кількість елементів комбобокса типу (порожній "" + типи символів).
	protected int SM_NavComboCount()
	{
		SCR_MapMarkerEntryMilitary cfg = SM_MilConfig();
		if (!cfg)
			return 0;
		return cfg.GetMilitaryTypes().Count() + 1;
	}

	// Цикл комбобокса (Вліво/Вправо) без відкриття дропдауна. SetCurrentItem кидає m_OnChanged → оновлює тип+індекс.
	protected void SM_NavCycleCombo(SCR_ComboBoxComponent combo, int cur, int dir)
	{
		if (!combo)
			return;
		int n = SM_NavComboCount();
		if (n <= 0)
			return;
		int idx = cur + dir;
		if (idx < 0)
			idx = 0;
		if (idx >= n)
			idx = n - 1;
		combo.SetCurrentItem(idx);
	}

	// Клік по ЦИВІЛЬНІЙ іконці → перемикаємось на цивільну мітку (ванільний хук).
	// Для наших сердець вибір робимо самі: ванільний код спитав би конфіг про зарезервований
	// індекс, не знайшов би і завантажив порожній imageset у прев'ю (помилки в лозі).
	override protected void OnIconEntryClicked(notnull SCR_ButtonBaseComponent component)
	{
		if (m_bSMSuppressIconSelect)	// зміна сторінки падом — не обираємо іконку й не міняємо вид мітки
			return;

		int id = m_mIconIDs.Get(component);
		if (id >= SM_HEART_ICON_BASE)
		{
			if (m_SelectedIconButton)
				m_SelectedIconButton.ColorizeBackground();
			component.ColorizeBackground();
			m_SelectedIconButton = component;
			m_iSelectedIconID = id;

			string quad;
			if (id == SM_HEART_ICON_BASE)
				quad = "anarchyHeart1";
			else
				quad = "anarchyHeart2";
			if (m_wMarkerPreview)
				m_wMarkerPreview.LoadImageFromSet(0, SM_HEART_IMAGESET, quad);
			if (m_wMarkerPreviewGlow)
				m_wMarkerPreviewGlow.LoadImageFromSet(0, SM_HEART_IMAGESET, quad);
		}
		else
		{
			super.OnIconEntryClicked(component);
		}

		m_iSMSelKind = SM_EMarkerKind.CIVILIAN;
	}

	// Видаляє всіх дітей віджета (перебудова селекторів).
	protected void SM_ClearChildren(Widget parent)
	{
		Widget ch = parent.GetChildren();
		while (ch)
		{
			ch.RemoveFromHierarchy();
			ch = parent.GetChildren();
		}
	}

	// Ховає ванільне прев'ю-мініатюру в панелі діалогу (нам вистачає живого прев'ю на мапі).
	protected void SM_HideVanillaPreview()
	{
		if (m_wMarkerPreview)
			m_wMarkerPreview.SetVisible(false);
		if (m_wMarkerPreviewGlow)
			m_wMarkerPreviewGlow.SetVisible(false);
		if (m_wMarkerPreviewText)
			m_wMarkerPreviewText.SetVisible(false);
	}

	// Видимість реальної мітки (icon+label) за id.
	protected void SM_SetMarkerVisible(int id, bool visible)
	{
		SM_MarkerVisual vis = m_mSMVisuals.Get(id);
		if (!vis)
			return;
		Widget main = vis.GetMainWidget();
		if (main)
			main.SetVisible(visible);
		if (vis.m_wLabel)
			vis.m_wLabel.SetVisible(visible);
		if (vis.m_wTime)
			vis.m_wTime.SetVisible(visible && vis.m_Data && vis.m_Data.m_iDate != 0);
	}

	// Перенаправлення підтвердження діалогу на нашу систему (замість ванільного маркера).
	// isLocal: Private-кнопка → PERSONAL; Public-кнопка → ALL (повний селектор видимості — згодом).
	override protected void OnInsertMarker(bool isLocal)
	{
		SCR_PlayerController pc = SM_LocalPC();
		if (pc)
		{
			SM_MapMarkerData d = new SM_MapMarkerData();
			d.m_iKind       = m_iSMSelKind;
			d.m_iRotation   = m_fRotation;
			d.m_iPosX       = m_iSMPlaceX;
			d.m_iPosY       = m_iSMPlaceY;
			d.m_iSize       = m_iSMSelectedSize;
			d.m_iVisibility = m_iSMSelectedVis;	// з нашого селектора (Public/Private-кнопки ігноруємо)

			if (m_iSMSelKind == SM_EMarkerKind.MILITARY)
			{
				d.m_iIdentity    = m_iSMSelIdentity;
				d.m_iDimension   = m_iSMSelDimension;
				d.m_iSymbolFlags = m_iSMTypeAFlags | m_iSMTypeBFlags;
				d.m_iColor       = SM_MilColor();	// палітра (якщо перефарбовано) інакше колір фракції
			}
			else
			{
				d.m_iIconEntry = m_iSelectedIconID;
				if (d.m_iIconEntry < 0)
					d.m_iIconEntry = 0;
				d.m_iColor = SM_SelectedColorARGB();
			}

			if (SM_IsTextColored())
				d.m_iTextColored = 1;
			SM_ResolveTimestamp(d.m_iDate, d.m_iTime);

			// GM-контроли: обрана фракція → канал Side-мітки; галочка Locked (сервер це поважає/застосовує).
			if (SM_IsEditorMap())
			{
				if (d.m_iVisibility == SM_EMarkerVisibility.FACTION && m_iSMGmFactionChosen >= 0)
					d.m_iChannel = m_iSMGmFactionChosen;
				if (m_wSMGmLockCheck && m_wSMGmLockCheck.IsChecked())
					d.m_iGmLocked = 1;
				if (m_wSMHideInfoCheck && m_wSMHideInfoCheck.IsChecked())
					d.m_iHideInfo = 1;
			}

			string text = "";
			if (m_EditBoxComp)
				text = m_EditBoxComp.GetValue();

			// Запам'ятовуємо як шаблон для Alt+ЛКМ-копії (id/owner/канал/час перевизначить сервер при копії).
			m_SMLastTemplate = d.SM_Clone();
			m_SMLastTemplate.m_sText = text;

			d.m_sText    = text;				// Local ops read the text from d itself (the server path passes it separately)
			d.m_iOwnerId = pc.GetPlayerId();	// Local marker owner = the local player (the server path overrides owner anyway)

			bool editingLocal = (m_iSMEditId != -1) && SM_MapMarkerStore.IsLocalId(m_iSMEditId);

			if (editingLocal)
			{
				if (d.m_iVisibility == SM_EMarkerVisibility.PERSONAL)
				{
					// Stays Local — update the client file, the server is not involved.
					SM_LocalMarkerPersistence.GetInstance().UpdateLocal(m_iSMEditId, d);
				}
				else
				{
					// ESCALATION Local -> Side/Group/Global: erase from the client file and hand it
					// to the server (it becomes a normal server marker with a server-side owner).
					SM_LocalMarkerPersistence.GetInstance().RemoveLocal(m_iSMEditId);
					pc.SM_RequestPlace(d.PackInts(), text);
				}
			}
			else if (m_iSMEditId != -1)
			{
				pc.SM_RequestEdit(m_iSMEditId, d.PackInts(), text);	// editing a server marker
			}
			else if (d.m_iVisibility == SM_EMarkerVisibility.PERSONAL)
			{
				SM_LocalMarkerPersistence.GetInstance().AddLocal(d);	// new Local marker — client file, not the server
			}
			else
			{
				pc.SM_RequestPlace(d.PackInts(), text);	// new server marker
			}
		}

		m_iSMEditId = -1;
		CleanupMarkerEditWidget();
	}

	override protected void OnEditCancelled(SCR_InputButtonComponent button)
	{
		m_iSMEditId = -1;
		super.OnEditCancelled(button);
	}

	// Будь-яке закриття діалогу (підтвердження/скасування) — прибираємо прев'ю + повертаємо видимість.
	override protected void CleanupMarkerEditWidget()
	{
		SM_EndPreview();
		if (m_bSMEditorUIHidden)
			SM_SetEditorUIHidden(false);	// повертаємо UI редактора після закриття нашого діалогу
		m_SMSizeSlider = null;		// контроли належать діалогу — лише відпускаємо посилання
		m_aSMVisWidgets.Clear();
		m_aSMMilPresetBtns.Clear();
		m_SMMilAddBtn = null;
		m_iSMHoveredMilPreset = -1;
		m_aSMGenPresetBtns.Clear();
		m_SMGenAddBtn = null;
		m_iSMHoveredGenPreset = -1;

		// Повертаємо видимість реальної мітки, яку ховали на час редагування.
		// (При підтвердженні редагування її все одно перебудує SM_OnMarkerChanged — SetVisible(true) нешкідливий.)
		if (m_iSMHiddenMarkerId != -1)
		{
			SM_SetMarkerVisible(m_iSMHiddenMarkerId, true);
			// Її віджет лишився на СТАРІЙ екранній позиції (мапа панорамувалась під час редагування,
			// а прихована мітка не перераховувалась) — позиціонуємо одразу, щоб не блимала й не «стрибала».
			SM_MarkerVisual vis = m_mSMVisuals.Get(m_iSMHiddenMarkerId);
			if (vis && vis.m_Data)
				SM_PositionVisual(vis, vis.m_Data.m_iPosX, vis.m_Data.m_iPosY, SM_ZoomFactor(), GetGame().GetWorkspace());
			m_iSMHiddenMarkerId = -1;
		}

		super.CleanupMarkerEditWidget();
	}

	// ЖИВЕ ПРЕВ'Ю НА МАПІ (поки відкритий діалог) — гравець бачить точний вигляд/розмір/позицію.
	protected void SM_BeginPreview()
	{
		SM_EndPreview();
		if (!m_wSMMapFrame)
			return;

		SM_MapMarkerData d = new SM_MapMarkerData();
		d.m_iKind = m_iSMSelKind;	// прев'ю стартує з поточного типу (важливо при редагуванні військової)
		d.m_iPosX = m_iSMPlaceX;
		d.m_iPosY = m_iSMPlaceY;

		m_PreviewVisual = new SM_MarkerVisual(d);
		SM_BuildVisualWidgets(m_PreviewVisual);
		if (!m_PreviewVisual.GetMainWidget())
			m_PreviewVisual = null;
	}

	protected void SM_EndPreview()
	{
		if (m_PreviewVisual)
		{
			m_PreviewVisual.Destroy();
			m_PreviewVisual = null;
		}
	}

	// Зчитує поточні вибори діалогу у дані прев'ю (виклик щокадру з Update).
	protected void SM_UpdatePreviewData()
	{
		if (!m_PreviewVisual || !m_PreviewVisual.m_Data)
			return;

		SM_MapMarkerData d = m_PreviewVisual.m_Data;

		// Зміна типу civ↔mil → перебудувати головний віджет прев'ю (іконка ↔ символ).
		if (d.m_iKind != m_iSMSelKind)
		{
			d.m_iKind = m_iSMSelKind;
			SM_RebuildMain(m_PreviewVisual);
		}

		d.m_iRotation = m_fRotation;
		d.m_iSize = m_iSMSelectedSize;	// прев'ю враховує вибраний розмір
		if (m_EditBoxComp)
			d.m_sText = m_EditBoxComp.GetValue();

		d.m_iTextColored = 0;
		if (SM_IsTextColored())
			d.m_iTextColored = 1;
		SM_ResolveTimestamp(d.m_iDate, d.m_iTime);

		if (m_iSMSelKind == SM_EMarkerKind.MILITARY)
		{
			d.m_iIdentity    = m_iSMSelIdentity;
			d.m_iDimension   = m_iSMSelDimension;
			d.m_iSymbolFlags = m_iSMTypeAFlags | m_iSMTypeBFlags;
			d.m_iColor       = SM_MilColor();
		}
		else
		{
			d.m_iIconEntry = m_iSelectedIconID;
			if (d.m_iIconEntry < 0)
				d.m_iIconEntry = 0;
			d.m_iColor = SM_SelectedColorARGB();
		}
	}

	// Колір військової мітки: вибраний у палітрі (якщо гравець клікнув колір) інакше колір фракції.
	protected int SM_MilColor()
	{
		if (m_bSMMilColorUser)
			return SM_SelectedColorARGB();
		return m_iSMMilColor;
	}

	// Поточний вибраний колір (ARGB): наша чорна кнопка АБО колір із палітри конфіга.
	protected int SM_SelectedColorARGB()
	{
		if (m_bSMBlackColor)
			return SM_BLACK_ARGB;
		if (m_PlacedMarkerConfig)
			return m_PlacedMarkerConfig.GetColorEntry(m_iSelectedColorID).PackToInt();
		return 0xFFFFFFFF;
	}

	// Нова мітка успадковує колір пензля з панелі малювання: підбираємо НАЙБЛИЖЧИЙ колір
	// ванільної палітри діалогу (плюс наша чорна кнопка як кандидат) і «клікаємо» його програмно —
	// селекція/підсвітка йдуть штатним шляхом. Стосується лише цивільної мітки: військовій колір
	// пензля не навʼязуємо (m_bSMMilColorUser скидаємо — вона лишиться в кольорі фракції,
	// поки гравець сам не обере колір у діалозі).
	protected void SM_PresetBrushColor()
	{
		if (!m_MarkerEditRoot || !m_PlacedMarkerConfig)
			return;
		Widget colorSel = m_MarkerEditRoot.FindAnyWidget("ColorSelector");
		if (!colorSel)
			return;
		Widget line = colorSel.FindAnyWidget("ColorSelectorLine");
		if (!line)
			return;

		// Кнопки кольорів у порядку створення (індекс = id запису конфіга; остання — наша чорна).
		array<SCR_ButtonImageComponent> btns = {};
		Widget ch = line.GetChildren();
		while (ch)
		{
			SCR_ButtonImageComponent bc = SCR_ButtonImageComponent.Cast(ch.FindHandler(SCR_ButtonImageComponent));
			if (bc)
				btns.Insert(bc);
			ch = ch.GetSibling();
		}
		if (btns.IsEmpty())
			return;

		int brush = SM_DrawCanvas.GetBrushColor();

		int cfgCount = btns.Count();
		if (m_SMBlackBtn)
			cfgCount--;	// остання кнопка — чорна, не з конфіга

		int bestIdx = -1;
		int bestDist = 0x7FFFFFFF;
		for (int i = 0; i < cfgCount; i++)
		{
			int c = m_PlacedMarkerConfig.GetColorEntry(i).PackToInt();
			int dist = SM_ColorDistSq(brush, c);
			if (dist < bestDist)
			{
				bestDist = dist;
				bestIdx = i;
			}
		}

		if (m_SMBlackBtn && SM_ColorDistSq(brush, SM_BLACK_ARGB) < bestDist)
			SM_OnBlackColorClicked(m_SMBlackBtn);
		else if (bestIdx >= 0 && bestIdx < btns.Count())
			OnColorEntryClicked(btns[bestIdx]);
		else
			return;

		m_bSMMilColorUser = false;	// військова мітка колір пензля НЕ бере (лише ручний вибір у діалозі)
	}

	// Квадрат RGB-відстані між двома ARGB (альфу ігноруємо).
	protected int SM_ColorDistSq(int a, int b)
	{
		int ar = (a >> 16) & 0xFF;
		int ag = (a >> 8)  & 0xFF;
		int ab = a & 0xFF;
		int br = (b >> 16) & 0xFF;
		int bg = (b >> 8)  & 0xFF;
		int bb = b & 0xFF;
		int dr = ar - br;
		int dg = ag - bg;
		int db = ab - bb;
		return dr * dr + dg * dg + db * db;
	}

	// Додає в палітру нашу чорну кнопку (конфіг кольорів ванільний — у код, не чіпаючи його).
	protected void SM_AddBlackColorButton()
	{
		m_SMBlackBtn = null;
		if (!m_MarkerEditRoot)
			return;
		Widget colorSel = m_MarkerEditRoot.FindAnyWidget("ColorSelector");
		if (!colorSel)
			return;
		Widget line = colorSel.FindAnyWidget("ColorSelectorLine");
		if (!line)
			return;

		Widget b = GetGame().GetWorkspace().CreateWidgets(m_sSelectorColorEntry, line);
		if (!b)
			return;
		SCR_ButtonImageComponent bc = SCR_ButtonImageComponent.Cast(b.FindHandler(SCR_ButtonImageComponent));
		if (!bc)
			return;
		bc.GetImageWidget().SetColor(Color.FromInt(SM_BLACK_ARGB));
		bc.m_OnClicked.Insert(SM_OnBlackColorClicked);
		bc.m_OnFocus.Insert(SM_OnBlackColorFocused);	// геймпад: вибір на фокусі (як ванільні кольори)
		m_SMBlackBtn = bc;
	}

	// Клік по чорній кнопці — інтегруємось у ванільну підсвітку (m_SelectedColorButton).
	protected void SM_OnBlackColorClicked(SCR_ButtonBaseComponent component)
	{
		SM_SelectBlack();
	}

	// Фокус на чорній кнопці — вибір ЛИШЕ на геймпаді (там навігація фокусна, вибір на фокусі).
	// На KB/M ховер = фокус (рушій фокусить під курсором), тож без гейта чорний застосовувався б
	// із самого наведення — на відміну від ванільних кольорів, які на KB/M обираються кліком.
	protected void SM_OnBlackColorFocused(SCR_ButtonBaseComponent component)
	{
		if (SM_NavOnKBM())
			return;
		SM_SelectBlack();
	}

	protected void SM_SelectBlack()
	{
		if (m_SelectedColorButton)
			m_SelectedColorButton.ColorizeBackground();	// зняти підсвітку з попередньої
		if (m_SMBlackBtn)
			m_SMBlackBtn.ColorizeBackground();			// підсвітити чорну
		m_SelectedColorButton = m_SMBlackBtn;			// наступний клік ванільного кольору зніме нашу
		m_bSMBlackColor = true;
		m_bSMMilColorUser = true;	// чорний теж «ручний» колір (стосується військових)
	}

	// Чи фарбувати підпис у колір мітки — читаємо рушійний чекбокс (інакше останній відомий стан).
	protected bool SM_IsTextColored()
	{
		if (m_wSMTextCheck)
			return m_wSMTextCheck.IsChecked();
		return m_bSMTextColored;
	}

	// SpinBox Timestamp Yes/No (1 = Yes).
	protected void SM_OnTimestampChanged(SCR_SpinBoxComponent comp, int selectedItem)
	{
		m_bSMTimestamp = (selectedItem == 1);
	}

	// Дата+час для поточної мітки (ІГРОВІ, сценарію). 0/0 якщо вимкнено.
	// Завжди ПОТОЧНИЙ час: нова мітка — час створення; збереження редагування — час останньої зміни.
	protected void SM_ResolveTimestamp(out int date, out int time)
	{
		date = 0;
		time = 0;
		if (!m_bSMTimestamp)
			return;

		// час/дата сценарію (синхронізовані в MP)
		ChimeraWorld world = GetGame().GetWorld();
		if (!world)
			return;
		TimeAndWeatherManagerEntity tw = TimeAndWeatherManagerEntity.Cast(world.GetTimeAndWeatherManager());
		if (!tw)
			return;

		int y, mo, d;
		tw.GetDate(y, mo, d);
		int h, mi, s;
		tw.GetHoursMinutesSeconds(h, mi, s);
		date = y * 10000 + mo * 100 + d;
		time = h * 100 + mi;
	}

	// yyyymmdd + hhmm → "DD.MM.YYYY HH:MM".
	protected string SM_DateTimeString(int date, int time)
	{
		int h = time / 100;
		int mi = time % 100;
		return SM_DateString(date) + " " + SM_Pad2(h) + ":" + SM_Pad2(mi);
	}

	// yyyymmdd → "DD.MM.YYYY".
	protected string SM_DateString(int date)
	{
		int y = date / 10000;
		int m = (date / 100) % 100;
		int d = date % 100;
		return SM_Pad2(d) + "." + SM_Pad2(m) + "." + y.ToString();
	}

	protected string SM_Pad2(int v)
	{
		if (v < 10)
			return "0" + v.ToString();
		return v.ToString();
	}

	// Індекс кольору в палітрі за упакованим ARGB (-1 якщо не знайдено).
	protected int SM_FindColorIndex(int argb)
	{
		if (!m_PlacedMarkerConfig)
			return -1;

		array<ref SCR_MarkerColorEntry> colors = m_PlacedMarkerConfig.GetColorEntries();
		if (!colors)
			return -1;

		foreach (int i, SCR_MarkerColorEntry c : colors)
		{
			if (c && c.GetColor().PackToInt() == argb)
				return i;
		}
		return -1;
	}
}
