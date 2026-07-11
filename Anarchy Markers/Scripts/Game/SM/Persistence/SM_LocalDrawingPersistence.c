// CLIENT-side storage for Local (PERSONAL) drawings — mirrors SM_LocalMarkerPersistence, but for
// strokes and fills. Lives on the player's machine.
//
// ONE FILE PER SERVER: $profile:SavingMarkers/Local/D_<code>.json
//
// Drawings carry their OWN server code, separate from the markers' one (the two have separate server
// files and separate config switches), so they get their own file. Faction tags each entry inside it.
//
// See SM_LocalMarkerPersistence for why one file per code: we never read another server's data, so
// splitting means a save touches only this server's file, a load reads only this server's file, and
// a stale code is just a file the player can delete. Strokes are immutable — add/remove only (a
// partial erase is remove + add of the pieces). Escalating Local -> Side is the draw canvas's job:
// RemoveLocal + a server RequestAdd.

class SM_LocalDrawingEntry
{
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

	protected ref array<ref SM_LocalDrawingEntry> m_aEntries = {};	// ONLY the loaded code's entries
	protected string m_sLoadedCode;		// whose file is in memory ("" = nothing loaded)
	protected string m_sActiveCode;
	protected string m_sActiveFaction;
	protected bool   m_bActive;
	protected bool   m_bSaveQueued;

	protected const string SAVE_DIR    = "$profile:SavingMarkers";
	protected const string LOCAL_DIR   = "$profile:SavingMarkers/Local";
	protected const string LEGACY_FILE = "$profile:SavingMarkers/SM_LocalDrawings.json";
	protected const int VERSION = 2;
	protected const int SAVE_DEBOUNCE_MS = 1500;

