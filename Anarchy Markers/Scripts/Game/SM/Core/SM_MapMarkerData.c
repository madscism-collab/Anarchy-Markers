// Модель даних мітки. Незалежна від ванільних класів, несе все потрібне, щоб мітку
// намалювати (цивільна іконка або військовий APP-6 символ) плюс наші поля: колір,
// видимість, серверний id, канал (фракція/група).
// Серіалізується двома способами: у JSON (збереження на сервері) і в array<int>+text
// (реплікація через RPC — так обходимо ліміт параметрів і не залежимо від внутрішньої
// серіалізації класів движка). m_iId призначає тільки сервер, по ньому йде вся дедуплікація.

enum SM_EMarkerKind
{
	CIVILIAN = 0,	// цивільна іконка (vanilla PLACED_CUSTOM, m_iIconEntry)
	MILITARY = 1,	// військовий APP-6 символ (identity/dimension/symbolFlags)
}

// Старий enum розміру. Лишився для сумісності зі збереженнями (тепер розмір зберігаємо у відсотках).
enum SM_EMarkerSize
{
	SMALL  = 0,
	MEDIUM = 1,
	LARGE  = 2,
	HUGE   = 3,
}

// Хто бачить мітку. Порядок важливий: чим більше число — тим ширша видимість
// (на цьому тримається правило "видимість можна лише розширювати").
enum SM_EMarkerVisibility
{
	PERSONAL = 0,	// лише автор
	GROUP    = 1,	// група автора (канал = group id)
	FACTION  = 2,	// фракція автора (канал = faction index), ворожі не бачать
	ALL      = 3,	// усі, незалежно від фракції
}

// Причина відмови в розміщенні — сервер шле власнику, клієнт показує локальне повідомлення.
// ТІЛЬКИ ДОПИСУВАТИ в кінець (значення їдуть по мережі — перенумерація зламала б стару версію клієнта).
enum SM_EPlaceDenyReason
{
	PER_PLAYER_LIMIT = 0,	// ліміт міток на гравця (загальний)
	TOTAL_LIMIT      = 1,	// загальний ліміт міток на сервері
	PER_MINUTE_LIMIT = 2,	// ліміт розміщень на гравця за хвилину
	MARKER_LOCKED    = 3,	// мітка заблокована зевсом — гравець не може рухати/редагувати/видаляти
	// --- малювання ---
	DRAW_PER_MINUTE_LIMIT = 4,	// ліміт штрихів на гравця за хвилину
	DRAW_PER_PLAYER_LIMIT = 5,	// ліміт штрихів на гравця
	DRAW_TOTAL_LIMIT      = 6,	// загальний ліміт штрихів на сервері
	DRAW_CHANNEL_DISABLED = 7,	// обраний канал видимості вимкнено конфігом сервера
	DRAW_LOCKED           = 8,	// штрих заблоковано зевсом — гравець не може стерти
	FILL_NOT_CLOSED       = 9,	// заливка: область не замкнута — фарба «витікає» за межі
	FILL_BLOCKED          = 10,	// заливка: клік у лінію/заливку — нема вільного місця
	FILL_NO_NARROW        = 11,	// заливка: видимість можна лише розширити — канал лишився, решта застосована
}

class SM_MapMarkerData
{
	// Скільки числових полів пакуємо в масив (id/owner/text ідуть окремо)
	static const int PACKED_INT_COUNT = 18;

	int    m_iId;			// унікальний id, призначає сервер; -1 = ще не призначено
	int    m_iOwnerId;		// playerId автора; -1 = серверна мітка
	int    m_iPosX;			// світова позиція X (метри)
	int    m_iPosY;			// світова позиція Y (метри)

	int    m_iKind;			// SM_EMarkerKind

	// CIVILIAN
	int    m_iIconEntry;	// індекс іконки у ванільному конфігу PLACED_CUSTOM

	// MILITARY (APP-6)
	int    m_iIdentity;		// EMilitarySymbolIdentity (форма рамки: OPFOR=ромб, BLUFOR=прямокутник…)
	int    m_iDimension;	// EMilitarySymbolDimension (LAND…)
	int    m_iSymbolFlags;	// EMilitarySymbolIcon бітмаска (INFANTRY/ARMOR…); 0 = лише рамка

