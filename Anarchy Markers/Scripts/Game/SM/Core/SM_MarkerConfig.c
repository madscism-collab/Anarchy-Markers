// Налаштування мода для власників серверів. Файл: $profile:SavingMarkers/SM_Config.cfg
// Простий текстовий формат key=value з #-коментарями (читабельний, кожен параметр в окремому
// рядку). Мод створює файл з дефолтами при першому старті; власник редагує і перезапускає.
// Одноразова міграція зі старого SM_Config.json (щоб не втратити налаштування при оновленні).
// Усі параметри застосовує СЕРВЕР. Перемикачі каналів (allow*) ще й реплікуються клієнтам,
// щоб у діалозі заблокувати заборонені кнопки видимості.
class SM_MarkerConfig
{
	protected static ref SM_MarkerConfig s_Instance;

	// 1-4. Дозволені канали розміщення
	bool m_bAllowLocal    = true;
	bool m_bAllowGroup    = true;
	bool m_bAllowSide     = true;
	bool m_bAllowGlobal   = true;
	// 5. Зберігати мітки між сесіями
	bool m_bPersist       = true;
	// 6. Показувати, хто останнім редагував/рухав мітку
	bool m_bShowLastEditor = true;
	// 7. Час сценарію (true) чи реальний (false) у позначці часу
	bool m_bScenarioTime  = true;
	// 8. Логувати, хто видалив мітку
	bool m_bLogDeleter    = true;
	// 9. Ліміт міток на одного гравця (0 = без обмеження)
	int  m_iPerPlayerLimit = 0;
	// 10. Загальний ліміт міток на сервері (0 = без обмеження)
	int  m_iTotalLimit     = 0;
	// 11. Назви фракцій у військовому діалозі: false = наші (Friendly/Enemy/Enemy 2…),
	//     true = ванільні (BLUFOR/OPFOR/INDFOR; "Enemy 2" лишається, бо це наша додана фракція)
	bool m_bVanillaFactionNames = false;
	// 12. Дозволити «показати пальцем» (вказівник на мапі). false — вимикає функцію і її підказку керування
	bool m_bAllowPointer = true;
	// 13. Дозволити Ctrl+ЛКМ — швидко поставити копію останньої розміщеної/редагованої гравцем мітки.
	//     false — вимикає функцію і її підказку керування
	bool m_bAllowCopyLast = true;
	// 14. Ліміт розміщень на гравця за хвилину (ковзне вікно 60 с). Спроби понад ліміт відхиляються.
	//     0 = без обмеження
	int  m_iPerMinuteLimit = 25;
	// 15. Поріг попередження про спам: якщо гравець за хвилину зробить БІЛЬШЕ за стільки спроб
	//     розміщення (рахуються і відхилені) — у консоль/лог сервера пишеться WARNING для адмінів
	//     (кік/бан тролів). 0 = вимкнено
	int  m_iSpamWarnPerMinute = 15;
	// 16. Очищати мітки при фактичному завершенні сценарію (перемога/поразка/нічия). Для серверів конфлікту,
	//     щоб нова гра не пам'ятала мітки старої. false = мітки зберігаються між іграми (як раніше).
	bool m_bClearOnGameEnd = false;
	// 17. Скільки бекапів робити перед очищенням (ротація, найновіший = _end1). 0 = не робити бекап.
	int  m_iMaxEndBackups = 15;
	// --- Малювання на мапі (олівець). Окремо від міток ---
	// ФІЛОСОФІЯ ДЕФОЛТІВ: із коробки діє ЛИШЕ анти-спам ліміт за хвилину; решта обмежень
	// вимкнена (0) — власник сервера вмикає сам за потреби.
	// 18. Дозволити малювання. false — вимикає фічу та її UI/підказки
	bool m_bAllowDrawing = true;
	// 19. Зберігати малюнки між сесіями (окремий файл SM_Drawings_<місія>.json)
	bool m_bDrawPersist = true;
	// 20. Макс. точок в одному штриху — ТЕХНІЧНИЙ захист передачі (штрих їде одним RPC);
	//     це не анти-спам. Мін. 2; надто велике значення ризикує не влізти в мережевий пакет.
	int  m_iDrawMaxPointsPerStroke = 200;
	// 21. Ліміт штрихів на гравця (0 = без обмеження)
	int  m_iDrawMaxPerPlayer = 0;
	// 22. Загальний ліміт штрихів на сервері (0 = без обмеження)
	int  m_iDrawMaxTotal = 0;
	// 23. Ліміт штрихів на гравця за хвилину (єдине дієве обмеження з коробки; 0 = вимкнути).
	//     Це вікно ділять і ручне малювання, і авто-малювання темплейтів: темплейт випускає штрихи
	//     БЕЗ пауз, поки їх уміщається в це вікно, тож темплейт на стільки ж штрихів малюється за
	//     секунди. 60 = типова заготовка йде швидко, і водночас це стеля проти спаму.
	int  m_iDrawPerMinuteLimit = 60;
	// 24. RDP-епсилон у метрах (спрощення штриха для економії трафіку). 0 = ВИМКНЕНО (дефолт):
	//     навіть 1 м спрощення робить видимі зломи на товстих лініях (сегменти без стиків),
	//     а капчур і так обмежений кроком 3 м + стелею точок — форма важливіша за ~1.5 КБ.
	int  m_iDrawRdpEpsilon = 0;
	// 25. Дозволити гравцям стирати ЧУЖІ штрихи цілком (Del/X/гумка). Дефолт: УВІМКНЕНО —
	//     як і з мітками, командна мапа спільна; залочені зевсом штрихи гравці не стирають ніколи.
	//     Стирає лише той, кому штрих взагалі видно (канал); часткове різання гумкою — завжди лише своїх.
	bool m_bDrawEraseOthers = true;
	// 26. Batch server-channel draw/erase ops: the client buffers them and sends one packet
	//     every N ms (fewer RPCs -> lighter on FPS, esp. mass-erasing / always-open tablet).
	//     The author sees own strokes instantly (optimistic); others after the packet is sent.
	//     The Local channel is always instant. 0 = off (send immediately, the old behavior).
	//     Replicated to clients.
	int  m_iDrawBatchIntervalMs = 3000;
	// 27. Дозволити темплейти (збереження/розміщення заготовок малюнків). Фіча суто клієнтська —
	//     штрихи йдуть звичайним шляхом під усі ліміти вище — тож вимикач лише прибирає UI у
	//     гравців цього сервера. Реплікується клієнтам.
	bool m_bAllowTemplates = true;

