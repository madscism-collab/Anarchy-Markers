// Заливка (flood fill) для малювання на мапі. Виконується НА КЛІЄНТІ один раз на клік:
//   1) растеризуємо всі видимі малюнки (штрихи з їх товщиною + наявні заливки) у сітку-перешкоди
//      навколо точки кліку;
//   2) BFS-розлив від кліку: не проходить крізь перешкоди; якщо дотік до краю вікна — пробуємо
//      більше вікно (half 32→64→128→256→512→1024→2048→4096 м); якщо тече й з найбільшого —
//      область не замкнута;
//   3) якщо розлив уперся в бюджет клітинок — теж ідемо на більше вікно (грубші клітинки беруть
//      ту саму площу дешевше); ЧАСТКОВО заливаємо лише коли бюджет ліг на найбільшому вікні —
//      наступний клік дофарбує решту, впершись у цю заливку як у перешкоду;
//   4) контур залитих клітинок обводимо (wall-follower), переводимо у світові метри та
//      спрощуємо RDP до ліміту точок штриха. Результат — полігон-контур для SM_MapDrawingData(fill=1).
//
// Тут же: point-in-polygon і тріангуляція (ear clipping) — статичні утиліти для рендера/хіт-тестів.

class SM_FloodFillResult
{
	ref array<int> m_aContour = {};	// x,z (метри), парами; замкнутий контур БЕЗ дубля першої точки
	bool m_bPartial;				// true = уперлись у бюджет, залито частину області
}

class SM_MapFloodFill
{
	// Коди результату Compute
	static const int OK          = 0;
	static const int NOT_CLOSED  = 1;	// область не замкнута (витекло за найбільше вікно)
	static const int NO_SPACE    = 2;	// клік у перешкоді (на лінії/в чужій заливці)
	static const int FAILED      = 3;	// внутрішня помилка (не мало б траплятись)

	// 512, не 256: на найбільших вікнах клітинка була 32 м, а штамп лінії роздувається на
	// (пів-ширини + пів-діагоналі клітинки) ≈ +22 м/бік — тонкий пензель ставав бар'єром ~47 м
	// завширшки й замуровував будь-який вужчий коридор (незалиті виїмки у складних областях).
	// Удвічі дрібніша клітинка вдвічі звужує роздування; ціна — вчетверо більше клітинок на клік,
	// але лише коли заливка справді велика (дрібні кишені беруть перше, найдрібніше вікно).
	protected static const int GRID_DIM   = 512;	// сітка завжди GRID_DIM×GRID_DIM клітинок
	protected static const int MAX_VISIT  = 208000;	// бюджет розливу (∝ площі: ×4 разом із клітинками); далі — часткова заливка
	protected static const int MAX_TRACE  = 600000;	// запобіжник обходу контуру

