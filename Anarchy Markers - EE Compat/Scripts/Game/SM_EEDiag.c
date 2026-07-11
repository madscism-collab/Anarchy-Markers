// Diagnostics, temporary — delete once EE integration is confirmed working.
// Dumps the exact marker list the radio action sees, plus bridge state and faction.

modded class Deko_RadioCallPickupAction
{
	protected static int s_iAMEE_LastDbg;

	override bool CanBeShownScript(IEntity user)
	{
		bool shown = super.CanBeShownScript(user);

		int now = System.GetTickCount();
		if (now - s_iAMEE_LastDbg > 3000)
		{
			s_iAMEE_LastDbg = now;

			int pid = -1;
			PlayerController pc = GetGame().GetPlayerController();
			if (pc)
				pid = pc.GetPlayerId();

			array<string> vm = {};
			Deko_GetAvailableInsertMarkers(pid, vm);

			string stats = "<NO INSTANCE>";
			Deko_HeliPickupAvailabilityComponent comp = Deko_HeliPickupAvailabilityComponent.GetInstance();
			if (comp)
				stats = comp.GetHeliStats();

			// Exactly what this call sees — the same path EE's filter takes
			string dump = "";
			SCR_MapMarkerManagerComponent mgr = SCR_MapMarkerManagerComponent.GetInstance();
			array<SCR_MapMarkerBase> markers = {};
			if (mgr)
				markers = mgr.GetStaticMarkers();
			foreach (SCR_MapMarkerBase m : markers)
			{
				if (!m)
				{
					dump += " <null>";
					continue;
				}
				dump += string.Format(" ['%1' own=%2 mid=%3 type=%4]",
					m.GetCustomText(), m.GetMarkerOwnerID(), m.GetMarkerID(), m.GetType());
			}

			Faction f = Deko_HeliPickupMissionManager.Deko_GetPlayerFaction(pid);
			string fk = "<null>";
			if (f)
				fk = f.GetFactionKey();

			PrintFormat("[AM-EE-RADIO] slot=%1 shown=%2 pid=%3 validMarkers=%4 heliStats='%5'",
				m_iSlotIndex, shown, pid, vm.Count(), stats);
			PrintFormat("[AM-EE-RADIO2] n=%1 bridge=%2 exposed=%3 faction=%4 list:%5",
				markers.Count(), AM_VanillaBridge.IsEnabled(), AM_VanillaBridge.GetExposedCount(), fk, dump);
		}

		return shown;
	}
}
