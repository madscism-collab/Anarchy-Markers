// Сховище міток — головне.
//   На сервері це авторитетне сховище: воно призначає унікальні id (методи Server*).
//   На клієнті це дзеркало серверного стану (методи Apply*, які кличе RPC).
// чиста логіка, без RPC і без ванільних класів. Мережа й рендер працюють зі сховищем
// тільки через ці методи та інвокери. Уся дедуплікація — по m_iId, тож застарілий чи
// повторний RPC дубля не створить.

// Інвокер: мітку додано або змінено
void SM_MarkerChangeInvoker(SM_MapMarkerData data);
typedef func SM_MarkerChangeInvoker;

// Інвокер: мітку видалено (по id)
void SM_MarkerRemoveInvoker(int markerId);
typedef func SM_MarkerRemoveInvoker;

class SM_MapMarkerStore
{
	protected static ref SM_MapMarkerStore s_Instance;

	protected ref array<ref SM_MapMarkerData> m_aMarkers = {};
	protected int m_iNextId = 1;	// серверний лічильник id (завжди >= 1)
	protected int m_iNextLocalId = -2;	// client-side id counter for Local markers (-2 and below; -1 stays the "unassigned" sentinel)

	// На ці інвокери підписується рендер (адаптер мапи)
	protected ref ScriptInvokerBase<SM_MarkerChangeInvoker> m_OnMarkerAdded   = new ScriptInvokerBase<SM_MarkerChangeInvoker>();
	protected ref ScriptInvokerBase<SM_MarkerChangeInvoker> m_OnMarkerChanged = new ScriptInvokerBase<SM_MarkerChangeInvoker>();
	protected ref ScriptInvokerBase<SM_MarkerRemoveInvoker> m_OnMarkerRemoved = new ScriptInvokerBase<SM_MarkerRemoveInvoker>();

	static SM_MapMarkerStore GetInstance()
	{
		if (!s_Instance)
			s_Instance = new SM_MapMarkerStore();
		return s_Instance;
	}

	ScriptInvokerBase<SM_MarkerChangeInvoker> GetOnMarkerAdded()   { return m_OnMarkerAdded; }
	ScriptInvokerBase<SM_MarkerChangeInvoker> GetOnMarkerChanged() { return m_OnMarkerChanged; }
	ScriptInvokerBase<SM_MarkerRemoveInvoker> GetOnMarkerRemoved() { return m_OnMarkerRemoved; }

	// Client-side Local record? (id <= -2). Server ids are >= 1, -1 = "unassigned" sentinel.
	static bool IsLocalId(int id)
	{
		return id <= -2;
	}

	SM_MapMarkerData FindById(int id)
	{
		if (id == -1)	// sentinel only — real markers have id >= 1 (server) or <= -2 (Local)
			return null;

		foreach (SM_MapMarkerData m : m_aMarkers)
		{
			if (m && m.m_iId == id)
				return m;
		}
		return null;
	}

	// Усі мітки (для збереження й первинної побудови UI). Повертає посилання — не міняти всередині.
	void GetAll(out array<SM_MapMarkerData> outMarkers)
	{
		if (!outMarkers)
			outMarkers = {};
		outMarkers.Clear();
		foreach (SM_MapMarkerData m : m_aMarkers)
		{
			if (m)
				outMarkers.Insert(m);
		}
	}

	int Count()
	{
		return m_aMarkers.Count();
	}

	// Скільки міток належить конкретному гравцю (для ліміту на гравця).
	int CountByOwner(int ownerId)
	{
		int n = 0;
		foreach (SM_MapMarkerData m : m_aMarkers)
		{
			if (m && m.m_iOwnerId == ownerId)
				n++;
		}
		return n;
	}

	// --- Серверні (авторитетні) операції ---

	// Створити нову мітку: призначити id і додати у сховище. Повертає мітку або null.
	SM_MapMarkerData ServerCreate(int ownerId, array<int> packed, string text)
	{
		SM_MapMarkerData data = new SM_MapMarkerData();
		if (!data.UnpackInts(packed))
			return null;

		data.m_iOwnerId = ownerId;
		data.m_iLastEditorId = ownerId;	// на момент створення автор і є останнім редактором
		data.m_sText = text;
		data.m_iId = m_iNextId;
		m_iNextId++;

		m_aMarkers.Insert(data);
		m_OnMarkerAdded.Invoke(data);	// одразу повідомляємо локальний рендер (важливо для listen-host/SP, де RPC до себе не ходить)
		return data;
	}

	// Посунути мітку. true, якщо знайшли й змінили.
	bool ServerMove(int id, int posX, int posY)
	{
		SM_MapMarkerData m = FindById(id);
		if (!m)
			return false;

		m.m_iPosX = posX;
		m.m_iPosY = posY;
		m_OnMarkerChanged.Invoke(m);
		return true;
	}