	//! obstacles — усі видимі малюнки клієнта (штрихи і заливки). maxPts — ліміт точок контуру.
	//! highDetail — режим «enhanced fill» (клієнтське налаштування): ущільнює джерело снапу, щоб
	//! криві лягали ідеально. Вимкнено (дефолт) — снап по грубшому контуру: усі фікси лишаються, але
	//! точок мало, тож тріангуляція дешева й немає фрізу при розміщенні великих складних заливок.
	static int Compute(int clickX, int clickZ, notnull array<SM_MapDrawingData> obstacles, int maxPts, bool highDetail, out SM_FloodFillResult res)
	{
		res = new SM_FloodFillResult();

		// Одна алокація сітки на весь виклик — не по одній на кожне вікно драбини. Обнуляється
		// перед кожною спробою (нижче); клітинок GRID_DIM² завжди, тож reuse економить саме алокації.
		array<int> grid = {};
		grid.Resize(GRID_DIM * GRID_DIM);

		// Відстань від кліку до НАЙБЛИЖЧОЇ лінії. Вікно, менше за неї, гарантовано витікає (навколо
		// кліку суцільна вільна зона аж до краю вікна), тож будувати й заливати його — чистий програш.
		// Пропуск цих вікон прибирає секундний фріз на великих заливках (клік у центрі великої області
		// далеко від ліній → 6-7 марних повнорозмірних BFS-розливів). Малим кишеням лінія поруч, тож
		// вони нічого не пропускають і зберігають найдрібнішу сітку.
		float nearest = NearestObstacleDist(clickX, clickZ, obstacles);

		// Windows: half-size in metres, cell = (2*half)/GRID_DIM (0.125 -> 0.25 -> ... -> 16 m).
		// We escalate while the paint runs off the window edge. The ladder STARTS SMALL on purpose:
		// small pockets used to be filled on a coarse grid, where the obstacle stamp (line half-width +
		// cell half-diagonal, needed to stay watertight) ate most of a 10 m pocket's interior, the
		// outline stepped in whole cells, and the free-cell hunt could jump clear out of the pocket.
		// At sub-metre cells all three problems disappear; anything the small window can't hold escapes
		// and climbs the ladder. The largest window covers ~8.2 km across (biggest fillable area
		// ~53 km²); its stepped outline is pulled back onto the real lines by SnapToStrokes below.
		for (int attempt = 0; attempt < 8; attempt++)
		{
			int half = 32;
			if (attempt == 1)
				half = 64;
			else if (attempt == 2)
				half = 128;
			else if (attempt == 3)
				half = 256;
			else if (attempt == 4)
				half = 512;
			else if (attempt == 5)
				half = 1024;
			else if (attempt == 6)
				half = 2048;
			else if (attempt == 7)
				half = 4096;

			// Provably escapes (free all the way to the border) — skip without touching the grid.
			// half*0.9 leaves a margin: the click is not exactly centred vs the nearest line, so a
			// window a touch larger than `nearest` can still be all-free on the near side.
			if (half * 0.9 < nearest && attempt < 7)
				continue;

			float cell = (2.0 * half) / GRID_DIM;
			int originX = clickX - half;	// світова позиція кутка сітки
			int originZ = clickZ - half;

			// Коди клітинок: 0 вільно, 1 перешкода-ЛІНІЯ, 4 перешкода-ЗАЛИВКА, 2 залито.
			// Лінії й заливки — різні коди, бо під лінії заливку «роздуваємо», а під чужі
			// заливки — ні (інакше сусідні заливки перекривались би).
			for (int i = 0; i < GRID_DIM * GRID_DIM; i++)
				grid[i] = 0;

			float maxHalfW;
			float minHalfW;
			StampObstacles(grid, originX, originZ, cell, obstacles, maxHalfW, minHalfW);
			if (minHalfW > 100)
				minHalfW = 2;	// вікно без жодного штриха — нейтральний дефолт

			int cx = (clickX - originX) / cell;
			int cz = (clickZ - originZ) / cell;
			if (cx < 0 || cz < 0 || cx >= GRID_DIM || cz >= GRID_DIM)
				return FAILED;
			if (grid[cz * GRID_DIM + cx] != 0)
			{
				// Клік у перешкоду (лінія/крапка/заливка під курсором) — як справжнє «відерце»,
				// шукаємо найближчу вільну клітинку й стартуємо з неї. Радіус — від ширини штампа,
				// не константа: на дрібних клітинках фіксовані 6 клітинок (1.5 м) не вибиралися б
				// із-під п'ятиметрового пензля, і клік по лінії давав NO_SPACE.
				int freeR = Math.Ceil((maxHalfW + cell * 0.7072) / cell) + 2;
				if (freeR < 6)
					freeR = 6;
				if (freeR > 96)
					freeR = 96;
				if (!FindFreeNear(grid, cx, cz, freeR))
					return NO_SPACE;
			}

			bool escaped;
			bool budgetHit;
			Flood(grid, cx, cz, escaped, budgetHit);

			if (escaped)
			{
				if (attempt < 7)
					continue;	// try a bigger window
				return NOT_CLOSED;
			}

			// Closed but too big for this resolution's cell budget: a coarser window takes the same
			// area whole. Partial fills are a last resort of the LARGEST window only — on the small
			// ones they would be a regression (a 100 m yard used to fill in one click on the 2 m grid).
			if (budgetHit && attempt < 7)
				continue;

			// Обводимо контур залитої області (по внутрішньому краю ліній — «сходинчастий»).
			array<int> corners = {};
			if (!TraceContour(grid, corners))
				return FAILED;

			// Кути сітки → світові метри.
			array<int> world = {};
			for (int k = 0; k < corners.Count(); k += 2)
			{
				world.Insert(originX + corners[k] * cell);
				world.Insert(originZ + corners[k + 1] * cell);
			}

			// Легкий RDP (точність пів-клітинки) — прибирає дрібний зигзаг сходинок, ЗБЕРІГАЮЧИ форму
			// (зокрема кривину ліній). База для растрового запасного варіанту нижче.
			array<int> coarse = SM_PolylineUtil.RDPSimplify(world, cell * 0.5);
			if (coarse.Count() < 6)
				coarse = world;

			// Джерело для СНАПУ: контур треба не спростити, а УЩІЛЬНИТИ. Растр має роздільність
			// клітинки (до 32 м на великих вікнах), а RDP уміє лише ВИДАЛЯТИ вершини — тож на кривій
			// лінії снап отримував опорні точки раз на клітинку й тягнув між ними пряму хорду:
			// фестони на дугах, які не прибирався жодним лімітом точок (бюджет тут просто не досягався).
			// Densify вставляє проміжні вершини кроком snapStep — тепер на кривій є що притягувати
			// кожні кілька метрів, і контур лягає по дузі. Крок масштабуємо від найтоншого штриха
			// (тонша лінія — щільніша дуга), із запобіжником на к-сть точок для велетенських периметрів.
			// High-detail: densify the source so curves get a vertex every few metres to snap onto.
			// Simple mode: snap the coarse contour as-is — the same edge-snap/wedge/narrow-channel
			// logic runs, just on fewer points, so the outline is a touch blockier on tight curves but
			// the triangulation stays small and placement never hitches.
			array<int> snapSrc = coarse;
			if (highDetail)
			{
				float snapStep = Math.Clamp(minHalfW, 2.0, 8.0);
				snapSrc = Densify(world, snapStep, 6000);
				if (snapSrc.Count() < 6)
					snapSrc = coarse;
			}

			float epsFinal = maxHalfW * 0.4;
			if (epsFinal < 1) epsFinal = 1;
			if (epsFinal > cell) epsFinal = cell;

			// Ladder, best first. Every rung is checked by triangulation (= the polygon is simple);
			// the first one that passes wins. All-or-nothing here was why busy regions kept coming out
			// stepped: ONE bad crossing anywhere threw the whole snapped contour away.
			//   A: snap to stroke edges + wedge-tip corners  (hugs everything, sharp corners filled)
			//   B: snap only                                 (a tip crossed something nearby — drop tips)
			//   C: raw raster outline                        (stepped; guaranteed simple)
			array<int> snappedBase = {};
			snappedBase.Copy(snapSrc);
			array<float> vSegs = {};
			array<int> vHasSeg = {};
			SnapToStrokes(snappedBase, obstacles, maxHalfW + cell * 3, vSegs, vHasSeg);

			// Post-snap RDP is about the POINT BUDGET, not smoothing: snapped points are exact, and a
			// coarse epsilon here was re-cutting the very curve detail the dense snap source exists
			// for. Scaled to the thinnest boundary stroke (same reasoning as srcSag above), floored at
			// 0.5 — below the int-metre rounding jitter there is nothing left to preserve. Straight
			// runs are collinear and fold regardless; FinalizeContour escalates if maxPts says too many.
			float epsSnap = Math.Clamp(minHalfW * 0.5, 0.5, 1.0);

			array<int> snapped = {};
			snapped.Copy(snappedBase);
			InsertWedgeTips(snapped, vSegs, vHasSeg);	// добудувати вершини гострих кутів (див. функцію)
			snapped = FinalizeContour(snapped, epsSnap, maxPts);
			array<int> tmpTri = {};
			if (snapped.Count() >= 6 && Triangulate(snapped, tmpTri))
			{
				res.m_aContour = snapped;
				res.m_bPartial = budgetHit;
				return OK;
			}

			array<int> noTips = FinalizeContour(snappedBase, epsSnap, maxPts);
			if (noTips.Count() >= 6 && Triangulate(noTips, tmpTri))
			{
				res.m_aContour = noTips;
				res.m_bPartial = budgetHit;
				return OK;
			}

			array<int> plain = FinalizeContour(coarse, epsFinal, maxPts);
			if (plain.Count() >= 6 && Triangulate(plain, tmpTri))
			{
				res.m_aContour = plain;
				res.m_bPartial = budgetHit;
				return OK;
			}
			return FAILED;
		}
		return FAILED;
	}

