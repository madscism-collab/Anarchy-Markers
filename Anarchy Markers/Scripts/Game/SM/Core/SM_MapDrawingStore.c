// Сховище малюнків — окреме від сховища міток, але та сама модель:
//   На сервері — авторитетне (призначає id, методи Server*).
//   На клієнті — дзеркало серверного стану (методи Apply*, які кличе RPC).
// Штрих незмінний після створення (Ф1): його або додають, або видаляють цілком —
// тож інвокерів лише два (Added/Removed), без Changed. Уся дедуплікація — по m_iId.

void SM_DrawingChangeInvoker(SM_MapDrawingData data);
typedef func SM_DrawingChangeInvoker;

void SM_DrawingRemoveInvoker(int drawingId);
typedef func SM_DrawingRemoveInvoker;

class SM_MapDrawingStore
{
	protected static ref SM_MapDrawingStore s_Instance;

	protected ref array<ref SM_MapDrawingData> m_aDrawings = {};
	protected int m_iNextId = 1;	// серверний лічильник id (завжди >= 1)
	protected int m_iNextLocalId = -2;	// client-side id counter for Local drawings (-2 and below; -1 stays the sentinel)

	protected ref ScriptInvokerBase<SM_DrawingChangeInvoker> m_OnAdded   = new ScriptInvokerBase<SM_DrawingChangeInvoker>();
	protected ref ScriptInvokerBase<SM_DrawingRemoveInvoker> m_OnRemoved = new ScriptInvokerBase<SM_DrawingRemoveInvoker>();

	static SM_MapDrawingStore GetInstance()
	{
		if (!s_Instance)
			s_Instance = new SM_MapDrawingStore();
		return s_Instance;
	}

	ScriptInvokerBase<SM_DrawingChangeInvoker> GetOnAdded()   { return m_OnAdded; }
	ScriptInvokerBase<SM_DrawingRemoveInvoker> GetOnRemoved() { return m_OnRemoved; }

	// Client-side Local stroke? (id <= -2). Server ids are >= 1, -1 = "unassigned" sentinel.
	static bool IsLocalId(int id)
	{
		return id <= -2;
	}

	SM_MapDrawingData FindById(int id)
	{
		if (id == -1)	// "unassigned" sentinel
			return null;
		foreach (SM_MapDrawingData d : m_aDrawings)
		{
			if (d && d.m_iId == id)
				return d;
		}
		return null;
	}

	void GetAll(out array<SM_MapDrawingData> outDrawings)
	{
		if (!outDrawings)
			outDrawings = {};
		outDrawings.Clear();
		foreach (SM_MapDrawingData d : m_aDrawings)
		{
			if (d)
				outDrawings.Insert(d);
		}
	}

	int Count() { return m_aDrawings.Count(); }

	int CountByOwner(int ownerId)
	{
		int n = 0;
		foreach (SM_MapDrawingData d : m_aDrawings)
		{
			if (d && d.m_iOwnerId == ownerId)
				n++;
		}
		return n;
	}

	//! Drawing budget SPENT: a freehand stroke is 1, a shape is its line cost (SM_ShapeGeometry).
	//! This is what the per-player / total limits weigh against, so a circle can't cost one slot and
	//! cover the map.
	int CostByOwner(int ownerId)
	{
		int c = 0;
		foreach (SM_MapDrawingData d : m_aDrawings)
		{
			if (d && d.m_iOwnerId == ownerId)
				c += RecordCost(d);
		}
		return c;
	}

	int TotalCost()
	{
		int c = 0;
		foreach (SM_MapDrawingData d : m_aDrawings)
		{
			if (d)
				c += RecordCost(d);
		}
		return c;
	}

	int CountGridsByOwner(int ownerId)
	{
		int n = 0;
		foreach (SM_MapDrawingData d : m_aDrawings)
		{
			if (d && d.m_iOwnerId == ownerId && SM_ShapeGeometry.IsGrid(d.m_iShape))
				n++;
		}
		return n;
	}

	static int RecordCost(notnull SM_MapDrawingData d)
	{
		if (d.m_iShape == 0)
			return 1;
		return SM_ShapeGeometry.StrokeCost(d.m_iShape, d.m_aPoints);
	}

	// --- Серверні (авторитетні) операції ---

