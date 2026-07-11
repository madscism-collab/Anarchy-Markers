// Збереження міток у файл. Своє, не ванільне (ванільний RplSave губив мітки). Працює тільки на сервері:
//   - на старті гейммоду читає мітки з JSON у сховище;
//   - зберігає після кожної зміни (з невеликою затримкою) плюс раз на хвилину про запас;
//   - клієнти беруть стан через мережу (RpcAsk_RequestSync), а не з файлу.
// Файл лежить у $profile:SavingMarkers/SM_Markers2_<місія>.json (окремо від старого мода).

class SM_MapMarkerPersistence
{
	protected static ref SM_MapMarkerPersistence s_Instance;

	protected string m_sSaveFile;
	protected string m_sBackupFile;
	protected bool   m_bLoaded;
	protected string m_sCode;	// random code of this game (stored in the marker file) — sent to clients, who key
								// their Local markers by it. A new game (ClearForNewScenario) gets a new code.
	protected const string SAVE_DIR = "$profile:SavingMarkers";
	protected const int AUTOSAVE_INTERVAL_MS = 60000;	// запасний автосейв раз на хвилину
	protected const int SAVE_DEBOUNCE_MS = 3000;		// затримка перед збереженням після зміни
	protected const int VERSION = 1;

	static SM_MapMarkerPersistence GetInstance()
	{
		if (!s_Instance)
			s_Instance = new SM_MapMarkerPersistence();
		return s_Instance;
	}

	static void ResetInstance()
	{
		s_Instance = null;
	}

	// This game's code (replicated to clients). Empty until InitServer/EnsureCode.
	string GetCode()
	{
		return m_sCode;
	}

	// Shared random code generator (24 chars, a-z0-9). Also used by the drawing persistence.
	// Not cryptographic — good enough to tell games/servers apart.
	static string SM_GenerateCode()
	{
		string charset = "0123456789abcdefghijklmnopqrstuvwxyz";
		int n = charset.Length();
		string code = "";
		for (int i = 0; i < 24; i++)
		{
			int idx = Math.RandomInt(0, n);
			code = code + charset.Substring(idx, 1);
		}
		return code;
	}

	// Make sure a code exists: if Load didn't produce one (fresh file, or an old save without
	// the field) — generate it now.
	protected void EnsureCode()
	{
		if (m_sCode == "")
		{
			m_sCode = SM_GenerateCode();
			Print(string.Format("[SM] Generated new marker server code: %1", m_sCode), LogLevel.NORMAL);
			if (SM_MarkerConfig.GetInstance().m_bPersist)
				Save();	// pin the code to the file so it stays stable across restarts
		}
	}

	// Сервер: готує ім'я файлу, завантажує мітки й запускає автозбереження.
	void InitServer()
	{
		if (!Replication.IsServer())
			return;

		SM_MarkerConfig.GetInstance().ServerLoadOrCreate();	// конфіг мода (створює дефолти при першому старті)

		FileIO.MakeDirectory(SAVE_DIR);
		m_sSaveFile = GenerateSaveFileName();
		m_sBackupFile = m_sSaveFile;
		m_sBackupFile.Replace(".json", "_backup.json");

		Load();
		EnsureCode();	// after Load: generate the code if the file didn't have one yet

		// Підписуємось на зміни сховища ПІСЛЯ Load, бо ServerLoad не фаєрить інвокери —
		// інакше саме завантаження одразу б тригерило збереження. Тепер будь-яка зміна
		// (поставив/посунув/відредагував/видалив) зберігається за кілька секунд.
		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
		store.GetOnMarkerAdded().Insert(OnStoreChanged);
		store.GetOnMarkerChanged().Insert(OnStoreChanged);
		store.GetOnMarkerRemoved().Insert(OnStoreRemoved);

		GetGame().GetCallqueue().CallLater(AutoSave, AUTOSAVE_INTERVAL_MS, true);
	}

	protected void AutoSave()
	{
		if (Replication.IsServer() && m_bLoaded)
			Save();
	}