	//------------------------------------------------------------------------------
	// Растеризація перешкод: штрихи — сегменти з половиною ширини, крапки — кола,
	// заливки — нутро полігона (лише клітинки з центром строго всередині: нова заливка
	// трохи перекриє край старої — шов без дірок).
	protected static void StampObstacles(array<int> grid, int originX, int originZ, float cell, array<SM_MapDrawingData> obstacles, out float maxHalfW, out float minHalfW)
	{
		maxHalfW = 0;
		minHalfW = 1000;	// callers clamp anyway; stays huge only when no stroke got stamped
		int winMaxX = originX + GRID_DIM * cell;
		int winMaxZ = originZ + GRID_DIM * cell;

		foreach (SM_MapDrawingData d : obstacles)
		{
			if (!d || d.GetPointCount() < 1)
				continue;
			if (!d.AABBOverlapsRect(originX, winMaxX, originZ, winMaxZ, 64))
				continue;

			// A parametric shape stamps its REAL lines, not its two parameter points — that is what lets
			// a click seal inside a circle, a rectangle, or one cell of a grid. Every line of the shape
			// is a boundary, so a grid's inner lines split it into fillable cells for free.
			if (d.m_iShape != 0)
			{
				float shalf = SM_DrawCanvas.WidthMeters(d.m_iWidthIdx) * 0.5;
				array<ref SM_ShapeLine> lines = {};
				SM_ShapeGeometry.Build(d.m_iShape, d.m_aPoints, SM_DrawCanvas.WidthMeters(d.m_iWidthIdx), lines);
				foreach (SM_ShapeLine sl : lines)
				{
					if (!sl || sl.m_aPts.Count() < 4)
						continue;
					float slh = sl.m_fWidthMeters * 0.5;
					if (slh > maxHalfW)
						maxHalfW = slh;
					if (slh < minHalfW)
						minHalfW = slh;
					float sr = slh + cell * 0.7072;
					for (int si = 2; si < sl.m_aPts.Count(); si += 2)
						StampSegment(grid, originX, originZ, cell, sl.m_aPts[si - 2], sl.m_aPts[si - 1], sl.m_aPts[si], sl.m_aPts[si + 1], sr);
				}
				continue;
			}

			if (d.m_iFill != 0)
			{
				StampPolygon(grid, originX, originZ, cell, d);	// заливка → код 4
				continue;
			}

			float half = SM_DrawCanvas.WidthMeters(d.m_iWidthIdx) * 0.5;
			if (half > maxHalfW)
				maxHalfW = half;
			if (half < minHalfW)
				minHalfW = half;

			int n = d.GetPointCount();
			// The stamp marks cells whose CENTRE lies within r of the segment. For a line to seal
			// every cell it crosses, r must be at least the cell half-diagonal (cell*sqrt(2)/2) —
			// the centre of a crossed cell can never be further away than that. With a smaller r a
			// thin diagonal line leaves unstamped cells and the fill leaks straight through it,
			// which the coarse grids of the large windows make very easy to hit.
			float r = half + cell * 0.7072;
			if (n == 1)
			{
				int px, pz;
				d.GetPoint(0, px, pz);
				StampSegment(grid, originX, originZ, cell, px, pz, px, pz, r);
				continue;
			}
			int ax, az;
			d.GetPoint(0, ax, az);
			for (int i = 1; i < n; i++)
			{
				int bx, bz;
				d.GetPoint(i, bx, bz);
				StampSegment(grid, originX, originZ, cell, ax, az, bx, bz, r);
				ax = bx;
				az = bz;
			}
		}
	}

	//! Позначити перешкодою всі клітинки, чий центр ближче r до відрізка a→b (світові метри).
	protected static void StampSegment(array<int> grid, int originX, int originZ, float cell, float ax, float az, float bx, float bz, float r)
	{
		// Межі відрізка в клітинках (+радіус)
		float minX = ax; if (bx < minX) minX = bx;
		float maxX = ax; if (bx > maxX) maxX = bx;
		float minZ = az; if (bz < minZ) minZ = bz;
		float maxZ = az; if (bz > maxZ) maxZ = bz;

		int c0 = (minX - r - originX) / cell; if (c0 < 0) c0 = 0;
		int c1 = (maxX + r - originX) / cell; if (c1 >= GRID_DIM) c1 = GRID_DIM - 1;
		int r0 = (minZ - r - originZ) / cell; if (r0 < 0) r0 = 0;
		int r1 = (maxZ + r - originZ) / cell; if (r1 >= GRID_DIM) r1 = GRID_DIM - 1;
		if (c1 < c0 || r1 < r0)
			return;

		float rSq = r * r;
		float dx = bx - ax;
		float dz = bz - az;
		float lenSq = dx * dx + dz * dz;

		for (int gz = r0; gz <= r1; gz++)
		{
			float pz = originZ + (gz + 0.5) * cell;
			int rowBase = gz * GRID_DIM;
			for (int gx = c0; gx <= c1; gx++)
			{
				float px = originX + (gx + 0.5) * cell;
				// відстань² точки до відрізка
				float t = 0;
				if (lenSq > 0.0001)
				{
					t = ((px - ax) * dx + (pz - az) * dz) / lenSq;
					if (t < 0) t = 0;
					else if (t > 1) t = 1;
				}
				float ex = px - (ax + t * dx);
				float ez = pz - (az + t * dz);
				if (ex * ex + ez * ez <= rSq)
					grid[rowBase + gx] = 1;
			}
		}
	}

