// Заливка (flood fill) для малювання на мапі. Виконується НА КЛІЄНТІ один раз на клік:
//   1) растеризуємо всі видимі малюнки (штрихи з їх товщиною + наявні заливки) у сітку-перешкоди
//      навколо точки кліку;
//   2) BFS-розлив від кліку: не проходить крізь перешкоди; якщо дотік до краю вікна — пробуємо
//      більше вікно (512→1024→2048 м); якщо тече й з найбільшого — область не замкнута;
//   3) якщо розлив уперся в бюджет клітинок — заливаємо ЧАСТКОВО (що встигли), наступний клік
//      дофарбує решту, впершись у цю заливку як у перешкоду;
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

	protected static const int GRID_DIM   = 256;	// сітка завжди GRID_DIM×GRID_DIM клітинок
	protected static const int MAX_VISIT  = 52000;	// бюджет розливу; далі — часткова заливка
	protected static const int MAX_TRACE  = 300000;	// запобіжник обходу контуру

	//! obstacles — усі видимі малюнки клієнта (штрихи і заливки). maxPts — ліміт точок контуру.
	static int Compute(int clickX, int clickZ, notnull array<SM_MapDrawingData> obstacles, int maxPts, out SM_FloodFillResult res)
	{
		res = new SM_FloodFillResult();

		// Вікна: half-size у метрах → клітинка = (2*half)/GRID_DIM (2м → 4м → 8м).
		for (int attempt = 0; attempt < 3; attempt++)
		{
			int half = 256;
			if (attempt == 1)
				half = 512;
			else if (attempt == 2)
				half = 1024;
			float cell = (2.0 * half) / GRID_DIM;
			int originX = clickX - half;	// світова позиція кутка сітки
			int originZ = clickZ - half;

			// Коди клітинок: 0 вільно, 1 перешкода-ЛІНІЯ, 4 перешкода-ЗАЛИВКА, 2 залито.
			// Лінії й заливки — різні коди, бо під лінії заливку «роздуваємо», а під чужі
			// заливки — ні (інакше сусідні заливки перекривались би).
			array<int> grid = {};
			grid.Resize(GRID_DIM * GRID_DIM);
			for (int i = 0; i < GRID_DIM * GRID_DIM; i++)
				grid[i] = 0;

			float maxHalfW;
			StampObstacles(grid, originX, originZ, cell, obstacles, maxHalfW);

			int cx = (clickX - originX) / cell;
			int cz = (clickZ - originZ) / cell;
			if (cx < 0 || cz < 0 || cx >= GRID_DIM || cz >= GRID_DIM)
				return FAILED;
			if (grid[cz * GRID_DIM + cx] != 0)
			{
				// Клік у перешкоду (лінія/крапка/заливка під курсором) — як справжнє «відерце»,
				// шукаємо найближчу вільну клітинку в невеликому радіусі й стартуємо з неї.
				if (!FindFreeNear(grid, cx, cz, 6))
					return NO_SPACE;
			}

			bool escaped;
			bool budgetHit;
			Flood(grid, cx, cz, escaped, budgetHit);

			if (escaped)
			{
				if (attempt < 2)
					continue;	// пробуємо більше вікно
				return NOT_CLOSED;
			}

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
			// (зокрема кривину ліній). Спільна база для обох варіантів контуру нижче.
			array<int> coarse = SM_PolylineUtil.RDPSimplify(world, cell * 0.5);
			if (coarse.Count() < 6)
				coarse = world;

			float epsFinal = maxHalfW * 0.4;
			if (epsFinal < 1) epsFinal = 1;
			if (epsFinal > cell) epsFinal = cell;

			// Варіант A (кращий): притягуємо КОЖНУ вершину на ВІСЬ найближчої лінії-межі — контур
			// лягає на реальні штрихи по всій кривині, заходить під лінію без зазору. Але snap може
			// зробити контур самоперетинним (перекрутити) — тоді тріангуляція провалиться. Тож
			// беремо snap-версію ЛИШЕ якщо вона тріангулюється; інакше — чистий не-snap контур.
			array<int> snapped = {};
			snapped.Copy(coarse);
			SnapToStrokes(snapped, obstacles, maxHalfW + cell * 3);
			snapped = FinalizeContour(snapped, epsFinal, maxPts);
			array<int> tmpTri = {};
			if (snapped.Count() >= 6 && Triangulate(snapped, tmpTri))
			{
				res.m_aContour = snapped;
				res.m_bPartial = budgetHit;
				return OK;
			}

			// Варіант B (запас): без притягання — гладкий контур (трохи всередині від ліній, дрібний
			// зазор), зате гарантовано простий полігон.
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
	protected static void StampObstacles(array<int> grid, int originX, int originZ, float cell, array<SM_MapDrawingData> obstacles, out float maxHalfW)
	{
		maxHalfW = 0;
		int winMaxX = originX + GRID_DIM * cell;
		int winMaxZ = originZ + GRID_DIM * cell;

		foreach (SM_MapDrawingData d : obstacles)
		{
			if (!d || d.GetPointCount() < 1)
				continue;
			if (!d.AABBOverlapsRect(originX, winMaxX, originZ, winMaxZ, 64))
				continue;

			if (d.m_iFill != 0)
			{
				StampPolygon(grid, originX, originZ, cell, d);	// заливка → код 4
				continue;
			}

			float half = SM_DrawCanvas.WidthMeters(d.m_iWidthIdx) * 0.5;
			if (half > maxHalfW)
				maxHalfW = half;

			int n = d.GetPointCount();
			float r = half + cell * 0.5;	// штамп трохи ширший клітинки, щоб не було дірок
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
	protected static void SnapToStrokes(array<int> contour, array<SM_MapDrawingData> obstacles, float maxSnapDist)
	{
		int n = contour.Count() / 2;
		float maxSq = maxSnapDist * maxSnapDist;
		int snapMargin = Math.Round(maxSnapDist) + 2;

		for (int i = 0; i < n; i++)
		{
			int px = contour[i * 2];
			int pz = contour[i * 2 + 1];
			float bestSq = maxSq;
			float bestX = px;
			float bestZ = pz;
			bool found = false;

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
					if (dsq < bestSq) { bestSq = dsq; bestX = ax; bestZ = az; found = true; }
					continue;
				}
				for (int s = 1; s < cnt; s++)
				{
					int bx, bz;
					d.GetPoint(s, bx, bz);
					float cxo, czo;
					float dsq = ClosestOnSeg(px, pz, ax, az, bx, bz, cxo, czo);
					if (dsq < bestSq) { bestSq = dsq; bestX = cxo; bestZ = czo; found = true; }
					ax = bx;
					az = bz;
				}
			}

			if (found)
			{
				contour[i * 2]     = Math.Round(bestX);
				contour[i * 2 + 1] = Math.Round(bestZ);
			}
		}
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

				// жодна інша вершина не всередині трикутника?
				bool blocked = false;
				for (int k = 0; k < count; k++)
				{
					int vk = v[k];
					if (vk == i0 || vk == i1 || vk == i2)
						continue;
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
