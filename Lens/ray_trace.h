#pragma once

//--------------------------------------------------------------------------------------
// This is a port of:
// http://resources.mpi-inf.mpg.de/lensflareRendering/pdf/flare-supplement.pdf
//--------------------------------------------------------------------------------------


#include <math.h>
#include <vector>
#include <algorithm>

using namespace std;

#define NUM_INTERFACE 1
#define NUM_BOUNCE 2
#define AP_IDX 14
#define PI 3.14159265359f

struct vec3 {
	vec3() { x = y = z = 0.f; }
	vec3(float a, float b, float c) : x(a), y(b), z(c) {};
	float x, y, z;
	vec3 operator-() { return vec3(-this->x, -this->y, -this->z); }
	vec3 operator-(const vec3& b) { return vec3(this->x - b.x, this->y - b.y, this->z - b.z); }
	vec3 operator+(const vec3& b) { return vec3(this->x + b.x, this->y + b.y, this->z + b.z); }
	vec3 operator*(const float b) { return vec3(this->x * b, this->y * b, this->z * b); }
	vec3& operator*=(const float b) { this->x *= b, this->y *= b, this->z *= b; return *this; }
	bool operator==(const float b) { return (this->x == b && this->y == b && this->z == b); }
};

struct vec4 {
	vec4() { x = y = z = a = 0.f; }
	vec4(float a, float b, float c, float d) : x(a), y(b), z(c), a(d) {};
	float x, y, z, a;
};

struct int2 {
	int x, y;
	int operator[](int i) { return i == 0 ? x : y; }
};

struct LensInterface {
	vec3 center;
	float radius;

	vec3 n;
	float sa;

	float d1;
	float flat;
	float pos;
	float w;
};

struct Ray {
	vec3 pos, dir;
	vec4 tex;
};

struct Intersection {
	Intersection() {};
	vec3 pos;
	vec3 norm;
	float theta;
	bool hit;
	bool inverted;
};

