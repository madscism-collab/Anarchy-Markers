// Parametric shapes: rectangle, circle, map grid. A shape drawing stores only its two parameter
// points (SM_MapDrawingData.m_iShape + m_aPoints); every client turns those into the SAME polylines
// here. Two points over the wire instead of hundreds — and the grid genuinely cannot desync, because
// nobody ever transmits its geometry in the first place.
//
// All output is world-space polylines (flat x,z metre pairs) with a per-line width in METRES, so the
// lines scale with zoom exactly like brush strokes. The canvas projects and tessellates them with the
// machinery it already has.

//! One line of a built shape.
class SM_ShapeLine
{
	ref array<int> m_aPts = {};	// world x,z pairs
	float m_fWidthMeters;
}

class SM_ShapeGeometry
{
	static const int SHAPE_NONE     = 0;
	static const int SHAPE_RECT     = 1;
	static const int SHAPE_CIRCLE   = 2;
	static const int SHAPE_GRID     = 3;
	static const int SHAPE_GRID_REV = 4;	// same grid, letters down the side instead of across the top
	static const int SHAPE_MAX      = 4;	// validation bound — raise with every shape added above

	//! Both grids are one shape everywhere except which header carries the letters: same geometry, same
	//! snapping, same per-cell fill, same quota. Everything that asks "is this a grid" goes through here
	//! so a new variant cannot be forgotten in one of the dozen places that check.
	static bool IsGrid(int shape)
	{
		return shape == SHAPE_GRID || shape == SHAPE_GRID_REV;
	}

	// The map's own grid is 100 m squares anchored at the world origin — ours snaps to it.
	static const int GRID_CELL      = 100;
	static const int GRID_MIN_CELLS = 10;	// 1 km² floor
	static const int GRID_MAX_CELLS = 26;	// the English alphabet is the hard limit, whichever axis carries it
	protected static const string LETTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

	//! Build the drawable lines for a shape. p = the two parameter points (x0,z0,x1,z1),
	//! borderMeters = the brush width the author picked (drives everything else).
	static void Build(int shape, notnull array<int> p, float borderMeters, notnull array<ref SM_ShapeLine> outLines)
	{
		outLines.Clear();
		if (p.Count() < 4)
			return;

		switch (shape)
		{
			case SHAPE_RECT:     BuildRect(p[0], p[1], p[2], p[3], borderMeters, outLines); break;
			case SHAPE_CIRCLE:   BuildCircle(p[0], p[1], p[2], p[3], borderMeters, outLines); break;
			case SHAPE_GRID:     BuildGrid(p[0], p[1], p[2], p[3], borderMeters, false, outLines); break;
			case SHAPE_GRID_REV: BuildGrid(p[0], p[1], p[2], p[3], borderMeters, true,  outLines); break;
		}
	}

	//! Coarse outline for hit tests (eraser, Del-under-cursor). The border is what the player aims at.
	static void BuildHitOutline(int shape, notnull array<int> p, notnull array<int> outPts)
	{
		outPts.Clear();
		if (p.Count() < 4)
			return;

		if (shape == SHAPE_RECT)
		{
			RectLoop(Math.Min(p[0], p[2]), Math.Min(p[1], p[3]), Math.Max(p[0], p[2]), Math.Max(p[1], p[3]), outPts);
		}
		else if (shape == SHAPE_CIRCLE)
		{
			float dx = p[2] - p[0];
			float dz = p[3] - p[1];
			float r = Math.Sqrt(dx * dx + dz * dz);
			for (int i = 0; i <= 16; i++)
			{
				float a = i * Math.PI2 / 16;
				outPts.Insert(p[0] + Math.Cos(a) * r);
				outPts.Insert(p[1] + Math.Sin(a) * r);
			}
		}
		else if (IsGrid(shape))
		{
			// Outer border incl. the header row/column.
			int loX = Math.Min(p[0], p[2]) - GRID_CELL;
			int hiX = Math.Max(p[0], p[2]);
			int loZ = Math.Min(p[1], p[3]);
			int hiZ = Math.Max(p[1], p[3]) + GRID_CELL;
			RectLoop(loX, loZ, hiX, hiZ, outPts);
		}
	}