	protected static bool s_bMigrated;

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
		{
			GetGame().GetCallqueue().Remove(s_Instance.SaveNow);	// drop the pending debounce; we flush right here
			s_Instance.SaveNow();
		}
		s_Instance = null;
	}

	protected static string FileForCode(string code)
	{
		// Same sanitising as the markers: the code names a file, so it never goes in unfiltered.
		string safe = SM_LocalMarkerPersistence.SafeCode(code);
		if (safe == "")
			return "";
		return string.Format("%1/D_%2.json", LOCAL_DIR, safe);
	}

	// --- Activating a (code, faction) slice ---

	void SetCodeAndActivate(string code, string factionKey)
	{
		Activate(code, factionKey);
	}

	void Activate(string code, string factionKey)
	{
		EnsureMigrated();

		if (code == "")
			return;
		if (m_bActive && code == m_sActiveCode && factionKey == m_sActiveFaction)
			return;

		Deactivate();

		if (code != m_sLoadedCode)
		{
			SaveNow();		// switching servers mid-session: don't lose the old one's pending write
			LoadCode(code);
		}

		m_sActiveCode = code;
		m_sActiveFaction = factionKey;
		m_bActive = true;

		if (factionKey == "")
			return;	// code known but faction not resolved yet — activate once it appears

		SM_MapDrawingStore store = SM_MapDrawingStore.GetInstance();
		int shown = 0;
		foreach (SM_LocalDrawingEntry e : m_aEntries)
		{
			if (e && e.m_Data && e.m_sFaction == factionKey)
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

	//! Add a Local stroke/fill. Returns the negative id, or 0 if the slice isn't usable yet.
	int AddLocal(notnull SM_MapDrawingData data)
	{
		if (!IsActiveSliceUsable())
			return 0;

		SM_MapDrawingData created = SM_MapDrawingStore.GetInstance().LocalCreate(data);
		if (!created)
			return 0;

		SM_LocalDrawingEntry e = new SM_LocalDrawingEntry();
		e.m_sFaction = m_sActiveFaction;
		e.m_Data     = data;
		e.m_iLiveId  = data.m_iId;
		m_aEntries.Insert(e);

		RequestSave();
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
				RequestSave();
				return true;
			}
		}
		return false;
	}

	// --- File I/O ---

	protected void LoadCode(string code)
	{
		m_aEntries.Clear();
		m_sLoadedCode = code;
		if (code == "")
			return;

		string path = FileForCode(code);
		if (path == "")
			return;	// unusable code — behave as if there were no file

		JsonLoadContext ctx = new JsonLoadContext();
		if (!ctx.LoadFromFile(path))
			return;	// never played this server — start empty

		int count;
		ctx.ReadValue("count", count);

		for (int i = 0; i < count; i++)
		{
			if (!ctx.StartObject(string.Format("e_%1", i)))
				continue;

			SM_LocalDrawingEntry e = new SM_LocalDrawingEntry();
			ctx.ReadValue("faction", e.m_sFaction);
			e.m_Data = new SM_MapDrawingData();
			e.m_Data.DeserializeFrom(ctx);
			ctx.EndObject();

			if (e.m_sFaction != "" && e.m_Data.IsValid())
				m_aEntries.Insert(e);
		}
		Print(string.Format("[SM] Loaded %1 local drawings for code=%2", m_aEntries.Count(), code), LogLevel.NORMAL);
	}

	//! Coalesce writes — erasing sprays remove/add of the leftover pieces, and each one used to hit
	//! the disk.
	void RequestSave()
	{
		if (m_bSaveQueued)
			return;
		m_bSaveQueued = true;
		GetGame().GetCallqueue().CallLater(SaveNow, SAVE_DEBOUNCE_MS, false);
	}

	void SaveNow()
	{
		m_bSaveQueued = false;
		if (m_sLoadedCode == "")
			return;

		WriteFile(m_sLoadedCode, m_aEntries, SM_MapMarkerPersistence.SM_MissionIdentifier());
	}

	protected static void WriteFile(string code, notnull array<ref SM_LocalDrawingEntry> entries, string label)
	{
		string path = FileForCode(code);
		if (path == "")
			return;

		// Drop the file once the last drawing is gone rather than leave an empty husk behind.
		int valid = 0;
		foreach (SM_LocalDrawingEntry v : entries)
		{
			if (v && v.m_Data)
				valid++;
		}
		if (valid == 0)
		{
			if (FileIO.FileExists(path))
				FileIO.DeleteFile(path);
			return;
		}

		FileIO.MakeDirectory(SAVE_DIR);
		FileIO.MakeDirectory(LOCAL_DIR);

		JsonSaveContext ctx = new JsonSaveContext();
		ctx.WriteValue("version", VERSION);
		ctx.WriteValue("label", label);	// which scenario this code came from
		ctx.WriteValue("count", valid);

		int written = 0;
		foreach (SM_LocalDrawingEntry e : entries)
		{
			if (!e || !e.m_Data)
				continue;
			ctx.StartObject(string.Format("e_%1", written));
			ctx.WriteValue("faction", e.m_sFaction);
			e.m_Data.SerializeTo(ctx);
			ctx.EndObject();
			written++;
		}

		if (!ctx.SaveToFile(path))
			Print(string.Format("[SM] Failed to save local drawings for code=%1!", code), LogLevel.ERROR);
	}

	// --- Migration from the old single-file format ---

	//! v1 kept every server in one file, each record tagged with its code. Split it per code. The old
	//! file is renamed, not deleted — this is the player's data.
	protected void EnsureMigrated()
	{
		if (s_bMigrated)
			return;
		s_bMigrated = true;

		if (!FileIO.FileExists(LEGACY_FILE))
			return;

		JsonLoadContext ctx = new JsonLoadContext();
		if (!ctx.LoadFromFile(LEGACY_FILE))
			return;

		int count;
		ctx.ReadValue("count", count);

		map<string, ref array<ref SM_LocalDrawingEntry>> byCode = new map<string, ref array<ref SM_LocalDrawingEntry>>();

		for (int i = 0; i < count; i++)
		{
			if (!ctx.StartObject(string.Format("e_%1", i)))
				continue;

			string code;
			ctx.ReadValue("code", code);

			SM_LocalDrawingEntry e = new SM_LocalDrawingEntry();
			ctx.ReadValue("faction", e.m_sFaction);
			e.m_Data = new SM_MapDrawingData();
			e.m_Data.DeserializeFrom(ctx);
			ctx.EndObject();

			if (code == "" || e.m_sFaction == "" || !e.m_Data.IsValid())
				continue;

			array<ref SM_LocalDrawingEntry> bucket = byCode.Get(code);
			if (!bucket)
			{
				bucket = {};
				byCode.Set(code, bucket);
			}
			bucket.Insert(e);
		}

		// The old file has no label — we can't know which scenario an old code came from.
		foreach (string c, array<ref SM_LocalDrawingEntry> list : byCode)
			WriteFile(c, list, "");

		FileIO.CopyFile(LEGACY_FILE, LEGACY_FILE + ".migrated");
		FileIO.DeleteFile(LEGACY_FILE);
		Print(string.Format("[SM] Migrated local drawings: %1 servers split into per-code files", byCode.Count()), LogLevel.NORMAL);
	}
}