float dot(const vec3& a, const vec3& b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3 normalize(vec3 a) {
	float l = sqrt(a.x * a.x + a.y*a.y + a.z*a.z);
	return vec3(a.x / l, a.y / l, a.z / l);
}

vec3 reflect(vec3 i, vec3 n) {
	return i - n * 2.f * dot(i, n);
}

vec3 refract(vec3 i, vec3 n, float eta) {
	float N_dot_I = dot(n, i);
	float k = 1.f - eta * eta * (1.f - N_dot_I * N_dot_I);
	if (k < 0.f)
		return vec3(0.f, 0.f, 0.f);
	else
		return i * eta - n * (eta * N_dot_I + sqrtf(k));
}

float length_xy(vec3& v) {
	return sqrt(v.x * v.x + v.y * v.y);
}

int2 BOUNCE[NUM_BOUNCE];
int LENGTH[NUM_BOUNCE];

Intersection testFLAT(Ray r, LensInterface F) {
	Intersection i;
	i.pos = r.pos + r.dir * ((F.center.z - r.pos.z) / r.dir.z);
	i.norm = r.dir.z > 0 ? vec3(0, 0, -1) : vec3(0, 0, 1);
	i.theta = 0;
	i.hit = true;
	i.inverted = false;
	return i;
}

Intersection testSPHERE(Ray r, LensInterface F) {
	Intersection i;
	vec3 D = r.pos - F.center;
	float B = dot(D, r.dir);
	float C = dot(D, D) - F.radius * F.radius;
	float B2_C = B*B-C;

	if (B2_C < 0)
		{ i.hit = false; return i; }

	float sgn = (F.radius * r.dir.z) > 0 ? 1.f : -1.f;
	float t = sqrtf(B2_C) * sgn - B;
	i.pos = r.dir * t + r.pos;
	i.norm = normalize(i.pos - F.center);
	if (dot(i.norm, r.dir) > 0) i.norm = -i.norm;
	i.theta = acos(dot(-r.dir, i.norm));
	i.hit = true;
	i.inverted = t < 0;
	
	return i;
}

float FresnelAR(
	float theta0, 
	float lambda,
	float d1,
	float n0,
	float n1,
	float n2
) {
	float theta1 = asin(sin(theta0) *n0 / n1);
	float theta2 = asin(sin(theta0) *n0 / n2);

	float rs01 = -sin(theta0-theta1) / sin(theta0 + theta1);
	float rp01 = tan(theta0-theta1) / tan(theta0 + theta1);
	float ts01 = 2 * sin(theta1) *cos(theta0) / sin(theta0 + theta1);
	float tp01 = ts01*cos(theta0-theta1);

	float rs12 = -sin(theta1-theta2) / sin(theta1 + theta2);
	float rp12 = +tan(theta1-theta2) / tan(theta1 + theta2);

	float ris = ts01*ts01*rs12;
	float rip = tp01*tp01*rp12;
	float dy = d1*n1;
	float dx = tanf(theta1) *dy;
	float delay = sqrt(dx*dx + dy*dy);
	float relPhase = 4.f * PI / lambda*(delay-dx*sin(theta0));

	float out_s2 = rs01*rs01 + ris*ris + 2 * rs01*ris*cos(relPhase);
	float out_p2 = rp01*rp01 + rip*rip + 2 * rp01*rip*cos(relPhase);
	return (out_s2 + out_p2) / 2.f; 
}

Ray Trace(
	Ray r,
	float lambda,
	std::vector<LensInterface>& INTERFACE,
	std::vector<vec3>& intersections1,
	std::vector<vec3>& intersections2,
	std::vector<vec3>& intersections3,
	int2 STR
) {

	intersections1.clear();
	intersections2.clear();
	intersections3.clear();

	intersections1.push_back(r.pos);

	int LEN = STR.x + (STR.x - STR.y) + ((int)INTERFACE.size() - STR.y) - 1;

	int PHASE = 0;
	int DELTA = 1;
	int T = 1;
	int k;
	for (k = 0; k < LEN; k++, T += DELTA) {
		LensInterface F = INTERFACE[T];

		bool bReflect = (T == STR[PHASE]) ? true : false;
		if (bReflect) {
			DELTA = -DELTA;
			PHASE++;
		}
		
		Intersection i = F.flat ? testFLAT(r, F) : testSPHERE(r, F);
		
		if (!i.hit) break;

		if (PHASE == 0) {
			intersections1.push_back(i.pos);
		} else if (PHASE == 1) {
			if(bReflect) intersections1.push_back(i.pos);
			intersections2.push_back(i.pos);
		}
		else {
			if (bReflect) intersections2.push_back(i.pos);
			intersections3.push_back(i.pos);
		}

		if (abs(i.pos.y) > F.sa) break;

		if (!F.flat)
			r.tex.z = max(r.tex.z, length_xy(i.pos) / F.sa);
		else if (T == AP_IDX) {
			r.tex.x = i.pos.x / INTERFACE[AP_IDX].radius;
			r.tex.y = i.pos.y / INTERFACE[AP_IDX].radius;
		};

		r.dir = normalize(i.pos-r.pos);
		
		if (i.inverted) r.dir *= -1;
		r.pos = i.pos;
		
		if (F.flat) continue;
		
		float n0 = r.dir.z < 0 ? F.n.x : F.n.z;
		float n1 = F.n.y;
		float n2 = r.dir.z < 0 ? F.n.z : F.n.x;
		
		if (!bReflect) {
			r.dir = refract(r.dir , i.norm , n0 / n2);
			if (r.dir == 0) break;
		}
		else {
			r.dir = reflect(r.dir , i.norm);
			float R = FresnelAR(i.theta , lambda , F.d1 , n0 , n1 , n2);
			r.tex.a *= R; 
		}
	}

	if (k<LEN) r.tex.a = 0;

	return r;
}