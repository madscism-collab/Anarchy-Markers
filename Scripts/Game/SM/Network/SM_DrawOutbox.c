// Client-side outbox for server-channel draw/erase operations. Instead of one RPC per
// operation, the client buffers them and sends a single packet (RpcAsk_DrawBatch) every
// config interval (SM_MarkerConfig.m_iDrawBatchIntervalMs). This cuts RPC count massively
// when mass-erasing or with an always-open tablet mod. The author sees his strokes RIGHT
// AWAY through an optimistic temp stroke (negative id, never persisted or synced); other
// players see them once the packet goes out. The Local channel never comes through here
// (it's client-only and instant anyway). interval=0 -> batching off, send immediately.
//
// Reconcile: the server returns the real id of every ADD in order; the client then drops
// its optimistic temp (the real stroke already arrived via broadcast). A whole remove hides
// the stroke at once. A partial erase is driven by the eraser drag (SM_DrawCanvas): it cuts
// the stroke client-side and shows the leftover pieces as temps, then hands them here via
// AdoptErasePart with one erase-part for the whole drag; those temps drop on reconcile once the
// server's authoritative pieces have arrived via broadcast.

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
	protected static ref map<int, ref array<int>> s_mSentErasePieces = new map<int, ref array<int>>();	// seq -> optimistic erase-piece temps to drop on reconcile
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
	// treats them as own) and drop them on reconcile once the server's real pieces arrive.
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
				s_aOps.Remove(i);	// not sent yet — just drop the add, nothing reaches the server
				return;
			}
		}
		// already sent (awaiting reconcile) — remember to remove the real stroke too
		s_KillOnReconcile.Insert(tempId);
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
		array<int> erasePieces = {};
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
				if (op.m_aPieceTemps)
					for (int i = 0; i < op.m_aPieceTemps.Count(); i++)
						erasePieces.Insert(op.m_aPieceTemps[i]);
			}
		}

		s_aOps.Clear();
		s_iPendingInts = 0;
		s_mSentTemps.Set(seq, temps);
		if (!erasePieces.IsEmpty())
			s_mSentErasePieces.Set(seq, erasePieces);
		pc.SM_DrawSendBatch(seq, blob);
	}

	// The server returned the real id of every ADD in batch seq (0 = rejected). Drop the
	// optimistic temps — the real strokes already arrived via broadcast. If a temp was erased
	// while in flight, remove its real counterpart too.
	static void OnReconcile(int seq, array<int> realIds)
	{
		SM_MapDrawingStore store = SM_MapDrawingStore.GetInstance();

		// Erase-part leftovers we showed optimistically: the server's real pieces have arrived
		// via broadcast, so drop our temps now.
		array<int> erasePieces = s_mSentErasePieces.Get(seq);
		if (erasePieces)
		{
			s_mSentErasePieces.Remove(seq);
			foreach (int ep : erasePieces)
			{
				store.ApplyRemove(ep);
				s_ServerTemps.RemoveItem(ep);
			}
		}

		array<int> temps = s_mSentTemps.Get(seq);
		if (!temps)
			return;
		s_mSentTemps.Remove(seq);

		for (int i = 0; i < temps.Count(); i++)
		{
			int tempId = temps[i];
			store.ApplyRemove(tempId);
			s_ServerTemps.RemoveItem(tempId);

			if (s_KillOnReconcile.Contains(tempId))
			{
				s_KillOnReconcile.RemoveItem(tempId);
				int realId = 0;
				if (i < realIds.Count())
					realId = realIds[i];
				if (realId > 0)
					SubmitRemove(realId);	// erased after sending — remove the real one (goes out with the next batch)
			}
		}
	}
}