	//! Segments in a circle of radius r — the single source both the renderer and the cost use.
	static int CircleSegments(float r)
	{
		return ClampI(Math.Round(r * 0.35), 24, 96);
	}

	//! How many ordinary strokes this shape is WORTH against the server's per-player / total drawing
	//! limits. A shape is one record on the wire, but it must not be a way to spend one slot and cover
	//! the map: a rectangle is its four sides, a circle is its segments. The grid is capped by a
	//! separate per-player COUNT (drawMaxGridsPerPlayer), so it costs the ordinary 1 here.
	static int StrokeCost(int shape, notnull array<int> pts)
	{
		if (pts.Count() < 4)
			return 1;
		if (shape == SHAPE_RECT)
			return 4;
		if (shape == SHAPE_CIRCLE)
		{
			float dx = pts[2] - pts[0];
			float dz = pts[3] - pts[1];
			return CircleSegments(Math.Sqrt(dx * dx + dz * dz));
		}
		return 1;
	}

	//! Is (x,z) inside this shape's area? For the grid, "inside" means within the CELL field (not the
	//! header row/column).
	static bool PointInShape(int shape, notnull array<int> p, int x, int z)
	{
		if (p.Count() < 4)
			return false;

		if (shape == SHAPE_CIRCLE)
		{
			float dx = x - p[0];
			float dz = z - p[1];
			float rdx = p[2] - p[0];
			float rdz = p[3] - p[1];
			return dx * dx + dz * dz <= rdx * rdx + rdz * rdz;
		}
		if (shape == SHAPE_RECT)
		{
			return x >= Math.Min(p[0], p[2]) && x <= Math.Max(p[0], p[2])
				&& z >= Math.Min(p[1], p[3]) && z <= Math.Max(p[1], p[3]);
		}
		if (IsGrid(shape))
		{
			return x >= p[0] && x <= p[2] && z >= p[3] && z <= p[1];
		}
		return false;
	}

	//! Relative area, for picking the SMALLEST shape a click falls in (a cell inside a big circle wins).
	//! No units — only compared against other shapes.
	static float ShapeArea(int shape, notnull array<int> p)
	{
		if (p.Count() < 4)
			return 1e18;
		if (shape == SHAPE_CIRCLE)
		{
			float dx = p[2] - p[0];
			float dz = p[3] - p[1];
			return dx * dx + dz * dz;	// r^2, monotonic in area
		}
		if (IsGrid(shape))
			return GRID_CELL * GRID_CELL;	// a fill is one CELL, always the smallest thing
		float w = Math.AbsFloat(p[2] - p[0]);
		float h = Math.AbsFloat(p[3] - p[1]);
		return w * h;
	}