	protected const string DIR      = "$profile:SavingMarkers";
	protected const string FILE     = "$profile:SavingMarkers/SM_Config.cfg";
	protected const string FILE_OLD = "$profile:SavingMarkers/SM_Config.json";	// для одноразової міграції

	static SM_MarkerConfig GetInstance()
	{
		if (!s_Instance)
			s_Instance = new SM_MarkerConfig();
		return s_Instance;
	}

	// Сервер: прочитати конфіг (.cfg). Якщо його нема — мігрувати зі старого .json (як був), інакше дефолти.
	// Наприкінці завжди пере-зберігаємо: дописує нові ключі/коментарі, додані в оновленнях.
	void ServerLoadOrCreate()
	{
		FileIO.MakeDirectory(DIR);

		if (FileIO.FileExists(FILE))
		{
			ParseCfg();
			Print("[SM] Config loaded from " + FILE, LogLevel.NORMAL);
		}
		else if (FileIO.FileExists(FILE_OLD))
		{
			MigrateOldJson();
			Print("[SM] Migrated old SM_Config.json -> SM_Config.cfg", LogLevel.NORMAL);
		}
		else
		{
			Print("[SM] Config not found — creating defaults at " + FILE, LogLevel.NORMAL);
		}

		Clamp();
		Save();	// записати/оновити читабельний .cfg
	}

