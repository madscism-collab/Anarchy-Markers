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

	[Attribute(defvalue: "1", uiwidget: UIWidgets.SpinBox, params: "0 1", desc: "Number copied markers: the first copy of 'Rally' becomes 'Rally [2]', the next 'Rally [3]'.")]
	int m_iCopyNumbering;

	// Covers the two simplifications that are VISIBLE as you zoom: the ladder of pre-simplified stroke
	// copies (2..64 m tolerances), and the coarse fill outline used once a shape is small on screen.
	// Both have a discrete step you can watch a shape snap through, and on a big fill the flattened edge
	// reads as a bug rather than an optimisation. Off by default: shape fidelity beats frame time, and
	// the cost only shows up on maps carrying hundreds of drawings. The sub-pixel screen merge stays on
	// unconditionally — that one has no visible step.
	[Attribute(defvalue: "0", uiwidget: UIWidgets.SpinBox, params: "0 1", desc: "Simplify strokes and fills when zoomed out. Faster on maps with hundreds of drawings, but shapes visibly change as you zoom.")]
	int m_iSimplifyDistant;

	// No settings-menu row for this one: the pin button on the panel IS its UI. It lives here only so
	// the choice survives between sessions.
	[Attribute(defvalue: "1", uiwidget: UIWidgets.SpinBox, params: "0 1", desc: "Drawing panel stays pinned on screen instead of auto-hiding.")]
	int m_iPanelPinned;
}

class SM_ClientPrefs
{
	//! Read live: a fill is a deliberate click, not a per-frame path, so touching the settings
	//! container each time is free and avoids any cache-invalidation dance when the menu changes it.
	static bool EnhancedFill()
	{
		return ReadFlag("m_iEnhancedFill", false);
	}

	//! Copies of a marker get a [2], [3], ... suffix. On by default.
	static bool CopyNumbering()
	{
		return ReadFlag("m_iCopyNumbering", true);
	}

	//! Walk pre-simplified copies of a stroke when zoomed out. Off by default — see the attribute.
	static bool SimplifyDistant()
	{
		return ReadFlag("m_iSimplifyDistant", false);
	}

	//! Drawing panel pinned (always on screen) vs auto-hiding. Toggled by the panel's pin button.
	static bool PanelPinned()
	{
		return ReadFlag("m_iPanelPinned", true);
	}

	static void SetPanelPinned(bool pinned)
	{
		BaseContainer c = GetGame().GetGameUserSettings().GetModule("SM_ClientSettings");
		if (!c)
			return;
		int v = 0;
		if (pinned)
			v = 1;
		c.Set("m_iPanelPinned", v);
		GetGame().SaveUserSettings();	// the settings menu saves on tab close; we have no tab to close
	}

	protected static bool ReadFlag(string name, bool fallback)
	{
		BaseContainer c = GetGame().GetGameUserSettings().GetModule("SM_ClientSettings");
		if (!c)
			return fallback;
		int v = 0;
		if (!c.Get(name, v))
			return fallback;	// setting never written yet — keep the documented default
		return v != 0;
	}
}
