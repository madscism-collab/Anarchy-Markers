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

		if (!to)
			return;

		if (Replication.IsServer())
		{
			// Listen-host/SP: the server store is authoritative (no client sync needed), but the
			// host's local player still keeps his Local markers in the client-side file — activate
			// them straight from the server persistence code, no RPC. Covers respawn and the host
			// switching sides.
			if (GetGame().GetPlayerController() == this)
			{
				GetGame().GetCallqueue().Remove(SM_HostSyncLocal);
				GetGame().GetCallqueue().CallLater(SM_HostSyncLocal, SM_SYNC_DELAY_MS, false);
			}
			return;
		}

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
		SM_DrawOutbox.Flush();	// send any buffered draw ops of the old faction before dropping the queue
		SM_DrawOutbox.Reset();
		SM_MapMarkerStore.GetInstance().Clear();	// drop stale markers of the old channel (incl. Local visuals)
		SM_MapDrawingStore.GetInstance().Clear();	// same for drawings
		SM_RequestSync();	// the server resends only what we're now allowed to see

		// Local markers/drawings: show the NEW faction's slice (same server code, other side).
		string fk = SM_LocalMarkerPersistence.GetLocalFactionKey();
		SM_LocalMarkerPersistence.GetInstance().ReactivateForFaction(fk);
		SM_LocalDrawingPersistence.GetInstance().ReactivateForFaction(fk);
	}

	// Public trigger for host-side Local activation (map layer calls it on map open —
	// on the host the regular server sync is a no-op).
	void SM_RequestHostLocalSync()
	{
		if (Replication.IsServer())
			SM_HostSyncLocal();
	}

	// Listen-host/SP: activate the local player's Local markers/drawings straight from the
	// server persistence codes (no RPC round-trip).
	protected void SM_HostSyncLocal()
	{
		if (!Replication.IsServer())
			return;
		string fk = SM_LocalMarkerPersistence.GetLocalFactionKey();
		SM_LocalMarkerPersistence.GetInstance().SetCodeAndActivate(SM_MapMarkerPersistence.GetInstance().GetCode(), fk);
		SM_LocalDrawingPersistence.GetInstance().SetCodeAndActivate(SM_DrawingPersistence.GetInstance().GetCode(), fk);
	}

	void ~SCR_PlayerController()
	{
		if (m_bSMGroupSubscribed)
		{
			SCR_AIGroup.GetOnPlayerAdded().Remove(SM_OnGroupChanged);
			SCR_AIGroup.GetOnPlayerRemoved().Remove(SM_OnGroupChanged);
		}
		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(SM_CheckResync);
			GetGame().GetCallqueue().Remove(SM_HostSyncLocal);
		}
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

	// The batch path can carry many rejected strokes in one packet — each would otherwise fire its own
	// popup and sound. Throttle: one message per reason per few seconds.
	protected static float s_fSMLastDenyMsgAt;
	protected static int   s_iSMLastDenyReason = -1;
	protected const float  SM_DENY_MSG_THROTTLE = 3.0;

	// Локальний показ повідомлення про відмову (кличе і RPC-обробник, і прямий шлях на хості).
	void SM_ShowPlaceDenied(SM_EPlaceDenyReason reason, int limit)
	{
		// A template that hits a per-minute limit is just going too fast: it WAITS the window out and
		// keeps drawing on its own (OnRateLimited), no re-press needed. The other drawing caps won't
		// clear by waiting (the player must erase, or the server is full, or the channel is off), so
		// those stop it (OnDenied). Marker refusals are none of a template's business.
		if (reason == SM_EPlaceDenyReason.DRAW_PER_MINUTE_LIMIT)
			SM_TemplateSession.GetInstance().OnRateLimited();
		else if (reason == SM_EPlaceDenyReason.DRAW_PER_PLAYER_LIMIT
			|| reason == SM_EPlaceDenyReason.DRAW_TOTAL_LIMIT
			|| reason == SM_EPlaceDenyReason.DRAW_CHANNEL_DISABLED)
		{
			SM_TemplateSession.GetInstance().OnDenied();
		}

		// Throttle the popup, but the template state above must update every time regardless.
		float nowDeny = System.GetTickCount() / 1000.0;
		if (reason == s_iSMLastDenyReason && nowDeny - s_fSMLastDenyMsgAt < SM_DENY_MSG_THROTTLE)
			return;
		s_iSMLastDenyReason = reason;
		s_fSMLastDenyMsgAt  = nowDeny;

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
			case SM_EPlaceDenyReason.DRAW_GRID_LIMIT:
				if (limit == 1)
					msg = "You can only have one grid at a time on this server. Erase your grid to place a new one.";
				else
					msg = string.Format("You've reached this server's grid limit (%1 per player). Erase one to place a new grid.", limit);
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

	// --- RPC client -> server (this = server-side copy of the requester's PC). Thin stubs only:
	// all rules/validation live in SM_MarkerNet.ServerHandle* so the whole server logic sits in
	// one manager class (same layout as SM_DrawingNet for drawings). ---

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Place(array<int> packed, string text)
	{
		if (!Replication.IsServer())
			return;
		SM_MarkerNet.ServerHandlePlace(this, packed, text);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Move(int id, int posX, int posY)
	{
		if (!Replication.IsServer())
			return;
		SM_MarkerNet.ServerHandleMove(this, id, posX, posY);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Edit(int id, array<int> packed, string text)
	{
		if (!Replication.IsServer())
			return;
		SM_MarkerNet.ServerHandleEdit(this, id, packed, text);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Remove(int id)
	{
		if (!Replication.IsServer())
			return;
		SM_MarkerNet.ServerHandleRemove(this, id);
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
		SM_SendConfig(cfg.m_bAllowLocal, cfg.m_bAllowGroup, cfg.m_bAllowSide, cfg.m_bAllowGlobal, cfg.m_bVanillaFactionNames, cfg.m_bAllowPointer, cfg.m_bAllowCopyLast, cfg.m_iDrawBatchIntervalMs);
		SM_SendDrawLimits(cfg.m_iDrawPerMinuteLimit, cfg.m_iDrawMaxPerPlayer, cfg.m_iDrawMaxTotal, cfg.m_iDrawMaxPointsPerStroke, cfg.m_bAllowTemplates, cfg.m_iDrawMaxGridsPerPlayer, cfg.m_iDrawMaxPointsPerFill);

		// This game's codes (separate for markers and drawings) — the client uses them to
		// activate its own Local markers/drawings for this server.
		SM_SendServerCodes(SM_MapMarkerPersistence.GetInstance().GetCode(), SM_DrawingPersistence.GetInstance().GetCode());

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
	void SM_SendConfig(bool allowLocal, bool allowGroup, bool allowSide, bool allowGlobal, bool vanillaFactionNames, bool allowPointer, bool allowCopyLast, int drawBatchMs)
	{
		Rpc(RpcDo_Config, allowLocal, allowGroup, allowSide, allowGlobal, vanillaFactionNames, allowPointer, allowCopyLast, drawBatchMs);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_Config(bool allowLocal, bool allowGroup, bool allowSide, bool allowGlobal, bool vanillaFactionNames, bool allowPointer, bool allowCopyLast, int drawBatchMs)
	{
		if (Replication.IsServer())
			return;
		SM_MarkerConfig.GetInstance().SetClientFlags(allowLocal, allowGroup, allowSide, allowGlobal, vanillaFactionNames, allowPointer, allowCopyLast, drawBatchMs);
	}

	// Server -> client: the drawing limits. A separate packet rather than four more parameters on
	// RpcDo_Config, which is already at eight. The client needs them to pace template auto-drawing
	// and to tell the player, before he starts, that a template cannot fit on this server.
	void SM_SendDrawLimits(int perMinute, int maxPerPlayer, int maxTotal, int maxPointsPerStroke, bool allowTemplates, int maxGrids, int maxPointsPerFill)
	{
		Rpc(RpcDo_DrawLimits, perMinute, maxPerPlayer, maxTotal, maxPointsPerStroke, allowTemplates, maxGrids, maxPointsPerFill);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_DrawLimits(int perMinute, int maxPerPlayer, int maxTotal, int maxPointsPerStroke, bool allowTemplates, int maxGrids, int maxPointsPerFill)
	{
		if (Replication.IsServer())
			return;
		SM_MarkerConfig.GetInstance().SetClientDrawLimits(perMinute, maxPerPlayer, maxTotal, maxPointsPerStroke, allowTemplates, maxGrids, maxPointsPerFill);
	}

	// Server -> client: this game's codes (separate for markers and drawings). The client keys
	// its Local files by them and activates the slice for its current faction.
	void SM_SendServerCodes(string markerCode, string drawCode)
	{
		Rpc(RpcDo_ServerCodes, markerCode, drawCode);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_ServerCodes(string markerCode, string drawCode)
	{
		if (Replication.IsServer())
			return;	// the host reads the codes straight from server persistence (SM_HostSyncLocal)

		string factionKey = SM_LocalMarkerPersistence.GetLocalFactionKey();
		SM_LocalMarkerPersistence.GetInstance().SetCodeAndActivate(markerCode, factionKey);
		SM_LocalDrawingPersistence.GetInstance().SetCodeAndActivate(drawCode, factionKey);
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
	// Instant path (batching disabled, or the host): delegates to the shared SM_DrawingNet handlers.
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_DrawAdd(array<int> meta, array<int> points)
	{
		if (!Replication.IsServer())
			return;
		SM_DrawingNet.ServerHandleAdd(GetPlayerId(), meta, points, this);	// this -> deny popups go to the player
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_DrawRemove(int id)
	{
		if (!Replication.IsServer())
			return;
		SM_DrawingNet.ServerHandleRemove(GetPlayerId(), id, this);
	}

	// Batched draw/erase ops (see SM_DrawOutbox): the client buffers operations and sends them
	// as one packet. The server processes them in order and returns the real id of every ADD
	// back to the author (reconcile).
	void SM_DrawSendBatch(int seq, array<int> blob)
	{
		Rpc(RpcAsk_DrawBatch, seq, blob);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_DrawBatch(int seq, array<int> blob)
	{
		if (!Replication.IsServer())
			return;
		array<int> realIds = {};
		array<int> erasePieceIds = {};
		SM_DrawingNet.ProcessBatch(this, blob, realIds, erasePieceIds);
		SM_SendDrawReconcile(seq, realIds, erasePieceIds);
	}

	void SM_SendDrawReconcile(int seq, array<int> realIds, array<int> erasePieceIds)
	{
		Rpc(RpcDo_DrawReconcile, seq, realIds, erasePieceIds);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_DrawReconcile(int seq, array<int> realIds, array<int> erasePieceIds)
	{
		if (Replication.IsServer())
			return;
		SM_DrawOutbox.OnReconcile(seq, realIds, erasePieceIds);
	}

	// Partial erase: replace a stroke with the pieces left outside the eraser. Owner-only;
	// piece meta is taken from the trusted server record (client sends geometry only).
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_DrawErasePart(int id, array<int> framed)
	{
		if (!Replication.IsServer())
			return;
		SM_DrawingNet.ServerHandleErasePart(GetPlayerId(), id, framed, this);
	}

	// Recolor a fill with the current panel settings (fill tool clicked on an existing fill).
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_DrawRecolor(int id, array<int> meta)
	{
		if (!Replication.IsServer())
			return;
		SM_DrawingNet.ServerHandleRecolor(this, id, meta);
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

	// ==========================================================================
	// Server-side handling of player marker requests. The RpcAsk_* stubs on
	// SCR_PlayerController delegate here so every rule (rate limits, permissions,
	// channel assignment) lives in this one manager class. pc = the requesting
	// player's controller — used for identity and for deny popups.
	// ==========================================================================

	static void ServerHandlePlace(notnull SCR_PlayerController pc, array<int> packed, string text)
	{
		int requesterId = pc.GetPlayerId();
		SM_MarkerConfig cfg = SM_MarkerConfig.GetInstance();
		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();

		// Sliding-window rate limit; also feeds the admin spam warning.
		if (!RegisterPlaceAttempt(requesterId))
		{
			Log(string.Format("DENY PLACE by %1: per-minute limit %2 reached", Who(requesterId), cfg.m_iPerMinuteLimit));
			pc.SM_SendPlaceDenied(SM_EPlaceDenyReason.PER_MINUTE_LIMIT, cfg.m_iPerMinuteLimit);
			return;
		}

		int wantVis = SM_MapMarkerData.VisibilityFromPacked(packed);

		// Local (PERSONAL) markers never reach the server anymore — they live in the client's
		// own file (SM_LocalMarkerPersistence). Our UI doesn't send them here; this guards
		// against external API misuse so the server store stays free of Local markers.
		if (wantVis == SM_EMarkerVisibility.PERSONAL)
		{
			Log(string.Format("IGNORE PLACE by %1: PERSONAL markers are client-side only", Who(requesterId)));
			return;
		}

		// Channel allowed? The client UI blocks this too — server check is the safety net.
		if (!cfg.IsVisibilityAllowed(wantVis))
		{
			Log(string.Format("DENY PLACE by %1: channel %2 disabled by config", Who(requesterId), VisName(wantVis)));
			return;
		}

		// Marker count limits (0 = unlimited)
		if (cfg.m_iTotalLimit > 0 && store.Count() >= cfg.m_iTotalLimit)
		{
			Log(string.Format("DENY PLACE by %1: total limit %2 reached", Who(requesterId), cfg.m_iTotalLimit));
			pc.SM_SendPlaceDenied(SM_EPlaceDenyReason.TOTAL_LIMIT, cfg.m_iTotalLimit);
			return;
		}
		if (cfg.m_iPerPlayerLimit > 0 && store.CountByOwner(requesterId) >= cfg.m_iPerPlayerLimit)
		{
			Log(string.Format("DENY PLACE by %1: per-player limit %2 reached", Who(requesterId), cfg.m_iPerPlayerLimit));
			pc.SM_SendPlaceDenied(SM_EPlaceDenyReason.PER_PLAYER_LIMIT, cfg.m_iPerPlayerLimit);
			return;
		}

		SM_MapMarkerData data = store.ServerCreate(requesterId, packed, text);
		if (!data)
			return;

		if (cfg.m_bShowLastEditor)
			data.m_sLastEditor = GetGame().GetPlayerManager().GetPlayerName(requesterId);	// author name for the tooltip (survives restarts)
		if (data.m_iDate != 0)
			GetGameTime(data.m_iDate, data.m_iTime);	// timestamp set by the server (scenario or real time per config)
		// Channel is normally derived from the placer's faction. A GM has no faction and picks
		// the side for FACTION markers himself — then the channel from the GM dropdown is kept.
		if (!(IsPlayerGameMaster(requesterId) && data.m_iVisibility == SM_EMarkerVisibility.FACTION))
			AssignChannel(requesterId, data);
		BroadcastUpsert(data);

		// The log shows vis/channel and what the faction/group API returned (the riskiest spot).
		Log(string.Format("PLACE #%1 by %2 vis=%3 ch=%4 (faction=%5 group=%6) kind=%7",
			data.m_iId, Who(requesterId), VisName(data.m_iVisibility), data.m_iChannel,
			GetPlayerFactionIndex(requesterId), GetPlayerGroupId(requesterId), data.m_iKind));
	}

	static void ServerHandleMove(notnull SCR_PlayerController pc, int id, int posX, int posY)
	{
		int requesterId = pc.GetPlayerId();
		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
		SM_MapMarkerData m = store.FindById(id);
		if (!m)
			return;
		if (!CanModify(requesterId, m))
		{
			Log(string.Format("DENY MOVE #%1 by %2 (vis=%3 ch=%4)", id, Who(requesterId), VisName(m.m_iVisibility), m.m_iChannel));
			if (m.m_iGmLocked != 0 && !IsPlayerGameMaster(requesterId))
				pc.SM_SendPlaceDenied(SM_EPlaceDenyReason.MARKER_LOCKED, 0);
			return;
		}

		m.m_iLastEditorId = requesterId;	// whoever moved it is the last editor
		if (SM_MarkerConfig.GetInstance().m_bShowLastEditor)
			m.m_sLastEditor = GetGame().GetPlayerManager().GetPlayerName(requesterId);
		if (m.m_iDate != 0)
			GetGameTime(m.m_iDate, m.m_iTime);	// refresh the timestamp before ServerMove

		if (!store.ServerMove(id, posX, posY))
			return;

		BroadcastUpsert(m);	// send full data (position + timestamp + editor)
		Log(string.Format("MOVE #%1 by %2 -> (%3, %4)", id, Who(requesterId), posX, posY));
	}

	static void ServerHandleEdit(notnull SCR_PlayerController pc, int id, array<int> packed, string text)
	{
		int requesterId = pc.GetPlayerId();
		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
		SM_MapMarkerData m = store.FindById(id);
		if (!m)
			return;
		if (!CanModify(requesterId, m))
		{
			Log(string.Format("DENY EDIT #%1 by %2 (vis=%3 ch=%4)", id, Who(requesterId), VisName(m.m_iVisibility), m.m_iChannel));
			if (m.m_iGmLocked != 0 && !IsPlayerGameMaster(requesterId))
				pc.SM_SendPlaceDenied(SM_EPlaceDenyReason.MARKER_LOCKED, 0);
			return;
		}

		int oldVis = m.m_iVisibility;	// remember before ServerUpdate overwrites it with the client's value

		if (!store.ServerUpdate(id, packed, text))
			return;

		int reqVis = m.m_iVisibility;	// what the client asked for (before clamping)

		// Visibility can only be widened (Local < Group < Side < Global), never narrowed.
		// Server-side protection even if the client somehow bypassed the disabled buttons.
		if (m.m_iVisibility < oldVis)
		{
			m.m_iVisibility = oldVis;
			Log(string.Format("EDIT #%1: narrow blocked, kept %2 (req %3)", id, VisName(oldVis), VisName(reqVis)));
		}

		// Channel disabled by config? Keep the previous one (safety net over the client UI).
		if (!SM_MarkerConfig.GetInstance().IsVisibilityAllowed(m.m_iVisibility))
			m.m_iVisibility = oldVis;

		m.m_iLastEditorId = requesterId;	// overrides whatever the client sent
		if (SM_MarkerConfig.GetInstance().m_bShowLastEditor)
			m.m_sLastEditor = GetGame().GetPlayerManager().GetPlayerName(requesterId);
		if (m.m_iDate != 0)
			GetGameTime(m.m_iDate, m.m_iTime);
		// A GM picks the side for FACTION markers himself — keep the channel from the data;
		// otherwise recalculate from the owner (visibility may have changed).
		if (!(IsPlayerGameMaster(requesterId) && m.m_iVisibility == SM_EMarkerVisibility.FACTION))
			AssignChannel(m.m_iOwnerId, m);
		BroadcastUpsertOrRemove(m);	// also removes the "ghost" from players who can no longer see it
		Log(string.Format("EDIT #%1 by %2 vis %3->%4 ch=%5", id, Who(requesterId), VisName(oldVis), VisName(m.m_iVisibility), m.m_iChannel));
	}

	static void ServerHandleRemove(notnull SCR_PlayerController pc, int id)
	{
		int requesterId = pc.GetPlayerId();
		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
		SM_MapMarkerData m = store.FindById(id);
		if (!m)
			return;
		if (!CanModify(requesterId, m))
		{
			Log(string.Format("DENY REMOVE #%1 by %2 (vis=%3 ch=%4)", id, Who(requesterId), VisName(m.m_iVisibility), m.m_iChannel));
			if (m.m_iGmLocked != 0 && !IsPlayerGameMaster(requesterId))
				pc.SM_SendPlaceDenied(SM_EPlaceDenyReason.MARKER_LOCKED, 0);
			return;
		}

		// Who deleted what (moderation trail for griefing) — enabled via config.
		if (SM_MarkerConfig.GetInstance().m_bLogDeleter)
			Print(string.Format("[SM] REMOVE #%1 by %2 (text: '%3')", id, Who(requesterId), m.m_sText), LogLevel.NORMAL);

		store.ServerRemove(id);
		BroadcastRemove(id);	// removal by id goes to everyone
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
			if (!m || SM_MapMarkerStore.IsLocalId(m.m_iId))	// Local markers (id <= -2, listen-host only) never replicate
				continue;
			if (!IsEligible(playerId, m))
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