	//! Позначити перешкодою клітинки, чий центр всередині полігона заливки (even-odd, сканлайн).
	protected static void StampPolygon(array<int> grid, int originX, int originZ, float cell, SM_MapDrawingData d)
	{
		int n = d.GetPointCount();
		if (n < 3)
			return;

		int minXi, maxXi, minZi, maxZi;
		if (!d.GetAABB(minXi, maxXi, minZi, maxZi))
			return;
		int r0 = (minZi - originZ) / cell; if (r0 < 0) r0 = 0;
		int r1 = (maxZi - originZ) / cell; if (r1 >= GRID_DIM) r1 = GRID_DIM - 1;

		array<float> xs = {};
		for (int gz = r0; gz <= r1; gz++)
		{
			float pz = originZ + (gz + 0.5) * cell;
			xs.Clear();

			int px1, pz1;
			d.GetPoint(n - 1, px1, pz1);
			for (int i = 0; i < n; i++)
			{
				int px2, pz2;
				d.GetPoint(i, px2, pz2);
				// перетин ребра px1,pz1 → px2,pz2 з горизонталлю pz
				if ((pz1 <= pz && pz2 > pz) || (pz2 <= pz && pz1 > pz))
				{
					float t = (pz - pz1) / (pz2 - pz1);
					xs.Insert(px1 + t * (px2 - px1));
				}
				px1 = px2;
				pz1 = pz2;
			}
			if (xs.Count() < 2)
				continue;
			xs.Sort();

			int rowBase = gz * GRID_DIM;
			for (int k = 0; k + 1 < xs.Count(); k += 2)
			{
				int c0 = (xs[k]     - originX) / cell + 1;	// строго всередині (центр правіше входу)
				int c1 = (xs[k + 1] - originX) / cell - 1;
				if (c0 < 0) c0 = 0;
				if (c1 >= GRID_DIM) c1 = GRID_DIM - 1;
				for (int gx = c0; gx <= c1; gx++)
				{
					float cxw = originX + (gx + 0.5) * cell;
					if (cxw > xs[k] && cxw < xs[k + 1])
						grid[rowBase + gx] = 4;	// код заливки (не роздуваємо під неї)
				}
			}
		}
	}

	//------------------------------------------------------------------------------
	//! Фінальне спрощення контуру: RDP з ескалацією eps до ліміту точок + прибирання дубля-замикання.
	//! Вставити проміжні вершини так, щоб сусідні були не далі step одна від одної (замкнутий контур).
	//! Протилежність RDP: не спрощує, а НАРОЩУЄ щільність — щоб снап мав що притягувати на кривій, чиї
	//! опорні точки растр дав рідко (крок клітинки). maxPts обмежує вартість: на велетенському периметрі
	//! крок збільшується, аби не наплодити десятки тисяч точок.
	protected static array<int> Densify(array<int> pts, float step, int maxPts)
	{
		int n = pts.Count() / 2;
		array<int> outp = {};
		if (n < 2 || step < 0.5)
		{
			outp.Copy(pts);
			return outp;
		}

		// Перевести крок під бюджет: якщо периметр/step перевищує maxPts — грубший крок.
		float perim = 0;
		for (int i = 0; i < n; i++)
		{
			int j = (i + 1) % n;
			float dx = pts[j * 2] - pts[i * 2];
			float dz = pts[j * 2 + 1] - pts[i * 2 + 1];
			perim += Math.Sqrt(dx * dx + dz * dz);
		}
		if (maxPts > 0 && perim / step > maxPts)
			step = perim / maxPts;

		for (int i = 0; i < n; i++)
		{
			int j = (i + 1) % n;
			int ax = pts[i * 2],     az = pts[i * 2 + 1];
			int bx = pts[j * 2],     bz = pts[j * 2 + 1];
			outp.Insert(ax);
			outp.Insert(az);

			float dx = bx - ax;
			float dz = bz - az;
			float len = Math.Sqrt(dx * dx + dz * dz);
			int sub = Math.Floor(len / step);	// скільки проміжних вставити
			for (int k = 1; k <= sub; k++)
			{
				float t = (k * step) / len;
				if (t >= 1.0)
					break;
				outp.Insert(Math.Round(ax + dx * t));
				outp.Insert(Math.Round(az + dz * t));
			}
		}
		return outp;
	}

	protected static array<int> FinalizeContour(array<int> pts, float eps, int maxPts)
	{
		array<int> s = SM_PolylineUtil.RDPSimplify(pts, eps);
		float e = eps;
		int guard = 0;
		while (s.Count() / 2 > maxPts && guard < 12)
		{
			e = e * 1.6;
			s = SM_PolylineUtil.RDPSimplify(pts, e);
			guard++;
		}
		int nS = s.Count();
		if (nS >= 8 && s[0] == s[nS - 2] && s[1] == s[nS - 1])
		{
			s.Remove(nS - 1);
			s.Remove(nS - 2);
		}
		return s;
	}