	// Планує збереження через SAVE_DEBOUNCE_MS. Серія швидких змін зіллється в одне збереження.
	void RequestSave()
	{
		if (!Replication.IsServer() || !m_bLoaded)
			return;

		GetGame().GetCallqueue().Remove(SaveDebounced);
		GetGame().GetCallqueue().CallLater(SaveDebounced, SAVE_DEBOUNCE_MS, false);
	}

	protected void SaveDebounced()
	{
		Save();
	}

	protected void OnStoreChanged(SM_MapMarkerData data)
	{
		RequestSave();
	}

	protected void OnStoreRemoved(int id)
	{
		RequestSave();
	}

	// Записати всі мітки зі сховища у JSON.
	void Save()
	{
		if (!Replication.IsServer())
			return;
		if (!SM_MarkerConfig.GetInstance().m_bPersist)	// збереження вимкнено в конфігу
			return;

		if (m_sSaveFile == "")
			m_sSaveFile = GenerateSaveFileName();

		CreateBackup();

		array<SM_MapMarkerData> markers = {};
		SM_MapMarkerStore.GetInstance().GetAll(markers);

		JsonSaveContext ctx = new JsonSaveContext();
		ctx.WriteValue("version", VERSION);
		ctx.WriteValue("code", m_sCode);	// this game's code — clients key their Local markers by it

		// Count server markers only (id >= 1). On a listen-host the store also holds Local ones
		// (id <= -2), but those belong to the client file — they never go into the server JSON.
		int serverCount = 0;
		foreach (SM_MapMarkerData mc : markers)
		{
			if (mc && !SM_MapMarkerStore.IsLocalId(mc.m_iId))
				serverCount++;
		}
		ctx.WriteValue("count", serverCount);

		int written = 0;
		for (int i = 0; i < markers.Count(); i++)
		{
			SM_MapMarkerData m = markers[i];
			if (!m)
				continue;
			if (SM_MapMarkerStore.IsLocalId(m.m_iId))	// Local marker — not the server's, skip
				continue;

			ctx.StartObject(string.Format("m_%1", written));
			m.SerializeTo(ctx);
			ctx.EndObject();
			written++;
		}

		if (ctx.SaveToFile(m_sSaveFile))
			Print(string.Format("[SM] Saved %1 markers", written), LogLevel.NORMAL);
		else
			Print("[SM] Failed to save markers!", LogLevel.ERROR);
	}

	// Прочитати мітки з JSON у сховище. Робиться один раз.
	void Load()
	{
		if (!Replication.IsServer() || m_bLoaded)
			return;
		if (!SM_MarkerConfig.GetInstance().m_bPersist)	// збереження вимкнено — стартуємо з порожнього
		{
			m_bLoaded = true;
			return;
		}

		if (m_sSaveFile == "")
			m_sSaveFile = GenerateSaveFileName();

		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
		store.ServerClear();	// важливо: сховище переживає Workbench Reload, тож чистимо накопичене

		JsonLoadContext ctx = new JsonLoadContext();
		if (!ctx.LoadFromFile(m_sSaveFile))
		{
			// нового файлу ще нема → одноразова міграція зі старого мода "Saving Markers" (якщо є старий файл)
			SM_MigrateFromOldMod();
			m_bLoaded = true;
			return;
		}

		m_sCode = "";
		ctx.ReadValue("code", m_sCode);	// may be missing in old saves -> stays "", EnsureCode generates one

		int count;
		ctx.ReadValue("count", count);

		int valid = 0;
		for (int i = 0; i < count; i++)
		{
			if (!ctx.StartObject(string.Format("m_%1", i)))
				continue;

			SM_MapMarkerData m = new SM_MapMarkerData();
			if (m.DeserializeFrom(ctx) && m.IsValid())
			{
				store.ServerLoad(m);	// дедуп по id
				valid++;
			}

			ctx.EndObject();
		}

		m_bLoaded = true;
		Print(string.Format("[SM] Loaded %1 markers", valid), LogLevel.NORMAL);
	}

