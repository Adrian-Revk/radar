#include "geometry.h"
#include "common/SHEval.h"

#include <algorithm>
#include <complex>
#pragma optimize("", off)

static bool even(int n) {
	return !(n & 1);
}

static int sign(f32 b) {
	return (b >= 0) - (b < 0);
}

//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////

vec3f Plane::RayIntersection(const vec3f &rayOrg, const vec3f &rayDir) {
	const f32 distance = N.Dot(P - rayOrg) / N.Dot(rayDir);
	return rayOrg + rayDir * distance;
}

vec3f Plane::ClampPointInRect(const Rectangle &rect, const vec3f &point) {
	const vec3f dir = P - point;

	vec2f dist2D(dir.Dot(rect.ex), dir.Dot(rect.ey));
	vec2f hSize(rect.hx, rect.hy);
	dist2D.x = std::min(hSize.x, std::max(-hSize.x, dist2D.x));
	dist2D.y = std::min(hSize.y, std::max(-hSize.y, dist2D.y));
	
	return point + rect.ex * dist2D.x + rect.ey * dist2D.y;
}
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////

Polygon::Polygon(const std::vector<vec3f> &pts) {
	int nv = (int) pts.size();
	for (int i = 0; i < nv-1; ++i) {
		edges.push_back(Edge{ pts[i], pts[i + 1] });
	}
	edges.push_back(Edge{ pts[nv - 1], pts[0] });
}

f32 Polygon::SolidAngle() const {

	if (edges.size() == 3) {
		const vec3f &A = edges[0].A;
		const vec3f &B = edges[1].A;
		const vec3f &C = edges[2].A;

		// Arvo solid angle : alpha + beta + gamma - pi
		// Oosterom & Strackee 83 method
		const vec3f tmp = A.Cross(B);
		const f32 num = std::fabsf(tmp.Dot(A));
		const f32 r1 = std::sqrtf(A.Dot(A));
		const f32 r2 = std::sqrtf(B.Dot(B));
		const f32 r3 = std::sqrtf(C.Dot(C));

		const f32 denom = r1 * r2 * r3 + A.Dot(B) * r3 + A.Dot(C) * r2 + B.Dot(C) * r1;

		// tan(phi/2) = num/denom
		f32 halPhi = std::atan2f(num, denom);
		if (halPhi < 0.f) halPhi += M_PI;

		return 2.f * halPhi;
	} else {
		// Algorithm of polyhedral cones by Mazonka http://arxiv.org/pdf/1205.1396v2.pdf
		std::complex<float> z(1, 0);
		for (unsigned int k = 0; k < edges.size(); ++k) {
			const vec3f& A = edges[(k > 0) ? k - 1 : edges.size() - 1].A;
			const vec3f& B = edges[k].A;
			const vec3f& C = edges[k].B;

			const float ak = A.Dot(C);
			const float bk = A.Dot(B);
			const float ck = B.Dot(C);
			const float dk = A.Dot(B.Cross(C));
			const std::complex<float> zk(bk*ck - ak, dk);
			z *= zk;
		}
		const float arg = std::arg(z);
		return arg;
	}
}

f32 Polygon::CosSumIntegralArvo(f32 x, f32 y, f32 c, int nMin, int nMax) const {
	const f32 sinx = std::sinf(x);
	const f32 siny = std::sinf(y);

	int i = even(nMax) ? 0 : 1;
	f32 F = even(nMax) ? y - x : siny - sinx;
	f32 S = 0.f;

	while (i <= nMax) {
		if (i >= nMin)
			S += std::pow(c, i) * F;

		const f32 T = std::pow(std::cosf(y), i + 1) * siny - std::pow(std::cosf(x), i + 1) * sinx;
		F = (T + (i + 1)*F) / (i + 2);
		i += 2;
	}

	return S;
}

f32 Polygon::LineIntegralArvo(const vec3f & A, const vec3f & B, const vec3f & w, int nMin, int nMax) const {
	const f32 eps = 1e-7f;
	if ((nMax < 0) || ((fabs(w.Dot(A)) < eps) && (fabs(w.Dot(B)) < eps))) {
		return 0.f;
	}

	vec3f s = A;
	s.Normalize();

	const f32 sDotB = s.Dot(B);

	vec3f t = B - s * sDotB;
	t.Normalize();

	const f32 a = w.Dot(s);
	const f32 b = w.Dot(t);
	const f32 c = std::sqrtf(a*a + b*b);

	const f32 cos_l = sDotB / B.Dot(B);
	const f32 l = std::acosf(std::max(-1.f, std::min(1.f, cos_l)));
	const f32 phi = sign(b) * std::acosf(a / c);

	return CosSumIntegralArvo(-phi, l - phi, c, nMin, nMax);
}

