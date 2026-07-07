// Серверна логіка малювання: анти-спам, канал, права на стирання, фан-аут, JIP.
// Перевикористовує примітиви SM_MarkerNet (фракція/група/GM/допуск).
//
// УВАГА: RPC-методи малювання (RpcAsk_Draw* / RpcDo_Draw* / SM_Draw*) живуть у
// SM_MapMarkerNetwork.c, в ОДНОМУ modded-блоці SCR_PlayerController з мітками.
// Окремий modded-блок тут не можна: блоки компілюються ланцюжком за абеткою, і цей
// файл іде РАНІШЕ — його блок не бачив би методів блоку міток (SM_SendPlaceDenied).

class SM_DrawingNet
{
	protected static ref map<int, ref array<float>> s_mDrawAttempts = new map<int, ref array<float>>();
	static const float DRAW_RATE_WINDOW = 60.0;	// секунд
	static const int MAX_ERASE_PIECES = 16;	// стеля шматків на одне часткове стирання

	// Анти-спам: ковзне вікно 60 с. true якщо в межах ліміту (або ліміт вимкнено).
	static bool RegisterDrawAttempt(int playerId)
	{
		SM_MarkerConfig cfg = SM_MarkerConfig.GetInstance();
		if (cfg.m_iDrawPerMinuteLimit <= 0)
			return true;

		float now = System.GetTickCount() / 1000.0;
		array<float> times = s_mDrawAttempts.Get(playerId);
		if (!times)
		{
			times = new array<float>();
			s_mDrawAttempts.Set(playerId, times);
		}
		int i = 0;
		while (i < times.Count())
		{
			if (now - times[i] > DRAW_RATE_WINDOW)
				times.Remove(i);
			else
				i++;
		}
		times.Insert(now);
		return times.Count() <= cfg.m_iDrawPerMinuteLimit;
	}

	static void ClearRateData(int playerId)
	{
		s_mDrawAttempts.Remove(playerId);
		s_GmDrawViewers.RemoveItem(playerId);
	}

	// --- GM-перегляд МАЛЮНКІВ (окремий прапорець від міток: кнопки в тулбарі незалежні) ---
	protected static ref set<int> s_GmDrawViewers = new set<int>();

	static bool IsGmDrawViewing(int playerId)
	{
		return s_GmDrawViewers.Contains(playerId);
	}

	static void SetGmDrawViewer(int playerId, bool on)
	{
		if (on)
		{
			if (!SM_MarkerNet.IsPlayerGameMaster(playerId))
			{
				SM_MarkerNet.Log(string.Format("DENY GM-draw-view for %1: not a Game Master", SM_MarkerNet.Who(playerId)));
				return;
			}
			s_GmDrawViewers.Insert(playerId);
		}
		else
		{
			s_GmDrawViewers.RemoveItem(playerId);
		}
		SM_MarkerNet.Log(string.Format("GM-draw-view %1 for %2", on, SM_MarkerNet.Who(playerId)));
	}

	// Чи гравець бачить штрих (спільне ядро з мітками, але GM-прапорець — малюнковий).
	static bool IsEligibleDrawing(int playerId, notnull SM_MapDrawingData d)
	{
		return SM_MarkerNet.IsEligibleCore(playerId, d.m_iOwnerId, d.m_iVisibility, d.m_iChannel,
			IsGmDrawViewing(playerId) && SM_MarkerNet.IsPlayerGameMaster(playerId));
	}

	// Сервер задає канал штриха за видимістю (як у міток).
	static int ChannelFor(int ownerId, int vis)
	{
		switch (vis)
		{
			case SM_EMarkerVisibility.FACTION: return SM_MarkerNet.GetPlayerFactionIndex(ownerId);
			case SM_EMarkerVisibility.GROUP:   return SM_MarkerNet.GetPlayerGroupId(ownerId);
		}
		return -1;	// PERSONAL / ALL канал не використовують
	}

	// Стирати штрих може власник або Game Master; чужі — лише якщо власник сервера ввімкнув
	// drawEraseOthersAllowed, і лише той, кому штрих узагалі ВИДНО (канал — без стирання «всліпу»).
	// Залочений зевсом штрих (gmLocked) гравець не стирає ВЗАГАЛІ — навіть якщо він власник; лише GM.
	static bool CanErase(int requesterId, notnull SM_MapDrawingData d)
	{
		if (d.m_iGmLocked != 0 && !SM_MarkerNet.IsPlayerGameMaster(requesterId))
			return false;
		if (requesterId == d.m_iOwnerId)
			return true;
		if (SM_MarkerNet.IsPlayerGameMaster(requesterId))
			return true;
		return SM_MarkerConfig.GetInstance().m_bDrawEraseOthers && IsEligibleDrawing(requesterId, d);
	}

	// Розіслати штрих усім, кому видно (включно з автором).
	static void BroadcastAdd(notnull SM_MapDrawingData d)
	{
		array<int> meta = d.PackMeta();
		array<int> players = {};
		GetGame().GetPlayerManager().GetPlayers(players);
		foreach (int pid : players)
		{
			if (!IsEligibleDrawing(pid, d))
				continue;
			SCR_PlayerController pc = GetController(pid);
			if (pc)
				pc.SM_SendDrawingAdd(d.m_iId, d.m_iOwnerId, meta, d.m_aPoints, d.m_sOwnerName);
		}
	}

	// Видалення шлемо всім (штрих міг бути видимим до зміни стану).
	static void BroadcastRemove(int id)
	{
		array<int> players = {};
		GetGame().GetPlayerManager().GetPlayers(players);
		foreach (int pid : players)
		{
			SCR_PlayerController pc = GetController(pid);
			if (pc)
				pc.SM_SendDrawingRemove(id);
		}
	}

	// Повна синхронізація малюнків конкретному гравцю (спавн/реконект/зміна каналу/GM).
	static void SendAllDrawingsTo(notnull SCR_PlayerController pc)
	{
		int playerId = pc.GetPlayerId();
		array<SM_MapDrawingData> all = {};
		SM_MapDrawingStore.GetInstance().GetAll(all);

		int sent = 0;
		foreach (SM_MapDrawingData d : all)
		{
			if (!d || !IsEligibleDrawing(playerId, d))
				continue;
			pc.SM_SendDrawingAdd(d.m_iId, d.m_iOwnerId, d.PackMeta(), d.m_aPoints, d.m_sOwnerName);
			sent++;
		}
		SM_MarkerNet.Log(string.Format("DRAW SYNC -> %1: sent %2/%3", SM_MarkerNet.Who(playerId), sent, all.Count()));
	}

	protected static SCR_PlayerController GetController(int playerId)
	{
		return SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
	}
}