	protected void Clamp()
	{
		if (m_iMaxEndBackups < 0)  m_iMaxEndBackups = 0;
		if (m_iPerPlayerLimit < 0) m_iPerPlayerLimit = 0;
		if (m_iTotalLimit < 0)     m_iTotalLimit = 0;
		if (m_iPerMinuteLimit < 0)    m_iPerMinuteLimit = 0;
		if (m_iSpamWarnPerMinute < 0) m_iSpamWarnPerMinute = 0;
		if (m_iDrawMaxPointsPerStroke < 2) m_iDrawMaxPointsPerStroke = 2;
		if (m_iDrawMaxPerPlayer < 0)  m_iDrawMaxPerPlayer = 0;
		if (m_iDrawMaxTotal < 0)      m_iDrawMaxTotal = 0;
		if (m_iDrawPerMinuteLimit < 0) m_iDrawPerMinuteLimit = 0;
		if (m_iDrawRdpEpsilon < 0)    m_iDrawRdpEpsilon = 0;
		if (m_iDrawBatchIntervalMs < 0) m_iDrawBatchIntervalMs = 0;
		if (m_iDrawBatchIntervalMs > 0 && m_iDrawBatchIntervalMs < 250) m_iDrawBatchIntervalMs = 250;	// sub-250ms is effectively instant, just extra overhead
	}

	// --- Читання текстового .cfg (key=value, # — коментар) ---
	protected void ParseCfg()
	{
		FileHandle h = FileIO.OpenFile(FILE, FileMode.READ);
		if (!h)
			return;
		string line;
		int guard = 0;
		// ReadLine повертає к-ть символів, -1 на EOF. Порожній рядок = 0 — НЕ кінець (тому >= 0, не > 0),
		// інакше читання спинялося б на першому пропуску між секціями й ключі не застосовувались.
		while (h.ReadLine(line) >= 0)
		{
			guard++;
			if (guard > 10000)	// страховка від зациклення
				break;

			line.Replace("\r", "");	// прибрати можливий CR (Windows-перенос)
			line.Trim();
			if (line.IsEmpty() || line.IndexOf("#") == 0)
				continue;
			int eq = line.IndexOf("=");
			if (eq <= 0)
				continue;
			string key = line.Substring(0, eq);
			string val = line.Substring(eq + 1, line.Length() - eq - 1);
			key.Trim();
			val.Trim();
			ApplyKeyValue(key, val);
		}
		h.Close();
	}

	protected void ApplyKeyValue(string key, string val)
	{
		if      (key == "allowLocalChannel")      m_bAllowLocal           = ParseBool(val);
		else if (key == "allowGroupChannel")      m_bAllowGroup           = ParseBool(val);
		else if (key == "allowSideChannel")       m_bAllowSide            = ParseBool(val);
		else if (key == "allowGlobalChannel")     m_bAllowGlobal          = ParseBool(val);
		else if (key == "persistMarkers")         m_bPersist              = ParseBool(val);
		else if (key == "showLastEditor")         m_bShowLastEditor       = ParseBool(val);
		else if (key == "useScenarioTime")        m_bScenarioTime         = ParseBool(val);
		else if (key == "logMarkerDeleter")       m_bLogDeleter           = ParseBool(val);
		else if (key == "maxMarkersPerPlayer")    m_iPerPlayerLimit       = val.ToInt();
		else if (key == "maxMarkersTotal")        m_iTotalLimit           = val.ToInt();
		else if (key == "useVanillaFactionNames") m_bVanillaFactionNames  = ParseBool(val);
		else if (key == "allowPointer")           m_bAllowPointer         = ParseBool(val);
		else if (key == "allowCopyLast")          m_bAllowCopyLast        = ParseBool(val);
		else if (key == "maxMarkersPerPlayerPerMinute") m_iPerMinuteLimit = val.ToInt();
		else if (key == "spamWarnPerPlayerPerMinute")   m_iSpamWarnPerMinute = val.ToInt();
		else if (key == "clearOnGameEnd")         m_bClearOnGameEnd       = ParseBool(val);
		else if (key == "maxEndBackups")          m_iMaxEndBackups        = val.ToInt();
		else if (key == "allowDrawing")           m_bAllowDrawing         = ParseBool(val);
		else if (key == "drawPersist")            m_bDrawPersist          = ParseBool(val);
		else if (key == "drawMaxPointsPerStroke") m_iDrawMaxPointsPerStroke = val.ToInt();
		else if (key == "drawMaxPerPlayer")       m_iDrawMaxPerPlayer     = val.ToInt();
		else if (key == "drawMaxTotal")           m_iDrawMaxTotal         = val.ToInt();
		else if (key == "drawPerPlayerPerMinute") m_iDrawPerMinuteLimit   = val.ToInt();
		else if (key == "drawRdpEpsilonMeters")   m_iDrawRdpEpsilon       = val.ToInt();
		else if (key == "drawEraseOthersAllowed") m_bDrawEraseOthers      = ParseBool(val);
		else if (key == "drawBatchIntervalMs")    m_iDrawBatchIntervalMs  = val.ToInt();
		else if (key == "allowTemplates")         m_bAllowTemplates       = ParseBool(val);
	}

