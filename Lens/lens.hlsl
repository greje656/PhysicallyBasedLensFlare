cbuffer InstanceUniforms : register(b0) {
	float4 color;
	float4 placement;
};

cbuffer GlobalUniforms : register(b1) {
	float time, g1, g2, c;
	float4 direction;
};

struct PS_INPUT {
	float4 Position : SV_POSITION;
	float4 Texture : TEXCOORD0;
};

struct LensInterface {
	float3 center;
	float radius;
	float3 n;
	float sa;
	float d1;
	bool flat;
	float h;
};

struct Ray {
	float3 pos, dir;
	float4 tex;
};

struct Intersection {
	float3 pos;
	float3 norm;
	float theta;
	bool hit;
	bool inverted;
};

#define NUM_BOUNCE 2
#define AP_IDX 15
#define PI 3.14159265359f

#define NUM_INTERFACE 29
static LensInterface interfaces[NUM_INTERFACE] = {

    { float3(0, 0, 130.812), 72.747, float3(1, 1, 1.603), 1, 1, false, 29 },
    { float3(0, 0, 164.259), 37, float3(1.603, 1, 1), 1, 1, false, 29 },
    { float3(0, 0, 361.068), -172.809, float3(1, 1, 1.58913), 1, 1, false, 26.2 },
    { float3(0, 0, 146.265), 39.894, float3(1.58913, 1, 1), 1, 1, false, 26.2 },
    { float3(0, 0, 135.339), 49.82, float3(1, 1, 1.86074), 1, 1, false, 20 },
    { float3(0, 0, 106.009), 74.75, float3(1.86074, 1, 1), 1, 1, false, 20 },
    { float3(0, 0, 64.215), 63.402, float3(1, 1, 1.86074), 1, 1, false, 16.1 },
    { float3(0, 0, 88.487), 37.53, float3(1.86074, 1, 1.5168), 1, 1, false, 16.1 },
    { float3(0, 0, 193.304), -75.887, float3(1.5168, 1, 1.80458), 1, 1, false, 16 },
    { float3(0, 0, 213.609), -97.792, float3(1.80458, 1, 1), 1, 1, false, 16.5 },
    { float3(0, 0, 12.72), 96.034, float3(1, 1, 1.62041), 1, 1, false, 18 },
    { float3(0, 0, -156.589), 261.743, float3(1.62041, 1, 1), 1, 1, false, 18 },
    { float3(0, 0, 50.792), 54.262, float3(1, 1, 1.6968), 1, 1, false, 18 },
    { float3(0, 0, 6094.33), -5995.28, float3(1.6968, 1, 1), 1, 1, false, 18 },
    { float3(0, 0, 97.522), 10, float3(1, 1, 1), 1, 1, true, 10 },
    { float3(0, 0, 169.136), -74.414, float3(1, 1, 1.90265), 1, 1, false, 13 },
    { float3(0, 0, 155.451), -62.929, float3(1.90265, 1, 1.5168), 1, 1, false, 13 },
    { float3(0, 0, -30.308), 121.38, float3(1.5168, 1, 1), 1, 1, false, 13.1 },
    { float3(0, 0, 174.295), -85.723, float3(1, 1, 1.49782), 1, 1, false, 13 },
    { float3(0, 0, 56.079), 31.093, float3(1.49782, 1, 1.80458), 1, 1, false, 13.1 },
    { float3(0, 0, -0.186005), 84.758, float3(1.80458, 1, 1), 1, 1, false, 13 },
    { float3(0, 0, -392.007), 459.69, float3(1, 1, 1.86074), 1, 1, false, 15 },
    { float3(0, 0, 26.043), 40.24, float3(1.86074, 1, 1.49782), 1, 1, false, 15 },
    { float3(0, 0, 108.754), -49.771, float3(1.49782, 1, 1), 1, 1, false, 15.2 },
    { float3(0, 0, -3.486), 62.369, float3(1, 1, 1.67025), 1, 1, false, 16 },
    { float3(0, 0, 128.337), -76.454, float3(1.67025, 1, 1), 1, 1, false, 16 },
    { float3(0, 0, 79.207), -32.524, float3(1, 1, 1.80454), 1, 1, false, 17 },
    { float3(0, 0, 94.877), -50.194, float3(1.80454, 1, 1), 1, 1, false, 17 },
    { float3(0, 0, 5), 0, float3(1, 1, 1), 1, 1, true, 0 },

};

int2 BOUNCE[NUM_BOUNCE];
int LENGTH[NUM_BOUNCE];


Intersection testFLAT(Ray r, LensInterface F) {
	Intersection i;
	i.pos = r.pos + r.dir * ((F.center.z - r.pos.z) / r.dir.z);
	i.norm = r.dir.z > 0 ? float3(0, 0, -1) : float3(0, 0, 1);
	i.theta = 0;
	i.hit = true;
	i.inverted = false;
	return i;
}

