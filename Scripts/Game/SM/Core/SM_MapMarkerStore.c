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
	protected int m_iNextId = 1;	// серверний лічильник id

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

	SM_MapMarkerData FindById(int id)
	{
		if (id < 0)
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
		m_aMarkers.Clear();
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
