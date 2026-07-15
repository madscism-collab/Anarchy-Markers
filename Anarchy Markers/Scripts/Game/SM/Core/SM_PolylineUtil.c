// Ramer-Douglas-Peucker polyline simplification. Run on the client right
// before a freehand stroke is committed, so we ship far fewer points over the
// network and store/redraw less. Points are a flat array of x,z METER pairs
// (matches our integer world-coord convention). Iterative (explicit stack) so
// a long stroke can't blow the script call depth.

class SM_PolylineUtil
{
	//! epsilonMeters = max allowed deviation of a dropped point from the kept
	//! line. Bigger = more aggressive simplification.
	static array<int> RDPSimplify(notnull array<int> pts, float epsilonMeters)
	{
		int n = pts.Count() / 2;
		array<int> result = {};
		if (n < 3 || epsilonMeters <= 0)
		{
			result.Copy(pts);
			return result;
		}

		array<bool> keep = {};
		keep.Resize(n);
		for (int i = 0; i < n; i++)
			keep[i] = false;
		keep[0]     = true;
		keep[n - 1] = true;

		// Work stack of (first,last) index pairs still to subdivide.
		array<int> stack = {};
		stack.Insert(0);
		stack.Insert(n - 1);

		float epsSq = epsilonMeters * epsilonMeters;

		while (!stack.IsEmpty())
		{
			int last  = stack[stack.Count() - 1]; stack.Remove(stack.Count() - 1);
			int first = stack[stack.Count() - 1]; stack.Remove(stack.Count() - 1);

			float ax = pts[first * 2];
			float az = pts[first * 2 + 1];
			float bx = pts[last * 2];
			float bz = pts[last * 2 + 1];
			float dx = bx - ax;
			float dz = bz - az;
			float lenSq = dx * dx + dz * dz;

			int   maxIdx    = -1;
			float maxDistSq = 0;

			for (int i = first + 1; i < last; i++)
			{
				float px = pts[i * 2];
				float pz = pts[i * 2 + 1];
				float distSq;
				if (lenSq < 0.0001)
				{
					float pdx = px - ax;
					float pdz = pz - az;
					distSq = pdx * pdx + pdz * pdz;
				}
				else
				{
					float t = ((px - ax) * dx + (pz - az) * dz) / lenSq;
					if (t < 0) t = 0;
					else if (t > 1) t = 1;
					float cx = ax + t * dx;
					float cz = az + t * dz;
					float ex = px - cx;
					float ez = pz - cz;
					distSq = ex * ex + ez * ez;
				}
				if (distSq > maxDistSq)
				{
					maxDistSq = distSq;
					maxIdx    = i;
				}
			}

			if (maxIdx >= 0 && maxDistSq > epsSq)
			{
				keep[maxIdx] = true;
				stack.Insert(first);  stack.Insert(maxIdx);
				stack.Insert(maxIdx); stack.Insert(last);
			}
		}

		for (int i = 0; i < n; i++)
		{
			if (keep[i])
			{
				result.Insert(pts[i * 2]);
				result.Insert(pts[i * 2 + 1]);
			}
		}
		return result;
	}

	//! Shortest distance (meters) from point (px,pz) to the polyline. Used by
	//! the eraser hit-test. Returns a big number for empty/degenerate input.
	static float DistanceToPolyline(notnull array<int> pts, float px, float pz)
	{
		int n = pts.Count() / 2;
		if (n == 0)
			return 1000000;
		if (n == 1)
		{
			float ddx = px - pts[0];
			float ddz = pz - pts[1];
			return Math.Sqrt(ddx * ddx + ddz * ddz);
		}

		float best = 1000000;
		for (int i = 0; i < n - 1; i++)
		{
			float ax = pts[i * 2];
			float az = pts[i * 2 + 1];
			float bx = pts[(i + 1) * 2];
			float bz = pts[(i + 1) * 2 + 1];
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
			float cx = ax + t * dx;
			float cz = az + t * dz;
			float ex = px - cx;
			float ez = pz - cz;
			float dsq = ex * ex + ez * ez;
			if (dsq < best * best)
				best = Math.Sqrt(dsq);
		}
		return best;
	}

	//! Does the segment p->q touch the axis-aligned rectangle at all? Used by the template selection
	//! box, where an AABB overlap is not enough: a long diagonal's box can straddle the rectangle while
	//! the line itself passes well clear of it.
	//!
	//! Liang-Barsky: clip the segment against the four edges and see whether anything survives.
	static bool SegmentIntersectsRect(int px, int pz, int qx, int qz, int loX, int loZ, int hiX, int hiZ)
	{
		float dx = qx - px;
		float dz = qz - pz;

		float t0 = 0;
		float t1 = 1;

		float p[4];
		float q[4];
		p[0] = -dx;  q[0] = px - loX;
		p[1] =  dx;  q[1] = hiX - px;
		p[2] = -dz;  q[2] = pz - loZ;
		p[3] =  dz;  q[3] = hiZ - pz;

		for (int i = 0; i < 4; i++)
		{
			if (p[i] == 0)
			{
				if (q[i] < 0)
					return false;	// parallel to this edge and outside it
				continue;
			}

			float r = q[i] / p[i];
			if (p[i] < 0)
			{
				if (r > t1)
					return false;
				if (r > t0)
					t0 = r;
			}
			else
			{
				if (r < t0)
					return false;
				if (r < t1)
					t1 = r;
			}
		}
		return true;
	}
}
