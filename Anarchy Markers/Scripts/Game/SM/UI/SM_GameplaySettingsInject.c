// Puts our one client option (enhanced map fill) into the vanilla Gameplay settings tab.
//
// Two halves, mirroring how any Reforger mod adds a settings row:
//  - UI/layouts/Menus/SettingsMenu/GameplaySettings.layout mods the vanilla settings-scroll prefab and
//    appends a No/Yes SpinBox row named "SM_EnhancedFill" (several mods can stack on that prefab, so
//    this coexists with other settings mods rather than replacing the tab).
//  - here we bind that widget to the SM_ClientSettings.m_iEnhancedFill profile value, the same way the
//    vanilla rows bind their own settings, so the game loads/saves/persists it for free.
modded class SCR_GameplaySettingsSubMenu
{
	override void OnTabCreate(Widget menuRoot, ResourceName buttonsLayout, int index)
	{
		super.OnTabCreate(menuRoot, buttonsLayout, index);

		SCR_SettingBindingGameplay bind = new SCR_SettingBindingGameplay("SM_ClientSettings", "m_iEnhancedFill", "SM_EnhancedFill");
		m_aSettingsBindings.Insert(bind);
		bind.LoadEntry(m_wScroll, false, true);
		bind.GetEntryChangedInvoker().Insert(OnMenuItemChanged);	// vanilla save/mark-dirty on change
	}
}
