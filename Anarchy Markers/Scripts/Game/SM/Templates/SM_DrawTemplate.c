// A drawing template: strokes and fills, saved once and stamped down again anywhere on the map.
//
// Geometry and style, nothing else — no visibility, owner, ids or timestamps. The channel is picked
// when the template is placed, which is what lets the same one go into Local here and Side there.
//
// Points are metres relative to the anchor (the centre of what was selected), so placing it is a plain
// translation: brush widths stay meaningful, and there is no rotation yet.

class SM_DrawTemplateStroke
{
	// A filled outline past this gets thinned on load — see SimplifyOversizedFill.
	protected const int TPL_FILL_MAX_PTS = 400;

	int m_iColor;
	int m_iWidthIdx;
	int m_iFill;					// 1 = filled area: the points are a closed outline
	ref array<int> m_aPoints = {};	// x,z pairs, metres, relative to the anchor

	// Triangulation of a filled outline, worked out once: the ghost is rebuilt every frame.
	protected ref array<int> m_aTri;
	protected bool m_bTriTried;

	//! Indices for a filled outline, or null when it is a plain stroke or a shape that cannot be
	//! triangulated (self-intersecting, degenerate).
	array<int> FillIndices()
	{
		if (m_iFill == 0)
			return null;
		if (!m_bTriTried)
		{
			m_bTriTried = true;
			array<int> tri = {};
			if (SM_MapFloodFill.Triangulate(m_aPoints, tri) && tri.Count() >= 3)
				m_aTri = tri;
		}
		return m_aTri;
	}

	int GetPointCount()
	{
		return m_aPoints.Count() / 2;
	}

	//! World points for an anchor, ready to hand to the drawing store.
	void ToWorld(int anchorX, int anchorZ, out array<int> outPts)
	{
		outPts.Clear();
		outPts.Reserve(m_aPoints.Count());
		for (int i = 0, n = m_aPoints.Count(); i < n; i += 2)
		{
			outPts.Insert(m_aPoints[i]     + anchorX);
			outPts.Insert(m_aPoints[i + 1] + anchorZ);
		}
	}

	void SerializeTo(notnull JsonSaveContext ctx)
	{
		ctx.WriteValue("color", m_iColor);
		ctx.WriteValue("width", m_iWidthIdx);
		ctx.WriteValue("fill",  m_iFill);
		ctx.WriteValue("pts",   m_aPoints);
	}

	bool DeserializeFrom(notnull JsonLoadContext ctx)
	{
		ctx.ReadValue("color", m_iColor);
		ctx.ReadValue("width", m_iWidthIdx);
		ctx.ReadValue("fill",  m_iFill);
		ctx.ReadValue("pts",   m_aPoints);

		// A template is a plain text file the player can edit, and one shared by a friend is a text
		// file SOMEBODY ELSE edited. The server clamps this too, and that is what actually protects
		// the map — but a template that would be silently corrected on arrival is a template whose
		// ghost lies about what it is going to draw.
		m_iWidthIdx = SM_DrawCanvas.ClampWidthIdx(m_iWidthIdx);
		if (m_iWidthIdx > SM_DrawCanvas.WIDTH_IDX_MAX_PENCIL)
			m_iWidthIdx = SM_DrawCanvas.WIDTH_IDX_MAX_PENCIL;
		if (m_iFill != 0)
		{
			m_iFill = 1;
			SimplifyOversizedFill();
		}

		return m_aPoints.Count() >= 4 && (m_aPoints.Count() % 2) == 0;	// at least two points
	}

	//! Fills traced around SCRIBBLES are the pathological case: the outline follows every wobble of a
	//! hand-drawn squiggle, so one fill can carry thousands of points. Triangulation is O(n²) and the
	//! ghost triangulates every fill the template holds, so a file with a few dozen of them locks the
	//! map up for seconds on load — measured against a 2245-stroke drawing that opens instantly because
	//! its fills are tiny. The dropped detail is invisible: these outlines are metre-precise around
	//! shapes tens of metres across. Only the in-memory copy is thinned; the file keeps what it had.
	protected void SimplifyOversizedFill()
	{
		if (GetPointCount() <= TPL_FILL_MAX_PTS)
			return;

		float eps = 1.0;
		array<int> simp = SM_PolylineUtil.RDPSimplify(m_aPoints, eps);
		int guard = 0;
		while (simp.Count() / 2 > TPL_FILL_MAX_PTS && guard < 14)
		{
			eps = eps * 1.6;
			simp = SM_PolylineUtil.RDPSimplify(m_aPoints, eps);
			guard++;
		}

		if (simp.Count() >= 8)
			m_aPoints = simp;
	}
}

