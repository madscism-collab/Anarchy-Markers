// Публічний API мода "Anarchy Markers" для інших модів. Єдина стабільна точка входу,
// в наші внутрішні класи (SM_MapMarkerStore, SM_MarkerNet тощо) напряму лізти не треба.
//
// Підключення: додайте "Anarchy Markers" у Dependencies свого addon.gproj
// і кличте AM_MarkerAPI.* напряму.
//
// Читання/події працюють на будь-якій стороні: сервер бачить усе, клієнт — лише те,
// що йому дозволено за каналом/фракцією. Request*-методи самі розгалужуються:
// на клієнті йдуть RPC-шляхом (сервер призначає id/власника/канал і застосовує
// ліміти та права), на сервері виконуються одразу. Server*-методи — тільки для
// серверного коду, повертають створений об'єкт.
//
// Координати — світові метри, у vector беремо [0]=X і [2]=Z (як GetOrigin сутностей).
//
// Приклад підписки на події:
//     AM_MarkerAPI.GetOnMarkerAdded().Insert(OnMarkerAdded);
//     void OnMarkerAdded(SM_MapMarkerData data) { ... }

class AM_MarkerAPI
{
	//! true на авторитетній стороні (виділений сервер / listen-host / SP-редактор).
	static bool IsServer()
	{
		return Replication.IsServer();
	}

	//! Локальний PlayerController (для клієнтського RPC-шляху). null на dedicated-сервері.
	protected static SCR_PlayerController LocalPC()
	{
		return SCR_PlayerController.Cast(GetGame().GetPlayerController());
	}

	// --- Маркери: читання ---

	//! Скільки маркерів у сховищі на цій стороні.
	static int GetMarkerCount()
	{
		return SM_MapMarkerStore.GetInstance().Count();
	}

	//! Усі маркери, видимі на ЦІЙ стороні. Повертає посилання на «живі» об'єкти —
	//! тільки ЧИТАЙТЕ їх (зміни — лише через Request*/Server*). Потрібна копія? data.SM_Clone().
	static void GetAllMarkers(out array<SM_MapMarkerData> outMarkers)
	{
		SM_MapMarkerStore.GetInstance().GetAll(outMarkers);
	}

	//! Маркер за серверним id або null.
	static SM_MapMarkerData GetMarkerById(int id)
	{
		return SM_MapMarkerStore.GetInstance().FindById(id);
	}

	//! Маркери конкретного гравця (playerId власника).
	static void GetMarkersByOwner(int playerId, out array<SM_MapMarkerData> outMarkers)
	{
		if (!outMarkers)
			outMarkers = {};
		outMarkers.Clear();

		array<SM_MapMarkerData> all = {};
		SM_MapMarkerStore.GetInstance().GetAll(all);
		foreach (SM_MapMarkerData m : all)
		{
			if (m && m.m_iOwnerId == playerId)
				outMarkers.Insert(m);
		}
	}

	//! Маркери в радіусі (метри) від світової точки. centerWorld: [0]=X, [2]=Z.
	static void GetMarkersInRadius(vector centerWorld, float radiusMeters, out array<SM_MapMarkerData> outMarkers)
	{
		if (!outMarkers)
			outMarkers = {};
		outMarkers.Clear();

		float cx = centerWorld[0];
		float cz = centerWorld[2];
		float r2 = radiusMeters * radiusMeters;

		array<SM_MapMarkerData> all = {};
		SM_MapMarkerStore.GetInstance().GetAll(all);
		foreach (SM_MapMarkerData m : all)
		{
			if (!m)
				continue;
			float dx = m.m_iPosX - cx;
			float dz = m.m_iPosY - cz;
			if (dx * dx + dz * dz <= r2)
				outMarkers.Insert(m);
		}
	}

	//! Світова позиція маркера як vector (X, 0, Z).
	static vector GetMarkerWorldPos(notnull SM_MapMarkerData m)
	{
		return Vector(m.m_iPosX, 0, m.m_iPosY);
	}

	// --- Маркери: події. Added/Changed: cb(SM_MapMarkerData), Removed: cb(int markerId) ---

	static ScriptInvokerBase<SM_MarkerChangeInvoker> GetOnMarkerAdded()
	{
		return SM_MapMarkerStore.GetInstance().GetOnMarkerAdded();
	}

	static ScriptInvokerBase<SM_MarkerChangeInvoker> GetOnMarkerChanged()
	{
		return SM_MapMarkerStore.GetInstance().GetOnMarkerChanged();
	}

	static ScriptInvokerBase<SM_MarkerRemoveInvoker> GetOnMarkerRemoved()
	{
		return SM_MapMarkerStore.GetInstance().GetOnMarkerRemoved();
	}

	// --- Маркери: запис ---

	//! Поставити маркер. data зручно збирати через NewCivilianMarker/NewMilitaryMarker.
	//! Результат на клієнті прийде подією OnMarkerAdded; потрібен сам об'єкт — ServerPlaceMarker.
	static bool RequestPlaceMarker(notnull SM_MapMarkerData data)
	{
		if (Replication.IsServer())
			return ServerPlaceMarker(data) != null;

		SCR_PlayerController pc = LocalPC();
		if (!pc)
			return false;
		pc.SM_RequestPlace(data.PackInts(), data.m_sText);
		return true;
	}

