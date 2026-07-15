// One template being stamped onto the map. Client-side only.
//
// Every stroke leaves through SM_DrawAddOrLocal, the same funnel a hand-drawn one uses, so the server
// needs no idea templates exist and every limit it enforces applies for free. To everyone else this
// looks like a player drawing slowly, because that is what it is.
//
// Static, so it survives closing the map, switching tools and waiting out a rate limit. It does not
// survive a game restart; the player just places it again.
class SM_TemplateSession
{
	protected static ref SM_TemplateSession s_Instance;

	protected string m_sTemplateId;
	protected int    m_iAnchorX, m_iAnchorZ;
	protected int    m_iVisibility;
	protected bool   m_bPlaced;		// the anchor is down, the ghost has stopped following the cursor
	protected bool   m_bConfirmed;	// Apply pressed — only now may it draw

	protected float  m_fNextStrokeAt;	// seconds; the rate gate
	protected bool   m_bDenied;			// the server turned a stroke down — stop until the player asks again
	protected float  m_fPlacedAt;		// seconds; when the anchor went down

	// Which strokes are still missing, cached. Recomputing it is O(template strokes x drawings on the
	// map) and BOTH the auto-draw tick and the ghost want it EVERY FRAME — 40 strokes against 300
	// drawings is 12,000 comparisons a frame, twice. It only changes when the store does, so it is
	// rebuilt on a drawing being added or removed, and never in the frame loop.
	protected ref array<int> m_aTodo = {};
	protected bool m_bTodoDirty = true;
	protected bool m_bSubscribed;

	static SM_TemplateSession GetInstance()
	{
		if (!s_Instance)
			s_Instance = new SM_TemplateSession();
		return s_Instance;
	}

	//! The store is the only thing that changes what is left to do: an erased stroke comes back onto the
	//! list, a landed one drops off it.
	protected void Subscribe()
	{
		if (m_bSubscribed)
			return;
		AM_MarkerAPI.GetOnDrawingAdded().Insert(OnStoreChanged);
		AM_MarkerAPI.GetOnDrawingRemoved().Insert(OnStoreRemoved);
		m_bSubscribed = true;
	}

	protected void OnStoreChanged(SM_MapDrawingData d)
	{
		m_bTodoDirty = true;
	}

	protected void OnStoreRemoved(int id)
	{
		m_bTodoDirty = true;
	}

	//! The anchor is down but not yet confirmed: the ghost sits still, waiting for Apply or Cancel.
	bool IsAnchored()
	{
		return m_bPlaced && !m_bConfirmed && Template() != null;
	}

	//! Confirmed: holding the button on it draws, and holding delete throws the whole thing away.
	bool IsConfirmed()
	{
		return m_bPlaced && m_bConfirmed && Template() != null;
	}

	bool IsPlaced()
	{
		return m_bPlaced && Template() != null;
	}

	void Confirm()
	{
		if (m_bPlaced)
			m_bConfirmed = true;
	}

	//! Back to following the cursor. Nothing was drawn yet, so nothing has to be undone.
	void Unanchor()
	{
		m_bPlaced = false;
		m_bConfirmed = false;
		m_bDenied = false;
	}

	SM_DrawTemplate Template()
	{
		if (m_sTemplateId == "")
			return null;
		return SM_TemplateStore.GetInstance().Find(m_sTemplateId);
	}

	int AnchorX() { return m_iAnchorX; }
	int AnchorZ() { return m_iAnchorZ; }
	int Visibility() { return m_iVisibility; }

	//! One instance at a time: a new template abandons whatever was half-drawn. Two ghosts on the map
	//! leaves no answer to "which one am I holding on?".
	void Place(notnull SM_DrawTemplate t, int worldX, int worldZ, int visibility)
	{
		m_sTemplateId = t.m_sId;
		m_iAnchorX    = worldX;
		m_iAnchorZ    = worldZ;
		m_iVisibility = visibility;
		m_bPlaced     = true;
		m_bConfirmed  = false;
		m_fNextStrokeAt = 0;
		m_bDenied     = false;
		m_fPlacedAt   = System.GetTickCount() / 1000.0;
		m_bTodoDirty  = true;
		Subscribe();
	}