	// Копія поточного файлу перед перезаписом.
	protected void CreateBackup()
	{
		if (m_sSaveFile == "" || m_sBackupFile == "")
			return;

		JsonLoadContext check = new JsonLoadContext();
		if (!check.LoadFromFile(m_sSaveFile))
			return;	// копіювати нічого

		FileIO.CopyFile(m_sSaveFile, m_sBackupFile);
	}

	// Ідентифікатор місії (логіка зі старого мода, перевірена) — спільний для нового й старого шляхів.
	// Static: the client-side Local persistence writes it into its files as a human-readable label, so
	// a folder full of random server codes still tells you which scenario each one came from.
	static string SM_MissionIdentifier()
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

	protected string GenerateSaveFileName()
	{
		return string.Format("%1/SM_Markers2_%2.json", SAVE_DIR, SM_MissionIdentifier());
	}

	// Файл збереження СТАРОГО мода "Saving Markers" (для одноразової міграції).
	protected string SM_OldSaveFileName()
	{
		return string.Format("$profile:SM_Markers_%1.json", SM_MissionIdentifier());
	}

	// ОДНОРАЗОВА міграція зі старого мода. Викликається лише коли НОВОГО файлу ще нема.
	// Читає старий $profile:SM_Markers_<місія>.json, конвертує мітки у наш формат, зберігає в новому
	// файлі й АРХІВУЄ старий (перейменовує), щоб більше ніколи не переробляти.
	protected void SM_MigrateFromOldMod()
	{
		string oldFile = SM_OldSaveFileName();
		JsonLoadContext ctx = new JsonLoadContext();
		if (!ctx.LoadFromFile(oldFile))
			return;	// старого файлу нема — мігрувати нічого

		int count = 0;
		ctx.ReadValue("markerCount", count);
		Print(string.Format("[SM] MIGRATION: found old 'Saving Markers' save '%1' with %2 markers — converting once...", oldFile, count), LogLevel.NORMAL);

		// Конфіги для конвертації (колір цивільних із палітри; identity/dimension військових із configID).
		SCR_MapMarkerEntryPlaced placed;
		SCR_MapMarkerEntryMilitary milCfg;
		SCR_MapMarkerManagerComponent mgr = SCR_MapMarkerManagerComponent.GetInstance();
		if (mgr)
		{
			SCR_MapMarkerConfig cfg = mgr.GetMarkerConfig();
			if (cfg)
			{
				placed = SCR_MapMarkerEntryPlaced.Cast(cfg.GetMarkerEntryConfigByType(SCR_EMapMarkerType.PLACED_CUSTOM));
				milCfg = SCR_MapMarkerEntryMilitary.Cast(cfg.GetMarkerEntryConfigByType(SCR_EMapMarkerType.PLACED_MILITARY));
			}
		}

		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
		int imported = 0;
		int mil = 0;

		for (int i = 0; i < count; i++)
		{
			if (!ctx.StartObject(string.Format("marker_%1", i)))
				continue;

			int markerID, ownerID, worldX, worldY, configID, factionFlags, colorEntry, iconEntry, rotation, markerType, flags;
			string customText;
			ctx.ReadValue("markerID", markerID);
			ctx.ReadValue("ownerID", ownerID);
			ctx.ReadValue("worldX", worldX);
			ctx.ReadValue("worldY", worldY);
			ctx.ReadValue("configID", configID);
			ctx.ReadValue("factionFlags", factionFlags);
			ctx.ReadValue("colorEntry", colorEntry);
			ctx.ReadValue("iconEntry", iconEntry);
			ctx.ReadValue("rotation", rotation);
			ctx.ReadValue("markerType", markerType);
			ctx.ReadValue("flags", flags);
			ctx.ReadValue("customText", customText);
			ctx.EndObject();

			SM_MapMarkerData d = new SM_MapMarkerData();
			d.m_iId       = markerID;
			d.m_iOwnerId  = ownerID;
			d.m_iPosX     = worldX;
			d.m_iPosY     = worldY;
			d.m_iRotation = rotation;
			d.m_sText     = customText;
			d.m_iSize     = 200;	// стандартний розмір для всіх відновлених
			d.m_sLastEditor = "";	// невідомо (стара сесія)

			bool isMil = (markerType == SCR_EMapMarkerType.PLACED_MILITARY || markerType == SCR_EMapMarkerType.PLACED_RECON);
			if (isMil && milCfg)
			{
				// Декодуємо так само, як ваніль: configID = dimension*100 + faction; icons = старе flags.
				d.m_iKind = SM_EMarkerKind.MILITARY;
				d.m_iSymbolFlags = flags;

				SCR_MarkerMilitaryFactionEntry fe = milCfg.GetFactionEntry(configID % 100);
				SCR_MarkerMilitaryDimension de = milCfg.GetDimensionEntry(configID / 100);
				if (fe)
				{
					d.m_iIdentity = fe.GetFactionIdentity();
					d.m_iColor = fe.GetColor().PackToInt();
				}
				else
				{
					d.m_iColor = 0xFFFFFFFF;
				}
				if (de)
					d.m_iDimension = de.GetDimension();
				mil++;
			}
			else
			{
				d.m_iKind = SM_EMarkerKind.CIVILIAN;
				d.m_iIconEntry = iconEntry;
				if (placed)
					d.m_iColor = placed.GetColorEntry(colorEntry).PackToInt();
				else
					d.m_iColor = 0xFFFFFFFF;
			}

			// Видимість: усі мігровані мітки робимо ЗАГАЛЬНОДОСТУПНИМИ (надійніше, ніж вгадувати сторону).
			d.m_iVisibility = SM_EMarkerVisibility.ALL;
			d.m_iChannel = -1;

			if (d.IsValid())
			{
				store.ServerLoad(d);	// дедуп по id, m_iNextId зсунеться
				imported++;
			}
		}

		// Зберігаємо вже в НОВОМУ форматі й архівуємо старий файл, щоб міграція НЕ повторювалась.
		Save();
		SM_ArchiveOldFile(oldFile);
		Print(string.Format("[SM] MIGRATION DONE: imported %1 markers (%2 military, %3 civilian) into new format. Old file archived — this conversion runs ONCE.", imported, mil, imported - mil), LogLevel.NORMAL);
	}