	//! The EXACT fill polygon for a shape clicked at (clickX, clickZ): a smooth circle, the rectangle
	//! itself, or the single grid CELL under the click. Closed outline, no duplicate first point — the
	//! same shape the flood-fill would try to approximate, but without the stairsteps. Empty if none.
	//! maxPts caps the circle's vertex count so the server (which trims to drawMaxPointsPerStroke)
	//! can't lop off part of the outline and leave a broken polygon. 0 = no cap.
	static void FillContour(int shape, notnull array<int> p, int clickX, int clickZ, int maxPts, notnull array<int> outContour)
	{
		outContour.Clear();
		if (p.Count() < 4)
			return;

		if (shape == SHAPE_CIRCLE)
		{
			float dx = p[2] - p[0];
			float dz = p[3] - p[1];
			float r = Math.Sqrt(dx * dx + dz * dz);
			if (r < 1)
				return;
			int n = CircleSegments(r);
			if (maxPts > 3 && n > maxPts)
				n = maxPts;
			for (int i = 0; i < n; i++)	// no closing duplicate: a fill polygon is implicitly closed
			{
				float a = i * Math.PI2 / n;
				outContour.Insert(p[0] + Math.Cos(a) * r);
				outContour.Insert(p[1] + Math.Sin(a) * r);
			}
		}
		else if (shape == SHAPE_RECT)
		{
			int loX = Math.Min(p[0], p[2]);
			int hiX = Math.Max(p[0], p[2]);
			int loZ = Math.Min(p[1], p[3]);
			int hiZ = Math.Max(p[1], p[3]);
			outContour.Insert(loX); outContour.Insert(loZ);
			outContour.Insert(hiX); outContour.Insert(loZ);
			outContour.Insert(hiX); outContour.Insert(hiZ);
			outContour.Insert(loX); outContour.Insert(hiZ);
		}
		else if (IsGrid(shape))
		{
			int col = Math.Floor((clickX - p[0]) / 100.0);
			int row = Math.Floor((p[1] - clickZ) / 100.0);
			int cx = p[0] + col * GRID_CELL;
			int czTop = p[1] - row * GRID_CELL;
			int czBot = czTop - GRID_CELL;
			outContour.Insert(cx);             outContour.Insert(czBot);
			outContour.Insert(cx + GRID_CELL); outContour.Insert(czBot);
			outContour.Insert(cx + GRID_CELL); outContour.Insert(czTop);
			outContour.Insert(cx);             outContour.Insert(czTop);
		}
	}

	//! Normalise grid parameters in place: anchor on the 100 m lattice, extent within 10..26 cells.
	//! p[0],p[1] = top-left corner of cell A1; p[2],p[3] = bottom-right of the field.
	static void SnapGridParams(notnull array<int> p)
	{
		if (p.Count() < 4)
			return;

		int ax = Math.Round(p[0] / 100.0) * GRID_CELL;
		int az = Math.Round(p[1] / 100.0) * GRID_CELL;
		int cols = ClampI(Math.Round((p[2] - ax) / 100.0), GRID_MIN_CELLS, GRID_MAX_CELLS);
		int rows = ClampI(Math.Round((az - p[3]) / 100.0), GRID_MIN_CELLS, GRID_MAX_CELLS);

		p[0] = ax;
		p[1] = az;
		p[2] = ax + cols * GRID_CELL;
		p[3] = az - rows * GRID_CELL;
	}

	// ------------------------------------------------------------------ shapes

	protected static void BuildRect(int x0, int z0, int x1, int z1, float w, notnull array<ref SM_ShapeLine> outLines)
	{
		SM_ShapeLine l = new SM_ShapeLine();
		l.m_fWidthMeters = w;
		RectLoop(Math.Min(x0, x1), Math.Min(z0, z1), Math.Max(x0, x1), Math.Max(z0, z1), l.m_aPts);
		outLines.Insert(l);
	}

	protected static void BuildCircle(int cx, int cz, int rx, int rz, float w, notnull array<ref SM_ShapeLine> outLines)
	{
		float dx = rx - cx;
		float dz = rz - cz;
		float r = Math.Sqrt(dx * dx + dz * dz);
		if (r < 1)
			return;

		// Enough segments to look round at any sane size, few enough to stay cheap.
		int n = CircleSegments(r);

		SM_ShapeLine l = new SM_ShapeLine();
		l.m_fWidthMeters = w;
		for (int i = 0; i <= n; i++)
		{
			float a = i * Math.PI2 / n;
			l.m_aPts.Insert(cx + Math.Cos(a) * r);
			l.m_aPts.Insert(cz + Math.Sin(a) * r);
		}
		outLines.Insert(l);
	}