f32 Polygon::BoundaryIntegralArvo(const vec3f & w, const vec3f & v, int nMin, int nMax) const {
	f32 b = 0.f;

	for (const Edge &e : edges) {
		vec3f n = e.A.Cross(e.B);
		n.Normalize();

		b += n.Dot(v) * LineIntegralArvo(e.A, e.B, w, nMin, nMax);
	}

	return b;
}

f32 Polygon::AxialMomentArvo(const vec3f & w, int order) {
	f32 a = -BoundaryIntegralArvo(w, w, 0, order - 1);

	if (even(order)) {
		a += SolidAngle();
	}

	return a / (order + 1);
}

f32 Polygon::DoubleAxisMomentArvo(const vec3f & w, const vec3f & v, int order) {
	if(order == 0)
		return AxialMomentArvo(w, order);

	f32 a = AxialMomentArvo(w, order - 1);
	f32 b = BoundaryIntegralArvo(w, v, order, order);

	return (order * a * w.Dot(v) - b) / (order + 2);
}

void Polygon::CosSumIntegral(f32 x, f32 y, f32 c, int n, std::vector<f32> &R) const {
	const f32 sinx = std::sinf(x);
	const f32 siny = std::sinf(y);
	const f32 cosx = std::cosf(x);
	const f32 cosy = std::cosf(y);
	const f32 cosxsq = cosx * cosx;
	const f32 cosysq = cosy * cosy;

	static const vec2f i1(1, 1);
	static const vec2f i2(2, 2);
	vec2i i(0, 1);
	vec2f F(y - x, siny - sinx);
	vec2f S(0.f, 0.f);

	vec2f pow_c(1.f, c);
	vec2f pow_cosx(cosx, cosxsq);
	vec2f pow_cosy(cosy, cosysq);

	while (i[1] <= n) {
		S += pow_c * F;

		R[i[1] + 0] = S[0];
		R[i[1] + 1] = S[1];

		vec2f T = pow_cosy * siny - pow_cosx * sinx;
		F = (T + (i + i1) * F) / (i + i2);

		i += i2;
		pow_c *= c*c;
		pow_cosx *= cosxsq;
		pow_cosy *= cosysq;
	}
}

void Polygon::LineIntegral(const vec3f &A, const vec3f &B, const vec3f &w, int n, std::vector<f32> &R) const {
	const f32 eps = 1e-7f;
	if ((n < 0) || ((fabs(w.Dot(A)) < eps) && (fabs(w.Dot(B)) < eps))) {
		return;
	}

	vec3f s = A;
	s.Normalize();

	const f32 sDotB = s.Dot(B);

	vec3f t = B - s * sDotB;
	t.Normalize();

	const f32 a = w.Dot(s);
	const f32 b = w.Dot(t);
	const f32 c = std::sqrtf(a*a + b*b);

	const f32 cos_l = sDotB / B.Dot(B);
	const f32 l = std::acosf(std::max(-1.f, std::min(1.f, cos_l)));
	const f32 phi = sign(b) * std::acosf(a / c);

	CosSumIntegral(-phi, l - phi, c, n, R);
}

void Polygon::BoundaryIntegral(const vec3f &w, const vec3f &v, int n, std::vector<f32> &R) const {
	std::vector<f32> b(n + 2, 0.f);

	for (const Edge &e : edges) {
		vec3f nrm = e.A.Cross(e.B);
		nrm.Normalize();
		f32 nDotv = nrm.Dot(v);

		LineIntegral(e.A, e.B, w, n, b);

		for (int i = 0; i < n + 2; ++i)
			R[i] += b[i] * nDotv;
	}
}

void Polygon::AxialMoment(const vec3f &w, int order, std::vector<f32> &R) const {
	// Compute the Boundary Integral of the polygon
	BoundaryIntegral(w, w, order - 1, R);

	// - boundary + solidangle for even orders
	f32 sA = SolidAngle();

	for (u32 i = 0; i < R.size(); ++i) {
		R[i] *= -1.f; // - boundary

		// add the solid angle for even orders
		if (even(i)) {
			R[i] += sA;
		}

		// normalize by order+1
		R[i] *= -1.f/ (f32) (i + 1);
	}
}

