// Збереження малюнків (штрихів) у файл — окремо від міток. Працює тільки на сервері:
//   - на старті гейммоду читає штрихи з JSON у сховище;
//   - зберігає після кожної зміни (з затримкою) плюс раз на хвилину про запас;
//   - клієнти беруть стан через мережу, а не з файлу.
// Файл: $profile:SavingMarkers/SM_Drawings_<місія>.json (окремо від SM_Markers2_*).
// Дзеркалить SM_MapMarkerPersistence; конфіг (SM_MarkerConfig) уже завантажений
// марковською персистенцією на старті, тут його не перечитуємо.

class SM_DrawingPersistence
{
	protected static ref SM_DrawingPersistence s_Instance;

	protected string m_sSaveFile;
	protected string m_sBackupFile;
	protected bool   m_bLoaded;
	protected const string SAVE_DIR = "$profile:SavingMarkers";
	protected const int AUTOSAVE_INTERVAL_MS = 60000;
	protected const int SAVE_DEBOUNCE_MS = 3000;
	protected const int VERSION = 1;

	static SM_DrawingPersistence GetInstance()
	{
		if (!s_Instance)
			s_Instance = new SM_DrawingPersistence();
		return s_Instance;
	}

	static void ResetInstance()
	{
		s_Instance = null;
	}

	void InitServer()
	{
		if (!Replication.IsServer())
			return;

		FileIO.MakeDirectory(SAVE_DIR);
		m_sSaveFile = GenerateSaveFileName();
		m_sBackupFile = m_sSaveFile;
		m_sBackupFile.Replace(".json", "_backup.json");

		Load();

		// Підписуємось ПІСЛЯ Load (ServerLoad не фаєрить інвокери — інакше саме завантаження
		// тригерило б збереження). Тепер будь-який доданий/стертий штрих зберігається.
		SM_MapDrawingStore store = SM_MapDrawingStore.GetInstance();
		store.GetOnAdded().Insert(OnStoreAdded);
		store.GetOnRemoved().Insert(OnStoreRemoved);

		GetGame().GetCallqueue().CallLater(AutoSave, AUTOSAVE_INTERVAL_MS, true);
	}

	protected void AutoSave()
	{
		if (Replication.IsServer() && m_bLoaded)
			Save();
	}

	void RequestSave()
	{
		if (!Replication.IsServer() || !m_bLoaded)
			return;
		GetGame().GetCallqueue().Remove(SaveDebounced);
		GetGame().GetCallqueue().CallLater(SaveDebounced, SAVE_DEBOUNCE_MS, false);
	}

	protected void SaveDebounced() { Save(); }

	protected void OnStoreAdded(SM_MapDrawingData data) { RequestSave(); }
	protected void OnStoreRemoved(int id)               { RequestSave(); }

	void Save()
	{
		if (!Replication.IsServer())
			return;
		if (!SM_MarkerConfig.GetInstance().m_bDrawPersist)
			return;

		if (m_sSaveFile == "")
			m_sSaveFile = GenerateSaveFileName();

		CreateBackup();

		array<SM_MapDrawingData> drawings = {};
		SM_MapDrawingStore.GetInstance().GetAll(drawings);

		JsonSaveContext ctx = new JsonSaveContext();
		ctx.WriteValue("version", VERSION);
		ctx.WriteValue("count", drawings.Count());

		int written = 0;
		for (int i = 0; i < drawings.Count(); i++)
		{
			SM_MapDrawingData d = drawings[i];
			if (!d)
				continue;
			ctx.StartObject(string.Format("d_%1", written));
			d.SerializeTo(ctx);
			ctx.EndObject();
			written++;
		}

		if (ctx.SaveToFile(m_sSaveFile))
			Print(string.Format("[SM] Saved %1 drawings", written), LogLevel.NORMAL);
		else
			Print("[SM] Failed to save drawings!", LogLevel.ERROR);
	}

