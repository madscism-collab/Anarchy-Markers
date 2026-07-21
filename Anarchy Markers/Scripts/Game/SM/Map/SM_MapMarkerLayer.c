// Адаптер мапи, ЯДРО (блок 1 із 2) — єдине місце, що торкається ванільного фреймворку мапи. Якщо BI
// щось у мапі змінить, правити доведеться ці два файли; ядро (дані/мережа/збереження) не зачіпається.
// Що тут: поля класу (ВСІ — див. правило нижче), візуали міток/діалог редагування/пресети/консольна
// навігація діалогу (SM_Nav*)/рендер-політика. Життєвий цикл (OnMapOpen/Update/OnMapClose), поллінг
// вводу і HUD-оверлеї (підказки/тултіпи/промпти/вказівники) — у SM_MapMarkerLayerInput.c.
//
// ПРАВИЛО РОЗРІЗУ: обидва файли — modded-блоки ОДНОГО класу SCR_MapMarkersUI; блоки компілюються
// ланцюжком за абеткою файлів. Цей файл іде РАНІШЕ, тож НІЩО тут не може посилатися на методи з
// SM_MapMarkerLayerInput.c — а той бачить усе звідси. Нові поля класу оголошуй ТІЛЬКИ тут.
//
//   1. Глушимо ванільне розміщення міток (OnRadialMenuInit / OnInputQuickMarkerMenu стають no-op).
//      Ліву панель (компас/лінійка/годинник) не чіпаємо — це окремий SCR_MapToolMenuUI.
//   2. Малюємо наші мітки зі сховища власними віджетами, прикріпленими до MapFrame. Самі рахуємо:
//        - позицію: WorldToScreen щокадру, центр мітки точно на світовій точці;
//        - розмір: "як на папері" — base*factor, на макс. наближенні 1.0, при віддаленні менше;
//        - колір/поворот; картинку беремо з ванільного конфігу іконок (без хардкоду).
//      Хіт-тест точний, бо віджет збігається з видимою іконкою.

