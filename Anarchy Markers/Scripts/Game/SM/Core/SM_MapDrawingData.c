// Модель даних малюнка (штриха) на мапі — окрема від міток, але та сама модель
// каналів видимості (SM_EMarkerVisibility) і той самий стиль серіалізації:
//   - JSON (збереження на сервері, окремий файл від міток);
//   - meta-масив<int> + точки<int> для RPC (обхід ліміту параметрів).
// Точки — плаский масив x,z у МЕТРАХ (як позиції міток). m_iId призначає сервер.

class SM_MapDrawingData
{
	static const int META_COUNT = 7;	// color, widthIdx, visibility, channel, gmLocked, hideInfo, fill

	int m_iId;			// унікальний id, сервер; -1 = ще не призначено
	int m_iOwnerId;		// playerId автора; -1 = серверний
	int m_iColor;		// ARGB (як у міток)
	int m_iWidthIdx;	// 0..4 — індекс товщини пензля (5 пресетів)
	int m_iVisibility;	// SM_EMarkerVisibility
	int m_iChannel;		// id фракції/групи для FACTION/GROUP; задає сервер; -1 = н/д
	int m_iCreatedMs;	// час створення (серверний тік), для впорядкування/діагностики
	int m_iGmLocked;	// 1 = штрих зевса: гравці не можуть стирати (гумкою/Del), лише GM
	int m_iHideInfo;	// 1 = ховати тултіп "Drawn by" від гравців (зевс у редакторі бачить)
	int m_iFill;		// 1 = залита область: точки — замкнутий контур полігона, не полілінія
	string m_sOwnerName;	// ІМ'Я автора — зберігається в JSON (playerId між сесіями не стабільний), як m_sLastEditor у міток

	ref array<int> m_aPoints = {};	// x,z у метрах, парами

	// AABB (метри) — для куллінгу поза екраном і швидкого hit-test гумки.
	protected int  m_iMinX, m_iMaxX, m_iMinZ, m_iMaxZ;
	protected bool m_bAABBValid;

	void SM_MapDrawingData()
	{
		m_iId         = -1;
		m_iOwnerId    = -1;
		m_iColor      = 0xFFFFFFFF;
		m_iWidthIdx   = 1;
		m_iVisibility = SM_EMarkerVisibility.FACTION;	// стандартно Side, як у міток
		m_iChannel    = -1;
		m_iCreatedMs  = 0;
		m_bAABBValid  = false;
	}

	int GetPointCount() { return m_aPoints.Count() / 2; }

	void GetPoint(int index, out int outX, out int outZ)
	{
		outX = m_aPoints[index * 2];
		outZ = m_aPoints[index * 2 + 1];
	}

	void SetPoints(notnull array<int> pts)
	{
		m_aPoints.Clear();
		m_aPoints.Copy(pts);
		RecomputeAABB();
	}

	SM_MapDrawingData SM_Clone()
	{
		SM_MapDrawingData c = new SM_MapDrawingData();
		c.CopyFrom(this);
		return c;
	}

	void CopyFrom(notnull SM_MapDrawingData s)
	{
		m_iId         = s.m_iId;
		m_iOwnerId    = s.m_iOwnerId;
		m_iColor      = s.m_iColor;
		m_iWidthIdx   = s.m_iWidthIdx;
		m_iVisibility = s.m_iVisibility;
		m_iChannel    = s.m_iChannel;
		m_iCreatedMs  = s.m_iCreatedMs;
		m_iGmLocked   = s.m_iGmLocked;
		m_iHideInfo   = s.m_iHideInfo;
		m_iFill       = s.m_iFill;
		m_sOwnerName  = s.m_sOwnerName;
		m_aPoints.Clear();
		m_aPoints.Copy(s.m_aPoints);
		RecomputeAABB();
	}

	// --- RPC: скалярна мета окремо, точки окремо (обидва array<int>) ---
	array<int> PackMeta()
	{
		array<int> a = {};
		a.Insert(m_iColor);
		a.Insert(m_iWidthIdx);
		a.Insert(m_iVisibility);
		a.Insert(m_iChannel);
		a.Insert(m_iGmLocked);
		a.Insert(m_iHideInfo);
		a.Insert(m_iFill);
		return a;
	}

	bool UnpackMeta(array<int> a)
	{
		if (!a || a.Count() < META_COUNT)
			return false;
		m_iColor      = a[0];
		m_iWidthIdx   = a[1];
		m_iVisibility = a[2];
		m_iChannel    = a[3];
		m_iGmLocked   = a[4];
		m_iHideInfo   = a[5];
		m_iFill       = a[6];
		return true;
	}

