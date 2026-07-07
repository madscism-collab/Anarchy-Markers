// Мережевий шар. Живе на modded SCR_PlayerController — це найстабільніший RPC-клас
// (кожен клієнт володіє своїм PlayerController), не залежить від ванільних марк-класів
// і не вимагає правок префабів.
// Потік: клієнт просить сервер (RplRcver.Server) -> сервер призначає/знаходить id у сховищі
// -> розсилає кожному, кому видно (RplRcver.Owner) -> клієнтське дзеркало оновлюється по id.
// Уся дедуплікація по id, тож повторний/застарілий RPC дубля не дасть. Один маркер = один RPC
// (array<int>+text), без розбиття на два пакети — нема гонки порядку.

modded class SCR_PlayerController
{
	protected bool m_bSMHasSynced = false;	// первинна синхронізація вже була
	protected bool m_bSMGroupSubscribed = false;	// підписані на події групи
	protected int  m_iSMLastFaction = -999;	// остання відома фракція клієнта (для детекту зміни)
	protected int  m_iSMLastGroup   = -999;	// остання відома група клієнта
	protected const int SM_SYNC_DELAY_MS = 1000;

	// Будь-яка зміна керованої сутності (спавн/реконект/респавн при зміні сторони) -> перевірити синк.
	// Сам перерахунок робить SM_CheckResync (звіряє фракцію/групу), тож тут просто плануємо перевірку.
	override void OnControlledEntityChanged(IEntity from, IEntity to)
	{
		super.OnControlledEntityChanged(from, to);

		if (!to || Replication.IsServer())	// на сервері/хості сховище авторитетне — клієнтський синк не потрібен
			return;

		// Підписка на зміну групи (наживо, без респавна) — один раз.
		if (!m_bSMGroupSubscribed)
		{
			m_bSMGroupSubscribed = true;
			SCR_AIGroup.GetOnPlayerAdded().Insert(SM_OnGroupChanged);
			SCR_AIGroup.GetOnPlayerRemoved().Insert(SM_OnGroupChanged);
		}

		GetGame().GetCallqueue().Remove(SM_CheckResync);
		GetGame().GetCallqueue().CallLater(SM_CheckResync, SM_SYNC_DELAY_MS, false);
	}

	// Подія зміни складу будь-якої групи. Реагуємо лише якщо це НАШ гравець.
	protected void SM_OnGroupChanged(SCR_AIGroup group, int playerID)
	{
		if (Replication.IsServer() || playerID != GetPlayerId())
			return;
		GetGame().GetCallqueue().Remove(SM_CheckResync);
		GetGame().GetCallqueue().CallLater(SM_CheckResync, SM_SYNC_DELAY_MS, false);
	}

	// Звіряє поточні фракцію/групу з останніми відомими. Перший раз — просто повний синк.
	// Якщо змінились — чистимо локальне дзеркало і просимо сервер надіслати лише дозволене заново.
	protected void SM_CheckResync()
	{
		if (Replication.IsServer())
			return;

		int f = SM_MarkerNet.GetPlayerFactionIndex(GetPlayerId());
		int g = SM_MarkerNet.GetPlayerGroupId(GetPlayerId());

		if (!m_bSMHasSynced)	// перший спавн/реконект — початковий повний синк
		{
			m_bSMHasSynced = true;
			m_iSMLastFaction = f;
			m_iSMLastGroup = g;
			SM_RequestSync();
			return;
		}

		if (f == m_iSMLastFaction && g == m_iSMLastGroup)
			return;	// нічого важливого не змінилось

		if (f == -1 && g == -1)
			return;	// транзитний стан / Zeus без афіляції — мітки не чіпаємо

		m_iSMLastFaction = f;
		m_iSMLastGroup = g;
		SM_MarkerNet.Log(string.Format("CLIENT channel changed (faction=%1 group=%2) -> clear + resync", f, g));
		SM_MapMarkerStore.GetInstance().Clear();	// прибрати застарілі мітки старого каналу
		SM_MapDrawingStore.GetInstance().Clear();	// так само застарілі малюнки
		SM_RequestSync();	// сервер надішле лише ті, які тепер дозволено бачити
	}

	void ~SCR_PlayerController()
	{
		if (m_bSMGroupSubscribed)
		{
			SCR_AIGroup.GetOnPlayerAdded().Remove(SM_OnGroupChanged);
			SCR_AIGroup.GetOnPlayerRemoved().Remove(SM_OnGroupChanged);
		}
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(SM_CheckResync);
	}

	// --- Клієнт -> сервер: запити (кличе адаптер мапи на власному PC) ---

	void SM_RequestPlace(array<int> packed, string text)
	{
		Rpc(RpcAsk_Place, packed, text);
	}

	void SM_RequestMove(int id, int posX, int posY)
	{
		Rpc(RpcAsk_Move, id, posX, posY);
	}

	void SM_RequestEdit(int id, array<int> packed, string text)
	{
		Rpc(RpcAsk_Edit, id, packed, text);
	}

	void SM_RequestRemove(int id)
	{
		Rpc(RpcAsk_Remove, id);
	}

	void SM_RequestSync()
	{
		if (Replication.IsServer())
			return;
		SM_MarkerNet.Log("CLIENT sync request sent");
		Rpc(RpcAsk_RequestSync);
	}

	// Зевс увімкнув/вимкнув перегляд міток (кличе кнопка тулбара на локальному PC).
	void SM_RequestGmView(bool enable)
	{
		if (Replication.IsServer())
		{
			// Хост: стор спільний і вже повний — рендер-фільтр у GM-мапі сам покаже Side+Global.
			SM_MarkerNet.SetGmViewer(GetPlayerId(), enable);
			return;
		}
		Rpc(RpcAsk_SetGmView, enable);
		if (!enable)
		{
			// Вимкнули GM-перегляд: прибираємо GM-only мітки з дзеркала й тягнемо лише легітимні.
			SM_MapMarkerStore.GetInstance().Clear();
			SM_RequestSync();
		}
	}

	// Зевс увімкнув/вимкнув перегляд МАЛЮНКІВ (окрема кнопка тулбара, окремий серверний прапорець).
	void SM_RequestGmDrawView(bool enable)
	{
		if (Replication.IsServer())
		{
			SM_DrawingNet.SetGmDrawViewer(GetPlayerId(), enable);	// хост: стор спільний, рендер-фільтр сам покаже
			return;
		}
		Rpc(RpcAsk_SetGmDrawView, enable);
		if (!enable)
		{
			// Вимкнули: прибираємо GM-only малюнки з дзеркала й тягнемо лише легітимні.
			SM_MapDrawingStore.GetInstance().Clear();
			SM_RequestSync();	// сервер дошле мітки (no-op зміни) + малюнки за звичайним допуском
		}
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_SetGmDrawView(bool enable)
	{
		if (!Replication.IsServer())
			return;
		SM_DrawingNet.SetGmDrawViewer(GetPlayerId(), enable);
		if (enable)
			SM_DrawingNet.SendAllDrawingsTo(this);	// дозаповнити дзеркало Side+Global малюнками
	}

	// --- Сервер -> клієнт: відправники (сервер кличе на PC цільового гравця;
	// RplRcver.Owner доставляє RPC власнику цього PC) ---

	void SM_SendUpsert(int id, int owner, array<int> packed, string text, string editor)
	{
		Rpc(RpcDo_Upsert, id, owner, packed, text, editor);
	}

	void SM_SendMove(int id, int posX, int posY)
	{
		Rpc(RpcDo_Move, id, posX, posY);
	}

	void SM_SendRemove(int id)
	{
		Rpc(RpcDo_Remove, id);
	}

	// Відмова в розміщенні — показуємо власнику локальне повідомлення (який саме ліміт).
	// На listen-server/у редакторі хост = сервер і клієнт водночас: RPC RplRcver.Owner до самого себе
	// не виконується (та й гард IsServer його б зрізав), тож показуємо попап напряму. На виділеному
	// сервері локального гравця нема (GetPlayerController()==null) → завжди шлемо RPC віддаленому клієнту.
	void SM_SendPlaceDenied(SM_EPlaceDenyReason reason, int limit)
	{
		if (GetGame().GetPlayerController() == this)
			SM_ShowPlaceDenied(reason, limit);	// хост — показуємо напряму
		else
			Rpc(RpcDo_PlaceDenied, reason, limit);
	}

	// Локальний показ повідомлення про відмову (кличе і RPC-обробник, і прямий шлях на хості).
	void SM_ShowPlaceDenied(SM_EPlaceDenyReason reason, int limit)
	{
		string msg;
		switch (reason)
		{
			case SM_EPlaceDenyReason.PER_MINUTE_LIMIT:
				msg = string.Format("You're placing markers too fast. This server allows up to %1 per minute per player. Wait a moment.", limit);
				break;
			case SM_EPlaceDenyReason.PER_PLAYER_LIMIT:
				msg = string.Format("You've reached this server's per-player marker limit (%1). Delete one of yours to place a new marker.", limit);
				break;
			case SM_EPlaceDenyReason.TOTAL_LIMIT:
				msg = string.Format("This server's total marker limit (%1) is reached. You can't place a new marker right now.", limit);
				break;
			case SM_EPlaceDenyReason.MARKER_LOCKED:
				msg = "This marker is locked by the Game Master. You can't move, edit or delete it.";
				break;
			case SM_EPlaceDenyReason.DRAW_PER_MINUTE_LIMIT:
				msg = string.Format("You're drawing too fast. This server allows up to %1 strokes per minute per player. Wait a moment.", limit);
				break;
			case SM_EPlaceDenyReason.DRAW_PER_PLAYER_LIMIT:
				msg = string.Format("You've reached this server's per-player drawing limit (%1 strokes). Erase some of yours to draw more.", limit);
				break;
			case SM_EPlaceDenyReason.DRAW_TOTAL_LIMIT:
				msg = string.Format("This server's total drawing limit (%1 strokes) is reached. You can't draw right now.", limit);
				break;
			case SM_EPlaceDenyReason.DRAW_CHANNEL_DISABLED:
				msg = "This visibility channel is disabled for drawings on this server. Pick another channel.";
				break;
			case SM_EPlaceDenyReason.DRAW_LOCKED:
				msg = "This drawing is locked by the Game Master. You can't erase it.";
				break;
			case SM_EPlaceDenyReason.FILL_NOT_CLOSED:
				msg = "The area isn't enclosed — paint leaks out. Close the outline with lines and try again.";
				break;
			case SM_EPlaceDenyReason.FILL_BLOCKED:
				msg = "No room to fill here. Click inside an enclosed area.";
				break;
			case SM_EPlaceDenyReason.FILL_NO_NARROW:
				msg = "A fill's visibility can only be widened, not narrowed. Channel kept, other changes applied.";
				break;
			default:
				msg = "This server doesn't allow placing a marker right now.";
		}

		SM_ShowLocalMessage(msg, 2.0);	// власне повідомлення: миттєве, без анімації, коротке

		// Звук при появі повідомлення (ядрова UI-подія HINT — точно відтворюється).
		SCR_UISoundEntity.SoundEvent(SCR_SoundEvent.HINT);
	}

	protected Widget m_wSMMsgRoot;	// власне повідомлення на екрані (одне за раз)

	// Показати коротке повідомлення по центрі-зверху. Без анімації: миттєво з'являється і зникає
	// через seconds (на відміну від SCR_PopUpNotification, який ще й fade-ить).
	void SM_ShowLocalMessage(string text, float seconds)
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;

		SM_HideMsg();	// прибрати попереднє, якщо висить

		TextWidget t = TextWidget.Cast(ws.CreateWidget(
			WidgetType.TextWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.NOWRAP | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(0xFFFF7020), 0, ws));	// помаранчевий (помітно)
		if (!t)
			return;

		t.SetVisible(true);
		t.SetText(text);
		t.SetFont("{3E7733BAC8C831F6}UI/Fonts/RobotoCondensed/RobotoCondensed_Regular.fnt");
		t.SetExactFontSize(26);
		FrameSlot.SetAnchorMin(t, 0.5, 0.2);
		FrameSlot.SetAnchorMax(t, 0.5, 0.2);
		FrameSlot.SetAlignment(t, 0.5, 0.5);	// центр по горизонталі, прив'язка зверху
		FrameSlot.SetSizeToContent(t, true);
		m_wSMMsgRoot = t;

		GetGame().GetCallqueue().Remove(SM_HideMsg);
		GetGame().GetCallqueue().CallLater(SM_HideMsg, seconds * 1000, false);
	}

	void SM_HideMsg()
	{
		if (m_wSMMsgRoot)
		{
			m_wSMMsgRoot.RemoveFromHierarchy();
			m_wSMMsgRoot = null;
		}
	}

	// --- RPC клієнт -> сервер (this = серверна копія PC того, хто просить) ---

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Place(array<int> packed, string text)
	{
		if (!Replication.IsServer())
			return;

		int requesterId = GetPlayerId();
		SM_MarkerConfig cfg = SM_MarkerConfig.GetInstance();
		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();

		// Анти-спам: рахуємо спробу за хвилину (веде попередження для адмінів) і застосовуємо ліміт.
		if (!SM_MarkerNet.RegisterPlaceAttempt(requesterId))
		{
			SM_MarkerNet.Log(string.Format("DENY PLACE by %1: per-minute limit %2 reached", SM_MarkerNet.Who(requesterId), cfg.m_iPerMinuteLimit));
			SM_SendPlaceDenied(SM_EPlaceDenyReason.PER_MINUTE_LIMIT, cfg.m_iPerMinuteLimit);
			return;
		}

		// Канал дозволено? (перевіряємо до створення — клієнтський UI це теж блокує, тут страховка)
		int wantVis = SM_MapMarkerData.VisibilityFromPacked(packed);
		if (!cfg.IsVisibilityAllowed(wantVis))
		{
			SM_MarkerNet.Log(string.Format("DENY PLACE by %1: channel %2 disabled by config", SM_MarkerNet.Who(requesterId), SM_MarkerNet.VisName(wantVis)));
			return;
		}

		// Ліміти кількості міток (0 = без обмеження)
		if (cfg.m_iTotalLimit > 0 && store.Count() >= cfg.m_iTotalLimit)
		{
			SM_MarkerNet.Log(string.Format("DENY PLACE by %1: total limit %2 reached", SM_MarkerNet.Who(requesterId), cfg.m_iTotalLimit));
			SM_SendPlaceDenied(SM_EPlaceDenyReason.TOTAL_LIMIT, cfg.m_iTotalLimit);
			return;
		}
		if (cfg.m_iPerPlayerLimit > 0 && store.CountByOwner(requesterId) >= cfg.m_iPerPlayerLimit)
		{
			SM_MarkerNet.Log(string.Format("DENY PLACE by %1: per-player limit %2 reached", SM_MarkerNet.Who(requesterId), cfg.m_iPerPlayerLimit));
			SM_SendPlaceDenied(SM_EPlaceDenyReason.PER_PLAYER_LIMIT, cfg.m_iPerPlayerLimit);
			return;
		}

		SM_MapMarkerData data = store.ServerCreate(requesterId, packed, text);
		if (!data)
			return;

		if (cfg.m_bShowLastEditor)
			data.m_sLastEditor = GetGame().GetPlayerManager().GetPlayerName(requesterId);	// ім'я автора (для тултіпа, переживає рестарт)
		if (data.m_iDate != 0)
			SM_MarkerNet.GetGameTime(data.m_iDate, data.m_iTime);	// час ставить сервер (за конфігом: сценарій/реальний)
		// Зазвичай канал задає сервер за фракцією того, хто ставить. Але GM фракції не має й сам обирає
		// сторону для Side-мітки — тоді лишаємо канал із даних (його надсилає GM-дропдаун).
		if (!(SM_MarkerNet.IsPlayerGameMaster(requesterId) && data.m_iVisibility == SM_EMarkerVisibility.FACTION))
			SM_MarkerNet.AssignChannel(requesterId, data);
		SM_MarkerNet.BroadcastUpsert(data);

		// у лозі видно vis/channel і що повертає faction/group API (найризикованіше місце)
		SM_MarkerNet.Log(string.Format("PLACE #%1 by %2 vis=%3 ch=%4 (faction=%5 group=%6) kind=%7",
			data.m_iId, SM_MarkerNet.Who(requesterId), SM_MarkerNet.VisName(data.m_iVisibility), data.m_iChannel,
			SM_MarkerNet.GetPlayerFactionIndex(requesterId), SM_MarkerNet.GetPlayerGroupId(requesterId), data.m_iKind));
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Move(int id, int posX, int posY)
	{
		if (!Replication.IsServer())
			return;

		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
		SM_MapMarkerData m = store.FindById(id);
		if (!m)
			return;
		if (!SM_MarkerNet.CanModify(GetPlayerId(), m))
		{
			SM_MarkerNet.Log(string.Format("DENY MOVE #%1 by %2 (vis=%3 ch=%4)", id, SM_MarkerNet.Who(GetPlayerId()), SM_MarkerNet.VisName(m.m_iVisibility), m.m_iChannel));
			if (m.m_iGmLocked != 0 && !SM_MarkerNet.IsPlayerGameMaster(GetPlayerId()))
				SM_SendPlaceDenied(SM_EPlaceDenyReason.MARKER_LOCKED, 0);
			return;
		}

		m.m_iLastEditorId = GetPlayerId();	// хто посунув = останній редактор
		if (SM_MarkerConfig.GetInstance().m_bShowLastEditor)
			m.m_sLastEditor = GetGame().GetPlayerManager().GetPlayerName(GetPlayerId());
		// якщо в мітки є позначка часу — оновлюємо її до ServerMove
		if (m.m_iDate != 0)
			SM_MarkerNet.GetGameTime(m.m_iDate, m.m_iTime);

		if (!store.ServerMove(id, posX, posY))
			return;

		SM_MarkerNet.BroadcastUpsert(m);	// шлемо повні дані (позиція + час + редактор)
		SM_MarkerNet.Log(string.Format("MOVE #%1 by %2 -> (%3, %4)", id, SM_MarkerNet.Who(GetPlayerId()), posX, posY));
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Edit(int id, array<int> packed, string text)
	{
		if (!Replication.IsServer())
			return;

		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
		SM_MapMarkerData m = store.FindById(id);
		if (!m)
			return;
		if (!SM_MarkerNet.CanModify(GetPlayerId(), m))
		{
			SM_MarkerNet.Log(string.Format("DENY EDIT #%1 by %2 (vis=%3 ch=%4)", id, SM_MarkerNet.Who(GetPlayerId()), SM_MarkerNet.VisName(m.m_iVisibility), m.m_iChannel));
			if (m.m_iGmLocked != 0 && !SM_MarkerNet.IsPlayerGameMaster(GetPlayerId()))
				SM_SendPlaceDenied(SM_EPlaceDenyReason.MARKER_LOCKED, 0);
			return;
		}

		int oldVis = m.m_iVisibility;	// запам'ятовуємо до оновлення (ServerUpdate перезапише полем клієнта)

		if (!store.ServerUpdate(id, packed, text))
			return;

		int reqVis = m.m_iVisibility;	// що попросив клієнт (до обрізання)

		// Видимість можна лише розширити (Local < Group < Side < Global), звузити не можна.
		// Це серверний захист — навіть якщо клієнт якось обійшов заблоковані кнопки.
		if (m.m_iVisibility < oldVis)
		{
			m.m_iVisibility = oldVis;
			SM_MarkerNet.Log(string.Format("EDIT #%1: narrow blocked, kept %2 (req %3)", id, SM_MarkerNet.VisName(oldVis), SM_MarkerNet.VisName(reqVis)));
		}

		// Канал заборонено конфігом? — лишаємо попередній (страховка поверх клієнтського UI).
		if (!SM_MarkerConfig.GetInstance().IsVisibilityAllowed(m.m_iVisibility))
			m.m_iVisibility = oldVis;

		m.m_iLastEditorId = GetPlayerId();	// хто редагував = останній редактор (перекриває значення з клієнта)
		if (SM_MarkerConfig.GetInstance().m_bShowLastEditor)
			m.m_sLastEditor = GetGame().GetPlayerManager().GetPlayerName(GetPlayerId());
		if (m.m_iDate != 0)
			SM_MarkerNet.GetGameTime(m.m_iDate, m.m_iTime);	// час ставить сервер (за конфігом)
		// GM сам обирає сторону для Side-мітки — лишаємо канал із даних; інакше рахуємо за фракцією власника.
		if (!(SM_MarkerNet.IsPlayerGameMaster(GetPlayerId()) && m.m_iVisibility == SM_EMarkerVisibility.FACTION))
			SM_MarkerNet.AssignChannel(m.m_iOwnerId, m);	// видимість могла змінитись — перерахуємо канал
		SM_MarkerNet.BroadcastUpsertOrRemove(m);	// заодно приберемо "привид" у тих, кому вже не видно
		SM_MarkerNet.Log(string.Format("EDIT #%1 by %2 vis %3->%4 ch=%5", id, SM_MarkerNet.Who(GetPlayerId()), SM_MarkerNet.VisName(oldVis), SM_MarkerNet.VisName(m.m_iVisibility), m.m_iChannel));
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Remove(int id)
	{
		if (!Replication.IsServer())
			return;

		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
		SM_MapMarkerData m = store.FindById(id);
		if (!m)
			return;
		if (!SM_MarkerNet.CanModify(GetPlayerId(), m))
		{
			SM_MarkerNet.Log(string.Format("DENY REMOVE #%1 by %2 (vis=%3 ch=%4)", id, SM_MarkerNet.Who(GetPlayerId()), SM_MarkerNet.VisName(m.m_iVisibility), m.m_iChannel));
			if (m.m_iGmLocked != 0 && !SM_MarkerNet.IsPlayerGameMaster(GetPlayerId()))
				SM_SendPlaceDenied(SM_EPlaceDenyReason.MARKER_LOCKED, 0);
			return;
		}

		// Хто що видалив (модерація на випадок грифу) — вмикається конфігом.
		if (SM_MarkerConfig.GetInstance().m_bLogDeleter)
			Print(string.Format("[SM] REMOVE #%1 by %2 (text: '%3')", id, SM_MarkerNet.Who(GetPlayerId()), m.m_sText), LogLevel.NORMAL);

		store.ServerRemove(id);
		SM_MarkerNet.BroadcastRemove(id);	// видалення по id шлемо всім
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_RequestSync()
	{
		if (!Replication.IsServer())
			return;

		// this = серверна копія PC того, хто просить — йому й відправляємо все
		SM_MarkerNet.Log(string.Format("SYNC requested by %1", SM_MarkerNet.Who(GetPlayerId())));

		// Спершу шлемо дозволені канали (щоб діалог одразу блокував заборонені кнопки), тоді мітки.
		SM_MarkerConfig cfg = SM_MarkerConfig.GetInstance();
		SM_SendConfig(cfg.m_bAllowLocal, cfg.m_bAllowGroup, cfg.m_bAllowSide, cfg.m_bAllowGlobal, cfg.m_bVanillaFactionNames, cfg.m_bAllowPointer, cfg.m_bAllowCopyLast);

		SM_MarkerNet.SendAllTo(this);
		SM_DrawingNet.SendAllDrawingsTo(this);	// малюнки тим самим синком
	}

	// Зевс просить увімкнути/вимкнути GM-перегляд (дедік). Сервер позначає гравця (з перевіркою GM)
	// і за увімкнення дошле тепер-дозволені Side+Global мітки в його дзеркало.
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_SetGmView(bool enable)
	{
		if (!Replication.IsServer())
			return;
		SM_MarkerNet.SetGmViewer(GetPlayerId(), enable);
		if (enable)
		{
			SM_MarkerNet.SendAllTo(this);	// дозаповнити дзеркало клієнта Side+Global мітками
			SM_DrawingNet.SendAllDrawingsTo(this);
		}
	}

	// Сервер -> клієнт: налаштування, потрібні клієнту (дозволені канали, назви фракцій, вказівник, копія останньої).
	void SM_SendConfig(bool allowLocal, bool allowGroup, bool allowSide, bool allowGlobal, bool vanillaFactionNames, bool allowPointer, bool allowCopyLast)
	{
		Rpc(RpcDo_Config, allowLocal, allowGroup, allowSide, allowGlobal, vanillaFactionNames, allowPointer, allowCopyLast);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_Config(bool allowLocal, bool allowGroup, bool allowSide, bool allowGlobal, bool vanillaFactionNames, bool allowPointer, bool allowCopyLast)
	{
		if (Replication.IsServer())
			return;
		SM_MarkerConfig.GetInstance().SetClientFlags(allowLocal, allowGroup, allowSide, allowGlobal, vanillaFactionNames, allowPointer, allowCopyLast);
	}

	// --- RPC сервер -> клієнт (виконується у власника цього PC) ---

	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_Upsert(int id, int owner, array<int> packed, string text, string editor)
	{
		if (Replication.IsServer())
			return;

		SM_MapMarkerData data = new SM_MapMarkerData();
		if (!data.UnpackInts(packed))
			return;
		data.m_iId = id;
		data.m_iOwnerId = owner;
		data.m_sText = text;
		data.m_sLastEditor = editor;

		SM_MapMarkerStore.GetInstance().ApplyUpsert(data);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_Move(int id, int posX, int posY)
	{
		if (Replication.IsServer())
			return;

		SM_MapMarkerStore.GetInstance().ApplyMove(id, posX, posY);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_Remove(int id)
	{
		if (Replication.IsServer())
			return;

		SM_MapMarkerStore.GetInstance().ApplyRemove(id);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_PlaceDenied(SM_EPlaceDenyReason reason, int limit)
	{
		if (Replication.IsServer())
			return;	// на хості показ іде прямим шляхом із SM_SendPlaceDenied

		SM_ShowPlaceDenied(reason, limit);
	}

	// --- ВКАЗІВНИК (показати пальцем) — тимчасовий, без збереження ---

	void SM_RequestPointUpdate(int x, int y)
	{
		Rpc(RpcAsk_PointUpdate, x, y);
	}

	void SM_RequestPointStop()
	{
		Rpc(RpcAsk_PointStop);
	}

	void SM_SendPointShow(int ownerId, int x, int y)
	{
		Rpc(RpcDo_PointShow, ownerId, x, y);
	}

	void SM_SendPointHide(int ownerId)
	{
		Rpc(RpcDo_PointHide, ownerId);
	}

	// Потік позиції — Unreliable (загублений пакет виправить наступний), без надмірної надійності.
	[RplRpc(RplChannel.Unreliable, RplRcver.Server)]
	protected void RpcAsk_PointUpdate(int x, int y)
	{
		if (!Replication.IsServer())
			return;
		SM_MarkerNet.BroadcastPointer(GetPlayerId(), x, y);
	}

	// Зупинка — Reliable (щоб точка точно зникла в інших).
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_PointStop()
	{
		if (!Replication.IsServer())
			return;
		SM_MarkerNet.BroadcastPointerHide(GetPlayerId());
	}

	[RplRpc(RplChannel.Unreliable, RplRcver.Owner)]
	protected void RpcDo_PointShow(int ownerId, int x, int y)
	{
		if (Replication.IsServer())
			return;
		SM_PointerHub.GetInstance().Show(ownerId, x, y, System.GetTickCount() / 1000.0);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_PointHide(int ownerId)
	{
		if (Replication.IsServer())
			return;
		SM_PointerHub.GetInstance().Hide(ownerId);
	}

	// ==========================================================================
	// МАЛЮВАННЯ на мапі (олівець). RPC живуть у ЦЬОМУ Ж modded-блоці, що й мітки:
	// другий modded-блок того самого класу компілюється ланцюжком за абеткою і не
	// бачить методів пізнішого (SM_SendPlaceDenied був недосяжним із окремого файлу).
	// Серверна логіка (анти-спам/канали/фан-аут) — у SM_DrawingNet (SM_MapDrawingNetwork.c).
	// ==========================================================================

	// --- Клієнт -> сервер ---
	void SM_DrawRequestAdd(array<int> meta, array<int> points)
	{
		Rpc(RpcAsk_DrawAdd, meta, points);
	}

	void SM_DrawRequestRemove(int id)
	{
		Rpc(RpcAsk_DrawRemove, id);
	}

	// Часткове стирання ВЛАСНОГО штриха: замінити його на шматки, що лишились поза гумкою.
	// framed: [кількість_шматків, довжина1(точок), x,z,..., довжина2, ...]
	void SM_DrawRequestErasePart(int id, array<int> framed)
	{
		Rpc(RpcAsk_DrawErasePart, id, framed);
	}

	// Перефарбувати заливку поточними налаштуваннями панелі (клік заливкою по залитій області).
	void SM_DrawRequestRecolor(int id, array<int> meta)
	{
		Rpc(RpcAsk_DrawRecolor, id, meta);
	}

	// --- Сервер -> клієнт (власнику цільового PC) ---
	void SM_SendDrawingAdd(int id, int owner, array<int> meta, array<int> points, string ownerName)
	{
		Rpc(RpcDo_DrawAdd, id, owner, meta, points, ownerName);
	}

	void SM_SendDrawingRemove(int id)
	{
		Rpc(RpcDo_DrawRemove, id);
	}

	//------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_DrawAdd(array<int> meta, array<int> points)
	{
		if (!Replication.IsServer())
			return;

		int requesterId = GetPlayerId();
		SM_MarkerConfig drawCfg = SM_MarkerConfig.GetInstance();
		if (!drawCfg.m_bAllowDrawing)
			return;

		int vis = SM_MapDrawingData.VisibilityFromMeta(meta);
		if (vis < 0)
			return;
		if (!drawCfg.IsVisibilityAllowed(vis))	// канал заборонено в конфізі
		{
			SM_MarkerNet.Log(string.Format("DENY DRAW by %1: channel %2 disabled by config",
				SM_MarkerNet.Who(requesterId), SM_MarkerNet.VisName(vis)));
			SM_SendPlaceDenied(SM_EPlaceDenyReason.DRAW_CHANNEL_DISABLED, 0);
			return;
		}
		if (!SM_DrawingNet.RegisterDrawAttempt(requesterId))	// анти-спам
		{
			SM_MarkerNet.Log(string.Format("DENY DRAW by %1: per-minute limit %2 reached",
				SM_MarkerNet.Who(requesterId), drawCfg.m_iDrawPerMinuteLimit));
			SM_SendPlaceDenied(SM_EPlaceDenyReason.DRAW_PER_MINUTE_LIMIT, drawCfg.m_iDrawPerMinuteLimit);
			return;
		}

		SM_MapDrawingStore drawStore = SM_MapDrawingStore.GetInstance();
		if (drawCfg.m_iDrawMaxTotal > 0 && drawStore.Count() >= drawCfg.m_iDrawMaxTotal)
		{
			SM_MarkerNet.Log(string.Format("DENY DRAW by %1: total limit %2 reached",
				SM_MarkerNet.Who(requesterId), drawCfg.m_iDrawMaxTotal));
			SM_SendPlaceDenied(SM_EPlaceDenyReason.DRAW_TOTAL_LIMIT, drawCfg.m_iDrawMaxTotal);
			return;
		}
		if (drawCfg.m_iDrawMaxPerPlayer > 0 && drawStore.CountByOwner(requesterId) >= drawCfg.m_iDrawMaxPerPlayer)
		{
			SM_MarkerNet.Log(string.Format("DENY DRAW by %1: per-player limit %2 reached",
				SM_MarkerNet.Who(requesterId), drawCfg.m_iDrawMaxPerPlayer));
			SM_SendPlaceDenied(SM_EPlaceDenyReason.DRAW_PER_PLAYER_LIMIT, drawCfg.m_iDrawMaxPerPlayer);
			return;
		}

		// Обрізати точки до серверного ліміту (захист від роздутого штриха).
		array<int> pts = points;
		int maxVals = drawCfg.m_iDrawMaxPointsPerStroke * 2;
		if (maxVals >= 4 && pts.Count() > maxVals)
		{
			array<int> trimmed = {};
			for (int i = 0; i < maxVals; i++)
				trimmed.Insert(pts[i]);
			pts = trimmed;
		}

		// Канал: зазвичай сервер за фракцією/групою автора. Але GM фракції не має й сам обирає
		// сторону для Side-штриха — тоді канал беремо з meta (вибір зевса на панелі), як у міток.
		bool isGm = SM_MarkerNet.IsPlayerGameMaster(requesterId);
		int channel;
		if (isGm && vis == SM_EMarkerVisibility.FACTION)
			channel = SM_MapDrawingData.ChannelFromMeta(meta);
		else
			channel = SM_DrawingNet.ChannelFor(requesterId, vis);

		string authorName = GetGame().GetPlayerManager().GetPlayerName(requesterId);
		SM_MapDrawingData d = drawStore.ServerCreate(requesterId, meta, pts, channel, System.GetTickCount(), authorName);
		if (!d)
			return;

		// Прапорці зевса (lock / hide info) приймаємо ЛИШЕ від справжнього GM — гравець їх підробити не може.
		if (!isGm)
		{
			d.m_iGmLocked = 0;
			d.m_iHideInfo = 0;
		}

		SM_DrawingNet.BroadcastAdd(d);
		SM_MarkerNet.Log(string.Format("DRAW #%1 by %2 vis=%3 ch=%4 pts=%5",
			d.m_iId, SM_MarkerNet.Who(requesterId), SM_MarkerNet.VisName(vis), channel, d.GetPointCount()));
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_DrawRemove(int id)
	{
		if (!Replication.IsServer())
			return;

		SM_MapDrawingData d = SM_MapDrawingStore.GetInstance().FindById(id);
		if (!d)
			return;
		if (!SM_DrawingNet.CanErase(GetPlayerId(), d))
		{
			if (d.m_iGmLocked != 0 && !SM_MarkerNet.IsPlayerGameMaster(GetPlayerId()))
				SM_SendPlaceDenied(SM_EPlaceDenyReason.DRAW_LOCKED, 0);	// пояснити гравцю: залочено зевсом
			SM_MarkerNet.Log(string.Format("DENY DRAW-ERASE #%1 by %2", id, SM_MarkerNet.Who(GetPlayerId())));
			return;
		}

		SM_MapDrawingStore.GetInstance().ServerRemove(id);
		SM_DrawingNet.BroadcastRemove(id);
		SM_MarkerNet.Log(string.Format("DRAW-ERASE #%1 by %2", id, SM_MarkerNet.Who(GetPlayerId())));
	}

	// Часткове стирання: замінити штрих на шматки. ЛИШЕ власник (гумка ріже тільки своє);
	// мета шматків (колір/ширина/видимість/канал) береться з ІСНУЮЧОГО запису на сервері —
	// клієнт передає лише геометрію, підробити атрибути не може. Один RPC на операцію,
	// тож анти-спам ліміт додавань це не зачіпає.
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_DrawErasePart(int id, array<int> framed)
	{
		if (!Replication.IsServer())
			return;
		if (!SM_MarkerConfig.GetInstance().m_bAllowDrawing)
			return;

		SM_MapDrawingStore store = SM_MapDrawingStore.GetInstance();
		SM_MapDrawingData old = store.FindById(id);
		if (!old)
			return;
		if (old.m_iOwnerId != GetPlayerId())	// часткове — суворо власник
			return;
		// Залочений зевсом штрих гравець не ріже (навіть власник); GM — може.
		if (old.m_iGmLocked != 0 && !SM_MarkerNet.IsPlayerGameMaster(GetPlayerId()))
		{
			SM_SendPlaceDenied(SM_EPlaceDenyReason.DRAW_LOCKED, 0);
			return;
		}
		if (!framed || framed.Count() < 1)
			return;

		// Розібрати кадр: [nPieces, len1, pts..., len2, pts...] з жорсткими лімітами.
		int nPieces = framed[0];
		if (nPieces < 1 || nPieces > SM_DrawingNet.MAX_ERASE_PIECES)
			return;
		int maxPts = SM_MarkerConfig.GetInstance().m_iDrawMaxPointsPerStroke;

		array<ref array<int>> pieces = {};
		int pos = 1;
		for (int p = 0; p < nPieces; p++)
		{
			if (pos >= framed.Count())
				return;	// зіпсований кадр
			int len = framed[pos];
			pos++;
			if (len < 2 || len > maxPts)
				return;
			if (pos + len * 2 > framed.Count())
				return;
			array<int> pts = {};
			for (int k = 0; k < len * 2; k++)
				pts.Insert(framed[pos + k]);
			pos += len * 2;
			pieces.Insert(pts);
		}

		// Мета зі старого запису (довірена). Канал/власника/автора переносимо як були.
		array<int> meta = old.PackMeta();
		int keepOwner   = old.m_iOwnerId;
		int keepChannel = old.m_iChannel;
		string keepName = old.m_sOwnerName;

		store.ServerRemove(id);
		SM_DrawingNet.BroadcastRemove(id);

		int made = 0;
		foreach (array<int> piecePts : pieces)
		{
			SM_MapDrawingData piece = store.ServerCreate(keepOwner, meta, piecePts, keepChannel, System.GetTickCount(), keepName);
			if (piece)
			{
				SM_DrawingNet.BroadcastAdd(piece);
				made++;
			}
		}
		SM_MarkerNet.Log(string.Format("DRAW-ERASE-PART #%1 by %2 -> %3 pieces", id, SM_MarkerNet.Who(GetPlayerId()), made));
	}

	// Перефарбування заливки: remove + create з тими ж точками і новою метою панелі.
	// Перефарбувач стає новим автором, канал рахуємо як для свіжого штриха.
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_DrawRecolor(int id, array<int> meta)
	{
		if (!Replication.IsServer())
			return;
		SM_MarkerConfig drawCfg = SM_MarkerConfig.GetInstance();
		if (!drawCfg.m_bAllowDrawing)
			return;

		SM_MapDrawingStore store = SM_MapDrawingStore.GetInstance();
		SM_MapDrawingData old = store.FindById(id);
		if (!old || old.m_iFill == 0)
			return;

		// Права ті самі, що на стирання.
		int requesterId = GetPlayerId();
		if (!SM_DrawingNet.CanErase(requesterId, old))
		{
			if (old.m_iGmLocked != 0 && !SM_MarkerNet.IsPlayerGameMaster(requesterId))
				SM_SendPlaceDenied(SM_EPlaceDenyReason.DRAW_LOCKED, 0);
			else
				SM_SendPlaceDenied(SM_EPlaceDenyReason.FILL_BLOCKED, 0);
			return;
		}

		SM_MapDrawingData req = new SM_MapDrawingData();
		if (!req.UnpackMeta(meta))
			return;

		// Видимість можна лише розширити (Local < Group < Side < Global), як у міток.
		// Спроба звузити: канал лишається старим, решта (колір тощо) застосовується, гравцю — попап.
		int newVis = req.m_iVisibility;
		bool narrowed = false;
		if (newVis < old.m_iVisibility)
		{
			newVis = old.m_iVisibility;
			narrowed = true;
		}
		if (newVis != old.m_iVisibility && !drawCfg.IsVisibilityAllowed(newVis))
		{
			SM_SendPlaceDenied(SM_EPlaceDenyReason.DRAW_CHANNEL_DISABLED, 0);
			return;
		}

		// Канал: при незмінній видимості лишаємо старий; при розширенні рахуємо за автором.
		// Зевс для Side сам обирає сторону на панелі.
		bool isGm = SM_MarkerNet.IsPlayerGameMaster(requesterId);
		int channel = old.m_iChannel;
		if (isGm && newVis == SM_EMarkerVisibility.FACTION)
			channel = req.m_iChannel;
		else if (newVis != old.m_iVisibility)
			channel = SM_DrawingNet.ChannelFor(requesterId, newVis);

		// Нова мета — зі старого (довіреного) запису; від клієнта беремо лише дозволене.
		SM_MapDrawingData tpl = old.SM_Clone();
		tpl.m_iColor      = req.m_iColor;
		tpl.m_iVisibility = newVis;
		tpl.m_iChannel    = channel;
		if (isGm)
		{
			tpl.m_iGmLocked = req.m_iGmLocked;
			tpl.m_iHideInfo = req.m_iHideInfo;
		}
		array<int> newMeta = tpl.PackMeta();
		array<int> pts = {};
		pts.Copy(old.m_aPoints);
		string newName = GetGame().GetPlayerManager().GetPlayerName(requesterId);

		store.ServerRemove(id);
		SM_DrawingNet.BroadcastRemove(id);

		SM_MapDrawingData fresh = store.ServerCreate(requesterId, newMeta, pts, channel, System.GetTickCount(), newName);
		if (fresh)
			SM_DrawingNet.BroadcastAdd(fresh);
		if (narrowed)
			SM_SendPlaceDenied(SM_EPlaceDenyReason.FILL_NO_NARROW, 0);
		SM_MarkerNet.Log(string.Format("FILL-RECOLOR #%1 by %2 vis=%3 ch=%4", id, SM_MarkerNet.Who(requesterId), SM_MarkerNet.VisName(newVis), channel));
	}

	//------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_DrawAdd(int id, int owner, array<int> meta, array<int> points, string ownerName)
	{
		// Дедуп у ApplyAdd по id (на listen-host штрих уже доданий через ServerCreate).
		SM_MapDrawingData d = new SM_MapDrawingData();
		d.UnpackMeta(meta);
		d.SetPoints(points);
		d.m_iId      = id;
		d.m_iOwnerId = owner;
		d.m_sOwnerName = ownerName;
		SM_MapDrawingStore.GetInstance().ApplyAdd(d);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_DrawRemove(int id)
	{
		SM_MapDrawingStore.GetInstance().ApplyRemove(id);
	}
}

// Серверні утиліти: розсилка та правила видимості/доступу. Кличеться тільки на сервері.
class SM_MarkerNet
{
	// Тимчасове логування для MP-тесту. Після тестів поставити false — вимкне всі рядки нижче.
	// Логуємо тільки події (дії гравців + рішення сервера), нічого щокадрово і нічого
	// "на кожного отримувача", лише підсумки. Префікс [SM] зручно фільтрувати в console.log.
	static const bool SM_LOG = false;

	static void Log(string msg)
	{
		if (SM_LOG)
			Print("[SM] " + msg, LogLevel.NORMAL);
	}

	// --- Анти-спам: ковзне вікно спроб розміщення на гравця (серверне) ---
	protected static ref map<int, ref array<float>> s_mPlaceAttempts = new map<int, ref array<float>>();
	protected static ref map<int, float> s_mLastSpamWarn = new map<int, float>();
	protected static const float SM_RATE_WINDOW = 60.0;			// ковзне вікно, с
	protected static const float SM_SPAM_WARN_THROTTLE = 10.0;	// не частіше разу на стільки с на гравця

	// Реєструє спробу розміщення і повертає, чи дозволити. Кличеться лише на сервері.
	// Веде попередження про спам (ЗАВЖДИ в лог сервера, для адмінів) і застосовує ліміт за хвилину.
	static bool RegisterPlaceAttempt(int playerId)
	{
		SM_MarkerConfig cfg = SM_MarkerConfig.GetInstance();
		float now = System.GetTickCount() / 1000.0;

		array<float> times = s_mPlaceAttempts.Get(playerId);
		if (!times)
		{
			times = new array<float>();
			s_mPlaceAttempts.Set(playerId, times);
		}

		// прибрати спроби, старші за вікно
		int i = 0;
		while (i < times.Count())
		{
			if (now - times[i] > SM_RATE_WINDOW)
				times.Remove(i);
			else
				i++;
		}

		times.Insert(now);	// поточна спроба
		int count = times.Count();

		// Попередження про спам (рахуємо й відхилені спроби; пишемо завжди, з троттлом на гравця).
		if (cfg.m_iSpamWarnPerMinute > 0 && count > cfg.m_iSpamWarnPerMinute)
		{
			float last = 0;
			s_mLastSpamWarn.Find(playerId, last);
			if (now - last >= SM_SPAM_WARN_THROTTLE)
			{
				s_mLastSpamWarn.Set(playerId, now);
				Print(string.Format("[SM] SPAM WARNING: %1 attempted %2 marker placements in the last minute (threshold %3). Possible troll — consider kick/ban.",
					Who(playerId), count, cfg.m_iSpamWarnPerMinute), LogLevel.WARNING);
			}
		}

		// Ліміт за хвилину — спроби понад ліміт відхиляємо.
		if (cfg.m_iPerMinuteLimit > 0 && count > cfg.m_iPerMinuteLimit)
			return false;

		return true;
	}

	// Прибрати дані гравця при відключенні (щоб мапи не росли).
	static void ClearRateData(int playerId)
	{
		s_mPlaceAttempts.Remove(playerId);
		s_mLastSpamWarn.Remove(playerId);
		s_GmViewers.RemoveItem(playerId);
	}

	// --- Видимість для Game Master (зевса) ---
	// Гравці, що увімкнули перегляд міток у редакторі (серверний стан). Для них IsEligible пропускає
	// усі Global+Side мітки (див. IsEligible). Дозволяємо лише справжнім GM (SetGmViewer перевіряє).
	protected static ref set<int> s_GmViewers = new set<int>();

	static bool IsGmViewing(int playerId)
	{
		return s_GmViewers.Contains(playerId);
	}

	// Чи має гравець доступ Game Master (є зареєстрований editor manager і редактор відкрито).
	static bool IsPlayerGameMaster(int playerId)
	{
		SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
		if (!core)
			return false;
		SCR_EditorManagerEntity mgr = core.GetEditorManager(playerId);
		return mgr && mgr.IsOpened();
	}

	// Увімкнути/вимкнути GM-перегляд для гравця (серверне). Вмикаємо лише справжнім GM.
	static void SetGmViewer(int playerId, bool on)
	{
		if (on)
		{
			if (!IsPlayerGameMaster(playerId))
			{
				Log(string.Format("DENY GM-view for %1: not a Game Master", Who(playerId)));
				return;
			}
			s_GmViewers.Insert(playerId);
		}
		else
		{
			s_GmViewers.RemoveItem(playerId);
		}
		Log(string.Format("GM-view %1 for %2", on, Who(playerId)));
	}

	// Ім'я+id гравця для читабельності логів.
	static string Who(int playerId)
	{
		string n = GetGame().GetPlayerManager().GetPlayerName(playerId);
		if (n == "")
			n = "?";
		return string.Format("%1(pid=%2)", n, playerId);
	}

	// Коротка назва області видимості для логів.
	static string VisName(int v)
	{
		switch (v)
		{
			case SM_EMarkerVisibility.PERSONAL: return "Local";
			case SM_EMarkerVisibility.GROUP:    return "Group";
			case SM_EMarkerVisibility.FACTION:  return "Side";
			case SM_EMarkerVisibility.ALL:      return "Global";
		}
		return "?";
	}

	// Чи може гравець міняти/видаляти мітку. Дозволяємо лише тим, кому її взагалі видно
	// (для ALL — усім; PERSONAL — тільки автору; FACTION/GROUP — своїм). Захист від PvP-абузу.
	static bool CanModify(int requesterId, notnull SM_MapMarkerData m)
	{
		// Залочена зевсом мітка: редагувати/рухати/видаляти може ЛИШЕ Game Master.
		if (m.m_iGmLocked != 0 && !IsPlayerGameMaster(requesterId))
			return false;
		return IsEligible(requesterId, m);
	}

	// Сервер задає канал мітки за її видимістю.
	static void AssignChannel(int ownerId, notnull SM_MapMarkerData m)
	{
		switch (m.m_iVisibility)
		{
			case SM_EMarkerVisibility.FACTION:
				m.m_iChannel = GetPlayerFactionIndex(ownerId);
				break;

			case SM_EMarkerVisibility.GROUP:
				m.m_iChannel = GetPlayerGroupId(ownerId);
				break;

			default:
				m.m_iChannel = -1;	// PERSONAL / ALL канал не використовують
		}
	}

	// Чи видно гравцю мітку. Для FACTION/GROUP — суто за збігом каналу, щоб при зміні сторони/групи
	// автор теж переставав бачити мітки старого каналу. Якщо канал ще не визначено (ch == -1,
	// напр. групу не встигли підтягнути на момент розміщення) — мітку бачить принаймні автор.
	static bool IsEligible(int playerId, notnull SM_MapMarkerData m)
	{
		return IsEligibleByChannel(playerId, m.m_iOwnerId, m.m_iVisibility, m.m_iChannel);
	}

	// Мітки: чи гравець бачить (GM-перегляд міток — свій прапорець s_GmViewers).
	static bool IsEligibleByChannel(int playerId, int ownerId, int vis, int channel)
	{
		return IsEligibleCore(playerId, ownerId, vis, channel,
			IsGmViewing(playerId) && IsPlayerGameMaster(playerId));
	}

	// Спільне ядро допуску за каналом — однакове для міток і малюнків (та сама модель безпеки);
	// gmSees = у цього гравця ввімкнено відповідний GM-перегляд І він досі Game Master.
	// Для FACTION/GROUP — суто за збігом каналу, щоб при зміні сторони/групи автор теж переставав бачити
	// старий канал. Якщо канал ще не визначено (-1) — бачить принаймні автор.
	static bool IsEligibleCore(int playerId, int ownerId, int vis, int channel, bool gmSees)
	{
		// Зевс із увімкненою видимістю бачить усі Global (ALL) + усі Side (FACTION) будь-якої фракції;
		// чужі Group/Personal — ні, але ВЛАСНІ (будь-який канал) бачить, щоб керувати.
		if (gmSees)
			return vis == SM_EMarkerVisibility.ALL || vis == SM_EMarkerVisibility.FACTION || playerId == ownerId;

		switch (vis)
		{
			case SM_EMarkerVisibility.ALL:
				return true;

			case SM_EMarkerVisibility.PERSONAL:
				return playerId == ownerId;

			case SM_EMarkerVisibility.FACTION:
				if (channel == -1)
					return playerId == ownerId;
				return GetPlayerFactionIndex(playerId) == channel;

			case SM_EMarkerVisibility.GROUP:
				if (channel == -1)
					return playerId == ownerId;
				return GetPlayerGroupId(playerId) == channel;
		}
		return true;
	}

	// Індекс фракції гравця, -1 якщо невідомо.
	// Треба звірити в грі — faction API часом міняється між оновленнями.
	static int GetPlayerFactionIndex(int playerId)
	{
		SCR_FactionManager fm = SCR_FactionManager.Cast(GetGame().GetFactionManager());
		if (!fm)
			return -1;

		Faction f = fm.GetPlayerFaction(playerId);
		if (!f)
			return -1;

		return fm.GetFactionIndex(f);
	}

	// Поточні дата+час (yyyymmdd / hhmm) для позначки часу. За конфігом — час сценарію або реальний.
	static void GetGameTime(out int date, out int time)
	{
		date = 0;
		time = 0;

		// Реальний час (конфіг useScenarioTime = false)
		if (!SM_MarkerConfig.GetInstance().m_bScenarioTime)
		{
			int ry, rmo, rd, rh, rmi, rs;
			System.GetYearMonthDay(ry, rmo, rd);
			System.GetHourMinuteSecond(rh, rmi, rs);
			date = ry * 10000 + rmo * 100 + rd;
			time = rh * 100 + rmi;
			return;
		}

		// Час сценарію (дефолт)
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

	// Id групи гравця для GROUP-видимості, -1 якщо без групи.
	static int GetPlayerGroupId(int playerId)
	{
		SCR_GroupsManagerComponent gm = SCR_GroupsManagerComponent.GetInstance();
		if (!gm)
			return -1;

		SCR_AIGroup grp = gm.GetPlayerGroup(playerId);
		if (!grp)
			return -1;

		return grp.GetGroupID();
	}

	// Розіслати мітку всім, кому її видно (включно з автором).
	static void BroadcastUpsert(notnull SM_MapMarkerData m)
	{
		array<int> packed = m.PackInts();
		array<int> players = {};
		GetGame().GetPlayerManager().GetPlayers(players);

		foreach (int pid : players)
		{
			if (!IsEligible(pid, m))
				continue;

			SCR_PlayerController pc = SM_GetController(pid);
			if (pc)
				pc.SM_SendUpsert(m.m_iId, m.m_iOwnerId, packed, m.m_sText, m.m_sLastEditor);
		}
	}

	// Для редагування: кому видно — оновити, кому ні — видалити (раптом видимість звузилась
	// і в них лишився "привид"). SM_SendRemove тому, хто мітки не мав, нешкідливий.
	static void BroadcastUpsertOrRemove(notnull SM_MapMarkerData m)
	{
		array<int> packed = m.PackInts();
		array<int> players = {};
		GetGame().GetPlayerManager().GetPlayers(players);

		foreach (int pid : players)
		{
			SCR_PlayerController pc = SM_GetController(pid);
			if (!pc)
				continue;

			if (IsEligible(pid, m))
				pc.SM_SendUpsert(m.m_iId, m.m_iOwnerId, packed, m.m_sText, m.m_sLastEditor);
			else
				pc.SM_SendRemove(m.m_iId);
		}
	}

	static void BroadcastMove(notnull SM_MapMarkerData m)
	{
		array<int> players = {};
		GetGame().GetPlayerManager().GetPlayers(players);

		foreach (int pid : players)
		{
			if (!IsEligible(pid, m))
				continue;

			SCR_PlayerController pc = SM_GetController(pid);
			if (pc)
				pc.SM_SendMove(m.m_iId, m.m_iPosX, m.m_iPosY);
		}
	}

	// Видалення шлемо всім (мітка могла бути видима до зміни видимості).
	static void BroadcastRemove(int id)
	{
		array<int> players = {};
		GetGame().GetPlayerManager().GetPlayers(players);

		foreach (int pid : players)
		{
			SCR_PlayerController pc = SM_GetController(pid);
			if (pc)
				pc.SM_SendRemove(id);
		}
	}

	static const float SM_POINT_RANGE = 10;	// метри: показуємо вказівник лише сусіднім союзникам

	// Розіслати вказівник власника СОЮЗНИМ гравцям у радіусі SM_POINT_RANGE (за позиціями персонажів).
	// Собі не шлемо — власник малює свою точку локально.
	static void BroadcastPointer(int ownerId, int x, int y)
	{
		IEntity ownerEnt = GetGame().GetPlayerManager().GetPlayerControlledEntity(ownerId);
		if (!ownerEnt)
			return;
		vector ownerPos = ownerEnt.GetOrigin();
		int ownerFaction = GetPlayerFactionIndex(ownerId);

		array<int> players = {};
		GetGame().GetPlayerManager().GetPlayers(players);
		foreach (int pid : players)
		{
			if (pid == ownerId)
				continue;
			if (GetPlayerFactionIndex(pid) != ownerFaction)	// лише союзні (та сама фракція)
				continue;
			IEntity ent = GetGame().GetPlayerManager().GetPlayerControlledEntity(pid);
			if (!ent)
				continue;
			if (vector.Distance(ent.GetOrigin(), ownerPos) > SM_POINT_RANGE)
				continue;
			SCR_PlayerController pc = SM_GetController(pid);
			if (pc)
				pc.SM_SendPointShow(ownerId, x, y);
		}
	}

	static void BroadcastPointerHide(int ownerId)
	{
		array<int> players = {};
		GetGame().GetPlayerManager().GetPlayers(players);
		foreach (int pid : players)
		{
			if (pid == ownerId)
				continue;
			SCR_PlayerController pc = SM_GetController(pid);
			if (pc)
				pc.SM_SendPointHide(ownerId);
		}
	}

	// Повна синхронізація конкретному гравцю (спавн/реконект).
	static void SendAllTo(notnull SCR_PlayerController pc)
	{
		int playerId = pc.GetPlayerId();

		array<SM_MapMarkerData> all = {};
		SM_MapMarkerStore.GetInstance().GetAll(all);

		int sent = 0;
		foreach (SM_MapMarkerData m : all)
		{
			if (!m || !IsEligible(playerId, m))
				continue;

			pc.SM_SendUpsert(m.m_iId, m.m_iOwnerId, m.PackInts(), m.m_sText, m.m_sLastEditor);
			sent++;
		}

		Log(string.Format("SYNC -> %1: sent %2/%3 markers", Who(playerId), sent, all.Count()));
	}

	protected static SCR_PlayerController SM_GetController(int playerId)
	{
		return SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
	}
}