	//! The anchor went down within the last few frames. The press that dropped it (map poll) and the
	//! Apply action listener ride the SAME physical button; whichever runs second must not treat the
	//! press as a second, confirming one.
	bool JustPlaced()
	{
		return m_bPlaced && System.GetTickCount() / 1000.0 - m_fPlacedAt < 0.15;
	}

	void Clear()
	{
		m_sTemplateId = "";
		m_bPlaced = false;
		m_bConfirmed = false;
		m_aTodo.Clear();
		m_bTodoDirty = true;
	}

	//! Throw it away, including whatever of it already reached the map. Abandoning a template and
	//! leaving half a drawing behind is the one outcome nobody wants.
	void Discard()
	{
		SM_DrawTemplate t = Template();
		if (t)
		{
			array<SM_MapDrawingData> mine = {};
			CollectMine(mine);

			foreach (SM_DrawTemplateStroke s : t.m_aStrokes)
			{
				if (!s)
					continue;
				SM_MapDrawingData d = FindOnMap(s, mine);
				if (!d)
					continue;

				if (SM_MapDrawingStore.IsLocalId(d.m_iId))
					SM_LocalDrawingPersistence.GetInstance().RemoveLocal(d.m_iId);
				else
					SM_DrawOutbox.SubmitRemove(d.m_iId);
			}
		}
		Clear();
	}

	// --- what is still missing ---

	//! Indices of the template's strokes that are NOT on the map right now.
	//!
	//! Matched by GEOMETRY, never by id. Ids come from the server and do not survive its restart, so an
	//! id-keyed session would quietly redraw the whole template a second time. Geometry also gets the
	//! other two requirements for free: erase a finished stroke and it reappears in this list, and a
	//! template whose strokes are all present is simply complete.
	void GetTodo(out array<int> outIdx)
	{
		if (!outIdx)
			outIdx = {};
		outIdx.Clear();

		RebuildTodo();
		outIdx.Copy(m_aTodo);
	}

	protected void RebuildTodo()
	{
		if (!m_bTodoDirty)
			return;
		m_bTodoDirty = false;
		m_aTodo.Clear();

		SM_DrawTemplate t = Template();
		if (!t)
			return;

		array<SM_MapDrawingData> mine = {};
		CollectMine(mine);

		foreach (int i, SM_DrawTemplateStroke s : t.m_aStrokes)
		{
			if (!s)
				continue;
			if (!IsOnMap(s, mine))
				m_aTodo.Insert(i);
		}
	}

	//! Only OUR drawings can be a template's output, and only they may be matched against it.
	protected void CollectMine(out array<SM_MapDrawingData> outMine)
	{
		outMine.Clear();

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		int myId = -1;
		if (pc)
			myId = pc.GetPlayerId();

		array<SM_MapDrawingData> all = {};
		SM_MapDrawingStore.GetInstance().GetAll(all);

		foreach (SM_MapDrawingData d : all)
		{
			if (!d || d.GetPointCount() < 2)
				continue;
			if (d.m_iOwnerId == myId || SM_MapDrawingStore.IsLocalId(d.m_iId))
				outMine.Insert(d);
		}
	}

	//! Is this template stroke already drawn?
	//!
	//! We compare style + FIRST point + LAST point + bounding box, not every vertex. That is not
	//! laziness: a server may run RDP simplification (drawRdpEpsilonMeters), and then the stroke that
	//! comes back has FEWER points than we sent. RDP never moves the endpoints and never drops an
	//! extreme, so exactly these five things survive it — and an exact vertex-by-vertex compare would
	//! declare the stroke missing and redraw it forever.
	protected bool IsOnMap(notnull SM_DrawTemplateStroke s, notnull array<SM_MapDrawingData> mine)
	{
		return FindOnMap(s, mine) != null;
	}

