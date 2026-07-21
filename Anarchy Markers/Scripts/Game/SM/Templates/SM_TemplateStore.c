// The player's templates: one file per template, in $profile:SavingMarkers/Templates/.
//
// Client-side only, and shared across every server the player joins — a template is a drawing he made
// once, not a thing that belongs to a mission. That is why it does NOT live under Local/, which is
// keyed by server code.
//
// The folder IS the list: it is scanned with FileIO.FindFiles, and every .json in it is a template.
// So deleting one in Explorer deletes it, renaming one renames it, and there is no index to go stale
// behind the player's back.

//! Why a template will not fit on this server. FIT means it will.
enum SM_ETemplateFit
{
	FIT,
	TOO_MANY_STROKES,		// more strokes than the per-player cap allows in total
	SERVER_FULL,			// the server's own ceiling leaves less room than the template needs
	STROKE_TOO_LONG,		// a single stroke has more points than the server accepts
	DRAWING_DISABLED,
	TEMPLATES_DISABLED		// the server switched the feature off entirely
}

class SM_TemplateStore
{
	protected static ref SM_TemplateStore s_Instance;

	protected ref array<ref SM_DrawTemplate> m_aTemplates = {};
	protected ref array<ref SM_DrawTemplate> m_aBuiltIns = {};	// rect/circle/grid — always first, never files
	protected string m_sSelectedId;		// the template "in hand", "" = none
	protected bool   m_bLoaded;

	protected const string DIR = "$profile:SavingMarkers/Templates";

	protected ref array<string> m_aScan = {};	// FindFiles hands its results to an array's Insert

	static SM_TemplateStore GetInstance()
	{
		if (!s_Instance)
			s_Instance = new SM_TemplateStore();
		return s_Instance;
	}

	//! Re-read the folder. Cheap, and it is what makes sharing work: drop a friend's .json in and it
	//! is there the next time the list opens, with no restart. A template that is currently in hand
	//! survives if its file is still there.
	void Reload()
	{
		string keep = m_sSelectedId;

		m_aTemplates.Clear();
		m_bLoaded = false;
		EnsureLoaded();

		if (keep != "" && !Find(keep))
			m_sSelectedId = "";	// somebody deleted the one we were holding
	}

	// --- reading ---

	void GetAll(out array<SM_DrawTemplate> outList)
	{
		EnsureLoaded();
		if (!outList)
			outList = {};
		outList.Clear();
		foreach (SM_DrawTemplate b : m_aBuiltIns)
		{
			if (b)
				outList.Insert(b);	// pinned to the top, in their own fixed order
		}
		foreach (SM_DrawTemplate t : m_aTemplates)
		{
			if (t)
				outList.Insert(t);
		}
	}

	SM_DrawTemplate Find(string id)
	{
		EnsureLoaded();
		foreach (SM_DrawTemplate b : m_aBuiltIns)
		{
			if (b && b.m_sId == id)
				return b;
		}
		foreach (SM_DrawTemplate t : m_aTemplates)
		{
			if (t && t.m_sId == id)
				return t;
		}
		return null;
	}

	int Count()
	{
		EnsureLoaded();
		return m_aBuiltIns.Count() + m_aTemplates.Count();
	}

	// --- the one in hand ---

	//! "" clears the selection. Returns false for an id we do not have.
	bool Select(string id)
	{
		if (id == "")
		{
			m_sSelectedId = "";
			return true;
		}
		if (!Find(id))
			return false;
		m_sSelectedId = id;
		return true;
	}

	SM_DrawTemplate Selected()
	{
		if (m_sSelectedId == "")
			return null;
		return Find(m_sSelectedId);
	}

	// --- will it fit here? ---

