// Why our Size/Color/Opacity dropdowns never showed on the tablet.
//
// The tablet arms clipping across the whole map subtree (GRS_ATAKMenuUI.ArmMapClipping): MapFrame gets
// CLIPCHILDREN, then every widget beneath it gets INHERIT_CLIPPING, so nothing can spill past the device
// screen. Their walk deliberately stops at any subtree that already clips its own children (their comment:
// vanilla task labels overflow tiny parents on purpose). Our panel root is Clipping False — so the walk
// walks straight in and arms our internals too.
//
// That kills the dropdowns specifically. A dropdown is a child of its combo and opens BELOW it: SizeCombo
// is ~50 tall, its dropdown starts ~18 past the combo's bottom edge and runs ~340 further. Once SizeCombo
// inherits clipping, the dropdown is entirely outside its clip rect and renders as nothing. The panel's
// buttons sit INSIDE their parents, which is why the toolbar looked fine and only dropdowns "didn't open"
// — they did open, with the right size, at the right place, invisibly.
//
// So: let them arm whatever they like, then lift the flag back off our own subtree — root included, since
// an armed widget clips to its own rect too and our root is a 100-tall strip. The result is the panel
// behaving exactly as it does on every other map. Nothing else on the tablet is touched.
//
// Hooked at ArmMapClipping rather than done once on open because arming is fired four times per open
// (500/2000/6000/15000 ms) — a one-shot fix would be undone by the next pass, and the 15s one could land
// while a dropdown is open.

modded class GRS_ATAKMenuUI
{
	protected override void ArmMapClipping()
	{
		super.ArmMapClipping();
		AMGA_UnclipDrawPanel();
	}

	protected void AMGA_UnclipDrawPanel()
	{
		if (!m_wMapFrame)
			return;

		// Anarchy's panel layouts share this root name. Only the drawing box exists on the tablet
		// (AMGA_MapConfig grants DRAWING_TOOLS but not MARKER_TOOLS), so one hit is all there is.
		Widget root = m_wMapFrame.FindAnyWidget("MarkerEditRoot");
		if (!root)
			return;

		// The root is freed too, not just its children: an armed widget clips to its OWN rect as well as
		// to the inherited one, and the root is a 100-tall strip holding a toolbar that dropdowns hang
		// well below. Leaving it armed cut every dropdown to a ~26px stub at the root's bottom edge.
		// Nothing is lost by freeing it — the panel sits comfortably inside the map frame, so there is
		// no overflow onto the bezel to guard against in the first place.
		root.ClearFlags(WidgetFlags.INHERIT_CLIPPING);

		// Root's children, never root's SIBLINGS — those are GRS's own widgets and their clipping is
		// none of our business.
		AMGA_UnclipWalk(root.GetChildren());
	}

	protected void AMGA_UnclipWalk(Widget w)
	{
		while (w)
		{
			w.ClearFlags(WidgetFlags.INHERIT_CLIPPING);
			AMGA_UnclipWalk(w.GetChildren());
			w = w.GetSibling();
		}
	}
}
