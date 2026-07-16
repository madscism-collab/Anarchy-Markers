// Дрібні візуальні структури шару мапи (винесені з SM_MapMarkerLayer.c, щоб не роздувати його).
// Те, чим мітка намальована на мапі: іконка (або військовий символ) + підпис + позначка часу.
// Підпис це окремий віджет, не дитина іконки, щоб він не зсував саму мітку, а просто стояв під нею.
class SM_MarkerVisual
{
	ref SM_MapMarkerData m_Data;	// сильне посилання: для прев'ю дані живуть локально й мають
									// пережити SM_BeginPreview (без ref їх прибрав би GC і був би краш)
	ImageWidget      m_wIcon;		// цивільна іконка
	Widget           m_wSymbol;		// військовий APP-6 overlay
	SCR_MilitarySymbolUIComponent m_SymbolComp;	// малює символ на m_wSymbol
	TextWidget       m_wLabel;		// підпис під міткою
	TextWidget       m_wTime;		// позначка часу, окремим удвічі меншим шрифтом

	void SM_MarkerVisual(SM_MapMarkerData data)
	{
		m_Data = data;
	}

	// Головний віджет мітки — іконка або символ. По ньому рахуємо позицію й розмір.
	Widget GetMainWidget()
	{
		if (m_wIcon)
			return m_wIcon;
		return m_wSymbol;
	}

	void Destroy()
	{
		if (m_wIcon)
			m_wIcon.RemoveFromHierarchy();
		if (m_wSymbol)
			m_wSymbol.RemoveFromHierarchy();
		if (m_wLabel)
			m_wLabel.RemoveFromHierarchy();
		if (m_wTime)
			m_wTime.RemoveFromHierarchy();
		m_wIcon = null;
		m_wSymbol = null;
		m_SymbolComp = null;
		m_wLabel = null;
		m_wTime = null;
	}
}

// Візуал тимчасового «вказівника»: жовто-помаранчева точка (з текстури з альфою) + ім'я гравця.
class SM_PointerVisual
{
	ImageWidget m_wDot;
	TextWidget  m_wName;

	void Destroy()
	{
		if (m_wDot)  m_wDot.RemoveFromHierarchy();
		if (m_wName) m_wName.RemoveFromHierarchy();
		m_wDot = null;
		m_wName = null;
	}
}