	//! Checked when the player PICKS a template, not when he has already held the button for half an
	//! hour. The client knows the server's limits because they are replicated with the config.
	//!
	//! outDetail carries the number the player needs to hear: how many strokes, how many points.
	static SM_ETemplateFit CheckFit(notnull SM_DrawTemplate t, int visibility, out int outDetail)
	{
		outDetail = 0;

		// The server can switch templates off wholesale. Checked before the Local shortcut on purpose:
		// the toggle means "no templates here", not "no templates the server would see".
		if (!SM_MarkerConfig.GetInstance().m_bAllowTemplates)
			return SM_ETemplateFit.TEMPLATES_DISABLED;

		// Local drawings never leave this machine, so no server limit touches them.
		if (visibility == SM_EMarkerVisibility.PERSONAL)
			return SM_ETemplateFit.FIT;

		SM_MarkerConfig cfg = SM_MarkerConfig.GetInstance();
		if (!cfg.m_bAllowDrawing)
			return SM_ETemplateFit.DRAWING_DISABLED;

		int need = t.StrokeCount();

		// Fills carry their own, higher point budget — checking them against the stroke cap turned
		// away templates that only "failed" because they contained a filled area.
		int overCap;
		if (t.HasOverlongStroke(cfg.m_iDrawMaxPointsPerStroke, cfg.m_iDrawMaxPointsPerFill, overCap))
		{
			outDetail = overCap;
			return SM_ETemplateFit.STROKE_TOO_LONG;
		}

		SM_MapDrawingStore store = SM_MapDrawingStore.GetInstance();
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		int myId = -1;
		if (pc)
			myId = pc.GetPlayerId();

		if (cfg.m_iDrawMaxPerPlayer > 0)
		{
			int room = cfg.m_iDrawMaxPerPlayer - store.CountByOwner(myId);
			if (room < need)
			{
				outDetail = room;
				return SM_ETemplateFit.TOO_MANY_STROKES;
			}
		}

		// The server's total is a shared budget: what the client sees in its store is only what it is
		// allowed to see, so this is a floor, not the truth. It still catches the hopeless case.
		if (cfg.m_iDrawMaxTotal > 0)
		{
			int roomTotal = cfg.m_iDrawMaxTotal - store.Count();
			if (roomTotal < need)
			{
				outDetail = roomTotal;
				return SM_ETemplateFit.SERVER_FULL;
			}
		}

		return SM_ETemplateFit.FIT;
	}

	//! One line the panel can put in front of the player. "" when it fits.
	static string FitMessage(SM_ETemplateFit fit, int detail, int strokes)
	{
		switch (fit)
		{
			case SM_ETemplateFit.DRAWING_DISABLED:
				return "Drawing is disabled on this server.";

			case SM_ETemplateFit.TEMPLATES_DISABLED:
				return "Templates are disabled on this server.";

			case SM_ETemplateFit.STROKE_TOO_LONG:
				return string.Format("This template has a stroke longer than the server allows (%1 points max).", detail);

			case SM_ETemplateFit.TOO_MANY_STROKES:
				if (detail <= 0)
					return string.Format("You have no drawing budget left on this server. This template needs %1 strokes.", strokes);
				return string.Format("This template needs %1 strokes; you have room for %2.", strokes, detail);

			case SM_ETemplateFit.SERVER_FULL:
				if (detail <= 0)
					return string.Format("The server's drawing limit is full. This template needs %1 strokes.", strokes);
				return string.Format("This template needs %1 strokes; the server has room for %2.", strokes, detail);
		}
		return "";
	}

	//! Minimum milliseconds between two auto-drawn strokes. This is a FRAME pacer, not a rate limiter:
	//! it only keeps one template from emitting its whole self in a single tick. The actual rate cap
	//! is the per-minute window, enforced by the session's own sliding window so a template draws in a
	//! burst while it fits and then waits — going faster than the window only earns a rejection.
	static int BurstIntervalMs(int visibility)
	{
		if (visibility == SM_EMarkerVisibility.PERSONAL)
			return 50;	// never leaves this machine; only paced so one template cannot spike a frame
		return 70;		// ~14/s: fast enough to feel instant, slow enough for the outbox to batch it
	}

	// --- writing ---