	protected bool ParseBool(string v)
	{
		return v == "true" || v == "1" || v == "yes" || v == "on";
	}

	protected string B2S(bool b)
	{
		if (b) return "true";
		return "false";
	}

	// Одноразова міграція значень зі старого JSON у поля (далі Save() запише .cfg).
	protected void MigrateOldJson()
	{
		JsonLoadContext ctx = new JsonLoadContext();
		if (!ctx.LoadFromFile(FILE_OLD))
			return;
		ctx.ReadValue("allowLocalChannel",  m_bAllowLocal);
		ctx.ReadValue("allowGroupChannel",  m_bAllowGroup);
		ctx.ReadValue("allowSideChannel",   m_bAllowSide);
		ctx.ReadValue("allowGlobalChannel", m_bAllowGlobal);
		ctx.ReadValue("persistMarkers",     m_bPersist);
		ctx.ReadValue("showLastEditor",     m_bShowLastEditor);
		ctx.ReadValue("useScenarioTime",    m_bScenarioTime);
		ctx.ReadValue("logMarkerDeleter",   m_bLogDeleter);
		ctx.ReadValue("maxMarkersPerPlayer", m_iPerPlayerLimit);
		ctx.ReadValue("maxMarkersTotal",     m_iTotalLimit);
		ctx.ReadValue("useVanillaFactionNames", m_bVanillaFactionNames);
		ctx.ReadValue("allowPointer",        m_bAllowPointer);
		ctx.ReadValue("allowCopyLast",       m_bAllowCopyLast);
		ctx.ReadValue("maxMarkersPerPlayerPerMinute", m_iPerMinuteLimit);
		ctx.ReadValue("spamWarnPerPlayerPerMinute",   m_iSpamWarnPerMinute);
		ctx.ReadValue("clearOnGameEnd", m_bClearOnGameEnd);
		ctx.ReadValue("maxEndBackups",  m_iMaxEndBackups);
		ctx.ReadValue("allowDrawing",            m_bAllowDrawing);
		ctx.ReadValue("drawPersist",             m_bDrawPersist);
		ctx.ReadValue("drawMaxPointsPerStroke",  m_iDrawMaxPointsPerStroke);
		ctx.ReadValue("drawMaxPerPlayer",        m_iDrawMaxPerPlayer);
		ctx.ReadValue("drawMaxTotal",            m_iDrawMaxTotal);
		ctx.ReadValue("drawPerPlayerPerMinute",  m_iDrawPerMinuteLimit);
		ctx.ReadValue("drawRdpEpsilonMeters",    m_iDrawRdpEpsilon);
		ctx.ReadValue("drawBatchIntervalMs",     m_iDrawBatchIntervalMs);
	}

