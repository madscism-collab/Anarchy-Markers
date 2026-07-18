// Client-side outbox for server-channel draw/erase operations. Instead of one RPC per
// operation, the client buffers them and sends a single packet (RpcAsk_DrawBatch) every
// config interval (SM_MarkerConfig.m_iDrawBatchIntervalMs). This cuts RPC count massively
// when mass-erasing or with an always-open tablet mod. The author sees his strokes RIGHT
// AWAY through an optimistic temp stroke (negative id, never persisted or synced); other
// players see them once the packet goes out. The Local channel never comes through here
// (it's client-only and instant anyway). interval=0 -> batching off, send immediately.
//
// Reconcile: the server reports the real id of every ADD, and the real pieces of every erase-part
// grouped per op; the client pairs each optimistic temp with its real counterpart by index, then
// drops the temp (the real stroke already arrived via broadcast). A whole remove hides the stroke at
// once. A partial erase is driven by the eraser drag (SM_DrawCanvas): it cuts the stroke client-side
// and shows the leftover pieces as temps, then hands them here via AdoptErasePart with one erase-part
// for the whole drag.
//
// The pairing is what lets a temp be erased AGAIN before its batch is answered: the kill is remembered
// against the temp and re-issued against the real id once reconcile names it. Without that the erase
// would be lost and the server's copy — already broadcast — would look like the stroke coming back.

// One queued operation. Encoded into the flat int blob only at send time — that way an
// unsent add can simply be dropped from the queue when the player erases it again.
class SM_DrawOpRec
{
	int m_iType;			// SM_DrawOutbox.OP_*
	int m_iTempId;			// ADD: optimistic negative id
	int m_iRefId;			// REMOVE / ERASE_PART: target id
	ref array<int> m_aMeta;		// ADD
	ref array<int> m_aPoints;	// ADD (x,z pairs)
	ref array<int> m_aFramed;	// ERASE_PART
	ref array<int> m_aPieceTemps;	// ERASE_PART: optimistic leftover-piece temps to drop on reconcile
}

class SM_DrawOutbox
{
	static const int OP_ADD        = 0;
	static const int OP_REMOVE     = 1;
	static const int OP_ERASE_PART = 2;

	static const int MAX_BATCH_INTS = 900;	// soft packet size cap -> flush early instead of growing the RPC

	protected static ref array<ref SM_DrawOpRec> s_aOps = {};		// current (unsent) batch
	protected static int s_iPendingInts = 0;						// rough size of the current batch
	protected static float s_fNextFlush = 0;						// next flush time (System.GetTickCount ms)
	protected static int s_iSeq = 1;								// batch counter
	protected static ref map<int, ref array<int>> s_mSentTemps = new map<int, ref array<int>>();	// seq -> ADD temp ids, in order
	// seq -> one group of erase-piece temps per ERASE_PART op, in send order. Grouped to match the
	// server's per-op reply, so temp[i] of an op pairs with that op's real piece[i].
	protected static ref map<int, ref array<ref array<int>>> s_mSentErasePieces = new map<int, ref array<ref array<int>>>();
	protected static ref set<int> s_ServerTemps = new set<int>();	// live optimistic temps (to tell them apart from Local-channel ids)
	protected static ref set<int> s_KillOnReconcile = new set<int>();	// temps erased AFTER sending -> also remove the real stroke on reconcile

	protected static int BatchIntervalMs()
	{
		return SM_MarkerConfig.GetInstance().m_iDrawBatchIntervalMs;
	}

	// Batching only runs on a client (the host's RPCs are local calls anyway) and only when
	// the interval is set.
	static bool Enabled()
	{
		return !Replication.IsServer() && BatchIntervalMs() > 0;
	}

	static bool IsServerTemp(int id)
	{
		return s_ServerTemps.Contains(id);
	}

	protected static SCR_PlayerController LocalPC()
	{
		return SCR_PlayerController.Cast(GetGame().GetPlayerController());
	}

	// Drop everything without sending (new mission). For faction change / map close call Flush() first.
	static void Reset()
	{
		s_aOps.Clear();
		s_iPendingInts = 0;
		s_fNextFlush = 0;
		s_mSentTemps.Clear();
		s_mSentErasePieces.Clear();
		s_ServerTemps.Clear();
		s_KillOnReconcile.Clear();
	}

	// --- Submitting operations (canvas/map layer call these for SERVER channels) ---