modded class SCR_MapMarkersUI
{
	protected ref map<int, ref SM_MarkerVisual> m_mSMVisuals = new map<int, ref SM_MarkerVisual>();
	protected bool m_bSMMapOpen = false;
	protected bool m_bSMEditorMap = false;	// кеш: чи поточна мапа — режим редактора (GM)
	protected bool m_bSMPadConfirmDown = false;	// фронт кнопки A (AM_MapAction) на геймпаді
	protected bool m_bSMPadPlaceDown = false;	// фронт кнопки Y (AM_CopyLastPad) на МАПІ — копія останньої мітки
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
	protected bool m_bSMPlaceDown = false;		// фронт кнопки Y (AM_CopyLastPad) у діалозі
	protected bool m_bSMDeleteDown = false;		// фронт кнопки X (AM_Delete) — видалити сфокусований пресет
	protected ImageWidget m_wSMNavHL;			// жовто-оранжевий оверлей підсвітки поточної секції
	protected const int SM_NAV_HL_COLOR = 0x24FFA000;	// ледь помітний жовто-оранжевий (низька альфа)
	protected bool m_bSMSubscribed = false;
	protected Widget m_wSMMapFrame;		// MapFrame, до нього чіпляємо наші іконки
	protected bool   m_bSMOwnFrame = false;	// true якщо m_wSMMapFrame — наш створений оверлей (GM-мапа), треба прибрати

	// AM_EMapFeature mask for the map screen currently open. The player's map and the GM editor get
	// everything; tablets, terminals and anything else default to view-only, so our hotkey listeners
	// and panels stay out of other mods' map screens. See AM_MapFeatures.
	protected int m_iSMFeatures = 63;	// AM_MapFeatures.FULL

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
	protected TextWidget m_wSMTplPrompt;	// підказка біля курсора для темплейтів (клікни/тримай/чекай)
	protected Widget m_wSMMapCursor;	// віджет курсора карти (ховаємо під час вказування пальцем)

	// Оптимізація рендера: масово переставляти мітки треба лише коли змінився вид (пан/зум) або набір міток
	protected bool  m_bSMNeedReposition = true;	// прапорець "треба перерахувати всі"
	protected float m_fSMLastZoom = -1;			// зум минулого кадру (щоб зловити зміну виду)
	protected int   m_iSMLastRefX = -99999;		// екранна позиція світового (0,0) — щоб зловити пан
	protected int   m_iSMLastRefY = -99999;

	// How a marker LOOKS (base size, label ratios, heart icons) lives in AM_MarkerWidgets — the map
	// is only one surface that draws them, an ATAK-style tablet screen is another. Sizes/ratios and
	// the widget builders come from there so both surfaces stay identical.
	//
	// «Паперовий» масштаб має бути ЧИСТО пропорційним (currentPPU/maxPPU): мітка зменшується
	// разом зі світом без плато, як будівлі/поля. Тримаємо лише мікроскопічний епсилон, щоб розмір
	// не став 0 (не «приклеюється» до мапи, як було з 0.02 — тоді на дальніх зумах мітка застигала ~14px).
	protected const float SM_MIN_ZOOM_SCALE = 0.0005;
	protected const int   SM_TEXT_CHAR_LIMIT = 128;	// ліміт тексту мітки в байтах (ваніль = 16)

	// TRAP: SCR_MapEntity's two projection calls do NOT speak the same space, and they are not
	// inverses of one another:
	//     WorldToScreen  RETURNS pixels local to the MAP WIDGET
	//     ScreenToWorld  TAKES   pixels local to the MAP FRAME
	// On the fullscreen map the map widget and the map frame both sit at the screen origin, so all
	// three spaces coincide and this never mattered. A tablet hosts the map in a small inset widget,
	// and then they differ by a constant PIXEL offset — which looks nearly right zoomed in and throws
	// everything kilometres off the terrain once you zoom out (constant pixels = growing metres).
	//
	// m_fSMMapOffX/Y is the shift from map-widget space into our frame's space, in layout units: add
	// it to a WorldToScreen result before it becomes a FrameSlot position. For the other direction,
	// subtract the FRAME's screen position from screen coordinates before calling ScreenToWorld.
	// It only changes when the layout does, so refresh it once per frame rather than per marker.
	protected float m_fSMMapOffX;
	protected float m_fSMMapOffY;

	protected bool m_bSMLastMarkersVisible = true;	// last state of the host's "show markers" switch
	protected int  m_iSMForcedVis = -1;				// channel pinned by the host screen; -1 = player picks
	protected bool m_bSMDrawPanelShown = true;		// host screens can start it hidden and toggle it
	protected bool m_bSMPanelStartsHidden;			// this screen's panel is the host's to open (RB toggles it)
	protected float m_fSMPanelScale;				// host-supplied UI scale for this map mode; 0 = panel fits itself
	protected float m_fSMPanelOffX, m_fSMPanelOffY;	// host screen shoves the drawing panel clear of its chrome
	protected float m_fSMHintDX, m_fSMHintDY;		// host screen shoves the control hints clear of its chrome

	//! Show/hide our drawing panel from outside. For a host screen that owns its own toolbar button
	//! (an ATAK tablet repurposes its pencil): the tools exist, but the host decides when the panel
	//! is on screen. Hiding it also disarms the active tool — a hidden panel must not keep drawing.
	void AM_SetDrawPanelShown(bool shown)
	{
		if (shown == m_bSMDrawPanelShown)
			return;
		m_bSMDrawPanelShown = shown;
		if (!shown && m_DrawCanvas)
			m_DrawCanvas.SetActive(false);
	}

	void AM_ToggleDrawPanel()
	{
		AM_SetDrawPanelShown(!m_bSMDrawPanelShown);
	}

	bool AM_IsDrawPanelShown()
	{
		return m_bSMDrawPanelShown && m_DrawPanel != null;
	}

	// Ввід для переміщення: окремої клавіші нема — утримав ЛКМ на мітці, підняв, клікнув куди поставити.
	protected int   m_iSMCarryId = -1;		// мітка, яку зараз тягнемо (-1 = жодної)
	static bool s_bSMCarrying = false;		// читає SM_DisableRadial, щоб не відкривати радіалку під час перенесення
	protected bool  m_bSMLmbDown = false;	// стан ЛКМ минулого кадру (ловимо натиск/відпуск)
	protected bool  m_bSMPickedThisPress = false;	// підняли на цьому ж утриманні (щоб відпуск не поставив одразу)
	protected float m_fSMPressTime = 0;		// коли натиснули ЛКМ (для утримання-підняття)
	protected int   m_iSMPressMarkerId = -1;	// мітка під курсором у мить натиску
	protected float m_fSMPressX = 0;		// позиція курсора в мить натиску
	protected float m_fSMPressY = 0;
	// Шаблон останньої розміщеної/редагованої гравцем мітки (Alt+ЛКМ ставить її копію). Живе всю сесію.
	protected ref SM_MapMarkerData m_SMLastTemplate;
	// Copy numbering: the number we handed out last and the label it belonged to. A server-channel
	// marker only reaches the store after the round trip, so without this a quick run of copies would
	// keep reading the same "lowest free" number and hand out [2] several times over.
	protected string m_sSMCopyStem;
	protected int    m_iSMCopyIssued;
	// Стан режиму розміщення зевса (Create Marker): відстежуємо, щоб клік по самій кнопці не зарахувався.
	protected bool m_bSMWasCreatePending = false;
	protected bool m_bSMCreateSawRelease = false;
	protected bool m_bSMEditorUIHidden = false;	// чи ми сховали UI редактора на час нашого діалогу
	protected bool m_bSMEditorUISub = false;	// чи підписані на подію зміни видимості UI редактора
	// Утримання тепер живе на самих екшенах (InputFilterHold у chimeraInputCommon.conf) — саме тому
	// підказка малює полосу заповнення сама, і саме тому обидва утримання можна перебіндити нарізно.
	// Ці константи ЛИШЕ дзеркалять HoldDuration звідти: вони потрібні на ВІДПУСКАННІ, коли екшен уже
	// впав у нуль і спитати його "чи це було утримання" вже нема як. Міняєш там — міняй і тут.
	protected const float SM_HOLD_SEC = 0.2;	// = AM_MarkerMove HoldDuration 200
	protected const float SM_POINT_HOLD_SEC = 0.3;	// = AM_Pointer HoldDuration 300
	protected const float SM_MOVE_THRESHOLD = 40;	// на скільки px курсор може зрушити й це ще "на місці"
	// Вказівник (показати пальцем): утримання ЛКМ на пустому місці водить тимчасову точку.
	protected bool  m_bSMPointing = false;
	protected bool  m_bSMHoldMoveDown = false;	// фронт AM_MarkerMove (власний, не від ЛКМ)
	protected bool  m_bSMHoldPointDown = false;	// фронт AM_Pointer
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

	// Ванільний Delete-хендлер читає СВІЙ трекінг наведеної мітки (власні ванільні віджети мапи) —
	// а ми рендеримо мітки повністю своїми віджетами поза цим механізмом, тож той трекінг завжди
	// порожній і ваніль падає на null-widget. Клавіша Delete за замовчуванням висить і на ванільній
	// дії, і на нашій AM_Delete одночасно, тож обидві спрацьовують з одного натиску — глушимо ванільну,
	// видалення веде SM_OnDelete (SM_MapMarkerLayerInput.c).
	override protected void OnInputMarkerDelete(float value, EActionTrigger reason)
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
		copy.m_sText = SM_NumberedCopyText(m_SMLastTemplate.m_sText);

		// A Local copy stays client-side; anything else goes to the server.
		if (copy.m_iVisibility == SM_EMarkerVisibility.PERSONAL)
		{
			copy.m_iOwnerId = cpc.GetPlayerId();
			SM_LocalMarkerPersistence.GetInstance().AddLocal(copy);
		}
		else
			cpc.SM_RequestPlace(copy.PackInts(), copy.m_sText);
	}

	//! Next label in a copy run: "Rally" -> "Rally [2]" -> "Rally [3]". A trailing [N] is REPLACED,
	//! not stacked, so the suffix never grows into "[2] [3]".
	//!
	//! Returns the text unchanged when: the player switched numbering off, the label is empty (a bare
	//! "[2]" is noise, not a name), or the result would run past the marker's character limit — the
	//! label the player typed matters more than the counter.
	protected string SM_NumberedCopyText(string src)
	{
		if (src == "" || !SM_ClientPrefs.CopyNumbering())
			return src;

		string stem = SM_CopyStem(src);
		int next = SM_NextFreeCopyNumber(stem);

		string outText;
		if (stem == "")
			outText = string.Format("[%1]", next);	// label was nothing but a counter — keep it that way
		else
			outText = string.Format("%1 [%2]", stem, next);

		if (outText.Length() > SM_TEXT_CHAR_LIMIT)
			return src;	// no room for the suffix — leave the label alone

		return outText;
	}

	//! The label without its trailing [N], e.g. "Rally [3]" -> "Rally". Text that doesn't end in a
	//! counter comes back untouched.
	protected string SM_CopyStem(string src)
	{
		int len = src.Length();
		if (len < 3 || src.Substring(len - 1, 1) != "]")
			return src;

		// Walk back over the digits to the '['. Bounded: a counter nobody will ever reach is not a
		// counter, and the scan must not wander into the label itself ("Point [Alpha]" stays as it is).
		int open = -1;
		for (int i = len - 2; i >= 0 && len - 1 - i <= 6; i--)
		{
			string ch = src.Substring(i, 1);
			if (ch == "[")
			{
				open = i;
				break;
			}
			if (!SM_IsDigit(ch))
				return src;
		}
		if (open < 0 || open >= len - 2)	// need at least one digit between the brackets
			return src;

		string stem = src.Substring(0, open);
		stem.TrimInPlace();		// drop the space before '[', it is re-added when reformatting
		return stem;
	}

	//! Lowest counter not currently taken by one of MY markers sharing this label, starting at 2 (the
	//! un-suffixed original counts as 1). Reusing freed numbers is the point: delete "Rally [2]" and
	//! the next copy fills that gap instead of climbing to [4] forever.
	//!
	//! Only the local player's own markers are considered — someone else's "Rally [3]" must never push
	//! our numbering around. Local-channel markers live in the same store, so they count too.
	protected int SM_NextFreeCopyNumber(string stem)
	{
		SCR_PlayerController pc = SM_LocalPC();
		if (!pc)
			return 2;
		int myId = pc.GetPlayerId();

		array<SM_MapMarkerData> all = {};
		SM_MapMarkerStore.GetInstance().GetAll(all);

		array<int> used = {};
		foreach (SM_MapMarkerData m : all)
		{
			if (!m || m.m_iOwnerId != myId || m.m_sText == "")
				continue;
			if (SM_CopyStem(m.m_sText) != stem)
				continue;
			int n = SM_CopyNumberOf(m.m_sText);
			if (n >= 2)
				used.Insert(n);
		}

		// The copy we handed out a moment ago may still be in flight to the server, so it isn't in the
		// store yet — hold its number anyway. Once it lands this is already in `used` and changes
		// nothing; if the server refused it, the number is simply skipped.
		if (m_iSMCopyIssued >= 2 && m_sSMCopyStem == stem && used.Find(m_iSMCopyIssued) == -1)
			used.Insert(m_iSMCopyIssued);

		int next = 2;
		while (used.Find(next) != -1)
			next++;

		m_sSMCopyStem   = stem;
		m_iSMCopyIssued = next;
		return next;
	}

	//! The [N] a label ends with, or 0 when it carries no counter.
	protected int SM_CopyNumberOf(string src)
	{
		string stem = SM_CopyStem(src);
		if (stem == src)
			return 0;	// nothing was stripped -> no counter

		int len = src.Length();
		int open = len - 2;
		while (open >= 0 && src.Substring(open, 1) != "[")
			open--;
		if (open < 0)
			return 0;
		return src.Substring(open + 1, len - open - 2).ToInt();
	}

	protected bool SM_IsDigit(string ch)
	{
		return ch.Length() == 1 && "0123456789".IndexOf(ch) >= 0;
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

		// Middle of the map widget, expressed MAP-FRAME-locally — that is the space ScreenToWorld takes.
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
		if (!vis || !m_wSMMapFrame)
			return;
		AM_MarkerWidgets.RebuildMain(vis, m_wSMMapFrame);
	}

	protected Widget SM_BuildSymbol(SM_MarkerVisual vis)
	{
		if (!vis || !m_wSMMapFrame)
			return null;
		return AM_MarkerWidgets.BuildSymbol(vis, m_wSMMapFrame);
	}

	protected SCR_MilitarySymbol SM_BuildMilitarySymbol(SM_MapMarkerData d)
	{
		if (!d)
			return null;
		return AM_MarkerWidgets.BuildMilitarySymbol(d);
	}

	protected void SM_RotateChildren(Widget root, float angle)
	{
		AM_MarkerWidgets.RotateChildren(root, angle);
	}

	protected ImageWidget SM_BuildIcon()
	{
		if (!m_wSMMapFrame)
			return null;
		return AM_MarkerWidgets.BuildIcon(m_wSMMapFrame);
	}

	protected TextWidget SM_BuildLabel()
	{
		if (!m_wSMMapFrame)
			return null;
		return AM_MarkerWidgets.BuildLabel(m_wSMMapFrame);
	}

	// Застосовує дані (іконка/колір/поворот/текст) до віджетів візуала.
	protected void SM_ApplyVisualData(SM_MarkerVisual vis)
	{
		if (!vis)
			return;
		AM_MarkerWidgets.Apply(vis);
	}

	//! Refresh the map-widget -> map-frame shift (see m_fSMMapOffX). Zero on the fullscreen map.
	protected void SM_RefreshMapOffset(WorkspaceWidget ws)
	{
		m_fSMMapOffX = 0;
		m_fSMMapOffY = 0;
		if (!m_wSMMapFrame || !ws)
			return;
		CanvasWidget mapW = m_MapEntity.GetMapWidget();
		if (!mapW)
			return;

		float mx, my, fx, fy;
		mapW.GetScreenPos(mx, my);
		m_wSMMapFrame.GetScreenPos(fx, fy);
		m_fSMMapOffX = ws.DPIUnscale(mx - fx);
		m_fSMMapOffY = ws.DPIUnscale(my - fy);
	}

	// Позиціонує+масштабує візуал у світовій точці (спільно для міток і прев'ю).
	// Проєкція мапи → екран; сам вигляд (розмір, підпис, час) кладе AM_MarkerWidgets.Place,
	// той самий, яким користується RT-екран планшета.
	// WorldToScreen's last argument is dpiScale, not a clamp: true, then DPIUnscale to layout units.
	// The result is MAP-WIDGET-local, so it needs the offset to land in the frame our widgets sit in.
	protected void SM_PositionVisual(SM_MarkerVisual vis, int wx, int wy, float factor, WorkspaceWidget ws)
	{
		if (!vis || !vis.m_Data)
			return;

		int sx, sy;
		m_MapEntity.WorldToScreen(wx, wy, sx, sy, true);
		AM_MarkerWidgets.Place(vis,
			ws.DPIUnscale(sx) + m_fSMMapOffX,
			ws.DPIUnscale(sy) + m_fSMMapOffY,
			AM_MarkerWidgets.SizePx(vis.m_Data, factor));
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
		return AM_MarkerWidgets.SizeFactor(sizePercent);
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
		SM_AddHeartButton(ws, AM_MarkerWidgets.HEART_ICON_BASE,     "anarchyHeart1");
		SM_AddHeartButton(ws, AM_MarkerWidgets.HEART_ICON_BASE + 1, "anarchyHeart2");
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
		comp.SetImage(AM_MarkerWidgets.HEART_IMAGESET, quad);
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

	protected bool SM_ResolveCivIcon(int iconEntry, out ResourceName imageset, out string quad)
	{
		return AM_MarkerWidgets.ResolveCivIcon(iconEntry, imageset, quad);
	}

	// One B press = one step back in the template flow, whichever listener it arrives through —
	// AM_Cancel, MenuBack and AM_TplCancel all ride the same button, hence the shared debounce.
	// CONSUMING the press (ResetAction) is the actual protection for the map: without it the very
	// same B walks on to the vanilla map menu and closes it — the context activation alone was not
	// enough, which is exactly how the panel's own two-level back already works.
	protected float m_fSMLastTplBack;
	protected bool SM_TryTemplateBack()
	{
		if (!m_DrawPanel || m_MarkerEditRoot)
			return false;
		if (m_bSMPanelPadNav && !m_DrawPanel.IsModalBusy())
			return false;	// inside the panel SM_TryPanelBack owns B — except when a modal is up
		if (!m_DrawPanel.IsTemplatesOpen() && !m_DrawPanel.IsModalBusy())
			return false;

		// The delete dialog is a menu of its own: it owns B (and A) and closes itself. Eating the press
		// here is what killed its buttons and trapped the pad inside it — hands off entirely.
		if (m_DrawPanel.IsDeleteDialogOpen())
			return false;

		InputManager im = GetGame().GetInputManager();
		bool kbm = !im || im.IsUsingMouseAndKeyboard();

		float now = System.GetTickCount() / 1000.0;
		if (now - m_fSMLastTplBack < 0.15)
			return true;	// the same physical press, delivered again

		bool stepped = false;
		if (!(kbm && m_DrawPanel.IsTypingName()))	// while typing on KB/M the keys belong to the text
			stepped = m_DrawPanel.PressTemplateButton(SM_DrawPanel.ACT_TPL_CANCEL);

		if (!stepped)
		{
			if (kbm)
				return false;	// RMB with nothing to step back: not ours (radial menu and friends)

			// Pad. During the name step an unaccepted B most likely just closed the screen keyboard —
			// swallow it and stay. Anywhere else B closes the TAB, which is what it must not do to
			// the map underneath.
			if (!m_DrawPanel.IsTypingName())
				m_DrawPanel.AbortTemplateFlow();
		}
		// A step that LANDS on the open tab raises the panel's landed flag; the Update consumes it and
		// walks the pad back into the panel. Deliberately not done right here: the same physical press
		// is still on its way to the other B listeners (SM_NavBack among them), and turning pad-nav on
		// mid-dispatch made it take a second step through SM_TryPanelBack. Update runs after the whole
		// input phase, so by then the press is spent.
		m_fSMLastTplBack = now;
		SM_EatBackPress(im, kbm);
		return true;
	}

	protected void SM_EatBackPress(InputManager im, bool kbm)
	{
		if (!im || kbm)
			return;	// RMB does not close the map; only the pad's B needs eating
		im.ResetAction("MenuBack");
		im.ResetAction("AM_Cancel");
		im.ResetAction("AM_TplCancel");
	}

	// Пад-B у панелі малювання: дворівневий вихід (дропдаун -> ряд -> мапа). true = оброблено.
	// Залежно від фокуса B приходить то як MenuBack, то як AM_Cancel, тому кличеться
	// з обох слухачів, а дебаунс не дає одним натиском перескочити рівень.
	protected bool SM_TryPanelBack()
	{
		if (!m_bSMPanelPadNav || !m_DrawPanel)
			return false;
		if (m_DrawPanel.IsModalBusy())
			return false;	// a template modal owns B (SM_TryTemplateBack), even with panel focus alive
		float now = System.GetTickCount() / 1000.0;
		if (now - m_fSMLastPanelBack < 0.15)
			return true;
		m_fSMLastPanelBack = now;

		if (!m_DrawPanel.HandleBack())
			SM_PanelExit();
		InputManager im = GetGame().GetInputManager();
		im.ResetAction("MenuBack");
		im.ResetAction("AM_Cancel");
		return true;
	}

	// Дзеркало SM_PanelExit: завести пад У панель — пад-режим, кнопки фокусні, фокус на target.
	// Used when a template flow ends on the open tab (a cancel, a save, the delete dialog closing):
	// the panel the player stepped back into has to actually HOLD the pad, or it sits on screen
	// unfocusable with the map's idle "open the panel" hint floating over it.
	protected void SM_PanelEnterPad(Widget target)
	{
		if (!m_DrawPanel)
			return;
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;
		if (!target)
			target = m_DrawPanel.GetFirstFocusTarget();
		if (!target)
			return;
		m_bSMPanelPadNav = true;
		m_DrawPanel.SetPadFocusMode(true);	// before the focus: the slots are focusable only in pad mode
		ws.SetFocusedWidget(target);
		m_DrawPanel.NotifyPadEntered();		// swallow the press that walked us in
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

	// Чи курсор біля точки натискання (в межах порога) — щоб відрізнити клік/утримання від перетягування.
	protected bool SM_CursorNearPress()
	{
		float dx = SCR_MapCursorInfo.x - m_fSMPressX;
		float dy = SCR_MapCursorInfo.y - m_fSMPressY;
		return (dx * dx + dy * dy) <= SM_MOVE_THRESHOLD * SM_MOVE_THRESHOLD;
	}

	// Світова позиція під курсором (канонічний ванільний патерн; координати курсора статичні).
	// ScreenToWorld wants MAP-FRAME-local pixels (not map-widget: the two are NOT inverses of each
	// other, see the note on m_fSMMapOffX), and SCR_MapCursorInfo is not a screen position on an
	// inset map at all — SM_DrawCanvas.CursorInFrame sorts both out.
	protected bool SM_GetCursorWorld(out int worldX, out int worldY)
	{
		// Prefer the canvas: it inverts the affine we actually RENDER with, so the world point is
		// guaranteed to sit under the brush circle. ScreenToWorld only agrees with it on a fullscreen
		// map — on an inset one the two disagree by a few pixels, which a thin brush makes obvious.
		if (m_DrawCanvas && m_DrawCanvas.CursorWorld(worldX, worldY))
			return true;

		float cfx, cfy;
		if (!SM_DrawCanvas.CursorInFrame(m_wSMMapFrame, cfx, cfy))
			return false;

		float wx, wy;
		m_MapEntity.ScreenToWorld(cfx, cfy, wx, wy);
		worldX = wx;
		worldY = wy;
		return true;
	}

	//! The cursor in PHYSICAL SCREEN pixels — what FindStrokeAtScreen expects.
	protected void SM_CursorPhysPx(out int px, out int py)
	{
		float fx, fy;
		SM_DrawCanvas.CursorPhys(m_wSMMapFrame, fx, fy);
		px = fx;
		py = fy;
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

		// The WorldToScreen below lands in MAP-WIDGET units, so put the cursor in that space once
		// rather than converting every marker. Identical on the fullscreen map.
		float cpx, cpy;
		if (!SM_DrawCanvas.CursorPhys(m_wSMMapFrame, cpx, cpy))
			return null;
		CanvasWidget mapWHit = m_MapEntity.GetMapWidget();
		if (mapWHit)
		{
			float mwx, mwy;
			mapWHit.GetScreenPos(mwx, mwy);
			cpx -= mwx;
			cpy -= mwy;
		}
		float cx = ws.DPIUnscale(cpx);
		float cy = ws.DPIUnscale(cpy);

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

			float half = AM_MarkerWidgets.BASE_SIZE * SM_SizeFactor(vis.m_Data.m_iSize) * factor * 0.5;
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
		c.SetAction("AM_CopyLastPad");	// гліф → Y (бренд-залежний: Xbox Y / PS △), оновлює візуал
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
		bool placeNow = pim && pim.GetActionValue("AM_CopyLastPad") > 0.5;
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
		if (SM_TryTemplateBack())	// B у темплейт-флоу: крок назад/закрити вкладку, НЕ мапу
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
		if (id >= AM_MarkerWidgets.HEART_ICON_BASE)
		{
			if (m_SelectedIconButton)
				m_SelectedIconButton.ColorizeBackground();
			component.ColorizeBackground();
			m_SelectedIconButton = component;
			m_iSelectedIconID = id;

			string quad;
			if (id == AM_MarkerWidgets.HEART_ICON_BASE)
				quad = "anarchyHeart1";
			else
				quad = "anarchyHeart2";
			if (m_wMarkerPreview)
				m_wMarkerPreview.LoadImageFromSet(0, AM_MarkerWidgets.HEART_IMAGESET, quad);
			if (m_wMarkerPreviewGlow)
				m_wMarkerPreviewGlow.LoadImageFromSet(0, AM_MarkerWidgets.HEART_IMAGESET, quad);
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
		TimeAndWeatherManagerEntity tw = world.GetTimeAndWeatherManager();
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
		return AM_MarkerWidgets.DateTimeString(date, time);
	}

	// yyyymmdd → "DD.MM.YYYY".
	protected string SM_DateString(int date)
	{
		return AM_MarkerWidgets.DateString(date);
	}

	protected string SM_Pad2(int v)
	{
		return AM_MarkerWidgets.Pad2(v);
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

// B on the pad, out on the map with a template armed: it cancels the placement (AM_TplCancel is in
// MapContext) AND closes the map. Stepping back WITHOUT closing has been tried twice and both ways are
// dead ends — do not go around this loop again:
//
//  * Activating MapMarkerEditContext while the tab is open: that context is BLOCKING — it replaces the
//    map's own, and the map's is what carries the stick panning. The camera froze exactly while a ghost
//    was being aimed. (Do not confuse this with AMMapContext, which the Update activates every frame:
//    that one is ours and non-blocking — it ADDS our actions everywhere without displacing anything.
//    The distinction is the blocking flag, not the activation itself.)
//  * Blocking SCR_MapEntity.CloseMap: CloseMap is the LAST step of the teardown — SCR_MapMenuUI closes
//    the menu first and only then asks the entity to close. Refusing it leaves the menu gone and the
//    entity alive, and the next stick pan walks that half-dead map into a null (FitPanBounds <- SetPan
//    <- Pan <- OnInputPanHGamepad).
//
// The way OUT of the loop (implemented): the blocking context, but only for the frames B is physically
// held over a cancelable state, plus the same shield fired from the AM_Cancel listener at input-dispatch
// time (with a MenuBack reset). Panning starves only for the duration of the cancel press itself, which
// is imperceptible — the dead end above was keeping the context up while a ghost was being AIMED.
// See the activation block in SM_MapMarkerLayerInput.c Update and SM_OnContext.
