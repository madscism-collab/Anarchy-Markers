// Client-LOCAL preferences (per player, saved in the profile — NOT server config, NOT replicated).
// Uses the game's own user-settings storage, the same mechanism vanilla gameplay options use, so the
// value persists across sessions and shows up in the settings menu (see the modded gameplay submenu
// and the GameplaySettings.layout row).
//
// So far it holds one thing: enhanced fill. OFF by default — the simple fill keeps every correctness
// fix (edge snapping, wedge corners, narrow channels) but a lower point budget, so its triangulation
// is cheap and there is no placement hitch. ON spends the points for maximum curve detail, at the cost
// of a heavier one-off triangulation when a big, busy area is filled.
//
// Stored as an int (0/1) rather than a bool so the menu row can be a No/Yes SpinBox, which is the
// binding the SCR_SettingBindingGameplay path is proven to drive.

class SM_ClientSettings : ModuleGameSettings
{
	[Attribute(defvalue: "0", uiwidget: UIWidgets.SpinBox, params: "0 1", desc: "Enhanced (high-detail) map fill. No = simpler, faster fill with no placement hitch.")]
	int m_iEnhancedFill;
}

class SM_ClientPrefs
{
	//! Read live: a fill is a deliberate click, not a per-frame path, so touching the settings
	//! container each time is free and avoids any cache-invalidation dance when the menu changes it.
	static bool EnhancedFill()
	{
		BaseContainer c = GetGame().GetGameUserSettings().GetModule("SM_ClientSettings");
		if (!c)
			return false;
		int v = 0;
		c.Get("m_iEnhancedFill", v);
		return v != 0;
	}
}