	// --- Запис читабельного .cfg: кожен параметр в окремому рядку + англійський коментар ---
	void Save()
	{
		FileIO.MakeDirectory(DIR);
		FileHandle h = FileIO.OpenFile(FILE, FileMode.WRITE);
		if (!h)
		{
			Print("[SM] Could not write config to " + FILE, LogLevel.WARNING);
			return;
		}

		h.WriteLine("# Anarchy Markers - server configuration");
		h.WriteLine("# Format: key=value (one per line). Lines starting with # are comments.");
		h.WriteLine("# Edit and restart the server to apply. true/false for toggles.");
		h.WriteLine("");
		h.WriteLine("# --- Visibility channels: which channels players may place markers/drawings on ---");
		h.WriteLine("# Local: only the author sees it.");
		h.WriteLine("allowLocalChannel=" + B2S(m_bAllowLocal));
		h.WriteLine("# Group: the author's group sees it.");
		h.WriteLine("allowGroupChannel=" + B2S(m_bAllowGroup));
		h.WriteLine("# Side: the author's faction sees it (enemies do not).");
		h.WriteLine("allowSideChannel=" + B2S(m_bAllowSide));
		h.WriteLine("# Global: everyone sees it, regardless of faction.");
		h.WriteLine("allowGlobalChannel=" + B2S(m_bAllowGlobal));
		h.WriteLine("");
		h.WriteLine("# --- Markers ---");
		h.WriteLine("# Persist markers across sessions (server restart / rejoin).");
		h.WriteLine("persistMarkers=" + B2S(m_bPersist));
		h.WriteLine("# Show who last created/moved a marker (hover tooltip).");
		h.WriteLine("showLastEditor=" + B2S(m_bShowLastEditor));
		h.WriteLine("# Timestamp uses scenario time (true) or real time (false).");
		h.WriteLine("useScenarioTime=" + B2S(m_bScenarioTime));
		h.WriteLine("# Log to the server console who deleted a marker (anti-grief).");
		h.WriteLine("logMarkerDeleter=" + B2S(m_bLogDeleter));
		h.WriteLine("# Vanilla faction names in the military dialog (BLUFOR/OPFOR/INDFOR) instead of Friendly/Enemy.");
		h.WriteLine("useVanillaFactionNames=" + B2S(m_bVanillaFactionNames));
		h.WriteLine("# Allow the map pointer ('point with finger'). false disables the feature and its hint.");
		h.WriteLine("allowPointer=" + B2S(m_bAllowPointer));
		h.WriteLine("# Allow Ctrl+LMB to quickly place a copy of the player's last marker.");
		h.WriteLine("allowCopyLast=" + B2S(m_bAllowCopyLast));
		h.WriteLine("# Max markers a single player may own (0 = unlimited).");
		h.WriteLine("maxMarkersPerPlayer=" + m_iPerPlayerLimit.ToString());
		h.WriteLine("# Max markers total on the server (0 = unlimited).");
		h.WriteLine("maxMarkersTotal=" + m_iTotalLimit.ToString());
		h.WriteLine("# Max marker placements per player per minute (0 = unlimited). Excess attempts are rejected.");
		h.WriteLine("maxMarkersPerPlayerPerMinute=" + m_iPerMinuteLimit.ToString());
		h.WriteLine("# Spam warning threshold: more attempts/min than this logs a WARNING for admins (0 = off).");
		h.WriteLine("spamWarnPerPlayerPerMinute=" + m_iSpamWarnPerMinute.ToString());
		h.WriteLine("");
		h.WriteLine("# --- Scenario end (Conflict-style servers) ---");
		h.WriteLine("# On real scenario end (win/lose/draw): back up and clear markers AND drawings so a new game starts fresh.");
		h.WriteLine("clearOnGameEnd=" + B2S(m_bClearOnGameEnd));
		h.WriteLine("# How many rotating backups to keep before clearing (newest = _end1). 0 = no backup.");
		h.WriteLine("maxEndBackups=" + m_iMaxEndBackups.ToString());
		h.WriteLine("");
		h.WriteLine("# --- Map drawing (pencil). Separate from markers, separate save file (SM_Drawings_<mission>.json) ---");
		h.WriteLine("# Allow map drawing. false disables the feature and its panel.");
		h.WriteLine("allowDrawing=" + B2S(m_bAllowDrawing));
		h.WriteLine("# Persist drawings across sessions.");
		h.WriteLine("drawPersist=" + B2S(m_bDrawPersist));
		h.WriteLine("# Max points in one stroke (server trims after RDP simplification).");
		h.WriteLine("drawMaxPointsPerStroke=" + m_iDrawMaxPointsPerStroke.ToString());
		h.WriteLine("# Max strokes a single player may own (0 = unlimited; default off — enable if you need it).");
		h.WriteLine("drawMaxPerPlayer=" + m_iDrawMaxPerPlayer.ToString());
		h.WriteLine("# Max strokes total on the server (0 = unlimited; default off).");
		h.WriteLine("drawMaxTotal=" + m_iDrawMaxTotal.ToString());
		h.WriteLine("# Max strokes per player per minute — the only limit active by default (0 = unlimited).");
		h.WriteLine("# Templates share this window: a template draws its strokes with no delay as long as they");
		h.WriteLine("# fit in it, so one of <= this many strokes stamps down in seconds. Raise it for bigger templates.");
		h.WriteLine("drawPerPlayerPerMinute=" + m_iDrawPerMinuteLimit.ToString());
		h.WriteLine("# RDP stroke simplification in meters (traffic saving). 0 = OFF (recommended):");
		h.WriteLine("# even 1m creates visible kinks on thick lines; capture is already capped anyway.");
		h.WriteLine("drawRdpEpsilonMeters=" + m_iDrawRdpEpsilon.ToString());
		h.WriteLine("# Allow players to erase OTHER players' strokes entirely (Del/eraser). Only strokes they can see.");
		h.WriteLine("# GM-locked strokes can never be erased by players. Partial erasing is always own-strokes-only.");
		h.WriteLine("drawEraseOthersAllowed=" + B2S(m_bDrawEraseOthers));
		h.WriteLine("# Batch server-channel drawing/erasing: client buffers ops and sends them in one packet every N ms");
		h.WriteLine("# (fewer RPCs -> lighter on FPS, esp. mass-erasing or an always-open tablet). The author sees own");
		h.WriteLine("# strokes instantly (optimistic); others see them after the packet is sent. Local channel is always");
		h.WriteLine("# instant. 0 = off (send immediately, as before). Replicated to clients.");
		h.WriteLine("drawBatchIntervalMs=" + m_iDrawBatchIntervalMs.ToString());
		h.WriteLine("# Allow drawing templates (saving/stamping reusable drawings). The strokes a template");
		h.WriteLine("# draws are ordinary strokes under all the limits above; false just removes the feature's UI.");
		h.WriteLine("allowTemplates=" + B2S(m_bAllowTemplates));

		h.Close();
	}

