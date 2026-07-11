// === Фаза 2, під-крок 1: toggle-кнопка «видимість міток» у тулбарі Game Master ===
//
// Додаємо кнопку ДОТОЧУВАННЯМ у список дій тулбара (modded SCR_ToolbarActionsEditorComponentClass),
// НЕ замінюючи префаб режиму редактора — сумісно з ваніллю та іншими модами.
// Кнопка перемикає КЛІЄНТСЬКИЙ прапорець SM_GmState.s_bMarkerView (дефолт OFF). Серверну видимість
// і CanModify підключимо в наступних під-кроках; поки кнопка лише тримає стан + дає подію.

//------------------------------------------------------------------------------------------------
//! Глобальний клієнтський стан зевса: чи показувати мітки гравців у GM-мапі.
class SM_GmState
{
	static bool s_bMarkerView = false;	// дефолт: вимкнено
	static ref ScriptInvoker s_OnMarkerViewChanged = new ScriptInvoker();	// (bool newValue)

	// Зевс натиснув Create Marker — чекаємо клік по мапі, щоб задати точку й відкрити діалог.
	static bool s_bCreatePending = false;

	// --- Малювання в GM-мапі ---
	static bool s_bDrawView = false;	// показ малюнків гравців (Side + Global) у GM-мапі
	static ref ScriptInvoker s_OnDrawViewChanged = new ScriptInvoker();	// (bool newValue)
	static bool s_bDrawPanel = false;	// показ панелі малювання (інтерфейсу) в GM-мапі
	static ref ScriptInvoker s_OnDrawPanelChanged = new ScriptInvoker();	// (bool newValue)

	// Вибір зевса для нових штрихів (панель-контроли; привʼязуються з layout):
	static int  s_iDrawSideChannel = -1;	// індекс фракції для Side-штриха (-1 = не обрано)
	static bool s_bDrawGmLock   = false;	// заборонити гравцям стирати штрих зевса
	static bool s_bDrawHideInfo = false;	// ховати "Drawn by" від гравців

	static void SetMarkerView(bool v)
	{
		if (v == s_bMarkerView)
			return;
		s_bMarkerView = v;
		s_OnMarkerViewChanged.Invoke(v);
	}

	static void SetDrawView(bool v)
	{
		if (v == s_bDrawView)
			return;
		s_bDrawView = v;
		s_OnDrawViewChanged.Invoke(v);
	}

	static void SetDrawPanel(bool v)
	{
		if (v == s_bDrawPanel)
			return;
		s_bDrawPanel = v;
		s_OnDrawPanelChanged.Invoke(v);
	}
}

//------------------------------------------------------------------------------------------------
//! Toggle-кнопка тулбара: показати/сховати мітки гравців (канали Side + Global) у мапі Game Master.
[BaseContainerProps(), SCR_BaseContainerCustomTitleUIInfo("m_Info")]
class SM_ToggleMarkerVisibilityToolbarAction : SCR_BaseToggleToolbarAction
{
	protected const ResourceName ICON_SET = "{3262679C50EF4F01}UI/Textures/Icons/icons_wrapperUI.imageset";
	protected const string ICON_QUAD = "scenarios";

	void SM_ToggleMarkerVisibilityToolbarAction()
	{
		// Тип TOGGLE — щоб тулбар обрав префаб кнопки з toggle-віджетом (інакше підсвітка ON не працює;
		// у ваніллі цей тип задає конфіг, а в нас інстанс через 'new', тож задаємо в коді).
		m_ActionType = EEditorActionType.TOGGLE;

		// Іконки/підписи станів задаємо в коді (інстанс створюється через 'new', без конфіг-атрибутів).
		m_Info        = SCR_UIInfo.CreateInfo("Player markers", "Show player markers (Side + Global)", ICON_SET, ICON_QUAD);
		m_InfoToggled = SCR_UIInfo.CreateInfo("Player markers", "Hide player markers", ICON_SET, ICON_QUAD);
	}

	override bool IsServer()
	{
		return false;
	}