	//! Save a set of world-space strokes as a template. Points come in world metres; they are stored
	//! relative to the centre of their bounding box, which becomes the template's anchor.
	string SaveFromStrokes(string name, notnull array<SM_MapDrawingData> strokes)
	{
		EnsureLoaded();

		if (name == "" || strokes.IsEmpty())
			return "";

		bool first = true;
		int loX, hiX, loZ, hiZ;
		foreach (SM_MapDrawingData d : strokes)
		{
			if (!d || d.GetPointCount() < 2)
				continue;

			int dloX, dhiX, dloZ, dhiZ;
			if (!d.GetAABB(dloX, dhiX, dloZ, dhiZ))
				continue;

			if (first)
			{
				loX = dloX; hiX = dhiX; loZ = dloZ; hiZ = dhiZ;
				first = false;
				continue;
			}
			if (dloX < loX) loX = dloX;
			if (dhiX > hiX) hiX = dhiX;
			if (dloZ < loZ) loZ = dloZ;
			if (dhiZ > hiZ) hiZ = dhiZ;
		}
		if (first)
			return "";

		int cx = (loX + hiX) / 2;
		int cz = (loZ + hiZ) / 2;

		SM_DrawTemplate t = new SM_DrawTemplate();
		t.m_sName = name;
		t.m_sId   = UniqueId(name);

		foreach (SM_MapDrawingData src : strokes)
		{
			if (!src || src.GetPointCount() < 2)
				continue;

			SM_DrawTemplateStroke s = new SM_DrawTemplateStroke();
			s.m_iColor    = src.m_iColor;
			s.m_iWidthIdx = src.m_iWidthIdx;
			s.m_iFill     = src.m_iFill;
			for (int j = 0, m = src.m_aPoints.Count(); j < m; j += 2)
			{
				s.m_aPoints.Insert(src.m_aPoints[j]     - cx);
				s.m_aPoints.Insert(src.m_aPoints[j + 1] - cz);
			}
			t.m_aStrokes.Insert(s);
		}

		if (t.m_aStrokes.IsEmpty())
			return "";

		t.RecomputeSpan();

		if (!WriteFile(t))
			return "";

		m_aTemplates.Insert(t);
		SortByName();
		Print(string.Format("[SM] Template saved: '%1' (%2 strokes)", t.m_sName, t.StrokeCount()), LogLevel.NORMAL);
		return t.m_sId;
	}

	//! What the panel's "Save" button calls: take whatever the selection box is holding on the canvas
	//! and write it out. Returns the new template's id, or "" if there was nothing to save.
	string SaveSelection(notnull SM_DrawCanvas canvas, string name)
	{
		array<SM_MapDrawingData> picked = {};
		canvas.CollectSelected(picked);
		if (picked.IsEmpty())
			return "";

		string id = SaveFromStrokes(name, picked);
		if (id != "")
			canvas.ClearSelection();
		return id;
	}

	bool Delete(string id)
	{
		EnsureLoaded();

		for (int i = m_aTemplates.Count() - 1; i >= 0; i--)
		{
			SM_DrawTemplate t = m_aTemplates[i];
			if (!t || t.m_sId != id)
				continue;
			if (t.m_bBuiltIn)
				return false;	// shipped with the mod — not the player's to remove

			if (t.m_sFile != "" && FileIO.FileExists(t.m_sFile))
				FileIO.DeleteFile(t.m_sFile);

			m_aTemplates.Remove(i);
			if (m_sSelectedId == id)
				m_sSelectedId = "";
				return true;
		}
		return false;
	}

	// --- files ---

	protected void EnsureLoaded()
	{
		if (m_bLoaded)
			return;
		m_bLoaded = true;

		if (m_aBuiltIns.IsEmpty())
		{
			m_aBuiltIns.Insert(MakeBuiltIn("__rect",   "Rectangle", SM_ShapeGeometry.SHAPE_RECT));
			m_aBuiltIns.Insert(MakeBuiltIn("__circle", "Circle",    SM_ShapeGeometry.SHAPE_CIRCLE));
			m_aBuiltIns.Insert(MakeBuiltIn("__grid",   "Grid",      SM_ShapeGeometry.SHAPE_GRID));
		}

		FileIO.MakeDirectory("$profile:SavingMarkers");
		FileIO.MakeDirectory(DIR);

		m_aScan.Clear();
		FileIO.FindFiles(m_aScan.Insert, DIR + "/", ".json");

		foreach (string found : m_aScan)
		{
			// FindFiles is not documented to say whether it hands back a full path or a bare name, so
			// take either: a name without a separator gets the folder put back on the front.
			string path = found;
			if (path.IndexOf("/") < 0 && path.IndexOf("\\") < 0)
				path = DIR + "/" + path;

			SM_DrawTemplate t = ReadFile(path);
			if (!t)
				continue;
			if (IsBuiltInId(t.m_sId))
				continue;	// a file named like a built-in would shadow it — the built-in wins
			m_aTemplates.Insert(t);
		}

		SortByName();
		Print(string.Format("[SM] Templates loaded: %1 (+%2 built-in)", m_aTemplates.Count(), m_aBuiltIns.Count()), LogLevel.NORMAL);
	}

	protected SM_DrawTemplate MakeBuiltIn(string id, string name, int shape)
	{
		SM_DrawTemplate t = new SM_DrawTemplate();
		t.m_sId = id;
		t.m_sName = name;
		t.m_bBuiltIn = true;
		t.m_iShape = shape;
		return t;
	}