	//! The grid of the screenshot: a header row of letters above row 1, a header column of numbers
	//! left of column A, thin cell lines, thick outer border and thick header separators. (ax,az) is
	//! the top-left corner of cell A1 — the cell the player clicked.
	//! reversed = letters run DOWN the header column and numbers across the header row, the mirror of the
	//! usual convention. Nothing else changes — the lines, the snapping and cell A1 are identical.
	protected static void BuildGrid(int ax, int az, int bx, int bz, float borderW, bool reversed, notnull array<ref SM_ShapeLine> outLines)
	{
		int cols = ClampI(Math.Round((bx - ax) / 100.0), 1, GRID_MAX_CELLS);
		int rows = ClampI(Math.Round((az - bz) / 100.0), 1, GRID_MAX_CELLS);

		int loX = ax - GRID_CELL;	// header column
		int hiZ = az + GRID_CELL;	// header row
		int hiX = ax + cols * GRID_CELL;
		int loZ = az - rows * GRID_CELL;

		float thinW = Math.Max(borderW * 0.25, 1.5);

		// Thin inner lines: verticals between columns, horizontals between rows (headers included).
		for (int c = 0; c < cols; c++)
			AddSegment(ax + c * GRID_CELL, hiZ, ax + c * GRID_CELL, loZ, thinW, outLines);
		for (int r = 0; r < rows; r++)
			AddSegment(loX, az - r * GRID_CELL, hiX, az - r * GRID_CELL, thinW, outLines);

		// Thick header separators (right of the number column, under the letter row) and border.
		float midW = Math.Max(borderW * 0.6, 2.5);
		AddSegment(ax, hiZ, ax, loZ, midW, outLines);
		AddSegment(loX, az, hiX, az, midW, outLines);

		SM_ShapeLine border = new SM_ShapeLine();
		border.m_fWidthMeters = borderW;
		RectLoop(loX, loZ, hiX, hiZ, border.m_aPts);
		outLines.Insert(border);

		// Labels. Normally letters run across the header row and numbers down the header column; the
		// reversed variant swaps which header gets which. The glyph heights stay tied to the header they
		// sit in (56 across the top, 46 down the side), so a swapped grid still reads at the same size.
		float glyphW = Math.Max(borderW * 0.35, 2.5);
		for (int c2 = 0; c2 < cols; c2++)
		{
			string top = LETTERS.Get(c2);
			if (reversed)
			{
				int cn = c2 + 1;
				top = cn.ToString();
			}
			AddLabel(top, ax + c2 * GRID_CELL + 50, az + 50, 56, glyphW, outLines);
		}
		for (int r2 = 0; r2 < rows; r2++)
		{
			int num = r2 + 1;
			string side = num.ToString();
			if (reversed)
				side = LETTERS.Get(r2);
			AddLabel(side, ax - 50, az - r2 * GRID_CELL - 50, 46, glyphW, outLines);
		}
	}

	// ------------------------------------------------------------------ helpers

	static int ClampI(int v, int lo, int hi)
	{
		if (v < lo) return lo;
		if (v > hi) return hi;
		return v;
	}

	protected static void RectLoop(int loX, int loZ, int hiX, int hiZ, notnull array<int> outPts)
	{
		outPts.Insert(loX); outPts.Insert(loZ);
		outPts.Insert(hiX); outPts.Insert(loZ);
		outPts.Insert(hiX); outPts.Insert(hiZ);
		outPts.Insert(loX); outPts.Insert(hiZ);
		outPts.Insert(loX); outPts.Insert(loZ);
	}

	protected static void AddSegment(int x0, int z0, int x1, int z1, float w, notnull array<ref SM_ShapeLine> outLines)
	{
		SM_ShapeLine l = new SM_ShapeLine();
		l.m_fWidthMeters = w;
		l.m_aPts.Insert(x0); l.m_aPts.Insert(z0);
		l.m_aPts.Insert(x1); l.m_aPts.Insert(z1);
		outLines.Insert(l);
	}

	// --- vector glyphs ---
	// There is no text draw command on a CanvasWidget, so the labels are polylines from a tiny stroke
	// font. Coordinates are a 0..1 box, y DOWN (screen-like); world conversion flips y onto -Z. Width
	// of a glyph is 0.62 of its height — two digits plus a gap fit a 100 m header cell at size 46.