	override bool CanBeShown(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
	{
		return true;	// кнопка існує лише в тулбарі редактора (тобто тільки для зевса)
	}

	override bool CanBePerformed(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
	{
		return true;
	}

	override void Perform(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags, int param = -1)
	{
		bool newVal = !SM_GmState.s_bMarkerView;
		SM_GmState.SetMarkerView(newVal);	// локально (рендер-фільтр + візуал кнопки)

		// Повідомити сервер (дедік): дозволити отримувати Side+Global мітки. На хості — стор спільний.
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (pc)
			pc.SM_RequestGmView(newVal);
	}

	// Track/Untrack — коли з'являється/зникає GUI-представлення кнопки: синхронізуємо візуал із станом.
	override void Track()
	{
		SyncVisual(SM_GmState.s_bMarkerView);
		SM_GmState.s_OnMarkerViewChanged.Insert(OnMarkerViewChanged);
	}

	override void Untrack()
	{
		SM_GmState.s_OnMarkerViewChanged.Remove(OnMarkerViewChanged);
	}

	protected void OnMarkerViewChanged(bool v)
	{
		SyncVisual(v);
	}

	protected void SyncVisual(bool on)
	{
		int v = 0;
		if (on)
			v = 1;
		Toggle(v, on);	// value + highlight (підсвітка ON)
	}
}

//------------------------------------------------------------------------------------------------
//! Кнопка тулбара: створити мітку. Завжди доступна; вмикає видимість (щоб зевс бачив результат) і
//! переводить у режим розміщення — наступний клік по мапі задасть точку й відкриє наш діалог.
[BaseContainerProps(), SCR_BaseContainerCustomTitleUIInfo("m_Info")]
class SM_CreateMarkerToolbarAction : SCR_EditorToolbarAction
{
	protected const ResourceName ICON_SET = "{E23427CAC80DA8B7}UI/Textures/Icons/icons_mapMarkersUI.imageset";
	protected const string ICON_QUAD = "target-reference-point-2";

	void SM_CreateMarkerToolbarAction()
	{
		m_Info = SCR_UIInfo.CreateInfo("Create marker", "Place a marker: click a point on the map", ICON_SET, ICON_QUAD);
	}

	override bool IsServer()
	{
		return false;
	}

	override bool CanBeShown(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
	{
		return true;
	}

	override bool CanBePerformed(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
	{
		return true;
	}

	override void Perform(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags, int param = -1)
	{
		// Вмикаємо видимість (щоб зевс бачив свою мітку) + повідомляємо сервер.
		SM_GmState.SetMarkerView(true);
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (pc)
			pc.SM_RequestGmView(true);

		SM_GmState.s_bCreatePending = true;	// наступний клік по мапі задасть точку
	}
}

//------------------------------------------------------------------------------------------------
//! Toggle-кнопка тулбара: показати/сховати МАЛЮНКИ гравців (Side + Global) у мапі Game Master.
[BaseContainerProps(), SCR_BaseContainerCustomTitleUIInfo("m_Info")]
class SM_ToggleDrawingVisibilityToolbarAction : SCR_BaseToggleToolbarAction
{
	protected const ResourceName ICON_TEX = "{9FF200C076288DA5}picture.edds";	// власна іконка «видимість малюнків»

	void SM_ToggleDrawingVisibilityToolbarAction()
	{
		m_ActionType  = EEditorActionType.TOGGLE;
		m_Info        = SCR_UIInfo.CreateInfo("Player drawings", "Show player map drawings (Side + Global)", ICON_TEX);
		m_InfoToggled = SCR_UIInfo.CreateInfo("Player drawings", "Hide player map drawings", ICON_TEX);
	}

	override bool IsServer()
	{
		return false;
	}

	override bool CanBeShown(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
	{
		return true;
	}

	override bool CanBePerformed(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
	{
		return true;
	}

	override void Perform(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags, int param = -1)
	{
		bool newVal = !SM_GmState.s_bDrawView;
		SM_GmState.SetDrawView(newVal);	// локально (рендер-фільтр полотна + візуал кнопки)

		// Повідомити сервер (дедік): дозволити отримувати Side+Global малюнки.
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (pc)
			pc.SM_RequestGmDrawView(newVal);
	}

	override void Track()
	{
		SyncVisual(SM_GmState.s_bDrawView);
		SM_GmState.s_OnDrawViewChanged.Insert(OnDrawViewChanged);
	}

	override void Untrack()
	{
		SM_GmState.s_OnDrawViewChanged.Remove(OnDrawViewChanged);
	}

	protected void OnDrawViewChanged(bool v)
	{
		SyncVisual(v);
	}

	protected void SyncVisual(bool on)
	{
		int v = 0;
		if (on)
			v = 1;
		Toggle(v, on);
	}
}

//------------------------------------------------------------------------------------------------
//! Toggle-кнопка тулбара: показати/сховати ПАНЕЛЬ малювання (інтерфейс) у мапі Game Master.
//! Вмикає заодно й показ малюнків — інакше зевс малював би «всліпу».
[BaseContainerProps(), SCR_BaseContainerCustomTitleUIInfo("m_Info")]
class SM_ToggleDrawPanelToolbarAction : SCR_BaseToggleToolbarAction
{
	protected const ResourceName ICON_TEX = "{DA1D66E21EE3F88A}brush.edds";	// власна іконка «панель малювання»

	void SM_ToggleDrawPanelToolbarAction()
	{
		m_ActionType  = EEditorActionType.TOGGLE;
		m_Info        = SCR_UIInfo.CreateInfo("Drawing tools", "Show the map drawing toolbar", ICON_TEX);
		m_InfoToggled = SCR_UIInfo.CreateInfo("Drawing tools", "Hide the map drawing toolbar", ICON_TEX);
	}

	override bool IsServer()
	{
		return false;
	}

	override bool CanBeShown(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
	{
		return true;
	}

	override bool CanBePerformed(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
	{
		return true;
	}

	override void Perform(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags, int param = -1)
	{
		bool newVal = !SM_GmState.s_bDrawPanel;
		SM_GmState.SetDrawPanel(newVal);

		if (newVal)	// відкрили інструменти — увімкнути й показ малюнків (локально + сервер)
		{
			SM_GmState.SetDrawView(true);
			SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
			if (pc)
				pc.SM_RequestGmDrawView(true);
		}
	}

	override void Track()
	{
		SyncVisual(SM_GmState.s_bDrawPanel);
		SM_GmState.s_OnDrawPanelChanged.Insert(OnDrawPanelChanged);
	}

	override void Untrack()
	{
		SM_GmState.s_OnDrawPanelChanged.Remove(OnDrawPanelChanged);
	}

	protected void OnDrawPanelChanged(bool v)
	{
		SyncVisual(v);
	}

	protected void SyncVisual(bool on)
	{
		int v = 0;
		if (on)
			v = 1;
		Toggle(v, on);
	}
}

//------------------------------------------------------------------------------------------------
//! Сумісне доточування наших кнопок у тулбар редактора (без заміни префаба).
//! ВАЖЛИВО: не чіпаємо m_ActionsSorted у класі даних — його напряму ітерує SetShortcuts, і будь-яке
//! втручання в конструктор класу ламало його (null-елемент). Натомість перевизначаємо GetActions()
//! на КОМПОНЕНТІ (це джерело дій для тулбар-UI) і доточуємо наш кешований екземпляр у вивід.
//! Так SetShortcuts нашої дії не бачить (вона й не потребує шорткату), а кнопка в тулбарі є.
modded class SCR_ToolbarActionsEditorComponent
{
	protected static ref SM_ToggleMarkerVisibilityToolbarAction s_SMMarkerAction;
	protected static ref SM_CreateMarkerToolbarAction s_SMCreateAction;
	protected static ref SM_ToggleDrawingVisibilityToolbarAction s_SMDrawViewAction;
	protected static ref SM_ToggleDrawPanelToolbarAction s_SMDrawPanelAction;

	override int GetActions(out notnull array<SCR_BaseEditorAction> actions)
	{
		int n = super.GetActions(actions);

		if (!s_SMMarkerAction)
			s_SMMarkerAction = new SM_ToggleMarkerVisibilityToolbarAction();	// один екземпляр на сесію
		if (!s_SMCreateAction)
			s_SMCreateAction = new SM_CreateMarkerToolbarAction();
		if (!s_SMDrawViewAction)
			s_SMDrawViewAction = new SM_ToggleDrawingVisibilityToolbarAction();
		if (!s_SMDrawPanelAction)
			s_SMDrawPanelAction = new SM_ToggleDrawPanelToolbarAction();

		actions.Insert(s_SMMarkerAction);
		actions.Insert(s_SMCreateAction);
		actions.Insert(s_SMDrawViewAction);
		actions.Insert(s_SMDrawPanelAction);
		return n + 4;
	}
}