	// Перейменовує старий файл у *_migrated.json, щоб його більше не бачили як «непереконвертований».
	protected void SM_ArchiveOldFile(string oldFile)
	{
		string archived = oldFile;
		archived.Replace(".json", "_migrated.json");
		FileIO.CopyFile(oldFile, archived);
		FileIO.DeleteFile(oldFile);
	}

	// Завершення сценарію (перемога/поразка/нічия): за конфігом — бекап з ротацією + очищення міток,
	// щоб НОВА гра не пам'ятала мітки старої (для серверів конфлікту).
	void ClearForNewScenario()
	{
		if (!Replication.IsServer())
			return;
		if (!SM_MarkerConfig.GetInstance().m_bClearOnGameEnd)
			return;
		if (m_sSaveFile == "")
			m_sSaveFile = GenerateSaveFileName();

		Save();						// зафіксувати поточні мітки у файл (раптом останні зміни не збереглись)
		SM_RotateEndBackups();		// ротація: копія поточного → _end1, старі зсуваються
		SM_MapMarkerStore.GetInstance().ServerClear();
		m_sCode = SM_GenerateCode();	// new game = new code: clients treat it as a "new server" and won't show old Local markers
		Save();						// перезаписати порожнім + новий код — нова гра почне без міток
		Print(string.Format("[SM] Scenario ended — markers cleared, new code %1 (backup kept).", m_sCode), LogLevel.NORMAL);
	}