	//! Змінити редаговані поля маркера (колір, текст, видимість, вид, розмір…). id і власник — незмінні.
	static bool RequestEditMarker(int id, notnull SM_MapMarkerData data)
	{
		if (Replication.IsServer())
		{
			SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
			if (!store.ServerUpdate(id, data.PackInts(), data.m_sText))
				return false;
			SM_MapMarkerData m = store.FindById(id);
			if (!m)
				return false;
			SM_MarkerNet.AssignChannel(m.m_iOwnerId, m);	// видимість могла змінитись — перерахувати канал
			SM_MarkerNet.BroadcastUpsertOrRemove(m);
			return true;
		}

		SCR_PlayerController pc = LocalPC();
		if (!pc)
			return false;
		pc.SM_RequestEdit(id, data.PackInts(), data.m_sText);
		return true;
	}

	//! Посунути маркер у нову світову точку ([0]=X, [2]=Z).
	static bool RequestMoveMarker(int id, vector world)
	{
		int x = world[0];
		int z = world[2];

		if (Replication.IsServer())
		{
			SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
			if (!store.ServerMove(id, x, z))
				return false;
			SM_MapMarkerData m = store.FindById(id);
			if (m)
				SM_MarkerNet.BroadcastMove(m);
			return true;
		}

		SCR_PlayerController pc = LocalPC();
		if (!pc)
			return false;
		pc.SM_RequestMove(id, x, z);
		return true;
	}

	//! Видалити маркер за id.
	static bool RequestRemoveMarker(int id)
	{
		if (Replication.IsServer())
		{
			if (!SM_MapMarkerStore.GetInstance().ServerRemove(id))
				return false;
			SM_MarkerNet.BroadcastRemove(id);
			return true;
		}

		SCR_PlayerController pc = LocalPC();
		if (!pc)
			return false;
		pc.SM_RequestRemove(id);
		return true;
	}

	//! Тільки сервер: створити маркер і розіслати. Повертає об'єкт із призначеним m_iId або null.
	//! Канал: data.m_iChannel >= 0 береться як є, інакше рахується за власником.
	static SM_MapMarkerData ServerPlaceMarker(notnull SM_MapMarkerData data)
	{
		if (!Replication.IsServer())
			return null;

		SM_MapMarkerStore store = SM_MapMarkerStore.GetInstance();
		SM_MapMarkerData created = store.ServerCreate(data.m_iOwnerId, data.PackInts(), data.m_sText);
		if (!created)
			return null;

		if (data.m_iChannel >= 0)
			created.m_iChannel = data.m_iChannel;
		else
			SM_MarkerNet.AssignChannel(created.m_iOwnerId, created);

		SM_MarkerNet.BroadcastUpsert(created);
		return created;
	}

	//! Тільки сервер: видалити маркер (те саме, що RequestRemoveMarker на сервері).
	static bool ServerRemoveMarker(int id)
	{
		if (!Replication.IsServer())
			return false;
		if (!SM_MapMarkerStore.GetInstance().ServerRemove(id))
			return false;
		SM_MarkerNet.BroadcastRemove(id);
		return true;
	}

	// --- Зручні конструктори ---

	//! Цивільний маркер (іконка з ванільного каталогу PLACED_CUSTOM за iconEntry).
	static SM_MapMarkerData NewCivilianMarker(vector world, int iconEntry, int argb, string text, SM_EMarkerVisibility visibility = SM_EMarkerVisibility.FACTION)
	{
		SM_MapMarkerData d = new SM_MapMarkerData();
		d.m_iPosX       = world[0];
		d.m_iPosY       = world[2];
		d.m_iKind       = SM_EMarkerKind.CIVILIAN;
		d.m_iIconEntry  = iconEntry;
		d.m_iColor      = argb;
		d.m_sText       = text;
		d.m_iVisibility = visibility;
		return d;
	}

	//! Військовий APP-6 маркер. identity=EMilitarySymbolIdentity, dimension=EMilitarySymbolDimension,
	//! symbolFlags=EMilitarySymbolIcon бітмаска (0 = лише рамка).
	static SM_MapMarkerData NewMilitaryMarker(vector world, int identity, int dimension, int symbolFlags, int argb, string text, SM_EMarkerVisibility visibility = SM_EMarkerVisibility.FACTION)
	{
		SM_MapMarkerData d = new SM_MapMarkerData();
		d.m_iPosX        = world[0];
		d.m_iPosY        = world[2];
		d.m_iKind        = SM_EMarkerKind.MILITARY;
		d.m_iIdentity    = identity;
		d.m_iDimension   = dimension;
		d.m_iSymbolFlags = symbolFlags;
		d.m_iColor       = argb;
		d.m_sText        = text;
		d.m_iVisibility  = visibility;
		return d;
	}

	// --- Малюнки (штрихи/заливки): читання ---

	static int GetDrawingCount()
	{
		return SM_MapDrawingStore.GetInstance().Count();
	}