	void Load()
	{
		if (!Replication.IsServer() || m_bLoaded)
			return;
		if (!SM_MarkerConfig.GetInstance().m_bDrawPersist)
		{
			m_bLoaded = true;
			return;
		}

		if (m_sSaveFile == "")
			m_sSaveFile = GenerateSaveFileName();

		SM_MapDrawingStore store = SM_MapDrawingStore.GetInstance();
		store.ServerClear();	// сховище переживає Workbench Reload — чистимо накопичене

		JsonLoadContext ctx = new JsonLoadContext();
		if (!ctx.LoadFromFile(m_sSaveFile))
		{
			m_bLoaded = true;	// файлу ще нема — стартуємо з порожнього
			return;
		}

		int count;
		ctx.ReadValue("count", count);

		int valid = 0;
		for (int i = 0; i < count; i++)
		{
			if (!ctx.StartObject(string.Format("d_%1", i)))
				continue;

			SM_MapDrawingData d = new SM_MapDrawingData();
			if (d.DeserializeFrom(ctx) && d.IsValid())
			{
				store.ServerLoad(d);
				valid++;
			}
			ctx.EndObject();
		}

		m_bLoaded = true;
		Print(string.Format("[SM] Loaded %1 drawings", valid), LogLevel.NORMAL);
	}

	protected void CreateBackup()
	{
		if (m_sSaveFile == "" || m_sBackupFile == "")
			return;
		JsonLoadContext check = new JsonLoadContext();
		if (!check.LoadFromFile(m_sSaveFile))
			return;
		FileIO.CopyFile(m_sSaveFile, m_sBackupFile);
	}

	// Завершення сценарію: за конфігом — бекап з ротацією + очищення (як у міток).
	void ClearForNewScenario()
	{
		if (!Replication.IsServer())
			return;
		if (!SM_MarkerConfig.GetInstance().m_bClearOnGameEnd)
			return;
		if (m_sSaveFile == "")
			m_sSaveFile = GenerateSaveFileName();

		Save();
		SM_RotateEndBackups();
		SM_MapDrawingStore.GetInstance().ServerClear();
		Save();
		Print("[SM] Scenario ended — drawings cleared (backup kept).", LogLevel.NORMAL);
	}

	protected string SM_EndBackupName(int i)
	{
		string s = m_sSaveFile;
		s.Replace(".json", string.Format("_end%1.json", i));
		return s;
	}

	protected void SM_RotateEndBackups()
	{
		int maxB = SM_MarkerConfig.GetInstance().m_iMaxEndBackups;
		if (maxB <= 0 || m_sSaveFile == "")
			return;
		for (int i = maxB - 1; i >= 1; i--)
		{
			string src = SM_EndBackupName(i);
			if (FileIO.FileExists(src))
				FileIO.CopyFile(src, SM_EndBackupName(i + 1));
		}
		if (FileIO.FileExists(m_sSaveFile))
			FileIO.CopyFile(m_sSaveFile, SM_EndBackupName(1));
	}

	protected string GenerateSaveFileName()
	{
		return string.Format("%1/SM_Drawings_%2.json", SAVE_DIR, SM_MissionIdentifier());
	}

	// Ідентифікатор місії — та сама логіка, що в SM_MapMarkerPersistence (там protected, тож копія).
	protected string SM_MissionIdentifier()
	{
		string identifier = "default";
		MissionHeader header = GetGame().GetMissionHeader();
		if (header)
		{
			string guid = header.GetHeaderResourceName();
			if (guid != "")
			{
				int lastSlash = guid.LastIndexOf("/");
				if (lastSlash >= 0)
					identifier = guid.Substring(lastSlash + 1, guid.Length() - lastSlash - 1);
				else
					identifier = guid;

				int dot = identifier.LastIndexOf(".");
				if (dot >= 0)
					identifier = identifier.Substring(0, dot);
			}
		}
		identifier.Replace(" ", "_");
		identifier.Replace("/", "_");
		identifier.Replace("\\", "_");
		identifier.Replace(":", "_");
		identifier.Replace("{", "");
		identifier.Replace("}", "");
		return identifier;
	}
}