	// Чи дозволено цей канал (за областю видимості).
	bool IsVisibilityAllowed(int vis)
	{
		switch (vis)
		{
			case SM_EMarkerVisibility.PERSONAL: return m_bAllowLocal;
			case SM_EMarkerVisibility.GROUP:    return m_bAllowGroup;
			case SM_EMarkerVisibility.FACTION:  return m_bAllowSide;
			case SM_EMarkerVisibility.ALL:      return m_bAllowGlobal;
		}
		return true;
	}

	// Клієнт: застосувати реплікований із сервера набір налаштувань, потрібних діалогу
	// (дозволені канали + чи ванільні назви фракцій).
	void SetClientFlags(bool local, bool group, bool side, bool global, bool vanillaFactionNames, bool allowPointer, bool allowCopyLast, int drawBatchMs)
	{
		m_bAllowLocal  = local;
		m_bAllowGroup  = group;
		m_bAllowSide   = side;
		m_bAllowGlobal = global;
		m_bVanillaFactionNames = vanillaFactionNames;
		m_bAllowPointer = allowPointer;
		m_bAllowCopyLast = allowCopyLast;
		m_iDrawBatchIntervalMs = drawBatchMs;
	}

	// Клієнт: серверні ліміти малювання. Раніше клієнт їх не знав — сервер просто відхиляв зайве.
	// Темплейтам вони потрібні ЗАЗДАЛЕГІДЬ: авто-малювання тримає темп під ліміт за хвилину, а
	// нездійсненний темплейт (штрихів більше, ніж сервер узагалі дозволить) треба показати гравцю
	// ДО того, як він півгодини протримає ЛКМ.
	void SetClientDrawLimits(int perMinute, int maxPerPlayer, int maxTotal, int maxPointsPerStroke, bool allowTemplates)
	{
		m_iDrawPerMinuteLimit     = perMinute;
		m_iDrawMaxPerPlayer       = maxPerPlayer;
		m_iDrawMaxTotal           = maxTotal;
		m_iDrawMaxPointsPerStroke = maxPointsPerStroke;
		m_bAllowTemplates         = allowTemplates;
	}
}