	// Add a stroke/fill. data must be final (meta set, SetPoints already called).
	static void SubmitAdd(notnull SM_MapDrawingData data)
	{
		if (!Enabled())
		{
			SCR_PlayerController pc = LocalPC();
			if (pc)
				pc.SM_DrawRequestAdd(data.PackMeta(), data.m_aPoints);
			return;
		}

		// Optimistic temp stroke: negative id, store-only (not persisted, not synced).
		SM_MapDrawingData opt = data.SM_Clone();
		SCR_PlayerController lpc = LocalPC();
		if (lpc)
			opt.m_iOwnerId = lpc.GetPlayerId();	// so the eraser treats the temp as "own" (partial erase works)
		SM_MapDrawingData created = SM_MapDrawingStore.GetInstance().LocalCreate(opt);
		if (!created)
			return;
		int tempId = created.m_iId;
		s_ServerTemps.Insert(tempId);

		SM_DrawOpRec op = new SM_DrawOpRec();
		op.m_iType   = OP_ADD;
		op.m_iTempId = tempId;
		op.m_aMeta   = data.PackMeta();
		op.m_aPoints = {};
		op.m_aPoints.Copy(data.m_aPoints);
		s_aOps.Insert(op);
		s_iPendingInts += 2 + op.m_aMeta.Count() + op.m_aPoints.Count();
		Arm();
	}

	// Remove a whole stroke (a real server id, or one of our temps).
	static void SubmitRemove(int id)
	{
		if (!Enabled())
		{
			SCR_PlayerController pc = LocalPC();
			if (pc)
				pc.SM_DrawRequestRemove(id);
			return;
		}

		if (IsServerTemp(id))
		{
			CancelTemp(id);
			return;
		}

		SM_MapDrawingStore.GetInstance().ApplyRemove(id);	// optimistic: vanish immediately
		SM_DrawOpRec op = new SM_DrawOpRec();
		op.m_iType  = OP_REMOVE;
		op.m_iRefId = id;
		s_aOps.Insert(op);
		s_iPendingInts += 2;
		Arm();
	}

	// Partially erase an OWN stroke (framed = pieces left outside the eraser; server format).
	static void SubmitErasePart(int id, notnull array<int> framed)
	{
		if (!Enabled())
		{
			SCR_PlayerController pc = LocalPC();
			if (pc)
				pc.SM_DrawRequestErasePart(id, framed);
			return;
		}

		// Only reached for an optimistic ADD temp (a real stroke erases through AdoptErasePart, which
		// keeps the drag's own progressive pieces). The temp isn't confirmed yet: cancel its add and
		// re-add the leftover pieces as fresh optimistic adds.
		if (IsServerTemp(id))
		{
			SM_MapDrawingData old = SM_MapDrawingStore.GetInstance().FindById(id);
			if (old)
				SplitTempIntoPieces(old, framed);
			CancelTemp(id);
			return;
		}

		// Fallback for an unexpected real id: drop it and queue the erase; the server splits it and
		// broadcasts the pieces. (Not used by the normal eraser path — kept defensive.)
		SM_MapDrawingStore.GetInstance().ApplyRemove(id);
		SM_DrawOpRec op = new SM_DrawOpRec();
		op.m_iType   = OP_ERASE_PART;
		op.m_iRefId  = id;
		op.m_aFramed = {};
		op.m_aFramed.Copy(framed);
		s_aOps.Insert(op);
		s_iPendingInts += 3 + framed.Count();
		Arm();
	}

	// Partial erase whose leftover pieces the CALLER already shows as temps (the eraser drag builds
	// them client-side against the original geometry and sends one erase-part when the drag ends).
	// We adopt those temps instead of making our own: mark them as server temps (so a later eraser
	// treats them as own) and pair them with the server's real pieces on reconcile.
	//
	// CONTRACT: temps must hold exactly one id per piece in `framed`, in the same order. The pairing is
	// by index — a temps list that doesn't mirror the pieces sent would map kills onto the wrong strokes.
	static void AdoptErasePart(int id, notnull array<int> framed, notnull array<int> temps)
	{
		if (!Enabled())
		{
			// Direct path has no reconcile: drop the temps and let the immediate broadcast show the
			// authoritative pieces (on a listen-host that broadcast is a local, same-frame call).
			SM_MapDrawingStore store = SM_MapDrawingStore.GetInstance();
			foreach (int t : temps)
				store.ApplyRemove(t);
			SCR_PlayerController pc = LocalPC();
			if (pc)
				pc.SM_DrawRequestErasePart(id, framed);
			return;
		}

		foreach (int t : temps)
			s_ServerTemps.Insert(t);

		SM_DrawOpRec op = new SM_DrawOpRec();
		op.m_iType       = OP_ERASE_PART;
		op.m_iRefId      = id;
		op.m_aFramed     = {};
		op.m_aFramed.Copy(framed);
		op.m_aPieceTemps = {};
		op.m_aPieceTemps.Copy(temps);
		s_aOps.Insert(op);
		s_iPendingInts += 3 + framed.Count();
		Arm();
	}