	//! Притягнути кожну вершину контуру на найближчу точку осі лінії-межі (в межах maxSnapDist).
	//! Растровий контур лягає точно на реальні штрихи → гладко, кути збережені, не виходить за межі.
	//! Заливки (m_iFill) як межі для притягання НЕ беремо — інакше нова заливка «прилипла» б до сусідньої.
	//! outSegs (4 float на вершину: ax,az,bx,bz) + outHasSeg (0/1) кажуть, на ЯКИЙ сегмент сіла кожна
	//! вершина — InsertWedgeTips по цьому знаходить сусідів на різних лініях і добудовує вершину кута.
	protected static void SnapToStrokes(array<int> contour, array<SM_MapDrawingData> obstacles, float maxSnapDist, out array<float> outSegs, out array<int> outHasSeg)
	{
		int n = contour.Count() / 2;
		float maxSq = maxSnapDist * maxSnapDist;
		int snapMargin = Math.Round(maxSnapDist) + 2;

		outSegs = {};
		outSegs.Resize(n * 4);
		outHasSeg = {};
		outHasSeg.Resize(n);

		for (int i = 0; i < n; i++)
		{
			int px = contour[i * 2];
			int pz = contour[i * 2 + 1];
			float bestSq = maxSq;
			float bestX = px;
			float bestZ = pz;
			bool found = false;
			bool haveSeg = false;
			float segAx, segAz, segBx, segBz;
			float bestHalfW = 0;

			foreach (SM_MapDrawingData d : obstacles)
			{
				if (!d || d.m_iFill != 0)
					continue;
				int cnt = d.GetPointCount();
				if (cnt < 1)
					continue;
				if (!d.AABBOverlapsRect(px, px, pz, pz, snapMargin))
					continue;

				int ax, az;
				d.GetPoint(0, ax, az);
				if (cnt == 1)
				{
					float ddx = px - ax;
					float ddz = pz - az;
					float dsq = ddx * ddx + ddz * ddz;
					if (dsq < bestSq) { bestSq = dsq; bestX = ax; bestZ = az; found = true; haveSeg = false; }
					continue;
				}
				for (int s = 1; s < cnt; s++)
				{
					int bx, bz;
					d.GetPoint(s, bx, bz);
					float cxo, czo;
					float dsq = ClosestOnSeg(px, pz, ax, az, bx, bz, cxo, czo);
					if (dsq < bestSq)
					{
						bestSq = dsq; bestX = cxo; bestZ = czo; found = true;
						haveSeg = true;
						segAx = ax; segAz = az; segBx = bx; segBz = bz;
						bestHalfW = SM_DrawCanvas.WidthMeters(d.m_iWidthIdx) * 0.5;
					}
					ax = bx;
					az = bz;
				}
			}

			outHasSeg[i] = 0;
			if (!found)
				continue;

			// The snap target is NOT the axis: it is the axis pushed 70% of the half-width toward the
			// vertex's own side. On the axis itself, the two banks of a contour wrapping a dead-end
			// line inside the region collapsed onto the same centerline — a degenerate overlap, the
			// main reason the snapped contour failed triangulation on busy regions and everything
			// fell back to the stepped raster outline. At 70% the banks stay a stroke-width apart
			// and the seam still hides under the stroke's paint (which extends to 100%).
			float tx = bestX;
			float tz = bestZ;
			if (haveSeg)
			{
				float ex = segBx - segAx;
				float ez = segBz - segAz;
				float elen = Math.Sqrt(ex * ex + ez * ez);
				if (elen > 0.05)
				{
					float perpX = -ez / elen;
					float perpZ =  ex / elen;
					float sideDot = (px - bestX) * perpX + (pz - bestZ) * perpZ;
					if (sideDot < -0.01 || sideDot > 0.01)	// on-axis vertex keeps the axis point
					{
						float side = 1;
						if (sideDot < 0)
							side = -1;
						float off = bestHalfW * 0.7;
						tx = bestX + perpX * side * off;
						tz = bestZ + perpZ * side * off;
						// The stored segment is offset the same way: wedge tips then come out as the
						// intersection of the two OFFSET edges — the corner of the free region, not of
						// the axes — and the two banks of one line read as parallel (no bogus tips).
						segAx += perpX * side * off;
						segAz += perpZ * side * off;
						segBx += perpX * side * off;
						segBz += perpZ * side * off;
					}
				}
			}

			contour[i * 2]     = Math.Round(tx);
			contour[i * 2 + 1] = Math.Round(tz);
			if (haveSeg)
			{
				outHasSeg[i] = 1;
				outSegs[i * 4]     = segAx;
				outSegs[i * 4 + 1] = segAz;
				outSegs[i * 4 + 2] = segBx;
				outSegs[i * 4 + 3] = segBz;
			}
		}
	}