	//! Створити штрих: призначити id, додати, повідомити локальний рендер.
	SM_MapDrawingData ServerCreate(int ownerId, array<int> meta, array<int> points, int channel, int createdMs, string ownerName = "")
	{
		SM_MapDrawingData d = new SM_MapDrawingData();
		if (!d.UnpackMeta(meta))
			return null;
		d.SetPoints(points);
		d.ServerSanitise();	// meta came off the wire — no field in it is trustworthy.
							// AFTER SetPoints: the shape checks need the points (count, grid snapping).
		d.m_iOwnerId   = ownerId;
		d.m_iChannel   = channel;	// канал ставить сервер (фракція/група), не клієнт
		d.m_iCreatedMs = createdMs;
		d.m_sOwnerName = ownerName;	// ім'я ставить сервер (переживає рестарти, на відміну від playerId)
		if (!d.IsValid())
			return null;
		d.m_iId = m_iNextId;
		m_iNextId++;

		m_aDrawings.Insert(d);
		m_OnAdded.Invoke(d);	// listen-host/SP: RPC до себе не ходить — повідомляємо одразу
		return d;
	}

	bool ServerRemove(int id)
	{
		for (int i = m_aDrawings.Count() - 1; i >= 0; i--)
		{
			if (m_aDrawings[i] && m_aDrawings[i].m_iId == id)
			{
				m_aDrawings.Remove(i);
				m_OnRemoved.Invoke(id);
				return true;
			}
		}
		return false;
	}

	//! Повне очищення без інвокерів — перед завантаженням з файлу (сховище — статичний
	//! singleton, переживає Workbench Reload; без цього штрихи накопичувались би).
	void ServerClear()
	{
		// Only SERVER strokes (id >= 1). Local ones (id <= -2, listen-host only) are client-side —
		// leave them alone (ServerClear doesn't fire invokers, the host would keep ghost visuals).
		for (int i = m_aDrawings.Count() - 1; i >= 0; i--)
		{
			if (m_aDrawings[i] && !IsLocalId(m_aDrawings[i].m_iId))
				m_aDrawings.Remove(i);
		}
		m_iNextId = 1;
	}

	//! Додати готовий штрих із збереження. Дедуп по id.
	void ServerLoad(notnull SM_MapDrawingData data)
	{
		if (FindById(data.m_iId))
			return;
		m_aDrawings.Insert(data);
		if (data.m_iId >= m_iNextId)
			m_iNextId = data.m_iId + 1;
	}

	// --- Клієнтське дзеркало (з RPC) ---

	void ApplyAdd(notnull SM_MapDrawingData data)
	{
		if (FindById(data.m_iId))
			return;	// дедуп
		m_aDrawings.Insert(data);
		m_OnAdded.Invoke(data);
	}

	void ApplyRemove(int id)
	{
		for (int i = m_aDrawings.Count() - 1; i >= 0; i--)
		{
			if (m_aDrawings[i] && m_aDrawings[i].m_iId == id)
			{
				m_aDrawings.Remove(i);
				m_OnRemoved.Invoke(id);
				return;
			}
		}
	}

	// --- Client-side Local drawings (the Local channel): exist only on this client, never reach
	// the server. Persisted in the client file (SM_LocalDrawingPersistence). Negative ids (<= -2)
	// can't collide with server ids (>= 1). Strokes stay immutable — add/remove only. ---

	//! Add a Local stroke: assign a negative id, insert, notify the renderer (same Added invoker).
	SM_MapDrawingData LocalCreate(notnull SM_MapDrawingData data)
	{
		if (!data.IsValid())
			return null;
		data.m_iId = m_iNextLocalId;
		m_iNextLocalId--;
		m_aDrawings.Insert(data);
		m_OnAdded.Invoke(data);
		return data;
	}

	//! Remove ALL Local strokes (id <= -2), notifying the renderer. Called on mission start —
	//! the static singleton survives Workbench Reload and would otherwise keep stale visuals.
	void ClearLocals()
	{
		for (int i = m_aDrawings.Count() - 1; i >= 0; i--)
		{
			if (m_aDrawings[i] && IsLocalId(m_aDrawings[i].m_iId))
			{
				int id = m_aDrawings[i].m_iId;
				m_aDrawings.Remove(i);
				m_OnRemoved.Invoke(id);
			}
		}
		m_iNextLocalId = -2;
	}

	//! Повне очищення зі сповіщенням рендеру по кожному штриху (зміна місії / новий сценарій).
	void Clear()
	{
		for (int i = m_aDrawings.Count() - 1; i >= 0; i--)
		{
			if (m_aDrawings[i])
				m_OnRemoved.Invoke(m_aDrawings[i].m_iId);
		}
		m_aDrawings.Clear();
	}
}