	// Cancel an optimistic temp: remove it from the store, and from the queue if not sent yet;
	// if it's already in flight, mark it so the real stroke gets removed on reconcile.
	protected static void CancelTemp(int tempId)
	{
		SM_MapDrawingStore.GetInstance().ApplyRemove(tempId);
		s_ServerTemps.RemoveItem(tempId);

		for (int i = s_aOps.Count() - 1; i >= 0; i--)
		{
			if (s_aOps[i] && s_aOps[i].m_iType == OP_ADD && s_aOps[i].m_iTempId == tempId)
			{
				// Not sent yet — just drop the add, nothing reaches the server. Ordered removal (ops
				// replay in queue order server-side) and the size counter follows the op out.
				s_iPendingInts -= 2 + s_aOps[i].m_aMeta.Count() + s_aOps[i].m_aPoints.Count();
				if (s_iPendingInts < 0)
					s_iPendingInts = 0;
				s_aOps.RemoveOrdered(i);
				return;
			}
		}

		if (PruneUnsentErasePiece(tempId))
			return;

		// already sent (awaiting reconcile) — remember to remove the real stroke too
		s_KillOnReconcile.Insert(tempId);
	}

	// An erase-piece temp whose ERASE_PART op is still sitting in the queue can be cut out of the op
	// itself: the server then never creates the doomed piece, other clients never see it flash in, and
	// no follow-up remove has to chase it a batch later. With the default 3s interval this is the
	// COMMON mass-erase case, not a corner. Returns true when the temp was handled this way.
	protected static bool PruneUnsentErasePiece(int tempId)
	{
		foreach (SM_DrawOpRec op : s_aOps)
		{
			if (!op || op.m_iType != OP_ERASE_PART || !op.m_aPieceTemps)
				continue;
			int k = op.m_aPieceTemps.Find(tempId);
			if (k == -1)
				continue;

			// Rebuild framed without piece k. The blocks are variable-length ([count, len, pts...]),
			// the arrays are tiny — a copy is simpler than in-place surgery and immune to walk mistakes.
			array<int> old = op.m_aFramed;
			array<int> next = {};
			next.Insert(old[0] - 1);
			int pos = 1;
			for (int p = 0; p < old[0]; p++)
			{
				if (pos >= old.Count())
					break;
				int block = 1 + old[pos] * 2;
				if (p != k)
				{
					for (int r = 0; r < block && pos + r < old.Count(); r++)
						next.Insert(old[pos + r]);
				}
				pos += block;
			}
			op.m_aPieceTemps.RemoveOrdered(k);

			if (next[0] <= 0)
			{
				// that was the last piece — every leftover is erased, so the whole stroke goes
				op.m_iType = OP_REMOVE;
				op.m_aFramed = null;
				op.m_aPieceTemps = null;
				s_iPendingInts -= 1 + old.Count();	// (3 + framed) became a 2-int remove
			}
			else
			{
				op.m_aFramed = next;
				s_iPendingInts -= old.Count() - next.Count();
			}
			if (s_iPendingInts < 0)
				s_iPendingInts = 0;
			return true;
		}
		return false;
	}

	// Split an optimistic temp into pieces (new optimistic adds) — used when the eraser cuts
	// a stroke that hasn't been confirmed by the server yet.
	protected static void SplitTempIntoPieces(notnull SM_MapDrawingData old, notnull array<int> framed)
	{
		if (framed.IsEmpty())
			return;
		int nPieces = framed[0];
		int pos = 1;
		int maxPts = SM_MarkerConfig.GetInstance().m_iDrawMaxPointsPerStroke;
		for (int p = 0; p < nPieces; p++)
		{
			if (pos >= framed.Count())
				break;
			int len = framed[pos];
			pos++;
			if (len < 2 || len > maxPts || pos + len * 2 > framed.Count())
				break;
			array<int> pts = {};
			for (int k = 0; k < len * 2; k++)
				pts.Insert(framed[pos + k]);
			pos += len * 2;

			SM_MapDrawingData piece = old.SM_Clone();
			piece.m_iId = -1;
			piece.SetPoints(pts);
			SubmitAdd(piece);
		}
	}

	// --- Send timer (SM_DrawCanvas.Tick calls this every frame while the map is open) ---

	protected static void Arm()
	{
		if (s_iPendingInts >= MAX_BATCH_INTS)
		{
			Flush();	// batch got big — send now instead of waiting out the interval
			return;
		}
		if (s_fNextFlush == 0)
			s_fNextFlush = System.GetTickCount() + BatchIntervalMs();
	}

	static void Tick()
	{
		if (s_aOps.IsEmpty())
			return;
		if (s_fNextFlush != 0 && System.GetTickCount() >= s_fNextFlush)
			Flush();
	}