	//! Гострі внутрішні кути. Растр фізично не дістає у вершину клина: штампи двох ліній
	//! (півширина + півдіагональ клітинки) перекриваються глибоко всередину кута, тому контур
	//! зупиняється біля «рота» клина, а снап садить його сусідні вершини на ДВІ РІЗНІ лінії —
	//! хорда між ними зрізає кут трикутною дірою (виразно видно на перетинах штрихів). Тут між
	//! такою парою вставляється перетин осей їхніх сегментів — справжня вершина кута. Чиста
	//! геометрія по снапнутих сегментах, тож працює на будь-якій роздільності растру, включно з
	//! грубими вікнами великих заливок. Опуклий злам однієї лінії проходить ту саму умову
	//! (сусідні сегменти одного штриха) і теж лягає точно на злам — так і треба.
	protected static void InsertWedgeTips(array<int> contour, array<float> segs, array<int> hasSeg)
	{
		int n = contour.Count() / 2;
		if (n < 3 || hasSeg.Count() != n)
			return;

		array<int> outPts = {};
		for (int i = 0; i < n; i++)
		{
			int j = (i + 1) % n;
			outPts.Insert(contour[i * 2]);
			outPts.Insert(contour[i * 2 + 1]);

			if (hasSeg[i] == 0 || hasSeg[j] == 0)
				continue;

			float ax = segs[i * 4],     az = segs[i * 4 + 1];
			float bx = segs[i * 4 + 2], bz = segs[i * 4 + 3];
			float cx = segs[j * 4],     cz = segs[j * 4 + 1];
			float dxx = segs[j * 4 + 2], dzz = segs[j * 4 + 3];

			if (ax == cx && az == cz && bx == dxx && bz == dzz)
				continue;	// той самий сегмент — кута нема

			float e1x = bx - ax, e1z = bz - az;
			float e2x = dxx - cx, e2z = dzz - cz;
			float len1 = Math.Sqrt(e1x * e1x + e1z * e1z);
			float len2 = Math.Sqrt(e2x * e2x + e2z * e2z);
			if (len1 < 0.1 || len2 < 0.1)
				continue;

			// Майже паралельні осі перетинаються за кілометри — це не клин. sin кута < ~5.7° ріжемо.
			float den = e1x * e2z - e1z * e2x;
			if (Math.AbsFloat(den) < 0.1 * len1 * len2)
				continue;

			float t = ((cx - ax) * e2z - (cz - az) * e2x) / den;
			float s = ((cx - ax) * e1z - (cz - az) * e1x) / den;

			// Вершина клина мусить лежати на обох сегментах (з випуском ~4 м за кінці).
			float ext1 = 4.0 / len1;
			float ext2 = 4.0 / len2;
			if (t < -ext1 || t > 1 + ext1 || s < -ext2 || s > 1 + ext2)
				continue;

			float tipX = ax + t * e1x;
			float tipZ = az + t * e1z;

			// Глибина обмежена шириною «рота»: 8×рот покриває кути до ~7°, гостріше — не малюємо
			// (разом із відсіканням паралельних це страхує від шпичаків у нескінченність).
			float mx = contour[j * 2] - contour[i * 2];
			float mz = contour[j * 2 + 1] - contour[i * 2 + 1];
			float cap = Math.Sqrt(mx * mx + mz * mz) * 8 + 16;
			float d1x = tipX - contour[i * 2], d1z = tipZ - contour[i * 2 + 1];
			float d2x = tipX - contour[j * 2], d2z = tipZ - contour[j * 2 + 1];
			float capSq = cap * cap;
			if (d1x * d1x + d1z * d1z > capSq || d2x * d2x + d2z * d2z > capSq)
				continue;
			if (d1x * d1x + d1z * d1z < 1 || d2x * d2x + d2z * d2z < 1)
				continue;	// вершина і так уже там — дубль не потрібен

			outPts.Insert(Math.Round(tipX));
			outPts.Insert(Math.Round(tipZ));
		}
		contour.Copy(outPts);
	}

	//! Найближча точка на відрізку a→b до p; повертає відстань² і саму точку (out cx,cz).
	protected static float ClosestOnSeg(float px, float pz, float ax, float az, float bx, float bz, out float cx, out float cz)
	{
		float dx = bx - ax;
		float dz = bz - az;
		float lenSq = dx * dx + dz * dz;
		float t = 0;
		if (lenSq > 0.0001)
		{
			t = ((px - ax) * dx + (pz - az) * dz) / lenSq;
			if (t < 0) t = 0;
			else if (t > 1) t = 1;
		}
		cx = ax + t * dx;
		cz = az + t * dz;
		float ex = px - cx;
		float ez = pz - cz;
		return ex * ex + ez * ez;
	}

	//! Відстань (метри) від точки до найближчої перешкоди — будь-що, що спиняє розлив: штрих, лінія
	//! фігури, контур чужої заливки. Використовується для пропуску завідомо-витікаючих вікон (див.
	//! Compute). Оцінка «згори» лише зменшила б роздільність, не зіпсувала б заливку, тож рахуємо все.
	protected static float NearestObstacleDist(int px, int pz, array<SM_MapDrawingData> obstacles)
	{
		float bestSq = 1000000000000.0;
		foreach (SM_MapDrawingData d : obstacles)
		{
			if (!d)
				continue;
			int cnt = d.GetPointCount();
			if (cnt < 1)
				continue;

			// AABB-відсів: перешкода, чий бокс далі за поточний мінімум, не може його побити.
			// Пропускаємо, доки мінімум ще «нескінченний» (перша перешкода його й задасть).
			if (bestSq < 100000000000.0)
			{
				int margin = Math.Ceil(Math.Sqrt(bestSq)) + 1;
				if (!d.AABBOverlapsRect(px, px, pz, pz, margin))
					continue;
			}

			if (d.m_iShape != 0)
			{
				array<ref SM_ShapeLine> lines = {};
				SM_ShapeGeometry.Build(d.m_iShape, d.m_aPoints, SM_DrawCanvas.WidthMeters(d.m_iWidthIdx), lines);
				foreach (SM_ShapeLine sl : lines)
				{
					if (!sl || sl.m_aPts.Count() < 4)
						continue;
					for (int si = 2; si < sl.m_aPts.Count(); si += 2)
					{
						float scx, scz;
						float sdsq = ClosestOnSeg(px, pz, sl.m_aPts[si - 2], sl.m_aPts[si - 1], sl.m_aPts[si], sl.m_aPts[si + 1], scx, scz);
						if (sdsq < bestSq)
							bestSq = sdsq;
					}
				}
				continue;
			}

			int ax, az;
			d.GetPoint(0, ax, az);
			if (cnt == 1)
			{
				float ex = px - ax;
				float ez = pz - az;
				float dsq0 = ex * ex + ez * ez;
				if (dsq0 < bestSq)
					bestSq = dsq0;
				continue;
			}
			for (int i = 1; i < cnt; i++)
			{
				int bx, bz;
				d.GetPoint(i, bx, bz);
				float cx, cz;
				float dsq = ClosestOnSeg(px, pz, ax, az, bx, bz, cx, cz);
				if (dsq < bestSq)
					bestSq = dsq;
				ax = bx;
				az = bz;
			}
			// Контур заливки замкнутий — врахувати ребро «остання→перша».
			if (d.m_iFill != 0 && cnt >= 3)
			{
				int fx, fz;
				d.GetPoint(0, fx, fz);
				float cx2, cz2;
				float dsqC = ClosestOnSeg(px, pz, ax, az, fx, fz, cx2, cz2);
				if (dsqC < bestSq)
					bestSq = dsqC;
			}
		}
		return Math.Sqrt(bestSq);
	}