	// Канал із meta (для GM Side: сервер бере обрану зевсом сторону звідси). -1 якщо некоректно.
	static int ChannelFromMeta(array<int> a)
	{
		if (!a || a.Count() < META_COUNT)
			return -1;
		return a[3];
	}

	// Прочитати лише видимість із meta (щоб сервер перевірив дозвіл каналу). -1 якщо некоректно.
	static int VisibilityFromMeta(array<int> a)
	{
		if (!a || a.Count() < META_COUNT)
			return -1;
		return a[2];
	}

	// --- AABB ---
	void RecomputeAABB()
	{
		int n = GetPointCount();
		if (n < 1)
		{
			m_bAABBValid = false;
			return;
		}
		m_iMinX = m_aPoints[0]; m_iMaxX = m_iMinX;
		m_iMinZ = m_aPoints[1]; m_iMaxZ = m_iMinZ;
		for (int i = 1; i < n; i++)
		{
			int x = m_aPoints[i * 2];
			int z = m_aPoints[i * 2 + 1];
			if (x < m_iMinX) m_iMinX = x;
			else if (x > m_iMaxX) m_iMaxX = x;
			if (z < m_iMinZ) m_iMinZ = z;
			else if (z > m_iMaxZ) m_iMaxZ = z;
		}
		m_bAABBValid = true;
	}

	// Копія світового AABB (для кешу рендеру → куллінг без повторного FindById щокадру).
	bool GetAABB(out int minX, out int maxX, out int minZ, out int maxZ)
	{
		if (!m_bAABBValid)
			return false;
		minX = m_iMinX; maxX = m_iMaxX;
		minZ = m_iMinZ; maxZ = m_iMaxZ;
		return true;
	}

	bool AABBOverlapsRect(int rMinX, int rMaxX, int rMinZ, int rMaxZ, int slack = 0)
	{
		if (!m_bAABBValid)
			return true;
		if (m_iMaxX + slack < rMinX) return false;
		if (m_iMinX - slack > rMaxX) return false;
		if (m_iMaxZ + slack < rMinZ) return false;
		if (m_iMinZ - slack > rMaxZ) return false;
		return true;
	}

	// --- JSON ---
	bool SerializeTo(JsonSaveContext context)
	{
		if (!context)
			return false;
		context.WriteValue("id", m_iId);
		context.WriteValue("owner", m_iOwnerId);
		context.WriteValue("color", m_iColor);
		context.WriteValue("w", m_iWidthIdx);
		context.WriteValue("vis", m_iVisibility);
		context.WriteValue("ch", m_iChannel);
		context.WriteValue("gmlock", m_iGmLocked);
		context.WriteValue("hideinfo", m_iHideInfo);
		context.WriteValue("fill", m_iFill);
		context.WriteValue("author", m_sOwnerName);
		context.WriteValue("pts", m_aPoints);
		return true;
	}

	bool DeserializeFrom(JsonLoadContext context)
	{
		if (!context)
			return false;
		context.ReadValue("id", m_iId);
		context.ReadValue("owner", m_iOwnerId);
		context.ReadValue("color", m_iColor);
		context.ReadValue("w", m_iWidthIdx);
		context.ReadValue("vis", m_iVisibility);
		context.ReadValue("ch", m_iChannel);
		context.ReadValue("gmlock", m_iGmLocked);
		context.ReadValue("hideinfo", m_iHideInfo);
		context.ReadValue("fill", m_iFill);	// старі сейви без поля → лишається 0 (звичайний штрих)
		context.ReadValue("author", m_sOwnerName);
		context.ReadValue("pts", m_aPoints);
		RecomputeAABB();
		return true;
	}

	bool IsValid()
	{
		int n = GetPointCount();
		if (n < 1)			// щонайменше 1 точка (1 = крапка)
			return false;
		if (m_iFill != 0 && n < 3)	// заливка = полігон, мінімум 3 вершини
			return false;
		// груба перевірка діапазону координат
		for (int i = 0; i < m_aPoints.Count(); i++)
		{
			if (m_aPoints[i] < -500000 || m_aPoints[i] > 500000)
				return false;
		}
		return true;
	}

	string ToDebugString()
	{
		return string.Format("SM_Drawing[id:%1 owner:%2 pts:%3 vis:%4 w:%5]",
			m_iId, m_iOwnerId, GetPointCount(), m_iVisibility, m_iWidthIdx);
	}
}
