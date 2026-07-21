// Our client options, as a proper "Anarchy Markers" section at the bottom of Settings -> Gameplay.
//
// Everything is BUILT AT RUNTIME rather than by shipping a modded GameplaySettings.layout. Overriding
// that layout is how some mods do it, but it needs our .meta to carry the vanilla layout's own GUID
// (otherwise the file is just an unused resource and nothing shows up), and whichever such mod loads
// last wins the whole tab. Spawning the stock widgets ourselves needs no resource override at all, so
// we coexist with every other settings mod.
//
// Rows are handed to the same binding the vanilla rows use, so the game loads, saves and persists our
// values exactly like built-in settings — and console players get gamepad navigation for free, since
// these ARE the game's own settings widgets.
//
// ADDING AN OPTION LATER: add the field to SM_ClientSettings, then one SM_AddSpinBox/SM_AddSlider call
// plus one SM_Bind call below. Nothing else.
modded class SCR_GameplaySettingsSubMenu
{
	protected static const ResourceName SM_TITLE_LAYOUT   = "{FEEEB639F2735BA1}UI/layouts/Menus/SettingsMenu/CustomWidgets/SettingsTitle.layout";
	protected static const ResourceName SM_SPINBOX_LAYOUT = "{C9DF0E6590F6C388}UI/layouts/WidgetLibrary/SpinBox/WLib_SpinBox.layout";

	override void OnTabCreate(Widget menuRoot, ResourceName buttonsLayout, int index)
	{
		super.OnTabCreate(menuRoot, buttonsLayout, index);

		if (!m_wScroll)
			return;

		// The scroll's single child is the vertical list every settings row lives in.
		Widget list = m_wScroll.GetChildren();
		if (!list)
			return;

		SM_AddTitle(list, "Anarchy Markers");

		if (SM_AddSpinBox(list, "SM_EnhancedFill", "Enhanced map fill", "#AR-UI_No", "#AR-UI_Yes"))
			SM_Bind("m_iEnhancedFill", "SM_EnhancedFill");

		if (SM_AddSpinBox(list, "SM_CopyNumbering", "Number copied markers", "#AR-UI_No", "#AR-UI_Yes"))
			SM_Bind("m_iCopyNumbering", "SM_CopyNumbering");

		if (SM_AddSpinBox(list, "SM_SimplifyDistant", "Simplify distant drawings and fills", "#AR-UI_No", "#AR-UI_Yes"))
			SM_Bind("m_iSimplifyDistant", "SM_SimplifyDistant");
	}

	//! Section header, same widget the vanilla "Field of view" heading uses.
	protected void SM_AddTitle(notnull Widget list, string text)
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;

		Widget title = ws.CreateWidgets(SM_TITLE_LAYOUT, list);
		if (!title)
			return;

		SCR_LabelComponent label = SCR_LabelComponent.Cast(title.FindHandler(SCR_LabelComponent));
		if (label)
			label.SetText(text);
	}

	//! A two-choice row (the settings menu's standard way of showing a yes/no option).
	//! widgetName is what the binding will look the row up by, so it must be unique in the tab.
	protected bool SM_AddSpinBox(notnull Widget list, string widgetName, string label, string optionA, string optionB)
	{
		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return false;

		Widget row = ws.CreateWidgets(SM_SPINBOX_LAYOUT, list);
		if (!row)
			return false;

		row.SetName(widgetName);

		SCR_SpinBoxComponent spin = SCR_SpinBoxComponent.Cast(row.FindHandler(SCR_SpinBoxComponent));
		if (!spin)
		{
			row.RemoveFromHierarchy();
			return false;
		}

		spin.AddItem(optionA);
		spin.AddItem(optionB);

		// UseLabel first: SetLabel is a no-op until the label widget exists. The prefab already carries
		// the vanilla settings label layout, so our rows line up with the built-in ones.
		spin.UseLabel(true);
		spin.SetLabel(label);
		return true;
	}

	//! Wire a row to its SM_ClientSettings field — the game handles load/save/persist from here.
	protected void SM_Bind(string settingName, string widgetName)
	{
		SCR_SettingBindingGameplay bind = new SCR_SettingBindingGameplay("SM_ClientSettings", settingName, widgetName);
		m_aSettingsBindings.Insert(bind);
		bind.LoadEntry(m_wScroll, false, true);
		bind.GetEntryChangedInvoker().Insert(OnMenuItemChanged);	// vanilla save/mark-dirty on change
	}
}
