// Тимчасові «вказівники» (показати пальцем по мапі). Повністю окремо від міток:
// БЕЗ збереження, без id-логіки, без реплікації префабів — щоб бути стійким до оновлень.
// Клієнтський хаб: тримає активні вказівники (ключ = playerId власника) і сповіщає рендер.
// Один вказівник на гравця. Авто-приховання за таймаутом (покриває втрачений stop / дисконект / відхід).

class SM_PointerData
{
	int   m_iOwnerId;
	int   m_iPosX;
	int   m_iPosY;
	float m_fLastUpdate;	// час останнього апдейту (для таймауту)
}

void SM_PointerInvoker(int ownerId);
typedef func SM_PointerInvoker;

class SM_PointerHub
{
	protected static ref SM_PointerHub s_Instance;
	protected ref map<int, ref SM_PointerData> m_mPointers = new map<int, ref SM_PointerData>();

	static SM_PointerHub GetInstance()
	{
		if (!s_Instance)
			s_Instance = new SM_PointerHub();
		return s_Instance;
	}

	map<int, ref SM_PointerData> GetAll()
	{
		return m_mPointers;
	}

	// Додати/оновити вказівник власника (з RPC або локально для себе).
	void Show(int ownerId, int x, int y, float now)
	{
		SM_PointerData p = m_mPointers.Get(ownerId);
		if (!p)
		{
			p = new SM_PointerData();
			p.m_iOwnerId = ownerId;
			m_mPointers.Insert(ownerId, p);
		}
		p.m_iPosX = x;
		p.m_iPosY = y;
		p.m_fLastUpdate = now;
	}

	void Hide(int ownerId)
	{
		if (m_mPointers.Contains(ownerId))
			m_mPointers.Remove(ownerId);
	}

	// Прибрати застарілі (без апдейту довше за timeout) — кличе рендер щокадру.
	void PruneStale(float now, float timeout)
	{
		array<int> stale = {};
		foreach (int id, SM_PointerData p : m_mPointers)
		{
			if (!p || now - p.m_fLastUpdate > timeout)
				stale.Insert(id);
		}
		foreach (int id : stale)
			m_mPointers.Remove(id);
	}

	void Clear()
	{
		m_mPointers.Clear();
	}
}