	// Send the buffered batch as one RPC. Public — also called on map close / faction change.
	static void Flush()
	{
		s_fNextFlush = 0;
		if (s_aOps.IsEmpty())
			return;

		SCR_PlayerController pc = LocalPC();
		if (!pc)
		{
			s_aOps.Clear();
			s_iPendingInts = 0;
			return;
		}

		int seq = s_iSeq;
		s_iSeq++;

		array<int> blob = {};
		array<int> temps = {};
		array<ref array<int>> erasePieceGroups = {};
		foreach (SM_DrawOpRec op : s_aOps)
		{
			if (!op)
				continue;
			blob.Insert(op.m_iType);
			if (op.m_iType == OP_ADD)
			{
				for (int i = 0; i < op.m_aMeta.Count(); i++)
					blob.Insert(op.m_aMeta[i]);
				blob.Insert(op.m_aPoints.Count() / 2);
				for (int k = 0; k < op.m_aPoints.Count(); k++)
					blob.Insert(op.m_aPoints[k]);
				temps.Insert(op.m_iTempId);
			}
			else if (op.m_iType == OP_REMOVE)
			{
				blob.Insert(op.m_iRefId);
			}
			else if (op.m_iType == OP_ERASE_PART)
			{
				blob.Insert(op.m_iRefId);
				blob.Insert(op.m_aFramed.Count());
				for (int i = 0; i < op.m_aFramed.Count(); i++)
					blob.Insert(op.m_aFramed[i]);
				// One group per erase-part, ALWAYS — the server replies per op, so an op that carries no
				// temps (the defensive real-id path in SubmitErasePart) still has to take its slot.
				array<int> group = {};
				if (op.m_aPieceTemps)
					group.Copy(op.m_aPieceTemps);
				erasePieceGroups.Insert(group);
			}
		}

		s_aOps.Clear();
		s_iPendingInts = 0;
		s_mSentTemps.Set(seq, temps);
		if (!erasePieceGroups.IsEmpty())
			s_mSentErasePieces.Set(seq, erasePieceGroups);
		pc.SM_DrawSendBatch(seq, blob);
	}

	// The server reports what batch seq produced: the real id of every ADD (0 = rejected), and the real
	// pieces of every ERASE_PART grouped per op. Both kinds of optimistic temp resolve the same way, so
	// both go through ResolveTemps.
	//
	// Erase-piece temps used to be dropped blind here, with no real id behind them. That lost every erase
	// aimed at a leftover piece while its batch was in flight (up to the whole batch interval): the temp
	// vanished, the kill request had nothing to name, and the server's authoritative piece — already
	// broadcast — simply stayed. That was strokes "coming back" after being erased.
	static void OnReconcile(int seq, array<int> realIds, array<int> erasePieceIds)
	{
		SM_MapDrawingStore store = SM_MapDrawingStore.GetInstance();

		array<ref array<int>> groups = s_mSentErasePieces.Get(seq);
		if (groups)
		{
			s_mSentErasePieces.Remove(seq);
			int pos = 0;
			foreach (array<int> groupTemps : groups)
			{
				array<int> made = {};
				if (erasePieceIds && pos < erasePieceIds.Count())
				{
					int n = erasePieceIds[pos];
					pos++;
					for (int k = 0; k < n && pos < erasePieceIds.Count(); k++)
					{
						made.Insert(erasePieceIds[pos]);
						pos++;
					}
				}
				ResolveTemps(store, groupTemps, made);
			}
		}

		array<int> temps = s_mSentTemps.Get(seq);
		if (!temps)
			return;
		s_mSentTemps.Remove(seq);
		ResolveTemps(store, temps, realIds);
	}

	// Drop optimistic temps now that their authoritative counterparts have arrived by broadcast. A temp
	// the player erased while the batch was in flight takes its real counterpart with it — that removal
	// rides out with the next batch. realIds pairs with temps by index; a short list (the server rejected
	// the op, so nothing was created and nothing can have come back) just leaves nothing to remove.
	protected static void ResolveTemps(SM_MapDrawingStore store, array<int> temps, array<int> realIds)
	{
		if (!temps)
			return;

		for (int i = 0; i < temps.Count(); i++)
		{
			int tempId = temps[i];
			store.ApplyRemove(tempId);
			s_ServerTemps.RemoveItem(tempId);

			if (!s_KillOnReconcile.Contains(tempId))
				continue;
			s_KillOnReconcile.RemoveItem(tempId);

			int realId = 0;
			if (realIds && i < realIds.Count())
				realId = realIds[i];
			if (realId > 0)
				SubmitRemove(realId);
		}
	}
}