	//! Draw `text` centred at (cx, cz), glyph height = size metres.
	protected static void AddLabel(string text, float cx, float cz, float size, float lineW, notnull array<ref SM_ShapeLine> outLines)
	{
		int n = text.Length();
		if (n < 1)
			return;

		float gw = size * 0.62;			// glyph box width
		float adv = gw + size * 0.18;	// advance between glyph origins
		float total = adv * n - size * 0.18;
		float left = cx - total * 0.5;
		float top  = cz + size * 0.5;	// world Z grows north = up

		array<ref array<float>> strokes = {};
		for (int i = 0; i < n; i++)
		{
			strokes.Clear();
			GlyphStrokes(text.Get(i), strokes);
			float gx = left + i * adv;
			foreach (array<float> s : strokes)
			{
				SM_ShapeLine l = new SM_ShapeLine();
				l.m_fWidthMeters = lineW;
				for (int k = 0; k + 1 < s.Count(); k += 2)
				{
					l.m_aPts.Insert(gx + s[k] * gw);
					l.m_aPts.Insert(top - s[k + 1] * size);	// y down in glyph space -> -Z in world
				}
				outLines.Insert(l);
			}
		}
	}

	protected static void S(notnull array<ref array<float>> outStrokes, notnull array<float> pts)
	{
		array<float> s = {};
		s.Copy(pts);
		outStrokes.Insert(s);
	}

