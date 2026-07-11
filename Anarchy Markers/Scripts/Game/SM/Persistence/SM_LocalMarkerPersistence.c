// CLIENT-side storage for Local (PERSONAL) markers. Unlike the server persistence this lives on the
// PLAYER'S machine (client or listen-host) and keeps private markers that never reach the server.
//
// ONE FILE PER SERVER: $profile:SavingMarkers/Local/M_<code>.json
//
//   code    — random code of the current game on this server (sent to the client on sync; markers
//             and drawings have separate codes). It names the file.
//   faction — the player's faction key ("US"/"USSR"/"FIA"...), stable across sessions. It tags each
//             entry INSIDE the file, because one server holds markers for every side the player has
//             played on it.
//
// Why one file per code rather than one big file with a code on every record: we never read another
// server's markers. Only the current code's slice is ever needed, so splitting means a save rewrites
// only THIS server's file (a marker drag used to rewrite the player's entire archive), a load reads
// only THIS server's file, and every other server costs nothing at all. It also makes the folder
// self-explaining: a server whose code was regenerated leaves a file that is simply never opened
// again, and the player can delete it — Explorer's own size and last-modified columns are exactly the
// two things you need to decide that, so we don't have to build any of it.
//
// The file also carries a "label" (the scenario identifier) so a folder of random hex codes still
// tells you where each one came from.
//
// Mutations (place/edit/move/remove) come from the map layer. Escalating Local -> Side is also the
// map layer's job: RemoveLocal + a server RequestPlace.

// One entry of one server's file: the marker, the side it belongs to, and its live store id while
// the slice is active.
class SM_LocalMarkerEntry
{
	string m_sFaction;
	ref SM_MapMarkerData m_Data;
	int m_iLiveId;	// negative store id while active; 0 = not in the store

	void SM_LocalMarkerEntry()
	{
		m_iLiveId = 0;
	}
}

class SM_LocalMarkerPersistence
{
	protected static ref SM_LocalMarkerPersistence s_Instance;

	protected ref array<ref SM_LocalMarkerEntry> m_aEntries = {};	// ONLY the loaded code's entries
	protected string m_sLoadedCode;		// whose file is in memory ("" = nothing loaded)
	protected string m_sActiveCode;
	protected string m_sActiveFaction;
	protected bool   m_bActive;
	protected bool   m_bSaveQueued;

	protected const string SAVE_DIR    = "$profile:SavingMarkers";
	protected const string LOCAL_DIR   = "$profile:SavingMarkers/Local";
	protected const string LEGACY_FILE = "$profile:SavingMarkers/SM_LocalMarkers.json";
	protected const int VERSION = 2;

	// A marker drag fires MoveLocal every frame. Saving straight from the mutation meant a file write
	// per frame — the server persistence already debounces for the same reason, so match it.
	protected const int SAVE_DEBOUNCE_MS = 1500;

	protected static bool s_bMigrated;	// the legacy one-big-file split runs once per session

	static SM_LocalMarkerPersistence GetInstance()
	{
		if (!s_Instance)
			s_Instance = new SM_LocalMarkerPersistence();
		return s_Instance;
	}

	static bool HasInstance()
	{
		return s_Instance != null;
	}

	//! Game mode start. Flush anything the debounce still owes before dropping the instance.
	static void ResetInstance()
	{
		if (s_Instance)
		{
			GetGame().GetCallqueue().Remove(s_Instance.SaveNow);	// drop the pending debounce; we flush right here
			s_Instance.SaveNow();
		}
		s_Instance = null;
	}