Intersection testSPHERE(Ray r, LensInterface F) {
	Intersection i;
	float3 D = r.pos - F.center;
	float B = dot(D, r.dir);
	float C = dot(D, D) - F.radius * F.radius;
	float B2_C = B*B-C;

	if (B2_C < 0)
		{ i.hit = false; return i; }

	float sgn = (F.radius * r.dir.z) > 0 ? 1.f : -1.f;
	float t = sqrt(B2_C) * sgn - B;
	i.pos = r.dir * t + r.pos;
	i.norm = normalize(i.pos - F.center);
	if (dot(i.norm, r.dir) > 0) i.norm = -i.norm;
	i.theta = acos(dot(-r.dir, i.norm));
	i.hit = true;
	i.inverted = t < 0;
	
	return i;
}

float FresnelAR(float theta0, float lambda, float d1, float n0, float n1, float n2) {
	float theta1 = asin(sin(theta0) * n0 / n1);
	float theta2 = asin(sin(theta0) * n0 / n2);

	float rs01 = -sin(theta0-theta1) / sin(theta0 + theta1);
	float rp01 =  tan(theta0-theta1) / tan(theta0 + theta1);
	float ts01 = 2 * sin(theta1) *cos(theta0) / sin(theta0 + theta1);
	float tp01 = ts01*cos(theta0-theta1);

	float rs12 = -sin(theta1-theta2) / sin(theta1 + theta2);
	float rp12 =  tan(theta1-theta2) / tan(theta1 + theta2);

	float ris = ts01*ts01*rs12;
	float rip = tp01*tp01*rp12;
	float dy = d1*n1;
	float dx = tan(theta1) *dy;
	float delay = sqrt(dx*dx + dy*dy);
	float relPhase = 4.f * PI / lambda*(delay-dx*sin(theta0));

	float out_s2 = rs01*rs01 + ris*ris + 2 * rs01*ris*cos(relPhase);
	float out_p2 = rp01*rp01 + rip*rip + 2 * rp01*rip*cos(relPhase);
	
	return (out_s2 + out_p2) / 2.f; 
}


Ray Trace( Ray r, float lambda, int2 STR) {

	static int2 BOUNCE[NUM_BOUNCE];
	static int LENGTH[NUM_BOUNCE];

	int LEN = STR.x + (STR.x - STR.y) + (NUM_INTERFACE - STR.y);

	int PHASE = 0;
	int DELTA = 1;
	int T = 1;
	int k;
	for (k = 0; k < LEN; k++, T += DELTA) {
		LensInterface F = interfaces[T - 1];

		bool bReflect = (T == STR[PHASE]) ? true : false;
		
		if (bReflect) { 
			DELTA = -DELTA; PHASE++;
		}
		
		Intersection i;
		
		[branch]
		if(F.flat)
			i = testFLAT(r, F);
		else
			i = testSPHERE(r, F);

		if (!i.hit) break;

		//if ((length(i.pos.xy)) > F.h) break;

		if (!F.flat)
			r.tex.z = max(r.tex.z, length(i.pos.xy) / F.sa);
		else if (T == AP_IDX) {
			r.tex.x = i.pos.x / interfaces[AP_IDX - 1].radius;
			r.tex.y = i.pos.y / interfaces[AP_IDX - 1].radius;
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
			if (length(r.dir) == 0.f) break;
		}
		else {
			r.dir = reflect(r.dir , i.norm);
			float R = FresnelAR(i.theta , lambda , F.d1 , n0 , n1 , n2);
			r.tex.a *= R; 
		}

		
	}

	//r.tex = float4(1,0,0,0.5);
	r.tex.b = 0.f;
	r.tex.a = color.a;
	return r;
}

//--------------------------------------------------------------------------------------
// Flare Vertex Shader
//--------------------------------------------------------------------------------------
PS_INPUT VS( float4 Pos : POSITION ) {
	PS_INPUT res;
	res.Position = Pos;
	
	Ray r;
	r.tex = float4(1,1,1,1);
	r.pos = float3(Pos.xy * 2.0, 300.f);
	r.dir = normalize(float3(0.f, 0.F, -1.f));

	Ray g = Trace(r, 1.f, int2(g1, g2));
	res.Position.xyz = float3(g.pos.xy * 0.009 * 30.f, 0.f);
	res.Position.x *= 0.5f;
	res.Texture = g.tex;

	return res;
}

//--------------------------------------------------------------------------------------
// Flare Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS( in PS_INPUT In ) : SV_Target {


	float4 res = In.Texture;
	res.a = length(In.Texture.xy) < 0.5 ? 1.f : 0.1f;

    return res;
}