std::vector<f32> Polygon::AxialMoments(const std::vector<vec3f> &directions) const {
	const u32 dsize = (u32) directions.size();
	const u32 order = (dsize - 1) / 2 + 1;

	std::vector<f32> result(dsize * order);
	std::vector<f32> dirResult(order);

	for (u32 i = 0; i < dsize; ++i) {
		const vec3f &d = directions[i];
		AxialMoment(d, order - 1, dirResult);
		std::copy(dirResult.begin(), dirResult.end(), result.begin() + i * order);
	}
	
	return result;
}
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////

void Triangle::InitUnit(const vec3f &p0, const vec3f &p1, const vec3f &p2, const vec3f &intPos) {
	q0 = p0 - intPos;
	q1 = p1 - intPos;
	q2 = p2 - intPos;
	q0.Normalize();
	q1.Normalize();
	q2.Normalize();

	const vec3f d1 = q1 - q0;
	const vec3f d2 = q2 - q0;

	const vec3f nrm = -d1.Cross(d2);
	const f32 nrmLen = std::sqrtf(nrm.Dot(nrm));
	area = solidAngle = nrmLen * 0.5f;

	// compute inset triangle's unit normal
	const f32 areaThresh = 1e-5f;
	bool badPlane = -1.0f * q0.Dot(nrm) <= 0.0f;

	if (badPlane || area < areaThresh) {
		unitNormal = -(q0 + q1 + q2);
		unitNormal.Normalize();
	} else {
		unitNormal = nrm / nrmLen;
	}
}

/// Same as InitUnit, except the resulting triangle is kept in world space. 
/// triNormal is the normal of the plane collinear with the triangle
void Triangle::InitWS(const vec3f &triNormal, const vec3f &p0, const vec3f &p1, const vec3f &p2, const vec3f &intPos) {
	unitNormal = triNormal;

	q0 = p0 - intPos;
	q1 = p1 - intPos;
	q2 = p2 - intPos;

	ComputeArea();

	const vec3f bary = (q0 + q1 + q2) * 0.3333333333333f;
	const f32 rayLenSqr = bary.Dot(bary);
	const f32 rayLen = std::sqrtf(rayLenSqr);
	solidAngle = -bary.Dot(unitNormal) * (area / (rayLenSqr * rayLen));
}

u32 Triangle::Subdivide4(Triangle subdivided[4]) const {
	vec3f q01 = (q0 + q1);
	vec3f q02 = (q0 + q2);
	vec3f q12 = (q1 + q2);

	subdivided[0].InitUnit(q0, q01, q02, vec3f(0.f));
	subdivided[1].InitUnit(q01, q1, q12, vec3f(0.f));
	subdivided[2].InitUnit(q02, q12, q2, vec3f(0.f));
	subdivided[3].InitUnit(q01, q12, q02, vec3f(0.f));

	return 4;
}

void Triangle::ComputeArea() {
	const vec3f v1 = q1 - q0, v2 = q2 - q0;
	const vec3f n1 = v1.Cross(v2);

	area = std::fabsf(n1.Dot(unitNormal)) * 0.5f;
}

vec3f Triangle::SamplePoint(f32 u1, f32 u2) const {
	const f32 su1 = std::sqrtf(u1);
	const vec2f bary(1.f - su1, u2 * su1);

	return q0 * bary.x + q1 * bary.y + q2 * (1.f - bary.x - bary.y);
}

f32 Triangle::SampleDir(vec3f &rayDir, const f32 s, const f32 t) const {
	rayDir = SamplePoint(s, t);
	const f32 rayLenSqr = rayDir.Dot(rayDir);
	const f32 rayLen = std::sqrtf(rayLenSqr);
	rayDir /= rayLen;

	const f32 costheta = -unitNormal.Dot(rayDir);

	return costheta / rayLenSqr;
}

//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////

Rectangle::Rectangle(const std::vector<vec3f>& verts) {
	Assert(verts.size() == 4);
	p0 = verts[0];
	p1 = verts[1];
	p2 = verts[2];
	p3 = verts[3];

	position = (p0 + p1 + p2 + p3) / 4.f;
	ex = p1 - p0;
	hx = 0.5f * ex.Len();
	ex.Normalize();
	ey = p3 - p0;
	hy = 0.5f * ex.Len();
	ey.Normalize();

	ez = -ex.Cross(ey);
	ez.Normalize();
}

