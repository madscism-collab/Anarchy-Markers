// === Фаза 1: рендер наших міток у мапі Game Master (Zeus) — СУМІСНИЙ підхід ===
//
// GM-мапа відкривається тим самим SCR_MapEntity, але з конфігом режиму EDITOR
// (ванільний {19C76194B21EC3E1}Configs/Map/MapEditor.conf), у якому нема SCR_MapMarkersUI,
// тому наш шар у GM не інстанціюється.
//
// Ми НЕ замінюємо редакторський конфіг (це конфліктувало б з іншими модами, які теж його міняють).
// Натомість перехоплюємо SetupMapConfig і ДОТОЧУЄМО наш компонент у вже зібраний конфіг.
// Так ваніль і будь-які інші моди лишаються недоторканими; кілька модів можуть так само
// доточувати своє в той самий список. Жодного кроку у Workbench не потрібно.
//
// Працює, бо SCR_MapEntity.SetupMapConfig повертає MapConfiguration з public-полем Components
// (array<ref SCR_MapUIBaseComponent>), а наш modded SCR_MapMarkersUI самодостатній: усі потрібні
// layout-атрибути ми задаємо в коді (Init), тож 'new SCR_MapMarkersUI()' не залежить від конфігу.
modded class SCR_MapEntity
{
	override MapConfiguration SetupMapConfig(EMapEntityMode mapMode, ResourceName configPath, Widget rootWidget)
	{
		MapConfiguration cfg = super.SetupMapConfig(mapMode, configPath, rootWidget);

		if (cfg && mapMode == EMapEntityMode.EDITOR && cfg.Components)
		{
			// Не дублювати: SetupMapConfig кешує конфіг режиму (повторне відкриття вертає той самий).
			bool present = false;
			foreach (SCR_MapUIBaseComponent c : cfg.Components)
			{
				if (SCR_MapMarkersUI.Cast(c))
				{
					present = true;
					break;
				}
			}
			if (!present)
				cfg.Components.Insert(new SCR_MapMarkersUI());	// наш modded-клас (SM_MapMarkerLayer)
		}

		return cfg;
	}
}
