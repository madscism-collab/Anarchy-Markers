// CLIENT-side storage for Local (PERSONAL) drawings — mirrors SM_LocalMarkerPersistence,
// but for strokes/fills. Lives on the player's machine, file
// $profile:SavingMarkers/SM_LocalDrawings.json. Uses its own server code, separate from
// markers (they have separate server files and config switches). Same (code, faction)
// keying. Strokes are immutable — add/remove only (a partial erase = remove + add pieces).
// Escalating Local -> Side is done by the draw canvas: RemoveLocal + a server RequestAdd.

class SM_LocalDrawingEntry
{
	string m_sCode;
	string m_sFaction;
	ref SM_MapDrawingData m_Data;
	int m_iLiveId;	// negative store id while active; 0 = not in the store

	void SM_LocalDrawingEntry()
	{
		m_iLiveId = 0;
	}
}

class SM_LocalDrawingPersistence
{
	protected static ref SM_LocalDrawingPersistence s_Instance;

	protected ref array<ref SM_LocalDrawingEntry> m_aEntries = {};
	protected string m_sActiveCode;
	protected string m_sActiveFaction;
	protected bool   m_bActive;
	protected bool   m_bLoaded;

	protected const string SAVE_DIR  = "$profile:SavingMarkers";
	protected const string SAVE_FILE = "$profile:SavingMarkers/SM_LocalDrawings.json";
	protected const int VERSION = 1;

	static SM_LocalDrawingPersistence GetInstance()
	{
		if (!s_Instance)
			s_Instance = new SM_LocalDrawingPersistence();
		return s_Instance;
	}

	static bool HasInstance()
	{
		return s_Instance != null;
	}

	static void ResetInstance()
	{
		if (s_Instance)
			s_Instance.Save();
		s_Instance = null;
	}

	// --- Activating a (code, faction) slice ---

	void SetCodeAndActivate(string code, string factionKey)
	{
		Activate(code, factionKey);
	}

	void Activate(string code, string factionKey)
	{
		if (!m_bLoaded)
			LoadFile();

		if (code == "")
			return;
		if (m_bActive && code == m_sActiveCode && factionKey == m_sActiveFaction)
			return;

		Deactivate();

		m_sActiveCode = code;
		m_sActiveFaction = factionKey;
		m_bActive = true;

		if (factionKey == "")
			return;	// code known but faction not resolved yet — activate once it appears

		SM_MapDrawingStore store = SM_MapDrawingStore.GetInstance();
		int shown = 0;
		foreach (SM_LocalDrawingEntry e : m_aEntries)
		{
			if (e && e.m_Data && e.m_sCode == code && e.m_sFaction == factionKey)
			{
				store.LocalCreate(e.m_Data);
				e.m_iLiveId = e.m_Data.m_iId;
				shown++;
			}
		}
		Print(string.Format("[SM] Local drawings activated: %1 for code=%2 faction=%3", shown, code, factionKey), LogLevel.NORMAL);
	}

	void Deactivate()
	{
		SM_MapDrawingStore store = SM_MapDrawingStore.GetInstance();
		foreach (SM_LocalDrawingEntry e : m_aEntries)
		{
			if (e && e.m_iLiveId != 0)
			{
				store.ApplyRemove(e.m_iLiveId);
				e.m_iLiveId = 0;
			}
		}
		m_bActive = false;
	}

	void ReactivateForFaction(string factionKey)
	{
		foreach (SM_LocalDrawingEntry e : m_aEntries)
		{
			if (e)
				e.m_iLiveId = 0;
		}
		m_bActive = false;
		m_sActiveFaction = "";	// force re-activation even if the code didn't change
		Activate(m_sActiveCode, factionKey);
	}

	// --- Mutations ---

	bool IsActiveSliceUsable()
	{
		// "none" (GM/spectator) is a valid bucket; unusable only while code/faction are unknown.
		return m_bActive && m_sActiveCode != "" && m_sActiveFaction != "";
	}

	// Add a Local stroke/fill. Returns the negative id, or 0 if the slice isn't usable yet.
	int AddLocal(notnull SM_MapDrawingData data)
	{
		if (!IsActiveSliceUsable())
			return 0;

		SM_MapDrawingData created = SM_MapDrawingStore.GetInstance().LocalCreate(data);
		if (!created)
			return 0;

		SM_LocalDrawingEntry e = new SM_LocalDrawingEntry();
		e.m_sCode    = m_sActiveCode;
		e.m_sFaction = m_sActiveFaction;
		e.m_Data     = data;
		e.m_iLiveId  = data.m_iId;
		m_aEntries.Insert(e);

		Save();
		return data.m_iId;
	}

	bool RemoveLocal(int liveId)
	{
		SM_MapDrawingStore.GetInstance().ApplyRemove(liveId);
		for (int i = m_aEntries.Count() - 1; i >= 0; i--)
		{
			if (m_aEntries[i] && m_aEntries[i].m_iLiveId == liveId)
			{
				m_aEntries.Remove(i);
				Save();
				return true;
			}
		}
		return false;
	}

	// --- File I/O ---

	void LoadFile()
	{
		m_bLoaded = true;
		m_aEntries.Clear();

		JsonLoadContext ctx = new JsonLoadContext();
		if (!ctx.LoadFromFile(SAVE_FILE))
			return;	// no file yet — start empty

		int count;
		ctx.ReadValue("count", count);

		for (int i = 0; i < count; i++)
		{
			if (!ctx.StartObject(string.Format("e_%1", i)))
				continue;

			SM_LocalDrawingEntry e = new SM_LocalDrawingEntry();
			ctx.ReadValue("code", e.m_sCode);
			ctx.ReadValue("faction", e.m_sFaction);
			e.m_Data = new SM_MapDrawingData();
			e.m_Data.DeserializeFrom(ctx);
			ctx.EndObject();

			if (e.m_sCode != "" && e.m_sFaction != "" && e.m_Data.IsValid())
				m_aEntries.Insert(e);
		}
		Print(string.Format("[SM] Loaded %1 local drawing entries", m_aEntries.Count()), LogLevel.NORMAL);
	}

	void Save()
	{
		FileIO.MakeDirectory(SAVE_DIR);

		JsonSaveContext ctx = new JsonSaveContext();
		ctx.WriteValue("version", VERSION);
		ctx.WriteValue("count", m_aEntries.Count());

		int written = 0;
		for (int i = 0; i < m_aEntries.Count(); i++)
		{
			SM_LocalDrawingEntry e = m_aEntries[i];
			if (!e || !e.m_Data)
				continue;
			ctx.StartObject(string.Format("e_%1", written));
			ctx.WriteValue("code", e.m_sCode);
			ctx.WriteValue("faction", e.m_sFaction);
			e.m_Data.SerializeTo(ctx);
			ctx.EndObject();
			written++;
		}

		if (!ctx.SaveToFile(SAVE_FILE))
			Print("[SM] Failed to save local drawings!", LogLevel.ERROR);
	}
}
