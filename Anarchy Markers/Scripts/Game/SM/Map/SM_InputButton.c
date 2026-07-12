// Коли друкуєш текст в едітбоксі, гра автоматично сіріє всі input-кнопки верхнього меню.
// Нам це заважає: у діалозі мітки треба й далі бачити вибраний канал видимості та тиснути
// Place/Cancel. Тому додаємо прапорець і ставимо його тільки нашим кнопкам.
modded class SCR_InputButtonComponent
{
	bool m_bSMNoInteractionDisable;

	override void OnActiveWidgetInteraction(bool isInteractionActive, int delay)
	{
		if (m_bSMNoInteractionDisable)
			return;	// наші кнопки не сіріють під час набору тексту

		super.OnActiveWidgetInteraction(isInteractionActive, delay);
	}

	// Робить з кнопки звичайну підказку: гліф і підпис лишаються, але вона більше не реагує
	// на саму дію (без звуку й анімації). Без цього підказка-ЛКМ блимала б на кожен клік по мапі.
	// Викликати після SetAction() — бо саме він чіпляє слухачів.
	//
	// A hold action keeps its listeners: OnButtonHold is what fills the progress bar, and that bar is
	// the whole point of showing the hint. It cannot misfire either — a hint has nothing wired to its
	// click invokers.
	void SM_MakeDisplayOnly()
	{
		if (!m_InputManager || m_sActionName.IsEmpty() || m_bIsHoldAction)
			return;
		m_InputManager.RemoveActionListener(m_sActionName, EActionTrigger.DOWN,  OnButtonPressed);
		m_InputManager.RemoveActionListener(m_sActionName, EActionTrigger.VALUE, OnButtonHold);
		m_InputManager.RemoveActionListener(m_sActionName, EActionTrigger.UP,    ActionReleased);
	}
}