Rectangle::Rectangle(const Polygon & P) {
	Assert(P.edges.size() == 4);

	std::vector<vec3f> verts(4);

	verts[0] = P.edges[0].A;
	verts[1] = P.edges[1].A;
	verts[2] = P.edges[2].A;
	verts[3] = P.edges[3].A;

	*this = Rectangle(verts);
}

vec3f Rectangle::SamplePoint(f32 u1, f32 u2) const{
	const vec3f bl = position - ex * hx - ey * hy;
	const f32 w = hx * 2.f;
	const f32 h = hy * 2.f;

	return bl + ex * w * u1 + ey * h * u2;
}

f32 Rectangle::SampleDir(vec3f & rayDir, const vec3f & pos, const f32 u1, const f32 u2) const {
	const vec3f pk = SamplePoint(u1, u2);
	rayDir = pk - pos;
	const f32 rayLenSq = rayDir.Dot(rayDir);
	const f32 rayLen = std::sqrtf(rayLenSq);
	rayDir /= rayLen;

	const f32 costheta = -ez.Dot(rayDir);

	return costheta / rayLenSq;
}

f32 Rectangle::SolidAngle(const vec3f & integrationPos) const {
	const vec3f q0 = p0 - integrationPos;
	const vec3f q1 = p1 - integrationPos;
	const vec3f q2 = p2 - integrationPos;
	const vec3f q3 = p3 - integrationPos;

	vec3f n0 = q0.Cross(q1);
	vec3f n1 = q1.Cross(q2);
	vec3f n2 = q2.Cross(q3);
	vec3f n3 = q3.Cross(q0);
	n0.Normalize();
	n1.Normalize();
	n2.Normalize();
	n3.Normalize();

	const f32 alpha = std::acosf(-n0.Dot(n1));
	const f32 beta = std::acosf(-n1.Dot(n2));
	const f32 gamma = std::acosf(-n2.Dot(n3));
	const f32 zeta = std::acosf(-n3.Dot(n0));

	return alpha + beta + gamma + zeta - 2 * M_PI;
}

f32 Rectangle::IntegrateStructuredSampling(const vec3f & integrationPos, const vec3f & integrationNrm) const {
	// Solving E(n) = Int_lightArea [ Lin <n.l> dl ] == lightArea * Lin * Average[<n.l>]
	// With Average[<n.l>] approximated with the average of the 4 corners and center of the rect

	// unit space solid angle (== unit space area)
	f32 solidAngle = SolidAngle(integrationPos);

	// unit space vectors to the 5 sample points

	vec3f q0 = p0 - integrationPos;
	vec3f q1 = p1 - integrationPos;
	vec3f q2 = p2 - integrationPos;
	vec3f q3 = p3 - integrationPos;
	vec3f q4 = position - integrationPos;

	q0.Normalize();
	q1.Normalize();
	q2.Normalize();
	q3.Normalize();
	q4.Normalize();

	// area * Average[<n.l>] (Lin is 1.0)
	return solidAngle * 0.2f * (
		std::max(0.f, q0.Dot(integrationNrm)) +
		std::max(0.f, q1.Dot(integrationNrm)) +
		std::max(0.f, q2.Dot(integrationNrm)) +
		std::max(0.f, q3.Dot(integrationNrm)) +
		std::max(0.f, q4.Dot(integrationNrm))
		);
}

f32 Rectangle::IntegrateMRP(const vec3f & integrationPos, const vec3f & integrationNrm) const {
	const vec3f d0p = -ez;
	const vec3f d1p = integrationNrm;

	const f32 nDotpN = std::max(0.f, integrationNrm.Dot(ez));

	vec3f d0 = d0p + integrationNrm * nDotpN;
	vec3f d1 = d1p - ez * nDotpN;
	d0.Normalize();
	d1.Normalize();

	vec3f dh = d0 + d1;
	dh.Normalize();

	Plane rectPlane = { position, ez };
	vec3f pH = rectPlane.RayIntersection(integrationPos, dh);
	pH = rectPlane.ClampPointInRect(*this, pH);

	const f32 solidAngle = SolidAngle(integrationPos);

	vec3f rayDir = pH - integrationPos;
	rayDir.Normalize();

	return solidAngle * std::max(0.f, integrationNrm.Dot(rayDir));
}

