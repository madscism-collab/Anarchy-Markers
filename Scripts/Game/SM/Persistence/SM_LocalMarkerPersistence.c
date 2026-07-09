// CLIENT-side storage for Local (PERSONAL) markers. Unlike the server persistence this
// lives on the PLAYER'S machine (client or listen-host) and keeps private markers that
// never reach the server. File: $profile:SavingMarkers/SM_LocalMarkers.json.
//
// Keying: every entry is bound to (code, faction), where
//   code    — random code of the current game on this server (sent to the client on sync;
//             markers and drawings have separate codes);
//   faction — the player's faction key ("US"/"USSR"/"FIA"...), stable across sessions.
// So the file holds separate Local markers per server AND per side. Player joins a server
// -> receives the code -> we activate the (code, current faction) slice: those markers get
// pushed into the render store with negative ids. Switching sides shows the other faction's
// slice; a different server (different code) shows a different slice.
//
// Mutations (place/edit/move/remove of a Local marker) come from the map layer
// (SM_MapMarkerLayer). Escalating Local -> Side is also done by the map layer:
// RemoveLocal + a server RequestPlace (the marker becomes a normal server marker).

// One file entry: the marker + which (code, faction) it belongs to + its live store id
// while the slice is active.
class SM_LocalMarkerEntry
{
	string m_sCode;
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

	protected ref array<ref SM_LocalMarkerEntry> m_aEntries = {};	// the whole file (all codes/factions)
	protected string m_sActiveCode;		// currently active slice
	protected string m_sActiveFaction;
	protected bool   m_bActive;
	protected bool   m_bLoaded;

	protected const string SAVE_DIR  = "$profile:SavingMarkers";
	protected const string SAVE_FILE = "$profile:SavingMarkers/SM_LocalMarkers.json";
	protected const int VERSION = 1;

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

	// Reset on game mode start. Mutations save immediately, so the extra Save here is
	// just a safety flush.
	static void ResetInstance()
	{
		if (s_Instance)
			s_Instance.Save();
		s_Instance = null;
	}

	// Faction key of the LOCAL player. "none" = no side yet (spectator/GM/transition) —
	// still a valid bucket, so a GM's Local markers work too.
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

	// --- Activating a (code, faction) slice in the render store ---

	// Set the server code (from the RPC on a client, or straight from server persistence on
	// the host) and activate the current faction's slice. Idempotent: calling again with the
	// same (code, faction) is a no-op.
	void SetCodeAndActivate(string code, string factionKey)
	{
		Activate(code, factionKey);
	}

	// Activate a slice. Takes the previous active slice out of the store and pushes the new one in.
	void Activate(string code, string factionKey)
	{
		if (!m_bLoaded)
			LoadFile();

		if (code == "")
			return;	// no code yet — nothing to activate

		if (m_bActive && code == m_sActiveCode && factionKey == m_sActiveFaction)
			return;	// this slice is already up

		Deactivate();

		m_sActiveCode = code;
		m_sActiveFaction = factionKey;
		m_bActive = true;

		if (factionKey == "")
			return;	// code known but faction not resolved yet — activate once it appears

		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
		int shown = 0;
		foreach (SM_LocalMarkerEntry e : m_aEntries)
		{
			if (e && e.m_Data && e.m_sCode == code && e.m_sFaction == factionKey)
			{
				store.LocalCreate(e.m_Data);	// assigns a fresh negative id
				e.m_iLiveId = e.m_Data.m_iId;
				shown++;
			}
		}
		Print(string.Format("[SM] Local markers activated: %1 for code=%2 faction=%3", shown, code, factionKey), LogLevel.NORMAL);
	}

	// Take the active Local markers out of the store (file stays untouched).
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

	// Client changed faction: the render store was already cleared outside (SM_CheckResync
	// does store.Clear()), so our live ids are gone — just reset them and activate the new
	// side's slice under the same code.
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

	// Add a new Local marker to the current slice. Returns the assigned negative id, or 0
	// if the slice isn't usable yet.
	int AddLocal(notnull SM_MapMarkerData data)
	{
		if (!IsActiveSliceUsable())
			return 0;

		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
		store.LocalCreate(data);	// assigns the negative id + renders

		SM_LocalMarkerEntry e = new SM_LocalMarkerEntry();
		e.m_sCode    = m_sActiveCode;
		e.m_sFaction = m_sActiveFaction;
		e.m_Data     = data;	// same object as in the store — edits stay in sync
		e.m_iLiveId  = data.m_iId;
		m_aEntries.Insert(e);

		Save();
		return data.m_iId;
	}

	// Update the editable fields of an existing Local marker (stays Local).
	bool UpdateLocal(int liveId, notnull SM_MapMarkerData src)
	{
		SM_LocalMarkerEntry e = FindByLiveId(liveId);
		if (!e)
			return false;
		SM_MapMarkerStore.GetInstance().LocalUpdate(liveId, src);	// mutates the shared object (= e.m_Data)
		Save();
		return true;
	}

	bool MoveLocal(int liveId, int posX, int posY)
	{
		SM_LocalMarkerEntry e = FindByLiveId(liveId);
		if (!e)
			return false;
		SM_MapMarkerStore.GetInstance().ApplyMove(liveId, posX, posY);	// mutates the shared object
		Save();
		return true;
	}

	// Remove a Local marker (from the store and the file). Used both for plain deletion and
	// for the Local -> Side escalation.
	bool RemoveLocal(int liveId)
	{
		SM_MapMarkerStore.GetInstance().ApplyRemove(liveId);
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

			SM_LocalMarkerEntry e = new SM_LocalMarkerEntry();
			ctx.ReadValue("code", e.m_sCode);
			ctx.ReadValue("faction", e.m_sFaction);
			e.m_Data = new SM_MapMarkerData();
			e.m_Data.DeserializeFrom(ctx);
			ctx.EndObject();

			if (e.m_sCode != "" && e.m_sFaction != "" && e.m_Data.IsValid())
				m_aEntries.Insert(e);
		}
		Print(string.Format("[SM] Loaded %1 local marker entries", m_aEntries.Count()), LogLevel.NORMAL);
	}

	// Write the WHOLE file back (all codes/factions). Active markers are the same objects the
	// store renders, so their live edits serialize automatically. Mutations call Save right
	// away — the file is small and changes are infrequent.
	void Save()
	{
		FileIO.MakeDirectory(SAVE_DIR);

		JsonSaveContext ctx = new JsonSaveContext();
		ctx.WriteValue("version", VERSION);
		ctx.WriteValue("count", m_aEntries.Count());

		int written = 0;
		for (int i = 0; i < m_aEntries.Count(); i++)
		{
			SM_LocalMarkerEntry e = m_aEntries[i];
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
			Print("[SM] Failed to save local markers!", LogLevel.ERROR);
	}
}