class SM_DrawTemplate
{
	string m_sId;		// file name without the extension
	string m_sFile;		// the file it came from; not serialised — rename it and this follows
	string m_sName;		// what the player sees
	bool   m_bBuiltIn;	// shipped with the mod: cannot be overwritten or deleted
	int    m_iShape;	// SM_ShapeGeometry.SHAPE_*: a built-in parametric shape, no strokes at all.
						// Never serialised — built-ins are code, not files.
	int    m_iSpanX, m_iSpanZ;	// bounding box in metres — the panel draws a thumbnail from it

	ref array<ref SM_DrawTemplateStroke> m_aStrokes = {};

	int StrokeCount()
	{
		return m_aStrokes.Count();
	}

	//! Longest stroke in the template. The server caps points per stroke, so this decides on its own
	//! whether the template can exist here at all.
	int MaxPointsInAStroke()
	{
		int worst = 0;
		foreach (SM_DrawTemplateStroke s : m_aStrokes)
		{
			if (s && s.GetPointCount() > worst)
				worst = s.GetPointCount();
		}
		return worst;
	}

	//! Does any stroke exceed the cap that applies to IT? Fills have their own, higher budget than
	//! hand-drawn strokes — a filled area's outline legitimately carries far more points — so measuring
	//! everything against the stroke cap refused perfectly placeable templates that contained a fill.
	//! Returns the offending stroke's own cap in outCap, so the message can quote the right number.
	bool HasOverlongStroke(int strokeCap, int fillCap, out int outCap)
	{
		outCap = strokeCap;
		foreach (SM_DrawTemplateStroke s : m_aStrokes)
		{
			if (!s)
				continue;
			int cap = strokeCap;
			if (s.m_iFill != 0)
				cap = fillCap;
			if (cap > 0 && s.GetPointCount() > cap)
			{
				outCap = cap;
				return true;
			}
		}
		return false;
	}

	void RecomputeSpan()
	{
		m_iSpanX = 0;
		m_iSpanZ = 0;
		if (m_aStrokes.IsEmpty())
			return;

		bool first = true;
		int loX, hiX, loZ, hiZ;
		foreach (SM_DrawTemplateStroke s : m_aStrokes)
		{
			if (!s)
				continue;
			for (int i = 0, n = s.m_aPoints.Count(); i < n; i += 2)
			{
				int x = s.m_aPoints[i];
				int z = s.m_aPoints[i + 1];
				if (first)
				{
					loX = x; hiX = x; loZ = z; hiZ = z;
					first = false;
					continue;
				}
				if (x < loX) loX = x;
				if (x > hiX) hiX = x;
				if (z < loZ) loZ = z;
				if (z > hiZ) hiZ = z;
			}
		}
		if (first)
			return;

		m_iSpanX = hiX - loX;
		m_iSpanZ = hiZ - loZ;
	}

	void SerializeTo(notnull JsonSaveContext ctx)
	{
		ctx.WriteValue("version", 1);
		ctx.WriteValue("name",  m_sName);
		ctx.WriteValue("spanX", m_iSpanX);
		ctx.WriteValue("spanZ", m_iSpanZ);
		ctx.WriteValue("count", m_aStrokes.Count());

		foreach (int i, SM_DrawTemplateStroke s : m_aStrokes)
		{
			ctx.StartObject(string.Format("s_%1", i));
			s.SerializeTo(ctx);
			ctx.EndObject();
		}
	}

	bool DeserializeFrom(notnull JsonLoadContext ctx)
	{
		ctx.ReadValue("name",  m_sName);
		ctx.ReadValue("spanX", m_iSpanX);
		ctx.ReadValue("spanZ", m_iSpanZ);

		int count;
		ctx.ReadValue("count", count);

		m_aStrokes.Clear();
		for (int i = 0; i < count; i++)
		{
			// JsonLoadContext can only read a name it already knows — it cannot enumerate keys. Hence
			// the flat count + s_0..s_N shape, the same one the local drawing file uses.
			if (!ctx.StartObject(string.Format("s_%1", i)))
				continue;

			SM_DrawTemplateStroke s = new SM_DrawTemplateStroke();
			bool ok = s.DeserializeFrom(ctx);
			ctx.EndObject();

			if (ok)
				m_aStrokes.Insert(s);
		}

		if (m_aStrokes.IsEmpty())
			return false;

		if (m_iSpanX == 0 && m_iSpanZ == 0)
			RecomputeSpan();	// written by an older/hand-made file
		return true;
	}
}