f32 Rectangle::IntegrateAngularStratification(const vec3f & integrationPos, const vec3f & integrationNrm, u32 sampleCount, std::vector<f32> &shvals, int nBand) const {
	const u32 sampleCountX = (u32) std::sqrt((f32) sampleCount);
	const u32 sampleCountY = sampleCountX;

	// bottom left point
	const vec3f a0 = position - ex * hx - ey * hy;

	const vec3f W1 = position - ex * hx;
	const vec3f W2 = position + ex * hx;
	const vec3f H1 = position - ey * hy;
	const vec3f H2 = position + ey * hy;

	const f32 lw1_2 = W1.Dot(W1);
	const f32 lw2_2 = W2.Dot(W2);
	const f32 lh1_2 = H1.Dot(H1);
	const f32 lh2_2 = H2.Dot(H2);

	const f32 rwidth = 2.f * hx;
	const f32 rwidth_2 = rwidth * rwidth;
	const f32 rheight = 2.f * hy;
	const f32 rheight_2 = rheight * rheight;

	const f32 lw1 = std::sqrtf(lw1_2);
	const f32 lw2 = std::sqrtf(lw2_2);
	const f32 lh1 = std::sqrtf(lh1_2);
	const f32 lh2 = std::sqrtf(lh2_2);

	const f32 cosx = -W1.Dot(ex) / lw1;
	const f32 sinx = std::sqrtf(1.f - cosx * cosx);
	const f32 cosy = -H1.Dot(ey) / lh1;
	const f32 siny = std::sqrtf(1.f - cosy * cosy);

	const f32 dx = 1.f / sampleCountX;
	const f32 dy = 1.f / sampleCountY;

	const f32 theta = std::acosf((lw1_2 + lw2_2 - rwidth_2) * 0.5f / (lw1 * lw2));
	const f32 gamma = std::acosf((lh1_2 + lh2_2 - rheight_2) * 0.5f / (lh1 * lh2));
	const f32 theta_n = theta * dx;
	const f32 gamma_n = gamma * dy;

	const f32 tanW = std::tanf(theta_n);
	const f32 tanH = std::tanf(gamma_n);

	const int nCoeff = nBand * nBand;
	std::vector<f32> shtmp(nCoeff);

	// Marching over the equi angular rectangles
	f32 x1 = 0.f;
	f32 tanx1 = 0.f;
	for (u32 x = 0; x < sampleCountX; ++x) {
		const f32 tanx2 = (tanx1 + tanW) / (1.f - tanx1 * tanW);
		const f32 x2 = lw1 * tanx2 / (sinx + tanx2 * cosx);
		const f32 lx = x2 - x1;

		f32 y1 = 0.f;
		f32 tany1 = 0.f;
		for (u32 y = 0; y < sampleCountY; ++y) {
			const f32 tany2 = (tany1 + tanH) / (1.f - tany1 * tanH);
			const f32 y2 = lh1 * tany2 / (siny + tany2 * cosy);
			const f32 ly = y2 - y1;

			const f32 u1 = (x1 + Random::Float() * lx) / rwidth;
			const f32 u2 = (y1 + Random::Float() * ly) / rheight;

			vec3f rayDir;
			const f32 invPdf = SampleDir(rayDir, integrationPos, u1, u2) * lx * ly * sampleCount;

			if (invPdf > 0.f) {
				SHEval(nBand, rayDir.x, rayDir.z, rayDir.y, &shtmp[0]);

				for (int i = 0; i < nCoeff; ++i) {
					shvals[i] += shtmp[i] * invPdf; // constant luminance of 1 for now
				}
			}

			y1 = y2;
			tany1 = tany2;
		}

		x1 = x2;
		tanx1 = tanx2;
	}

	return 1.f / (f32) sampleCount;
}

f32 Rectangle::IntegrateRandom(const vec3f & integrationPos, const vec3f & integrationNrm, u32 sampleCount, std::vector<f32>& shvals, int nBand) const {
	// Rectangle area
	const f32 area = 4.f * hx * hy;
	
	const int nCoeff = nBand * nBand;
	std::vector<f32> shtmp(nCoeff);

	// costheta * A / r^3
	for (u32 i = 0; i < sampleCount; ++i) {
		const vec2f randV = Random::Vec2f();

		vec3f rayDir;
		const f32 invPdf = SampleDir(rayDir, integrationPos, randV.x, randV.y);

		if (invPdf > 0.f) {
			SHEval(nBand, rayDir.x, rayDir.z, rayDir.y, &shtmp[0]);

			for (int j = 0; j < nCoeff; ++j) {
				shvals[j] += shtmp[j] * invPdf; // constant luminance of 1 for now
			}
		}
	}

	return area / (f32) sampleCount;
}


