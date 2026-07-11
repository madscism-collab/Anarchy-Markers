// Server-owner settings for the vanilla marker bridge. File: $profile:SavingMarkers/SM_VanillaBridge.cfg
// Same key=value format as the core's SM_Config.cfg, and it lives next to it on purpose.
//
// The server owns these settings and replicates them (see AMVB_Compat.c). The bridge has to expose
// the SAME marker list on both sides: a consumer mod typically offers something on the client and
// then re-validates it on the server, and if the two saw different lists that check would fail for
// reasons nobody could trace.

class AMVB_Config
{
	protected static ref AMVB_Config s_Instance;

	// 1. Master switch. Uninstalling the addon is the other way to turn the bridge off.
	bool m_bEnabled = true;
	// 2. Expose only markers whose text starts with one of these prefixes (case-insensitive, comma
	//    separated). Empty = every marker the player may see. Narrow it if a mod on your server
	//    iterates or deletes vanilla markers and trips over ours.
	ref array<string> m_aPrefixes = {};
	// 3. Also expose the player's own Local-channel markers. ON: a mod that wants a marker usually
	//    wants THIS one — "drop a CAS marker, then call it in" — and Local is the channel a player
	//    naturally picks for a marker only he needs.
	bool m_bIncludeLocal = true;

	protected const string DIR  = "$profile:SavingMarkers";
	protected const string FILE = "$profile:SavingMarkers/SM_VanillaBridge.cfg";

	static AMVB_Config GetInstance()
	{
		if (!s_Instance)
			s_Instance = new AMVB_Config();
		return s_Instance;
	}

	//! Server only. Rewrites the file afterwards, so keys added in an update appear in it.
	void ServerLoadOrCreate()
	{
		FileIO.MakeDirectory(DIR);

		if (FileIO.FileExists(FILE))
			ParseCfg();
		else
			Print("[AMVB] Config not found — creating defaults at " + FILE, LogLevel.NORMAL);

		Save();
	}

	//! Push the settings into the bridge. The filter goes in before the switch, so the bridge is
	//! never briefly enabled with the wrong one.
	void Apply()
	{
		AM_VanillaBridge.SetTextFilter(m_aPrefixes);
		AM_VanillaBridge.SetIncludeLocal(m_bIncludeLocal);
		AM_VanillaBridge.SetEnabled(m_bEnabled);
	}

	string PrefixesCsv()
	{
		string csv;
		foreach (int i, string p : m_aPrefixes)
		{
			if (i > 0)
				csv += ",";
			csv += p;
		}
		return csv;
	}

	void SetPrefixesCsv(string csv)
	{
		m_aPrefixes.Clear();

		array<string> parts = {};
		csv.Split(",", parts, true);
		foreach (string p : parts)
		{
			p.Trim();
			if (p != "")
				m_aPrefixes.Insert(p);
		}
	}

	protected void ParseCfg()
	{
		FileHandle h = FileIO.OpenFile(FILE, FileMode.READ);
		if (!h)
			return;

		string line;
		int guard = 0;
		// An empty line reads as 0 characters, not EOF (-1) — hence >= 0, or parsing would stop at
		// the first blank line between sections.
		while (h.ReadLine(line) >= 0)
		{
			guard++;
			if (guard > 1000)
				break;

			line.Replace("\r", "");
			line.Trim();
			if (line.IsEmpty() || line.IndexOf("#") == 0)
				continue;

			int eq = line.IndexOf("=");
			if (eq <= 0)
				continue;

			string key = line.Substring(0, eq);
			string val = line.Substring(eq + 1, line.Length() - eq - 1);
			key.Trim();
			val.Trim();

			if      (key == "bridgeEnabled")       m_bEnabled       = ParseBool(val);
			else if (key == "markerTextPrefixes")  SetPrefixesCsv(val);
			else if (key == "includeLocalMarkers") m_bIncludeLocal  = ParseBool(val);
		}
		h.Close();

		Print(string.Format("[AMVB] Config loaded: enabled=%1 prefixes='%2' includeLocal=%3", m_bEnabled, PrefixesCsv(), m_bIncludeLocal), LogLevel.NORMAL);
	}

	protected bool ParseBool(string v)
	{
		return v == "true" || v == "1" || v == "yes" || v == "on";
	}

	protected string B2S(bool b)
	{
		if (b) return "true";
		return "false";
	}

	void Save()
	{
		FileIO.MakeDirectory(DIR);
		FileHandle h = FileIO.OpenFile(FILE, FileMode.WRITE);
		if (!h)
		{
			Print("[AMVB] Could not write config to " + FILE, LogLevel.WARNING);
			return;
		}

		h.WriteLine("# Anarchy Markers - vanilla marker bridge");
		h.WriteLine("# Lets mods that only read the VANILLA marker list (SCR_MapMarkerManagerComponent.GetStaticMarkers)");
		h.WriteLine("# see Anarchy Markers markers. Read-only: those mods can look, not write.");
		h.WriteLine("# Format: key=value (one per line). # starts a comment. Edit and restart the server.");
		h.WriteLine("");
		h.WriteLine("# Master switch for the bridge.");
		h.WriteLine("bridgeEnabled=" + B2S(m_bEnabled));
		h.WriteLine("");
		h.WriteLine("# Expose only markers whose text starts with one of these prefixes (case-insensitive,");
		h.WriteLine("# comma separated). Empty = every marker the player is allowed to see.");
		h.WriteLine("# Set this if a mod on your server counts, iterates or deletes vanilla markers: it would");
		h.WriteLine("# otherwise meet markers it never created.");
		h.WriteLine("# EE Transport (Heli Enhanced Frame) only ever reads lz/ln/cas, so for EE alone use:");
		h.WriteLine("#   markerTextPrefixes=lz,ln,cas");
		h.WriteLine("markerTextPrefixes=" + PrefixesCsv());
		h.WriteLine("");
		h.WriteLine("# Also expose the player's own Local-channel markers (only ever to that player).");
		h.WriteLine("# On: 'drop a CAS marker, then call it in' is how these mods work, and a marker meant");
		h.WriteLine("# for the player alone is the one he will reach for. Turn off only to hide them.");
		h.WriteLine("includeLocalMarkers=" + B2S(m_bIncludeLocal));

		h.Close();
	}
}