	// COMMON
	int    m_iColor;		// колір ARGB
	int    m_iRotation;		// напрямок у градусах (0..359)
	int    m_iSize;			// розмір у відсотках від базового
	int    m_iVisibility;	// SM_EMarkerVisibility
	int    m_iChannel;		// id фракції/групи для FACTION/GROUP; задає сервер; -1 = н/д
	int    m_iTextColored;	// 0 = чорний підпис; 1 = підпис у колір мітки
	int    m_iDate;			// ігрова дата сценарію як yyyymmdd (0 = без позначки часу)
	int    m_iTime;			// ігровий час як hhmm
	int    m_iLastEditorId;	// playerId того, хто останнім змінив (тільки в межах сесії)
	int    m_iGmLocked;		// 1 = мітка зевса: гравці не можуть редагувати/рухати/видаляти (лише GM)
	int    m_iHideInfo;		// 1 = ховати тултіп «Edited by»/«Side» для гравців (зевс однаково бачить)
	string m_sLastEditor;	// ІМ'Я того, хто останнім змінив — зберігається в JSON (playerId між сесіями не стабільний)
	string m_sText;			// підпис

	void SM_MapMarkerData()
	{
		m_iId         = -1;
		m_iOwnerId    = -1;
		m_iPosX       = 0;
		m_iPosY       = 0;
		m_iKind       = SM_EMarkerKind.CIVILIAN;
		m_iIconEntry  = 0;
		m_iIdentity   = 0;
		m_iDimension  = 0;
		m_iSymbolFlags = 0;
		m_iColor      = 0xFFFFFFFF;
		m_iRotation   = 0;
		m_iSize       = 200;	// стандартно 200%
		m_iVisibility = SM_EMarkerVisibility.FACTION;	// стандартно Side
		m_iChannel    = -1;
		m_iTextColored = 0;
		m_iDate       = 0;
		m_iTime       = 0;
		m_iLastEditorId = -1;
		m_iGmLocked   = 0;
		m_iHideInfo   = 0;
		m_sLastEditor = "";
		m_sText       = "";
	}

	// Своя копія (Clone у движку нема)
	SM_MapMarkerData SM_Clone()
	{
		SM_MapMarkerData c = new SM_MapMarkerData();
		c.CopyFrom(this);
		return c;
	}

	void CopyFrom(notnull SM_MapMarkerData s)
	{
		m_iId         = s.m_iId;
		m_iOwnerId    = s.m_iOwnerId;
		m_iPosX       = s.m_iPosX;
		m_iPosY       = s.m_iPosY;
		m_iKind       = s.m_iKind;
		m_iIconEntry  = s.m_iIconEntry;
		m_iIdentity   = s.m_iIdentity;
		m_iDimension  = s.m_iDimension;
		m_iSymbolFlags = s.m_iSymbolFlags;
		m_iColor      = s.m_iColor;
		m_iRotation   = s.m_iRotation;
		m_iSize       = s.m_iSize;
		m_iVisibility = s.m_iVisibility;
		m_iChannel    = s.m_iChannel;
		m_iTextColored = s.m_iTextColored;
		m_iDate       = s.m_iDate;
		m_iTime       = s.m_iTime;
		m_iLastEditorId = s.m_iLastEditorId;
		m_iGmLocked   = s.m_iGmLocked;
		m_iHideInfo   = s.m_iHideInfo;
		m_sLastEditor = s.m_sLastEditor;
		m_sText       = s.m_sText;
	}

	// Пакування для RPC. Порядок тут і в UnpackInts має збігатися.
	array<int> PackInts()
	{
		array<int> a = {};
		a.Reserve(PACKED_INT_COUNT);
		a.Insert(m_iPosX);
		a.Insert(m_iPosY);
		a.Insert(m_iKind);
		a.Insert(m_iIconEntry);
		a.Insert(m_iIdentity);
		a.Insert(m_iDimension);
		a.Insert(m_iSymbolFlags);
		a.Insert(m_iColor);
		a.Insert(m_iRotation);
		a.Insert(m_iSize);
		a.Insert(m_iVisibility);
		a.Insert(m_iChannel);
		a.Insert(m_iTextColored);
		a.Insert(m_iDate);
		a.Insert(m_iTime);
		a.Insert(m_iLastEditorId);
		a.Insert(m_iGmLocked);
		a.Insert(m_iHideInfo);
		return a;
	}