	protected bool IsBuiltInId(string id)
	{
		foreach (SM_DrawTemplate b : m_aBuiltIns)
		{
			if (b && b.m_sId == id)
				return true;
		}
		return false;
	}

	//! Alphabetical by the name the player gave it — so "1. Push" sorts before "2. Hold" with no
	//! ordering field of our own, and a template downloaded from a friend slots straight in. Digits
	//! come before letters, so a numeric prefix is all it takes to pin something to the top.
	protected void SortByName()
	{
		// A plain insertion sort: the list is a handful of entries, and array has no comparator sort
		// for ref objects.
		for (int i = 1; i < m_aTemplates.Count(); i++)
		{
			ref SM_DrawTemplate cur = m_aTemplates[i];
			int j = i - 1;
			while (j >= 0 && m_aTemplates[j].m_sName.Compare(cur.m_sName, false) > 0)
			{
				m_aTemplates[j + 1] = m_aTemplates[j];
				j--;
			}
			m_aTemplates[j + 1] = cur;
		}
	}

	protected SM_DrawTemplate ReadFile(string path)
	{
		JsonLoadContext ctx = new JsonLoadContext();
		if (!ctx.LoadFromFile(path))
			return null;

		SM_DrawTemplate t = new SM_DrawTemplate();
		t.m_sFile = path;
		t.m_sId   = IdFromPath(path);
		if (!t.DeserializeFrom(ctx))
			return null;
		return t;
	}

	//! The file name without folder or extension. It is the id, so a template renamed on disk keeps
	//! working under its new name instead of vanishing.
	protected string IdFromPath(string path)
	{
		string name = path;

		int cut = name.LastIndexOf("/");
		int back = name.LastIndexOf("\\");
		if (back > cut)
			cut = back;
		if (cut >= 0)
			name = name.Substring(cut + 1, name.Length() - cut - 1);

		int dot = name.LastIndexOf(".");
		if (dot > 0)
			name = name.Substring(0, dot);
		return name;
	}

	protected bool WriteFile(notnull SM_DrawTemplate t)
	{
		string path = t.m_sFile;
		if (path == "")
		{
			path = PathFor(t.m_sId);
			if (path == "")
				return false;
			t.m_sFile = path;
		}

		FileIO.MakeDirectory("$profile:SavingMarkers");
		FileIO.MakeDirectory(DIR);

		JsonSaveContext ctx = new JsonSaveContext();
		t.SerializeTo(ctx);
		if (!ctx.SaveToFile(path))
		{
			Print(string.Format("[SM] Failed to write template '%1'!", t.m_sId), LogLevel.ERROR);
			return false;
		}
		return true;
	}


	protected string PathFor(string id)
	{
		if (id == "")
			return "";
		return string.Format("%1/%2.json", DIR, id);
	}

	//! Turn the player's name into something a file system will accept, WITHOUT flattening it to bare
	//! letters and digits the way the server-code sanitiser does. The name is the file name now, so it
	//! has to stay readable — spaces, dots, dashes and brackets survive; path separators and the other
	//! characters Windows forbids do not. Leading/trailing spaces and dots are trimmed because Windows
	//! silently drops them and the file would not be found again.
	protected string SafeFileName(string name)
	{
		string keep = " .-_()[]";
		string outp;
		for (int i = 0; i < name.Length(); i++)
		{
			string ch = name.Get(i);
			int a = ch.ToAscii();
			bool ok = (a >= 48 && a <= 57) || (a >= 65 && a <= 90) || (a >= 97 && a <= 122)
				|| keep.IndexOf(ch) >= 0;
			if (ok)
				outp = outp + ch;
		}
		outp.TrimInPlace();
		while (outp.StartsWith(".") || outp.EndsWith("."))
		{
			if (outp.StartsWith("."))
				outp = outp.Substring(1, outp.Length() - 1);
			if (outp != "" && outp.EndsWith("."))
				outp = outp.Substring(0, outp.Length() - 1);
			if (outp == "")
				break;
		}
		if (outp.Length() > 60)
			outp = outp.Substring(0, 60);
		return outp;
	}

	//! A file-safe id, made unique by suffixing so two templates called "Ambush" cannot overwrite
	//! one another.
	protected string UniqueId(string name)
	{
		string stem = SafeFileName(name);
		if (stem == "")
			stem = "Template";

		string candidate = stem;
		int n = 2;
		while (FileIO.FileExists(PathFor(candidate)) || Find(candidate))
		{
			candidate = stem + " " + n.ToString();
			n++;
			if (n > 999)
				break;
		}
		return candidate;
	}
}