	// Faction key of the LOCAL player. "none" = no side yet (spectator/GM/transition) — still a valid
	// bucket, so a GM's Local markers work too.
	static string GetLocalFactionKey()
	{
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!pc)
			return "none";
		int pid = pc.GetPlayerId();
		if (pid <= 0)
			return "none";
		SCR_FactionManager fm = SCR_FactionManager.Cast(GetGame().GetFactionManager());
		if (!fm)
			return "none";
		Faction f = fm.GetPlayerFaction(pid);
		if (!f)
			return "none";
		string key = f.GetFactionKey();
		if (key == "")
			return "none";
		return key;
	}

	// --- Paths ---

	//! The code comes off the wire and ends up NAMING A FILE, so it never goes in unfiltered: anything
	//! outside this alphabet is dropped, and a code that filters down to nothing is refused outright.
	//! A path separator smuggled into a server code must not be able to write outside the folder.
	static string SafeCode(string code)
	{
		string allowed = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
		string safe = "";
		for (int i = 0; i < code.Length(); i++)
		{
			string ch = code.Substring(i, 1);
			if (allowed.IndexOf(ch) >= 0)
				safe = safe + ch;
		}
		return safe;
	}

	//! "" if the code sanitises down to nothing — callers must treat that as "no file", not as a path.
	protected static string FileForCode(string code)
	{
		string safe = SafeCode(code);
		if (safe == "")
			return "";
		return string.Format("%1/M_%2.json", LOCAL_DIR, safe);
	}

	// --- Activating a (code, faction) slice in the render store ---

	//! Set the server code (from the RPC on a client, or straight from server persistence on the host)
	//! and activate the current faction's slice. Idempotent for the same (code, faction).
	void SetCodeAndActivate(string code, string factionKey)
	{
		Activate(code, factionKey);
	}

	//! Activate a slice: take the previous one out of the store, load this server's file if it isn't
	//! already in memory, and push the matching side's markers in.
	void Activate(string code, string factionKey)
	{
		EnsureMigrated();

		if (code == "")
			return;	// no code yet — nothing to activate

		if (m_bActive && code == m_sActiveCode && factionKey == m_sActiveFaction)
			return;	// this slice is already up

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

		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
		int shown = 0;
		foreach (SM_LocalMarkerEntry e : m_aEntries)
		{
			if (e && e.m_Data && e.m_sFaction == factionKey)
			{
				store.LocalCreate(e.m_Data);	// assigns a fresh negative id
				e.m_iLiveId = e.m_Data.m_iId;
				shown++;
			}
		}
		Print(string.Format("[SM] Local markers activated: %1 for code=%2 faction=%3", shown, code, factionKey), LogLevel.NORMAL);
	}

	//! Take the active Local markers out of the store (the file stays untouched).
	void Deactivate()
	{
		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
		foreach (SM_LocalMarkerEntry e : m_aEntries)
		{
			if (e && e.m_iLiveId != 0)
			{
				store.ApplyRemove(e.m_iLiveId);
				e.m_iLiveId = 0;
			}
		}
		m_bActive = false;
	}

	//! Client changed faction: the render store was already cleared outside (SM_CheckResync does
	//! store.Clear()), so our live ids are gone — reset them and activate the new side under the same
	//! code. No file work: one file already holds every side of this server.
	void ReactivateForFaction(string factionKey)
	{
		foreach (SM_LocalMarkerEntry e : m_aEntries)
		{
			if (e)
				e.m_iLiveId = 0;
		}
		m_bActive = false;
		m_sActiveFaction = "";	// force re-activation even if the code didn't change
		Activate(m_sActiveCode, factionKey);
	}

	// --- Mutations of the active slice (called by the map layer) ---

	bool IsActiveSliceUsable()
	{
		// "none" (GM/spectator) is a valid bucket; unusable only while code/faction are unknown.
		return m_bActive && m_sActiveCode != "" && m_sActiveFaction != "";
	}

	//! Add a Local marker to the current slice. Returns the assigned negative id, or 0 if the slice
	//! isn't usable yet.
	int AddLocal(notnull SM_MapMarkerData data)
	{
		if (!IsActiveSliceUsable())
			return 0;

		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
		store.LocalCreate(data);	// assigns the negative id + renders

		SM_LocalMarkerEntry e = new SM_LocalMarkerEntry();
		e.m_sFaction = m_sActiveFaction;
		e.m_Data     = data;	// same object as in the store — edits stay in sync
		e.m_iLiveId  = data.m_iId;
		m_aEntries.Insert(e);

		RequestSave();
		return data.m_iId;
	}

	//! Update the editable fields of an existing Local marker (stays Local).
	bool UpdateLocal(int liveId, notnull SM_MapMarkerData src)
	{
		SM_LocalMarkerEntry e = FindByLiveId(liveId);
		if (!e)
			return false;
		SM_MapMarkerStore.GetInstance().LocalUpdate(liveId, src);	// mutates the shared object (= e.m_Data)
		RequestSave();
		return true;
	}

	bool MoveLocal(int liveId, int posX, int posY)
	{
		SM_LocalMarkerEntry e = FindByLiveId(liveId);
		if (!e)
			return false;
		SM_MapMarkerStore.GetInstance().ApplyMove(liveId, posX, posY);	// mutates the shared object
		RequestSave();
		return true;
	}

	//! Remove a Local marker (from the store and the file). Used both for plain deletion and for the
	//! Local -> Side escalation.
	bool RemoveLocal(int liveId)
	{
		SM_MapMarkerStore.GetInstance().ApplyRemove(liveId);
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

	protected SM_LocalMarkerEntry FindByLiveId(int liveId)
	{
		foreach (SM_LocalMarkerEntry e : m_aEntries)
		{
			if (e && e.m_iLiveId == liveId)
				return e;
		}
		return null;
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

			SM_LocalMarkerEntry e = new SM_LocalMarkerEntry();
			ctx.ReadValue("faction", e.m_sFaction);
			e.m_Data = new SM_MapMarkerData();
			e.m_Data.DeserializeFrom(ctx);
			ctx.EndObject();

			if (e.m_sFaction != "" && e.m_Data.IsValid())
				m_aEntries.Insert(e);
		}
		Print(string.Format("[SM] Loaded %1 local markers for code=%2", m_aEntries.Count(), code), LogLevel.NORMAL);
	}

	//! Coalesce writes: a marker drag calls MoveLocal every frame, and each one used to hit the disk.
	void RequestSave()
	{
		if (m_bSaveQueued)
			return;
		m_bSaveQueued = true;
		GetGame().GetCallqueue().CallLater(SaveNow, SAVE_DEBOUNCE_MS, false);
	}

	//! Write the loaded server's file — and only that one. Other servers are untouched because they
	//! are not even in memory.
	void SaveNow()
	{
		m_bSaveQueued = false;
		if (m_sLoadedCode == "")
			return;

		WriteFile(m_sLoadedCode, m_aEntries, SM_MapMarkerPersistence.SM_MissionIdentifier());
	}

	//! Shared by SaveNow and the legacy migration.
	protected static void WriteFile(string code, notnull array<ref SM_LocalMarkerEntry> entries, string label)
	{
		string path = FileForCode(code);
		if (path == "")
			return;	// unusable code — refuse to write rather than guess at a path

		// Drop the file entirely once the last marker is gone, instead of leaving an empty husk in a
		// folder the player is meant to be able to read at a glance.
		int valid = 0;
		foreach (SM_LocalMarkerEntry v : entries)
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
		ctx.WriteValue("label", label);	// which scenario this code came from — the folder is hex otherwise
		ctx.WriteValue("count", valid);

		int written = 0;
		foreach (SM_LocalMarkerEntry e : entries)
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
			Print(string.Format("[SM] Failed to save local markers for code=%1!", code), LogLevel.ERROR);
	}

	// --- Migration from the old single-file format ---

	//! v1 kept every server in one file, each record tagged with its code. Split it into per-code
	//! files. The old file is KEPT (renamed) rather than deleted — this is the player's data, and a
	//! bug here must not be the thing that eats it.
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

		map<string, ref array<ref SM_LocalMarkerEntry>> byCode = new map<string, ref array<ref SM_LocalMarkerEntry>>();

		for (int i = 0; i < count; i++)
		{
			if (!ctx.StartObject(string.Format("e_%1", i)))
				continue;

			string code;
			ctx.ReadValue("code", code);

			SM_LocalMarkerEntry e = new SM_LocalMarkerEntry();
			ctx.ReadValue("faction", e.m_sFaction);
			e.m_Data = new SM_MapMarkerData();
			e.m_Data.DeserializeFrom(ctx);
			ctx.EndObject();

			if (code == "" || e.m_sFaction == "" || !e.m_Data.IsValid())
				continue;

			array<ref SM_LocalMarkerEntry> bucket = byCode.Get(code);
			if (!bucket)
			{
				bucket = {};
				byCode.Set(code, bucket);
			}
			bucket.Insert(e);
		}

		// The old file has no label — we can't know which scenario a stranger's code came from.
		foreach (string c, array<ref SM_LocalMarkerEntry> list : byCode)
			WriteFile(c, list, "");

		FileIO.CopyFile(LEGACY_FILE, LEGACY_FILE + ".migrated");
		FileIO.DeleteFile(LEGACY_FILE);
		Print(string.Format("[SM] Migrated local markers: %1 servers split into per-code files", byCode.Count()), LogLevel.NORMAL);
	}
}