	bool UnpackInts(array<int> a)
	{
		if (!a || a.Count() < PACKED_INT_COUNT)
			return false;

		m_iPosX        = a[0];
		m_iPosY        = a[1];
		m_iKind        = a[2];
		m_iIconEntry   = a[3];
		m_iIdentity    = a[4];
		m_iDimension   = a[5];
		m_iSymbolFlags = a[6];
		m_iColor       = a[7];
		m_iRotation    = a[8];
		m_iSize        = a[9];
		m_iVisibility  = a[10];
		m_iChannel     = a[11];
		m_iTextColored = a[12];
		m_iDate        = a[13];
		m_iTime        = a[14];
		m_iLastEditorId = a[15];
		m_iGmLocked    = a[16];
		m_iHideInfo    = a[17];
		return true;
	}

	// Прочитати лише видимість із запакованого масиву (індекс як у PackInts) — щоб сервер міг
	// перевірити дозволеність каналу ще до створення мітки. -1 якщо масив некоректний.
	static int VisibilityFromPacked(array<int> a)
	{
		if (!a || a.Count() < PACKED_INT_COUNT)
			return -1;
		return a[10];
	}

	// Збереження у JSON. m_iLastEditorId живе лише в межах сесії.
	bool SerializeTo(JsonSaveContext context)
	{
		if (!context)
			return false;

		context.WriteValue("id", m_iId);
		context.WriteValue("owner", m_iOwnerId);
		context.WriteValue("x", m_iPosX);
		context.WriteValue("y", m_iPosY);
		context.WriteValue("kind", m_iKind);
		context.WriteValue("icon", m_iIconEntry);
		context.WriteValue("ident", m_iIdentity);
		context.WriteValue("dim", m_iDimension);
		context.WriteValue("sym", m_iSymbolFlags);
		context.WriteValue("color", m_iColor);
		context.WriteValue("rot", m_iRotation);
		context.WriteValue("size", m_iSize);
		context.WriteValue("vis", m_iVisibility);
		context.WriteValue("ch", m_iChannel);
		context.WriteValue("txtcol", m_iTextColored);
		context.WriteValue("date", m_iDate);
		context.WriteValue("time", m_iTime);
		context.WriteValue("gmlock", m_iGmLocked);
		context.WriteValue("hideinfo", m_iHideInfo);
		context.WriteValue("editor", m_sLastEditor);
		context.WriteValue("text", m_sText);

		return true;
	}

	bool DeserializeFrom(JsonLoadContext context)
	{
		if (!context)
			return false;

		context.ReadValue("id", m_iId);
		context.ReadValue("owner", m_iOwnerId);
		context.ReadValue("x", m_iPosX);
		context.ReadValue("y", m_iPosY);
		context.ReadValue("kind", m_iKind);
		context.ReadValue("icon", m_iIconEntry);
		context.ReadValue("ident", m_iIdentity);
		context.ReadValue("dim", m_iDimension);
		context.ReadValue("sym", m_iSymbolFlags);
		context.ReadValue("color", m_iColor);
		context.ReadValue("rot", m_iRotation);
		context.ReadValue("size", m_iSize);
		context.ReadValue("vis", m_iVisibility);
		context.ReadValue("ch", m_iChannel);
		context.ReadValue("txtcol", m_iTextColored);
		context.ReadValue("date", m_iDate);
		context.ReadValue("time", m_iTime);
		context.ReadValue("gmlock", m_iGmLocked);
		context.ReadValue("hideinfo", m_iHideInfo);
		context.ReadValue("editor", m_sLastEditor);
		context.ReadValue("text", m_sText);

		return true;
	}

	bool IsValid()
	{
		if (m_iPosX < -500000 || m_iPosX > 500000)
			return false;
		if (m_iPosY < -500000 || m_iPosY > 500000)
			return false;
		if (m_iPosX == 0 && m_iPosY == 0)
			return false;

		return true;
	}

	string ToDebugString()
	{
		return string.Format("SM_Marker[id:%1 owner:%2 pos:(%3,%4) kind:%5 vis:%6]",
			m_iId, m_iOwnerId, m_iPosX, m_iPosY, m_iKind, m_iVisibility);
	}
}