void SphericalRectangle::Init(const Rectangle &rect, const vec3f &org) {
	o = org;
	const f32 w = rect.hx * 2.f;
	const f32 h = rect.hy * 2.f;

	// Compute Local reference system R (4.1)
	x = rect.ex;
	y = rect.ey;
	z = rect.ez;

	const vec3f s = rect.p0; // left-bottom vertex or rectangle
	const vec3f d = s - org;

	x0 = d.Dot(x);
	y0 = d.Dot(y);
	z0 = d.Dot(z);

	// flip Z if necessary, it should be away from Q
	if (z0 > 0.f) {
		z0 *= -1.f;
		z *= -1.f;
	}

	z0sq = z0 * z0;
	x1 = x0 + w;
	y1 = y0 + h;
	y0sq = y0 * y0;
	y1sq = y1 * y1;

	// Compute Solid angle subtended by Q (4.2)
	const vec3f v00(x0, y0, z0);
	const vec3f v01(x0, y1, z0);
	const vec3f v10(x1, y0, z0);
	const vec3f v11(x1, y1, z0);

	vec3f n0 = v00.Cross(v10);
	vec3f n1 = v10.Cross(v11);
	vec3f n2 = v11.Cross(v01);
	vec3f n3 = v01.Cross(v00);
	n0.Normalize();
	n1.Normalize();
	n2.Normalize();
	n3.Normalize();

	const f32 g0 = std::acosf(-n0.Dot(n1));
	const f32 g1 = std::acosf(-n1.Dot(n2));
	const f32 g2 = std::acosf(-n2.Dot(n3));
	const f32 g3 = std::acosf(-n3.Dot(n0));

	S = g0 + g1 + g2 + g3 - 2.f * M_PI;

	// Additional constants for future sampling
	b0 = n0.z;
	b1 = n2.z;
	b0sq = b0 * b0;
	k = 2.f * M_PI - g2 - g3;
}

vec3f SphericalRectangle::Sample(f32 u1, f32 u2) const {
	// compute cu
	const f32 phi_u = u1 * S + k;
	const f32 fu = (std::cosf(phi_u) * b0 - b1) / std::sinf(phi_u);

	f32 cu = sign(fu) / std::sqrtf(fu * fu + b0sq);
	cu = std::max(-1.f, std::min(1.f, cu));

	// compute xu
	f32 xu = -(cu * z0) / std::sqrtf(1.f - cu * cu);
	xu = std::max(x0, std::min(x1, xu)); // bound the result in spherical width

												// compute yv
	const f32 d = std::sqrtf(xu*xu + z0sq);
	const f32 h0 = y0 / std::sqrtf(d*d + y0sq);
	const f32 h1 = y1 / std::sqrtf(d*d + y1sq);

	const f32 hv = h0 + u2 * (h1 - h0); const f32 hvsq = hv * hv;
	const f32 yv = (hvsq < (1.f - 1e-6f)) ? hv * d / std::sqrtf(1.f - hvsq) : y1;

	// transform to world coordinates
	return o + x * xu + y * yv + z * z0;
}

f32 SphericalRectangle::Integrate(const vec3f & integrationNrm, u32 sampleCount, std::vector<f32>& shvals, int nBand) const {
	// Construct a Spherical Rectangle from the point integrationPos

	const f32 area = S; // spherical rectangle area/solidangle
	

	const int nCoeff = nBand * nBand;
	std::vector<f32> shtmp(nCoeff);

	// Sample the spherical rectangle
	for (u32 i = 0; i < sampleCount; ++i) {
		const vec2f randV = Random::Vec2f();

		vec3f rayDir = Sample(randV.x, randV.y) - o;
		const f32 rayLenSq = rayDir.Dot(rayDir);
		rayDir.Normalize();

		SHEval(nBand, rayDir.x, rayDir.z, rayDir.y, &shtmp[0]);

		for (int j = 0; j < nCoeff; ++j) {
			shvals[j] += shtmp[j];
		}
	}

	return area / (f32) sampleCount;
}
