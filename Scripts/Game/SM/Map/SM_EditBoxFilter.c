// Поле m_iCharacterLimit у EditBoxFilterComponent приватне, тож відкриваємо до нього сетер —
// треба підняти ліміт довжини тексту мітки (ванільний дефолт у layout = 16).
// Увага: ліміт рахується в БАЙТАХ, а кирилиця це 2 байти на символ.
modded class EditBoxFilterComponent
{
	void SM_SetCharacterLimit(int limit)
	{
		m_iCharacterLimit = limit;
	}
}