	//! Strokes of one glyph in a 0..1 box (x right, y down). Angular, hand-drawn-map style.
	protected static void GlyphStrokes(string ch, notnull array<ref array<float>> outStrokes)
	{
		switch (ch)
		{
			case "A": S(outStrokes, {0,1, 0.5,0, 1,1}); S(outStrokes, {0.2,0.62, 0.8,0.62}); break;
			case "B": S(outStrokes, {0,1, 0,0, 0.8,0.05, 0.85,0.25, 0.7,0.48, 0,0.5}); S(outStrokes, {0.7,0.48, 0.95,0.6, 0.95,0.85, 0.75,1, 0,1}); break;
			case "C": S(outStrokes, {0.95,0.15, 0.6,0, 0.15,0.1, 0,0.5, 0.15,0.9, 0.6,1, 0.95,0.85}); break;
			case "D": S(outStrokes, {0,0, 0,1, 0.6,1, 0.95,0.75, 0.95,0.25, 0.6,0, 0,0}); break;
			case "E": S(outStrokes, {1,0, 0,0, 0,1, 1,1}); S(outStrokes, {0,0.5, 0.75,0.5}); break;
			case "F": S(outStrokes, {1,0, 0,0, 0,1}); S(outStrokes, {0,0.5, 0.75,0.5}); break;
			case "G": S(outStrokes, {0.95,0.15, 0.6,0, 0.15,0.1, 0,0.5, 0.15,0.9, 0.6,1, 0.95,0.9, 0.95,0.55, 0.55,0.55}); break;
			case "H": S(outStrokes, {0,0, 0,1}); S(outStrokes, {1,0, 1,1}); S(outStrokes, {0,0.5, 1,0.5}); break;
			case "I": S(outStrokes, {0.2,0, 0.8,0}); S(outStrokes, {0.5,0, 0.5,1}); S(outStrokes, {0.2,1, 0.8,1}); break;
			case "J": S(outStrokes, {0.3,0, 0.9,0}); S(outStrokes, {0.7,0, 0.7,0.8, 0.5,1, 0.15,0.95, 0.05,0.75}); break;
			case "K": S(outStrokes, {0,0, 0,1}); S(outStrokes, {0.95,0, 0,0.55}); S(outStrokes, {0.35,0.4, 1,1}); break;
			case "L": S(outStrokes, {0,0, 0,1, 0.95,1}); break;
			case "M": S(outStrokes, {0,1, 0,0, 0.5,0.6, 1,0, 1,1}); break;
			case "N": S(outStrokes, {0,1, 0,0, 1,1, 1,0}); break;
			case "O": S(outStrokes, {0.5,0, 0.1,0.15, 0,0.5, 0.1,0.85, 0.5,1, 0.9,0.85, 1,0.5, 0.9,0.15, 0.5,0}); break;
			case "P": S(outStrokes, {0,1, 0,0, 0.75,0.05, 0.9,0.25, 0.75,0.5, 0,0.52}); break;
			case "Q": S(outStrokes, {0.5,0, 0.1,0.15, 0,0.5, 0.1,0.85, 0.5,1, 0.9,0.85, 1,0.5, 0.9,0.15, 0.5,0}); S(outStrokes, {0.6,0.7, 1,1.05}); break;
			case "R": S(outStrokes, {0,1, 0,0, 0.75,0.05, 0.9,0.25, 0.75,0.5, 0,0.52}); S(outStrokes, {0.45,0.52, 1,1}); break;
			case "S": S(outStrokes, {0.9,0.12, 0.5,0, 0.1,0.12, 0.1,0.38, 0.9,0.62, 0.9,0.88, 0.5,1, 0.1,0.88}); break;
			case "T": S(outStrokes, {0,0, 1,0}); S(outStrokes, {0.5,0, 0.5,1}); break;
			case "U": S(outStrokes, {0,0, 0,0.8, 0.2,1, 0.8,1, 1,0.8, 1,0}); break;
			case "V": S(outStrokes, {0,0, 0.5,1, 1,0}); break;
			case "W": S(outStrokes, {0,0, 0.2,1, 0.5,0.45, 0.8,1, 1,0}); break;
			case "X": S(outStrokes, {0,0, 1,1}); S(outStrokes, {1,0, 0,1}); break;
			case "Y": S(outStrokes, {0,0, 0.5,0.5, 1,0}); S(outStrokes, {0.5,0.5, 0.5,1}); break;
			case "Z": S(outStrokes, {0,0, 1,0, 0,1, 1,1}); break;
			case "0": S(outStrokes, {0.5,0, 0.1,0.15, 0,0.5, 0.1,0.85, 0.5,1, 0.9,0.85, 1,0.5, 0.9,0.15, 0.5,0}); break;
			case "1": S(outStrokes, {0.2,0.25, 0.55,0, 0.55,1}); S(outStrokes, {0.2,1, 0.9,1}); break;
			case "2": S(outStrokes, {0.05,0.2, 0.4,0, 0.85,0.05, 0.95,0.3, 0.05,1, 1,1}); break;
			case "3": S(outStrokes, {0.05,0.1, 0.6,0, 0.9,0.2, 0.55,0.45, 0.95,0.7, 0.6,1, 0.05,0.9}); break;
			case "4": S(outStrokes, {0.7,1, 0.7,0, 0,0.7, 1,0.7}); break;
			case "5": S(outStrokes, {0.95,0, 0.1,0, 0.05,0.45, 0.6,0.4, 0.95,0.6, 0.9,0.9, 0.5,1, 0.05,0.9}); break;
			case "6": S(outStrokes, {0.85,0.05, 0.35,0, 0.05,0.35, 0.05,0.8, 0.4,1, 0.85,0.9, 0.95,0.65, 0.6,0.45, 0.1,0.55}); break;
			case "7": S(outStrokes, {0,0, 1,0, 0.4,1}); break;
			case "8": S(outStrokes, {0.5,0.47, 0.12,0.32, 0.15,0.08, 0.5,0, 0.85,0.08, 0.88,0.32, 0.5,0.47, 0.1,0.65, 0.12,0.92, 0.5,1, 0.88,0.92, 0.9,0.65, 0.5,0.47}); break;
			case "9": S(outStrokes, {0.15,0.95, 0.65,1, 0.95,0.65, 0.95,0.2, 0.6,0, 0.15,0.1, 0.05,0.35, 0.4,0.55, 0.9,0.45}); break;
		}
	}
}