	//! BFS-розлив у 4 напрямки. escaped = дотекли до краю сітки; budgetHit = уперлись у MAX_VISIT.
	protected static void Flood(array<int> grid, int startX, int startZ, out bool escaped, out bool budgetHit)
	{
		escaped = false;
		budgetHit = false;

		array<int> queue = {};	// плаский індекс клітинки
		queue.Insert(startZ * GRID_DIM + startX);
		grid[startZ * GRID_DIM + startX] = 2;

		int head = 0;
		int visited = 1;
		while (head < queue.Count())
		{
			int idx = queue[head];
			head++;

			int gx = idx % GRID_DIM;
			int gz = idx / GRID_DIM;

			if (gx == 0 || gz == 0 || gx == GRID_DIM - 1 || gz == GRID_DIM - 1)
			{
				escaped = true;	// дотекли до краю вікна — область не замкнута в цьому вікні
				return;
			}
			if (visited >= MAX_VISIT)
			{
				budgetHit = true;	// бюджет — заливаємо те, що встигли
				return;
			}

			// 4-звʼязність (діагональні щілини не протікають)
			int ni = idx - 1;
			if (grid[ni] == 0) { grid[ni] = 2; queue.Insert(ni); visited++; }
			ni = idx + 1;
			if (grid[ni] == 0) { grid[ni] = 2; queue.Insert(ni); visited++; }
			ni = idx - GRID_DIM;
			if (grid[ni] == 0) { grid[ni] = 2; queue.Insert(ni); visited++; }
			ni = idx + GRID_DIM;
			if (grid[ni] == 0) { grid[ni] = 2; queue.Insert(ni); visited++; }
		}
	}

	//------------------------------------------------------------------------------
	//! Обхід зовнішнього контуру залитої області (grid==2) за годинниковою стрілкою,
	//! «тримаючись стіни» (область праворуч від напрямку руху). Точки — кути клітинок,
	//! емітяться лише на поворотах. outCorners: cx,cz парами (координати кутової ґратки).
	protected static bool TraceContour(array<int> grid, array<int> outCorners)
	{
		// Стартова клітинка: найменший рядок, у ньому найменший стовпець.
		int sx = -1, sz = -1;
		for (int i = 0; i < GRID_DIM * GRID_DIM; i++)
		{
			if (grid[i] == 2)
			{
				sx = i % GRID_DIM;
				sz = i / GRID_DIM;
				break;
			}
		}
		if (sx < 0)
			return false;

		// Йдемо праворуч уздовж верхнього ребра стартової клітинки; область (клітинка) знизу-праворуч.
		int cx = sx, cz = sz;		// поточний кут
		int dirX = 1, dirZ = 0;		// напрямок руху
		int startCx = cx, startCz = cz, startDx = dirX, startDz = dirZ;

		int steps = 0;
		while (steps < MAX_TRACE)
		{
			steps++;

			// Кандидати напрямку у порядку: ПРАВОРУЧ (тримаємось стіни), ПРЯМО, ЛІВОРУЧ.
			// Ребро валідне лише коли область праворуч від руху, а зліва вільно —
			// тобто кожен крок іде рівно по межі (сідлові конфігурації розв'язуються самі).
			bool moved = false;
			for (int c = 0; c < 3; c++)
			{
				int newDx, newDz;
				if (c == 0)      { newDx = -dirZ; newDz = dirX;  }	// поворот праворуч
				else if (c == 1) { newDx = dirX;  newDz = dirZ;  }	// прямо
				else             { newDx = dirZ;  newDz = -dirX; }	// поворот ліворуч

				int leftX, leftZ, rightX, rightZ;
				if (newDx == 1)       { leftX = cx;     leftZ = cz - 1; rightX = cx;     rightZ = cz;     }
				else if (newDx == -1) { leftX = cx - 1; leftZ = cz;     rightX = cx - 1; rightZ = cz - 1; }
				else if (newDz == 1)  { leftX = cx;     leftZ = cz;     rightX = cx - 1; rightZ = cz;     }
				else                  { leftX = cx - 1; leftZ = cz - 1; rightX = cx;     rightZ = cz - 1; }

				if (!CellFilled(grid, rightX, rightZ) || CellFilled(grid, leftX, leftZ))
					continue;	// не межове ребро

				// Замкнулись: збираємось удруге пройти СТАРТОВЕ ребро (той самий кут і напрямок).
				// Перевірка саме ПЕРЕД кроком: у стартовий кут обхід прибуває з іншим напрямком,
				// тож перевірка «після кроку» не спрацьовує ніколи.
				if (steps > 1 && cx == startCx && cz == startCz && newDx == startDx && newDz == startDz)
				{
					// Якщо в стартовому куті є поворот — це справжня вершина полігона, не загубити її.
					if (newDx != dirX || newDz != dirZ)
					{
						outCorners.Insert(cx);
						outCorners.Insert(cz);
					}
					return outCorners.Count() >= 6;
				}

				if (newDx != dirX || newDz != dirZ)
				{
					outCorners.Insert(cx);	// кут на повороті
					outCorners.Insert(cz);
					dirX = newDx;
					dirZ = newDz;
				}
				cx += dirX;
				cz += dirZ;
				moved = true;
				break;
			}
			if (!moved)
				return false;	// нема валідного ребра (не мало б статись на звʼязній області)
		}
		return false;
	}