	// Оновити редаговані поля. id і owner лишаємо як були.
	bool ServerUpdate(int id, array<int> packed, string text)
	{
		SM_MapMarkerData m = FindById(id);
		if (!m)
			return false;

		int keepId = m.m_iId;
		int keepOwner = m.m_iOwnerId;
		if (!m.UnpackInts(packed))
			return false;
		m.m_iId = keepId;
		m.m_iOwnerId = keepOwner;
		m.m_sText = text;
		m_OnMarkerChanged.Invoke(m);
		return true;
	}

	// Видалити по id. true, якщо знайшли.
	bool ServerRemove(int id)
	{
		for (int i = m_aMarkers.Count() - 1; i >= 0; i--)
		{
			if (m_aMarkers[i] && m_aMarkers[i].m_iId == id)
			{
				m_aMarkers.Remove(i);
				m_OnMarkerRemoved.Invoke(id);
				return true;
			}
		}
		return false;
	}

	// Повне очищення без інвокерів — кличемо перед завантаженням з файлу.
	// Сховище це статичний singleton і переживає Workbench Reload, тож без цього мітки
	// накопичувались би з кожним перезапуском (дублі id, роздуте збереження).
	void ServerClear()
	{
		// Only SERVER markers (id >= 1). Local ones (id <= -2, listen-host only) belong to
		// SM_LocalMarkerPersistence — leave them alone here, otherwise the host would keep
		// ghost visuals (ServerClear doesn't fire invokers).
		for (int i = m_aMarkers.Count() - 1; i >= 0; i--)
		{
			if (m_aMarkers[i] && !IsLocalId(m_aMarkers[i].m_iId))
				m_aMarkers.Remove(i);
		}
		m_iNextId = 1;
	}

	// Додати готову мітку із збереження. Дедуп по id: якщо такий вже є — пропускаємо.
	void ServerLoad(notnull SM_MapMarkerData data)
	{
		if (FindById(data.m_iId))
			return;

		m_aMarkers.Insert(data);
		if (data.m_iId >= m_iNextId)
			m_iNextId = data.m_iId + 1;
	}

	// --- Клієнтське дзеркало (з RPC). Усе по id, дублі неможливі. ---

	// Додати або замінити мітку по id. Фаєрить Added (нова) або Changed (вже була).
	void ApplyUpsert(notnull SM_MapMarkerData data)
	{
		SM_MapMarkerData existing = FindById(data.m_iId);
		if (existing)
		{
			existing.CopyFrom(data);
			m_OnMarkerChanged.Invoke(existing);
		}
		else
		{
			m_aMarkers.Insert(data);
			m_OnMarkerAdded.Invoke(data);
		}
	}

	void ApplyMove(int id, int posX, int posY)
	{
		SM_MapMarkerData m = FindById(id);
		if (!m)
			return;

		m.m_iPosX = posX;
		m.m_iPosY = posY;
		m_OnMarkerChanged.Invoke(m);
	}

	void ApplyRemove(int id)
	{
		for (int i = m_aMarkers.Count() - 1; i >= 0; i--)
		{
			if (m_aMarkers[i] && m_aMarkers[i].m_iId == id)
			{
				m_aMarkers.Remove(i);
				m_OnMarkerRemoved.Invoke(id);
				return;
			}
		}
	}

	// --- Client-side Local markers (the Local channel): they exist only on this client and
	// never reach the server. Persisted in the client file (SM_LocalMarkerPersistence), not
	// the server JSON. Negative ids (<= -2) can't collide with server ids (>= 1) anywhere. ---

	// Add a Local marker: assign a negative id, insert, notify the renderer (same Added invoker).
	SM_MapMarkerData LocalCreate(notnull SM_MapMarkerData data)
	{
		data.m_iId = m_iNextLocalId;
		m_iNextLocalId--;
		m_aMarkers.Insert(data);
		m_OnMarkerAdded.Invoke(data);
		return data;
	}

	// Remove ALL Local markers (id <= -2), notifying the renderer. Called on mission start —
	// the store is a static singleton and survives Workbench Reload, so without this the old
	// session's Local visuals would linger/duplicate.
	void ClearLocals()
	{
		for (int i = m_aMarkers.Count() - 1; i >= 0; i--)
		{
			if (m_aMarkers[i] && IsLocalId(m_aMarkers[i].m_iId))
			{
				int id = m_aMarkers[i].m_iId;
				m_aMarkers.Remove(i);
				m_OnMarkerRemoved.Invoke(id);
			}
		}
		m_iNextLocalId = -2;
	}

	// Update the editable fields of an existing Local marker (stays Local). id/owner untouched.
	bool LocalUpdate(int id, notnull SM_MapMarkerData src)
	{
		SM_MapMarkerData m = FindById(id);
		if (!m)
			return false;
		int keepId = m.m_iId;
		m.CopyFrom(src);
		m.m_iId = keepId;
		m_OnMarkerChanged.Invoke(m);
		return true;
	}

	// Повне очищення зі сповіщенням рендеру по кожній мітці (зміна місії / новий сценарій).
	void Clear()
	{
		for (int i = m_aMarkers.Count() - 1; i >= 0; i--)
		{
			if (m_aMarkers[i])
				m_OnMarkerRemoved.Invoke(m_aMarkers[i].m_iId);
		}
		m_aMarkers.Clear();
	}
}