	// Ім'я файлу-бекапу завершення: SM_Markers2_<місія>_endN.json (N=1 найновіший).
	protected string SM_EndBackupName(int i)
	{
		string s = m_sSaveFile;
		s.Replace(".json", string.Format("_end%1.json", i));
		return s;
	}

	// Кільцева ротація бекапів: зсуваємо _i → _(i+1) (найстаріший _max випадає), поточний → _1.
	protected void SM_RotateEndBackups()
	{
		int maxB = SM_MarkerConfig.GetInstance().m_iMaxEndBackups;
		if (maxB <= 0 || m_sSaveFile == "")
			return;

		// Копіюємо лише наявні файли — інакше CopyFile наплодив би порожні _endN при першому ж завершенні.
		for (int i = maxB - 1; i >= 1; i--)
		{
			string src = SM_EndBackupName(i);
			if (FileIO.FileExists(src))
				FileIO.CopyFile(src, SM_EndBackupName(i + 1));
		}
		if (FileIO.FileExists(m_sSaveFile))
			FileIO.CopyFile(m_sSaveFile, SM_EndBackupName(1));
	}
}

// Хуки гейммоду для збереження (сервер).
modded class SCR_BaseGameMode
{
	override void OnGameModeStart()
	{
		super.OnGameModeStart();

		SM_ResetSessionState();

		if (Replication.IsServer())
		{
			// невелика затримка, щоб MissionHeader точно встиг з'явитись
			GetGame().GetCallqueue().CallLater(SM_InitMarkerPersistence, 1000, false);
		}
	}

	// Drop everything a previous session may have left behind. All of this state is static
	// and survives mission changes and Workbench Reload, so a new game mode must start clean.
	// Server persistence re-initializes in SM_InitMarkerPersistence; the client-side Local
	// storages activate later — on a client when the server codes arrive (RpcDo_ServerCodes),
	// on a listen-host via SM_HostSyncLocal.
	protected void SM_ResetSessionState()
	{
		SM_MapMarkerPersistence.ResetInstance();
		SM_DrawingPersistence.ResetInstance();
		SM_LocalMarkerPersistence.ResetInstance();	// flushes unsaved Local data before dropping
		SM_LocalDrawingPersistence.ResetInstance();
		SM_DrawOutbox.Reset();						// pending draw batches of the old session
		SM_MapMarkerStore.GetInstance().ClearLocals();	// leftover Local visuals in the stores
		SM_MapDrawingStore.GetInstance().ClearLocals();
	}

	protected void SM_InitMarkerPersistence()
	{
		SM_MapMarkerPersistence.GetInstance().InitServer();	// він читає SM_MarkerConfig
		SM_DrawingPersistence.GetInstance().InitServer();	// після конфігу — потрібен m_bDrawPersist
	}

	override void OnGameStateChanged()
	{
		super.OnGameStateChanged();

		if (Replication.IsServer() && GetState() == SCR_EGameModeState.POSTGAME)
		{
			SM_MapMarkerPersistence.GetInstance().Save();
			SM_DrawingPersistence.GetInstance().Save();
		}
	}

	override void OnPlayerDisconnected(int playerId, KickCauseCode cause, int timeout)
	{
		super.OnPlayerDisconnected(playerId, cause, timeout);

		if (Replication.IsServer())
		{
			SM_MarkerNet.ClearRateData(playerId);	// прибрати анти-спам дані гравця
			SM_DrawingNet.ClearRateData(playerId);
		}
	}

	// Фактичне завершення сценарію (перемога/поразка/нічия). За конфігом — бекап + очищення міток,
	// щоб нова гра почала «з чистого аркуша» (сервери конфлікту).
	override protected void OnGameModeEnd(SCR_GameModeEndData endData)
	{
		super.OnGameModeEnd(endData);

		if (Replication.IsServer())
		{
			SM_MapMarkerPersistence.GetInstance().ClearForNewScenario();
			SM_DrawingPersistence.GetInstance().ClearForNewScenario();
		}
	}
}
