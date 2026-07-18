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
			if (!d || SM_MapDrawingStore.IsLocalId(d.m_iId))	// Local strokes (id <= -2, listen-host only) never replicate
				continue;
			if (!IsEligibleDrawing(playerId, d))
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

	// ==========================================================================
	// Server-side handling of drawing operations. Shared by the instant RPCs
	// (RpcAsk_Draw*) and the batched path (ProcessBatch) so both enforce the exact
	// same rules. denyPc != null -> the player gets a deny popup (instant path);
	// the batch path passes null (a rejected stroke just disappears on reconcile,
	// no popup spam).
	// ==========================================================================

	// Add one stroke/fill. Returns the new id, or 0 if rejected.
	static int ServerHandleAdd(int requesterId, array<int> meta, array<int> points, SCR_PlayerController denyPc)
	{
		SM_MarkerConfig drawCfg = SM_MarkerConfig.GetInstance();
		if (!drawCfg.m_bAllowDrawing)
			return 0;

		int vis = SM_MapDrawingData.VisibilityFromMeta(meta);
		if (vis < 0)
			return 0;
		// Local (PERSONAL) drawings are client-side only (SM_LocalDrawingPersistence).
		// Our UI never sends them here; this guards against external API misuse.
		if (vis == SM_EMarkerVisibility.PERSONAL)
		{
			SM_MarkerNet.Log(string.Format("IGNORE DRAW by %1: PERSONAL drawings are client-side only", SM_MarkerNet.Who(requesterId)));
			return 0;
		}
		if (!drawCfg.IsVisibilityAllowed(vis))
		{
			if (denyPc)
				denyPc.SM_SendPlaceDenied(SM_EPlaceDenyReason.DRAW_CHANNEL_DISABLED, 0);
			return 0;
		}
		if (!RegisterDrawAttempt(requesterId))	// rate limit
		{
			if (denyPc)
				denyPc.SM_SendPlaceDenied(SM_EPlaceDenyReason.DRAW_PER_MINUTE_LIMIT, drawCfg.m_iDrawPerMinuteLimit);
			return 0;
		}

		SM_MapDrawingStore drawStore = SM_MapDrawingStore.GetInstance();

		// A shape is one record but weighs its line cost against the drawing limits — a rectangle is 4,
		// a circle its segments — so it can't be a cheap way to blanket the map. The grid is capped by a
		// separate per-player count instead (one is usually all anyone needs). Cost is read from the
		// raw meta/points; StrokeCost is self-guarding and the grid snap doesn't change it.
		int shape = SM_MapDrawingData.ShapeFromMeta(meta);
		int cost = 1;
		if (shape != 0 && points.Count() == 4)
			cost = SM_ShapeGeometry.StrokeCost(shape, points);

		if (shape == SM_ShapeGeometry.SHAPE_GRID && drawCfg.m_iDrawMaxGridsPerPlayer > 0
			&& drawStore.CountGridsByOwner(requesterId) >= drawCfg.m_iDrawMaxGridsPerPlayer)
		{
			if (denyPc)
				denyPc.SM_SendPlaceDenied(SM_EPlaceDenyReason.DRAW_GRID_LIMIT, drawCfg.m_iDrawMaxGridsPerPlayer);
			return 0;
		}
		if (drawCfg.m_iDrawMaxTotal > 0 && drawStore.TotalCost() + cost > drawCfg.m_iDrawMaxTotal)
		{
			if (denyPc)
				denyPc.SM_SendPlaceDenied(SM_EPlaceDenyReason.DRAW_TOTAL_LIMIT, drawCfg.m_iDrawMaxTotal);
			return 0;
		}
		if (drawCfg.m_iDrawMaxPerPlayer > 0 && drawStore.CostByOwner(requesterId) + cost > drawCfg.m_iDrawMaxPerPlayer)
		{
			if (denyPc)
				denyPc.SM_SendPlaceDenied(SM_EPlaceDenyReason.DRAW_PER_PLAYER_LIMIT, drawCfg.m_iDrawMaxPerPlayer);
			return 0;
		}

		// Trim points to the server limit (protects against an oversized stroke).
		array<int> pts = points;
		int maxVals = drawCfg.m_iDrawMaxPointsPerStroke * 2;
		if (maxVals >= 4 && pts.Count() > maxVals)
		{
			array<int> trimmed = {};
			for (int i = 0; i < maxVals; i++)
				trimmed.Insert(pts[i]);
			pts = trimmed;
		}

		// Channel: derived from the author's faction/group. Exception: a GM has no faction
		// and picks the side for FACTION strokes himself, so his channel comes from meta.
		bool isGm = SM_MarkerNet.IsPlayerGameMaster(requesterId);
		int channel;
		if (isGm && vis == SM_EMarkerVisibility.FACTION)
			channel = SM_MapDrawingData.ChannelFromMeta(meta);
		else
			channel = ChannelFor(requesterId, vis);

		string authorName = GetGame().GetPlayerManager().GetPlayerName(requesterId);
		SM_MapDrawingData d = drawStore.ServerCreate(requesterId, meta, pts, channel, System.GetTickCount(), authorName);
		if (!d)
			return 0;

		// GM-only flags are accepted from actual Game Masters only.
		if (!isGm)
		{
			d.m_iGmLocked = 0;
			d.m_iHideInfo = 0;
		}

		BroadcastAdd(d);
		SM_MarkerNet.Log(string.Format("DRAW #%1 by %2 vis=%3 ch=%4 pts=%5",
			d.m_iId, SM_MarkerNet.Who(requesterId), SM_MarkerNet.VisName(vis), channel, d.GetPointCount()));
		return d.m_iId;
	}

	// Remove one whole stroke. Returns true if removed.
	static bool ServerHandleRemove(int requesterId, int id, SCR_PlayerController denyPc)
	{
		SM_MapDrawingData d = SM_MapDrawingStore.GetInstance().FindById(id);
		if (!d)
			return false;
		if (!CanErase(requesterId, d))
		{
			if (denyPc && d.m_iGmLocked != 0 && !SM_MarkerNet.IsPlayerGameMaster(requesterId))
				denyPc.SM_SendPlaceDenied(SM_EPlaceDenyReason.DRAW_LOCKED, 0);
			SM_MarkerNet.Log(string.Format("DENY DRAW-ERASE #%1 by %2", id, SM_MarkerNet.Who(requesterId)));
			return false;
		}
		SM_MapDrawingStore.GetInstance().ServerRemove(id);
		BroadcastRemove(id);
		SM_MarkerNet.Log(string.Format("DRAW-ERASE #%1 by %2", id, SM_MarkerNet.Who(requesterId)));
		return true;
	}

	// Partial erase: replace the requester's OWN stroke with the leftover pieces.
	// Piece meta (color/width/visibility/channel) comes from the trusted server record —
	// the client only sends geometry, so attributes can't be forged.
	// madeIds (optional): the id of every piece created, in the order the pieces arrived. The batched
	// client pairs them with the optimistic temps it is showing, so a piece erased again while the batch
	// was still in flight can be removed for real instead of resurrecting on the next broadcast.
	// A piece that fails to create still takes a slot (0) — the pairing is by index.
	static void ServerHandleErasePart(int requesterId, int id, array<int> framed, SCR_PlayerController denyPc, array<int> madeIds = null)
	{
		if (!SM_MarkerConfig.GetInstance().m_bAllowDrawing)
			return;
		SM_MapDrawingStore store = SM_MapDrawingStore.GetInstance();
		SM_MapDrawingData old = store.FindById(id);
		if (!old)
			return;
		if (old.m_iOwnerId != requesterId)	// partial erase is strictly owner-only
			return;
		if (old.m_iShape != 0)
			return;	// a shape is parameters, not geometry — it can only ever be removed whole
		if (old.m_iGmLocked != 0 && !SM_MarkerNet.IsPlayerGameMaster(requesterId))
		{
			if (denyPc)
				denyPc.SM_SendPlaceDenied(SM_EPlaceDenyReason.DRAW_LOCKED, 0);
			return;
		}
		if (!framed || framed.Count() < 1)
			return;

		int nPieces = framed[0];
		if (nPieces < 1 || nPieces > MAX_ERASE_PIECES)
			return;
		int maxPts = SM_MarkerConfig.GetInstance().m_iDrawMaxPointsPerStroke;

		array<ref array<int>> pieces = {};
		int pos = 1;
		for (int p = 0; p < nPieces; p++)
		{
			if (pos >= framed.Count())
				return;
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

		array<int> meta = old.PackMeta();
		int keepOwner   = old.m_iOwnerId;
		int keepChannel = old.m_iChannel;
		string keepName = old.m_sOwnerName;

		store.ServerRemove(id);
		BroadcastRemove(id);

		int made = 0;
		foreach (array<int> piecePts : pieces)
		{
			SM_MapDrawingData piece = store.ServerCreate(keepOwner, meta, piecePts, keepChannel, System.GetTickCount(), keepName);
			if (piece)
			{
				BroadcastAdd(piece);
				made++;
			}
			if (madeIds)
			{
				if (piece)
					madeIds.Insert(piece.m_iId);
				else
					madeIds.Insert(0);	// keep the slot so the client's temps still pair up by index
			}
		}
		SM_MarkerNet.Log(string.Format("DRAW-ERASE-PART #%1 by %2 -> %3 pieces", id, SM_MarkerNet.Who(requesterId), made));
	}

	// Recolor a fill in place: remove + recreate with the same points and the new panel meta.
	// The recoloring player becomes the new author. Same permissions as erasing.
	static void ServerHandleRecolor(notnull SCR_PlayerController pc, int id, array<int> meta)
	{
		SM_MarkerConfig drawCfg = SM_MarkerConfig.GetInstance();
		if (!drawCfg.m_bAllowDrawing)
			return;

		SM_MapDrawingStore store = SM_MapDrawingStore.GetInstance();
		SM_MapDrawingData old = store.FindById(id);
		if (!old || old.m_iFill == 0)
			return;

		int requesterId = pc.GetPlayerId();
		if (!CanErase(requesterId, old))
		{
			if (old.m_iGmLocked != 0 && !SM_MarkerNet.IsPlayerGameMaster(requesterId))
				pc.SM_SendPlaceDenied(SM_EPlaceDenyReason.DRAW_LOCKED, 0);
			else
				pc.SM_SendPlaceDenied(SM_EPlaceDenyReason.FILL_BLOCKED, 0);
			return;
		}

		SM_MapDrawingData req = new SM_MapDrawingData();
		if (!req.UnpackMeta(meta))
			return;

		// Visibility can only be widened (Local < Group < Side < Global), same as markers.
		// On a narrow attempt the old channel is kept, other changes still apply, player gets a popup.
		int newVis = req.m_iVisibility;
		bool narrowed = false;
		if (newVis < old.m_iVisibility)
		{
			newVis = old.m_iVisibility;
			narrowed = true;
		}
		if (newVis != old.m_iVisibility && !drawCfg.IsVisibilityAllowed(newVis))
		{
			pc.SM_SendPlaceDenied(SM_EPlaceDenyReason.DRAW_CHANNEL_DISABLED, 0);
			return;
		}

		// Channel: unchanged visibility keeps the old channel; widening recalculates it from
		// the requester. A GM picks the side for FACTION fills on the panel.
		bool isGm = SM_MarkerNet.IsPlayerGameMaster(requesterId);
		int channel = old.m_iChannel;
		if (isGm && newVis == SM_EMarkerVisibility.FACTION)
			channel = req.m_iChannel;
		else if (newVis != old.m_iVisibility)
			channel = ChannelFor(requesterId, newVis);

		// New meta starts from the old (trusted) record; only the allowed fields come from the client.
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
		BroadcastRemove(id);

		SM_MapDrawingData fresh = store.ServerCreate(requesterId, newMeta, pts, channel, System.GetTickCount(), newName);
		if (fresh)
			BroadcastAdd(fresh);
		if (narrowed)
			pc.SM_SendPlaceDenied(SM_EPlaceDenyReason.FILL_NO_NARROW, 0);
		SM_MarkerNet.Log(string.Format("FILL-RECOLOR #%1 by %2 vis=%3 ch=%4", id, SM_MarkerNet.Who(requesterId), SM_MarkerNet.VisName(newVis), channel));
	}

	// Process one client batch (RpcAsk_DrawBatch). Fills addRealIds with the real id of every
	// ADD in order (0 = rejected) — the client uses it to drop its optimistic temp strokes.
	// addRealIds: one id per ADD, in order (0 = rejected).
	// erasePieceIds: one GROUP per ERASE_PART, in order, each [count, id, id, ...]. Grouped rather than
	// flat because an erase-part can be rejected outright (count 0) and a flat list would then silently
	// shift every following op's ids onto the wrong temps.
	static void ProcessBatch(SCR_PlayerController pc, array<int> blob, out array<int> addRealIds, out array<int> erasePieceIds)
	{
		addRealIds = {};
		erasePieceIds = {};
		if (!pc || !blob)
			return;
		int requesterId = pc.GetPlayerId();
		int metaN = SM_MapDrawingData.META_COUNT;

		int pos = 0;
		int guard = 0;
		while (pos < blob.Count() && guard < 100000)
		{
			guard++;
			int type = blob[pos];
			pos++;

			if (type == SM_DrawOutbox.OP_ADD)
			{
				if (pos + metaN + 1 > blob.Count())
					break;
				array<int> meta = {};
				for (int i = 0; i < metaN; i++)
				{
					meta.Insert(blob[pos]);
					pos++;
				}
				int n = blob[pos];
				pos++;
				if (n < 0 || pos + n * 2 > blob.Count())
					break;
				array<int> pts = {};
				for (int k = 0; k < n * 2; k++)
				{
					pts.Insert(blob[pos]);
					pos++;
				}
				// pc (not null) so a rate-limited add tells the player WHY his strokes vanished, and a
				// template learns it hit the window. The popup is throttled client-side against a batch
				// carrying many rejects. Remove/erase below stay silent: their only denial is GM-lock,
				// which would spam on a mass-erase and is not what any rate cap is about.
				addRealIds.Insert(ServerHandleAdd(requesterId, meta, pts, pc));
			}
			else if (type == SM_DrawOutbox.OP_REMOVE)
			{
				if (pos + 1 > blob.Count())
					break;
				int rid = blob[pos];
				pos++;
				ServerHandleRemove(requesterId, rid, null);
			}
			else if (type == SM_DrawOutbox.OP_ERASE_PART)
			{
				if (pos + 2 > blob.Count())
					break;
				int eid = blob[pos];
				pos++;
				int flen = blob[pos];
				pos++;
				if (flen < 0 || pos + flen > blob.Count())
					break;
				array<int> framed = {};
				for (int i = 0; i < flen; i++)
				{
					framed.Insert(blob[pos]);
					pos++;
				}
				array<int> made = {};
				ServerHandleErasePart(requesterId, eid, framed, null, made);
				erasePieceIds.Insert(made.Count());
				foreach (int mid : made)
					erasePieceIds.Insert(mid);
			}
			else
			{
				break;	// unknown op type -> corrupt blob, stop
			}
		}
	}
}