	//! Усі малюнки, видимі на цій стороні. Точки — data.m_aPoints (x,z парами, метри).
	//! Заливка (замкнута зона) — data.m_iFill != 0; звичайна лінія — 0.
	static void GetAllDrawings(out array<SM_MapDrawingData> outDrawings)
	{
		SM_MapDrawingStore.GetInstance().GetAll(outDrawings);
	}

	static SM_MapDrawingData GetDrawingById(int id)
	{
		return SM_MapDrawingStore.GetInstance().FindById(id);
	}

	static void GetDrawingsByOwner(int playerId, out array<SM_MapDrawingData> outDrawings)
	{
		if (!outDrawings)
			outDrawings = {};
		outDrawings.Clear();

		array<SM_MapDrawingData> all = {};
		SM_MapDrawingStore.GetInstance().GetAll(all);
		foreach (SM_MapDrawingData d : all)
		{
			if (d && d.m_iOwnerId == playerId)
				outDrawings.Insert(d);
		}
	}

	//! Лише заливки (замкнуті полігони). Зручно трактувати як «намальовані зони».
	static void GetAllFills(out array<SM_MapDrawingData> outFills)
	{
		if (!outFills)
			outFills = {};
		outFills.Clear();

		array<SM_MapDrawingData> all = {};
		SM_MapDrawingStore.GetInstance().GetAll(all);
		foreach (SM_MapDrawingData d : all)
		{
			if (d && d.m_iFill != 0)
				outFills.Insert(d);
		}
	}

	// --- Малюнки: події. Added: cb(SM_MapDrawingData), Removed: cb(int drawingId).
	//     Штрих незмінний після створення, події Changed немає. ---

	static ScriptInvokerBase<SM_DrawingChangeInvoker> GetOnDrawingAdded()
	{
		return SM_MapDrawingStore.GetInstance().GetOnAdded();
	}

	static ScriptInvokerBase<SM_DrawingRemoveInvoker> GetOnDrawingRemoved()
	{
		return SM_MapDrawingStore.GetInstance().GetOnRemoved();
	}

	// --- Малюнки: запис ---

	//! Додати штрих/заливку. data зручно збирати через NewDrawing нижче.
	static bool RequestAddDrawing(notnull SM_MapDrawingData data)
	{
		if (Replication.IsServer())
			return ServerAddDrawing(data) != null;

		SCR_PlayerController pc = LocalPC();
		if (!pc)
			return false;
		pc.SM_DrawRequestAdd(data.PackMeta(), data.m_aPoints);
		return true;
	}

	//! Видалити штрих/заливку за id.
	static bool RequestRemoveDrawing(int id)
	{
		if (Replication.IsServer())
		{
			if (!SM_MapDrawingStore.GetInstance().ServerRemove(id))
				return false;
			SM_DrawingNet.BroadcastRemove(id);
			return true;
		}

		SCR_PlayerController pc = LocalPC();
		if (!pc)
			return false;
		pc.SM_DrawRequestRemove(id);
		return true;
	}

	//! Тільки сервер: створити штрих/заливку й розіслати. Канал: data.m_iChannel >= 0 береться
	//! як є, інакше за фракцією/групою власника. Повертає створений об'єкт або null.
	static SM_MapDrawingData ServerAddDrawing(notnull SM_MapDrawingData data)
	{
		if (!Replication.IsServer())
			return null;

		int channel = data.m_iChannel;
		if (channel < 0)
			channel = SM_DrawingNet.ChannelFor(data.m_iOwnerId, data.m_iVisibility);

		SM_MapDrawingData d = SM_MapDrawingStore.GetInstance().ServerCreate(
			data.m_iOwnerId, data.PackMeta(), data.m_aPoints, channel, System.GetTickCount(), data.m_sOwnerName);
		if (!d)
			return null;

		SM_DrawingNet.BroadcastAdd(d);
		return d;
	}

	//! Тільки сервер: видалити малюнок.
	static bool ServerRemoveDrawing(int id)
	{
		if (!Replication.IsServer())
			return false;
		if (!SM_MapDrawingStore.GetInstance().ServerRemove(id))
			return false;
		SM_DrawingNet.BroadcastRemove(id);
		return true;
	}

	//! Зібрати штрих (fill=false) або заливку (fill=true) зі світових точок.
	//! widthIdx 0..4 — товщина пензля. Для заливки контур замкнутий, мінімум 3 точки.
	static SM_MapDrawingData NewDrawing(int argb, int widthIdx, bool fill, SM_EMarkerVisibility visibility, notnull array<vector> pointsWorld)
	{
		SM_MapDrawingData d = new SM_MapDrawingData();
		d.m_iColor      = argb;
		d.m_iWidthIdx   = widthIdx;
		d.m_iVisibility = visibility;
		if (fill)
			d.m_iFill = 1;
		else
			d.m_iFill = 0;

		array<int> pts = {};
		foreach (vector p : pointsWorld)
		{
			int px = p[0];
			int pz = p[2];
			pts.Insert(px);
			pts.Insert(pz);
		}
		d.SetPoints(pts);
		return d;
	}
}
