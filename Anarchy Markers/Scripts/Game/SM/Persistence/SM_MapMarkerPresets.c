// Пресети (шаблони) міток. Зберігаються локально в профілі гравця і не синхронізуються —
// це особисте. Пресет це по суті SM_MapMarkerData без позиції.
// Дві категорії:
//   GENERAL — запам'ятовують усі параметри (тип, іконка/символ, колір, розмір, текст, поворот,
//             видимість, галочка тексту, час).
//   MILITARY — тільки військові параметри (identity/dimension/symbolFlags/колір). Перші кілька
//              штук вбудовані: спільні, не видаляються і у файл не пишуться.
class SM_MapMarkerPresets
{
	protected static ref SM_MapMarkerPresets s_Instance;

	protected ref array<ref SM_MapMarkerData> m_aGeneral  = {};
	protected ref array<ref SM_MapMarkerData> m_aMilitary = {};
	protected int  m_iBuiltinMilCount = 0;	// скільки перших військових пресетів вбудовані
	protected bool m_bLoaded = false;

	protected const string DIR  = "$profile:SavingMarkers";
	protected const string FILE = "$profile:SavingMarkers/SM_Presets.json";

	static SM_MapMarkerPresets GetInstance()
	{
		if (!s_Instance)
		{
			s_Instance = new SM_MapMarkerPresets();
			s_Instance.Load();
		}
		return s_Instance;
	}

	array<ref SM_MapMarkerData> GetGeneral()  { return m_aGeneral; }
	array<ref SM_MapMarkerData> GetMilitary() { return m_aMilitary; }
	bool IsBuiltinMilitary(int idx)           { return idx < m_iBuiltinMilCount; }

	// Додати загальний пресет — копія всіх параметрів поточної мітки.
	void AddGeneral(notnull SM_MapMarkerData src)
	{
		m_aGeneral.Insert(src.SM_Clone());
		Save();
	}

	// Додати військовий пресет (тип примусово MILITARY).
	void AddMilitary(notnull SM_MapMarkerData src)
	{
		SM_MapMarkerData c = src.SM_Clone();
		c.m_iKind = SM_EMarkerKind.MILITARY;
		m_aMilitary.Insert(c);
		Save();
	}

	void RemoveGeneral(int idx)
	{
		if (idx < 0 || idx >= m_aGeneral.Count())
			return;
		m_aGeneral.Remove(idx);
		Save();
	}

	// Вбудовані військові пресети видалити не можна.
	void RemoveMilitary(int idx)
	{
		if (idx < m_iBuiltinMilCount || idx >= m_aMilitary.Count())
			return;
		m_aMilitary.Remove(idx);
		Save();
	}

	// 6 спільних вбудованих: 3 ворожі (червоні) + 3 дружні (сині).
	// Кожна трійка: піхота, бронетехніка, укріплення (рамка INSTALLATION).
	protected void AddBuiltinMilitary()
	{
		// Ворожі (OPFOR, червоний)
		AddBuiltin(EMilitarySymbolIdentity.OPFOR,  EMilitarySymbolDimension.LAND,         EMilitarySymbolIcon.INFANTRY, 0xFFD83A3A);
		AddBuiltin(EMilitarySymbolIdentity.OPFOR,  EMilitarySymbolDimension.LAND,         EMilitarySymbolIcon.ARMOR,    0xFFD83A3A);
		AddBuiltin(EMilitarySymbolIdentity.OPFOR,  EMilitarySymbolDimension.INSTALLATION, 0,                            0xFFD83A3A);
		// Дружні (BLUFOR, синій)
		AddBuiltin(EMilitarySymbolIdentity.BLUFOR, EMilitarySymbolDimension.LAND,         EMilitarySymbolIcon.INFANTRY, 0xFF2E6FE6);
		AddBuiltin(EMilitarySymbolIdentity.BLUFOR, EMilitarySymbolDimension.LAND,         EMilitarySymbolIcon.ARMOR,    0xFF2E6FE6);
		AddBuiltin(EMilitarySymbolIdentity.BLUFOR, EMilitarySymbolDimension.INSTALLATION, 0,                            0xFF2E6FE6);
		m_iBuiltinMilCount = m_aMilitary.Count();
	}

	protected void AddBuiltin(int identity, int dimension, int iconFlag, int color)
	{
		SM_MapMarkerData d = new SM_MapMarkerData();
		d.m_iKind        = SM_EMarkerKind.MILITARY;
		d.m_iIdentity    = identity;
		d.m_iDimension   = dimension;
		d.m_iSymbolFlags = iconFlag;
		d.m_iColor       = color;
		m_aMilitary.Insert(d);
	}

	void Load()
	{
		if (m_bLoaded)
			return;

		m_aGeneral.Clear();
		m_aMilitary.Clear();
		AddBuiltinMilitary();	// спершу вбудовані військові

		JsonLoadContext ctx = new JsonLoadContext();
		if (ctx.LoadFromFile(FILE))
		{
			int gc = 0;
			ctx.ReadValue("gcount", gc);
			for (int i = 0; i < gc; i++)
			{
				if (!ctx.StartObject(string.Format("g_%1", i)))
					continue;
				SM_MapMarkerData d = new SM_MapMarkerData();
				if (d.DeserializeFrom(ctx))
					m_aGeneral.Insert(d);
				ctx.EndObject();
			}

			int mc = 0;
			ctx.ReadValue("mucount", mc);
			for (int i = 0; i < mc; i++)
			{
				if (!ctx.StartObject(string.Format("mu_%1", i)))
					continue;
				SM_MapMarkerData d = new SM_MapMarkerData();
				if (d.DeserializeFrom(ctx))
				{
					d.m_iKind = SM_EMarkerKind.MILITARY;
					m_aMilitary.Insert(d);
				}
				ctx.EndObject();
			}
		}

		m_bLoaded = true;
	}

	// Пишемо тільки користувацькі пресети (загальні + військові поза вбудованими).
	void Save()
	{
		FileIO.MakeDirectory(DIR);

		JsonSaveContext ctx = new JsonSaveContext();
		ctx.WriteValue("version", 1);

		ctx.WriteValue("gcount", m_aGeneral.Count());
		for (int i = 0; i < m_aGeneral.Count(); i++)
		{
			if (!m_aGeneral[i])
				continue;
			ctx.StartObject(string.Format("g_%1", i));
			m_aGeneral[i].SerializeTo(ctx);
			ctx.EndObject();
		}

		int userMil = m_aMilitary.Count() - m_iBuiltinMilCount;
		if (userMil < 0)
			userMil = 0;
		ctx.WriteValue("mucount", userMil);
		int w = 0;
		for (int i = m_iBuiltinMilCount; i < m_aMilitary.Count(); i++)
		{
			if (!m_aMilitary[i])
				continue;
			ctx.StartObject(string.Format("mu_%1", w));
			m_aMilitary[i].SerializeTo(ctx);
			ctx.EndObject();
			w++;
		}

		ctx.SaveToFile(FILE);
	}
}