	//! Знайти найближчу вільну клітинку в квадраті ±radius навколо (cx,cz). true → cx/cz оновлені.
	protected static bool FindFreeNear(array<int> grid, inout int cx, inout int cz, int radius)
	{
		int bestX = -1, bestZ = -1;
		int bestDistSq = 0;
		for (int dz = -radius; dz <= radius; dz++)
		{
			int gz = cz + dz;
			if (gz < 1 || gz >= GRID_DIM - 1)
				continue;
			for (int dx = -radius; dx <= radius; dx++)
			{
				int gx = cx + dx;
				if (gx < 1 || gx >= GRID_DIM - 1)
					continue;
				if (grid[gz * GRID_DIM + gx] != 0)
					continue;
				int dsq = dx * dx + dz * dz;
				if (bestX < 0 || dsq < bestDistSq)
				{
					bestX = gx;
					bestZ = gz;
					bestDistSq = dsq;
				}
			}
		}
		if (bestX < 0)
			return false;
		cx = bestX;
		cz = bestZ;
		return true;
	}

	protected static bool CellFilled(array<int> grid, int gx, int gz)
	{
		if (gx < 0 || gz < 0 || gx >= GRID_DIM || gz >= GRID_DIM)
			return false;
		return grid[gz * GRID_DIM + gx] == 2;
	}

	//------------------------------------------------------------------------------
	// Утиліти для рендера/хіт-тестів заливок

	//! Точка всередині полігона (even-odd). pts — x,z парами (метри).
	static bool PointInPolygon(notnull array<int> pts, int px, int pz)
	{
		int n = pts.Count() / 2;
		if (n < 3)
			return false;
		bool inside = false;
		int j = n - 1;
		for (int i = 0; i < n; i++)
		{
			float xi = pts[i * 2];
			float zi = pts[i * 2 + 1];
			float xj = pts[j * 2];
			float zj = pts[j * 2 + 1];
			if ((zi > pz) != (zj > pz))
			{
				float xCross = xj + (pz - zj) / (zi - zj) * (xi - xj);
				if (px < xCross)
					inside = !inside;
			}
			j = i;
		}
		return inside;
	}

	//! Тріангуляція простого полігона (ear clipping) у СВІТОВИХ координатах. Індекси вершин
	//! лишаються валідними після афінної проєкції на екран (афінне зберігає опуклість).
	//! Повертає false, якщо полігон виродженний — тоді рендер хай малює PolygonDrawCommand.
	static bool Triangulate(notnull array<int> pts, notnull array<int> outIndices)
	{
		outIndices.Clear();
		int n = pts.Count() / 2;
		if (n < 3)
			return false;

		// Орієнтація (знакова площа): робимо список індексів у CCW-порядку.
		float area2 = 0;
		for (int i = 0; i < n; i++)
		{
			int j = (i + 1) % n;
			area2 += pts[i * 2] * pts[j * 2 + 1] - pts[j * 2] * pts[i * 2 + 1];
		}

		array<int> v = {};	// робочий список індексів
		if (area2 >= 0)
		{
			for (int i = 0; i < n; i++)
				v.Insert(i);
		}
		else
		{
			for (int i = n - 1; i >= 0; i--)
				v.Insert(i);
		}

		int count = n;
		int guard = 0;
		int maxGuard = n * n + 16;
		while (count > 3 && guard < maxGuard)
		{
			bool clipped = false;
			for (int i = 0; i < count; i++)
			{
				int i0 = v[(i + count - 1) % count];
				int i1 = v[i];
				int i2 = v[(i + 1) % count];

				float ax = pts[i0 * 2], az = pts[i0 * 2 + 1];
				float bx = pts[i1 * 2], bz = pts[i1 * 2 + 1];
				float cx = pts[i2 * 2], cz = pts[i2 * 2 + 1];

				// опукле «вухо»? (CCW: поворот ліворуч)
				float cross = (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
				if (cross <= 0)
					continue;

				// жодна інша вершина не всередині трикутника? Перевіряємо ЛИШЕ увігнуті (reflex)
				// вершини: опукла всередині вуха лежати не може (класична властивість ear-clipping для
				// простих полігонів). Заливки переважно опуклі, тож reflex-множина мала — саме це
				// прибирає внутрішнє O(n) і рятує від O(n³) на великих контурах (секундний фріз кліку).
				bool blocked = false;
				for (int k = 0; k < count; k++)
				{
					int vk = v[k];
					if (vk == i0 || vk == i1 || vk == i2)
						continue;

					// reflex? (CCW-полігон: опукла = лівий поворот, cross > 0 — пропускаємо)
					int vpk = v[(k + count - 1) % count];
					int vnk = v[(k + 1) % count];
					float rc = (pts[vk * 2] - pts[vpk * 2]) * (pts[vnk * 2 + 1] - pts[vpk * 2 + 1])
						- (pts[vk * 2 + 1] - pts[vpk * 2 + 1]) * (pts[vnk * 2] - pts[vpk * 2]);
					if (rc > 0)
						continue;	// опукла — блокувати вухо не може

					if (PointInTri(pts[vk * 2], pts[vk * 2 + 1], ax, az, bx, bz, cx, cz))
					{
						blocked = true;
						break;
					}
				}
				if (blocked)
					continue;

				outIndices.Insert(i0);
				outIndices.Insert(i1);
				outIndices.Insert(i2);
				v.RemoveOrdered(i);	// саме Ordered: Remove() підставляє останній елемент і ламає порядок контуру
				count--;
				clipped = true;
				break;
			}
			if (!clipped)
				guard = maxGuard;	// не знайшли вуха (самоперетин/виродження) — здаємось
			guard++;
		}

		if (count == 3)
		{
			outIndices.Insert(v[0]);
			outIndices.Insert(v[1]);
			outIndices.Insert(v[2]);
			return true;
		}
		return false;
	}

	protected static bool PointInTri(float px, float pz, float ax, float az, float bx, float bz, float cx, float cz)
	{
		float d1 = (px - bx) * (az - bz) - (ax - bx) * (pz - bz);
		float d2 = (px - cx) * (bz - cz) - (bx - cx) * (pz - cz);
		float d3 = (px - ax) * (cz - az) - (cx - ax) * (pz - az);
		bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
		bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
		return !(hasNeg && hasPos);
	}
}