	//! The drawing this template stroke produced, or null if it is not on the map.
	protected SM_MapDrawingData FindOnMap(notnull SM_DrawTemplateStroke s, notnull array<SM_MapDrawingData> mine)
	{
		int n = s.GetPointCount();
		if (n < 2)
			return null;

		int fx = s.m_aPoints[0] + m_iAnchorX;
		int fz = s.m_aPoints[1] + m_iAnchorZ;
		int lx = s.m_aPoints[(n - 1) * 2]     + m_iAnchorX;
		int lz = s.m_aPoints[(n - 1) * 2 + 1] + m_iAnchorZ;

		int loX, hiX, loZ, hiZ;
		StrokeBounds(s, loX, hiX, loZ, hiZ);

		foreach (SM_MapDrawingData d : mine)
		{
			if (d.m_iColor != s.m_iColor || d.m_iWidthIdx != s.m_iWidthIdx || d.m_iFill != s.m_iFill)
				continue;

			int dn = d.GetPointCount();
			int dfx, dfz, dlx, dlz;
			d.GetPoint(0, dfx, dfz);
			d.GetPoint(dn - 1, dlx, dlz);
			if (dfx != fx || dfz != fz || dlx != lx || dlz != lz)
				continue;

			int dloX, dhiX, dloZ, dhiZ;
			if (!d.GetAABB(dloX, dhiX, dloZ, dhiZ))
				continue;
			if (dloX != loX || dhiX != hiX || dloZ != loZ || dhiZ != hiZ)
				continue;

			return d;
		}
		return null;
	}

	protected void StrokeBounds(notnull SM_DrawTemplateStroke s, out int loX, out int hiX, out int loZ, out int hiZ)
	{
		loX = s.m_aPoints[0] + m_iAnchorX;
		hiX = loX;
		loZ = s.m_aPoints[1] + m_iAnchorZ;
		hiZ = loZ;

		for (int i = 2, cnt = s.m_aPoints.Count(); i < cnt; i += 2)
		{
			int x = s.m_aPoints[i]     + m_iAnchorX;
			int z = s.m_aPoints[i + 1] + m_iAnchorZ;
			if (x < loX) loX = x;
			if (x > hiX) hiX = x;
			if (z < loZ) loZ = z;
			if (z > hiZ) hiZ = z;
		}
	}


	// --- auto-drawing ---

	//! One stroke at most, and only once the rate gate expires — the server counts every stroke against
	//! a per-minute window, so going faster than the window only earns a rejection.
	//! Returns true while there is still something to do.
	bool Tick(notnull SM_DrawCanvas canvas)
	{
		if (!IsConfirmed() || m_bDenied)
			return false;

		SM_DrawTemplate t = Template();
		if (!t)
			return false;

		array<int> todo = {};
		GetTodo(todo);
		if (todo.IsEmpty())
		{
			// Done. Let go of the template entirely — leaving it in hand meant the very next click
			// started ANOTHER copy of it under the cursor. What is on the map is now just a drawing.
			Clear();
			SM_TemplateStore.GetInstance().Select("");
			canvas.SetActive(false);
			return false;
		}

		float now = System.GetTickCount() / 1000.0;
		if (now < m_fNextStrokeAt)
			return true;	// still work to do, just not this frame

		SM_DrawTemplateStroke s = t.m_aStrokes[todo[0]];
		if (!s)
			return true;

		array<int> pts = {};
		s.ToWorld(m_iAnchorX, m_iAnchorZ, pts);

		SM_MapDrawingData d = new SM_MapDrawingData();
		d.m_iColor      = s.m_iColor;		// the alpha the author drew with is already baked in
		d.m_iWidthIdx   = s.m_iWidthIdx;
		d.m_iFill       = s.m_iFill;
		d.m_iVisibility = m_iVisibility;
		d.m_iChannel    = -1;				// the server assigns it from the visibility

		canvas.SM_TemplateEmit(d, pts);

		m_fNextStrokeAt = now + SM_TemplateStore.AutoDrawIntervalMs(m_iVisibility) / 1000.0;
		return true;
	}

	//! A refusal STOPS the drawing; retrying next frame would just hammer the server. The instance stays
	//! put, so the player waits the window out and holds the button again.
	void OnDenied()
	{
		if (m_bPlaced)
			m_bDenied = true;
	}

	//! The player pressed the button again — he has decided to carry on.
	void ClearDenied()
	{
		m_bDenied = false;
		m_fNextStrokeAt = 0;
	}
}
