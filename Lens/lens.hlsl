cbuffer GlobalUniforms : register(b1) {
	float time, g1, g2, spread;
	float4 direction;
};

struct PS_INPUT {
	float4 position : SV_POSITION;
	float4 color : TEXCOORD0;
	float4 mask : TEXCOORD1;
};

struct LensInterface {
	float3 center;
	float radius;
	float3 n;
	float sa;
	float d1;
	bool flat;
};

struct Ray {	
	float3 pos;
	float3 dir;
	float4 tex;
};

struct Intersection {
	float3 pos;
	float3 norm;
	float theta;
	bool hit;
	bool inverted;
};

StructuredBuffer<PS_INPUT> Buffer0 : register(t0);
RWStructuredBuffer<PS_INPUT> BufferA : register(u0);

#define GLOBAL_SCALE 0.09f
#define NUM_THREADS 4
#define PATH_TESSELATION 32
#define NUM_BOUNCE 2
#define AP_IDX 14
#define PI 3.14159265359f
#define NUM_INTERFACE 29

static LensInterface interfaces[NUM_INTERFACE] = {

    { float3(0, 0, 130.812), 72.747, float3(1, 1, 1.603), 29, 1.38, false },
    { float3(0, 0, 164.259), 37, float3(1.603, 1, 1), 29, 1.38, false },
    { float3(0, 0, 361.068), -172.809, float3(1, 1, 1.58913), 26.2, 1.38, false },
    { float3(0, 0, 146.265), 39.894, float3(1.58913, 1, 1), 26.2, 1.38, false },
    { float3(0, 0, 135.339), 49.82, float3(1, 1, 1.86074), 20, 1.38, false },
    { float3(0, 0, 106.009), 74.75, float3(1.86074, 1, 1), 20, 1.38, false },
    { float3(0, 0, 64.215), 63.402, float3(1, 1, 1.86074), 16.1, 1.38, false },
    { float3(0, 0, 88.487), 37.53, float3(1.86074, 1, 1.5168), 16.1, 1.38, false },
    { float3(0, 0, 193.304), -75.887, float3(1.5168, 1, 1.80458), 16, 1.38, false },
    { float3(0, 0, 213.609), -97.792, float3(1.80458, 1, 1), 16.5, 1.38, false },
    { float3(0, 0, 12.72), 96.034, float3(1, 1, 1.62041), 18, 1.38, false },
    { float3(0, 0, -156.589), 261.743, float3(1.62041, 1, 1), 18, 1.38, false },
    { float3(0, 0, 50.792), 54.262, float3(1, 1, 1.6968), 18, 1.38, false },
    { float3(0, 0, 6094.33), -5995.28, float3(1.6968, 1, 1), 18, 1.38, false },
    { float3(0, 0, 97.522), 0, float3(1, 1, 1), 10, 1.38, true },
    { float3(0, 0, 169.136), -74.414, float3(1, 1, 1.90265), 13, 1.38, false },
    { float3(0, 0, 155.451), -62.929, float3(1.90265, 1, 1.5168), 13, 1.38, false },
    { float3(0, 0, -30.308), 121.38, float3(1.5168, 1, 1), 13.1, 1.38, false },
    { float3(0, 0, 174.295), -85.723, float3(1, 1, 1.49782), 13, 1.38, false },
    { float3(0, 0, 56.079), 31.093, float3(1.49782, 1, 1.80458), 13.1, 1.38, false },
    { float3(0, 0, -0.186005), 84.758, float3(1.80458, 1, 1), 13, 1.38, false },
    { float3(0, 0, -392.007), 459.69, float3(1, 1, 1.86074), 15, 1.38, false },
    { float3(0, 0, 26.043), 40.24, float3(1.86074, 1, 1.49782), 15, 1.38, false },
    { float3(0, 0, 108.754), -49.771, float3(1.49782, 1, 1), 15.2, 1.38, false },
    { float3(0, 0, -3.486), 62.369, float3(1, 1, 1.67025), 16, 1.38, false },
    { float3(0, 0, 128.337), -76.454, float3(1.67025, 1, 1), 16, 1.38, false },
    { float3(0, 0, 79.207), -32.524, float3(1, 1, 1.80454), 17, 1.38, false },
    { float3(0, 0, 94.877), -50.194, float3(1.80454, 1, 1), 17, 1.38, false },
    { float3(0, 0, 5), 0, float3(1, 1, 1), 0, 1.38, true },

};

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
	float ts01 = 2.f * sin(theta1) *cos(theta0) / sin(theta0 + theta1);
	float tp01 = ts01*cos(theta0-theta1);

	float rs12 = -sin(theta1-theta2) / sin(theta1 + theta2);
	float rp12 =  tan(theta1-theta2) / tan(theta1 + theta2);

	float ris = ts01*ts01*rs12;
	float rip = tp01*tp01*rp12;
	float dy = d1*n1;
	float dx = tan(theta1) *dy;
	float delay = sqrt(dx*dx + dy*dy);
	float relPhase = 4.f * PI / lambda*(delay-dx*sin(theta0));

	float out_s2 = rs01*rs01 + ris*ris + 2.f * rs01*ris*cos(relPhase);
	float out_p2 = rp01*rp01 + rip*rip + 2.f * rp01*rip*cos(relPhase);
	
	if(isnan(out_s2))
		out_s2 = 1.f;

	if(isnan(out_p2))
		out_p2 = 1.f;


	out_s2 = saturate(out_s2);
	out_p2 = saturate(out_p2);

	return (out_s2 + out_p2) / 2.f; 
}

float Reflectance(float lambda, float d, float theta1, float n1, float n2, float n3)
{
    // Apply Snell's law to get the other angles
    float theta2 = asin(n1 * sin(theta1) / n2);
    float theta3 = asin(n1 * sin(theta1) / n3);
 
    float cos1 = cos(theta1);
    float cos2 = cos(theta2);
    float cos3 = cos(theta3);
 
    float beta = (2.0f * 3.14159265359) / lambda * n2 * d * cos2;
 
    // Compute the fresnel terms for the first and second interfaces for both s and p polarized
    // light
    float r12p = (n2 * cos1 - n1 * cos2) / (n2 * cos1 + n1 * cos2);
    float r12p2 = r12p * r12p;
 
    float r23p = (n3 * cos2 - n2 * cos3) / (n3 * cos2 + n2 * cos3);
    float r23p2 = r23p * r23p;
 
    float rp = (r12p2 + r23p2 + 2.0f * r12p * r23p * cos(2.0f * beta)) /
        (1.0f + r12p2 * r23p2 + 2.0f * r12p * r23p * cos(2.0f * beta));
 
    float r12s = (n1 * cos1 - n2 * cos2) / (n1 * cos1 + n2 * cos2);
    float r12s2 = r12s * r12s;
 
    float r23s = (n2 * cos2 - n3 * cos3) / (n2 * cos2 + n3 * cos3);
    float r23s2 = r23s * r23s;
 
    float rs = (r12s2 + r23s2 + 2.0f * r12s * r23s * cos(2.0f * beta)) /
        (1.0f + r12s2 * r23s2 + 2.0f * r12s * r23s * cos(2.0f * beta));
 
    return (rs + rp) * 0.5f;
}

Ray Trace( Ray r, float lambda, int2 STR) {

	static int2 BOUNCE[NUM_BOUNCE];
	static int LENGTH[NUM_BOUNCE];

	int LEN = STR.x + (STR.x - STR.y) + (NUM_INTERFACE - STR.y) - 1;

	int PHASE = 0;
	int DELTA = 1;
	int T = 1;
	int k;

	[loop]
	for( k=0; k < LEN; k++, T += DELTA ) {
		
		LensInterface F = interfaces[T];

		bool bReflect = (T==STR[PHASE]) ? true : false;
		if (bReflect) { DELTA = -DELTA; PHASE++; } // intersection test

		Intersection i;
		if(F.flat)
			i = testFLAT(r,F);
		else 
			i = testSPHERE(r,F);

		[branch]
		if (!i.hit){
			r.pos = i.pos;
			break; // exit upon miss
		}

		// record texture coord . or max. rel . radius
		[branch]
		if (!F.flat)
			r.tex.z = max(r.tex.z, length(i.pos.xy) / F.sa);
		else if(T==AP_IDX) // iris aperture plane
			r.tex.xy = i.pos.xy/interfaces[AP_IDX].sa; // update ray direction and position

		r.dir = normalize(i.pos- r.pos);

		if (i.inverted) r.dir *= -1.f; // correct an ← inverted ray
		
		r.pos = i.pos;

		// skip reflection / refraction for flat surfaces
		if (F.flat) continue;

		// do reflection / refraction for spher . surfaces
		float n0 = r.dir.z < 0.f ? F.n.x : F.n.z;
		float n1 = F.n.y;
		float n2 = r.dir.z < 0.f ? F.n.z : F.n.x;

		//float n0 = r.dir.z < 0.f ? F.n.z : F.n.x;
		//float n1 = F.n.y;
		//float n2 = r.dir.z < 0.f ? F.n.x : F.n.z;

		if (!bReflect) // refraction
		{
			r.dir = refract(r.dir,i.norm,n0/n2);
			if(length(r.dir)==0) break; // total reflection
		}
		else // reflection with AR Coating
		{
			r.dir = reflect(r.dir,i.norm);

			float _lambda = 5.0e-7;
			float _n1 = max(sqrt(n0*n2), 1 + n2 * 2); // 1.38= lowest achievable d1 = lambda0 / 4 / n1; // phase delay
			float _Fd1 = _lambda / 4.f * 2;

			//float R = Reflectance(i.theta, _lambda, _Fd1, n0, _n1, n2);
			float R = FresnelAR(i.theta, _lambda, _Fd1, n0, _n1, n2);

			r.tex.a *= R; // update ray intensity
		}
	}

	if (k<LEN) r.tex.a = 0; // early−exit rays = invalid
	
	return r;
}

uint pos_to_offset(int2 pos) {
	return pos.x + pos.y * PATH_TESSELATION;
}

uint pos_to_offset_clamped(int2 pos) {
	int x = clamp(pos.x, 0, PATH_TESSELATION);
	int y = clamp(pos.y, 0, PATH_TESSELATION);
	return x + y * PATH_TESSELATION;
}

float get_area(int2 pos) {
	int c1 = pos_to_offset_clamped(pos + int2(-1, 1));
	int c2 = pos_to_offset_clamped(pos + int2( 1, 1));
	int c3 = pos_to_offset_clamped(pos + int2(-1,-1));
	int c4 = pos_to_offset_clamped(pos + int2( 1,-1));

	return BufferA[c1].position.x;
}

// Compute
// ----------------------------------------------------------------------------------
[numthreads(NUM_THREADS, NUM_THREADS, 1)]
void CS(int3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID) {

	int2 pos = gid * NUM_THREADS + gtid;
	float2 uv = pos / float(PATH_TESSELATION - 1);
	float2 ndc = (uv - 0.5f) * 2.f;

	float3 a1 = float3(ndc * spread, 400.f);
	float3 d1 = float3(0, 0, -1.f);
	
	Ray r1 = { a1, d1, float4(0,0,0,0) };
	Intersection i1 = testSPHERE(r1, interfaces[0]);
	float3 a2 = i1.pos - d1;

	Ray r = { a2, d1, float4(0,0,0,1) };
	Ray g = Trace(r, 1.f, int2(g1, g2));

	PS_INPUT result;
	result.position = float4(g.pos.xy * GLOBAL_SCALE, 0.f, 1.f);
	result.position.y *= 2.f;
	result.mask = float4(ndc, 0, 0);
	result.color = g.tex;


	uint offset = pos_to_offset(pos);
	BufferA[offset] = result;

	GroupMemoryBarrierWithGroupSync();

	//float area = get_area(pos);
	//BufferA[offset].Texture.b += area;
}

// Vertex Shader
// ----------------------------------------------------------------------------------
PS_INPUT VS( float4 Pos : POSITION, uint id : SV_VertexID ) {
	return Buffer0[id];
}

// Pixel Shader
// ----------------------------------------------------------------------------------
float4 PS( in PS_INPUT input ) : SV_Target {
	float4 color = input.color;
	float alpha = (color.z <= 1.f);
    return float4(color.rgb, alpha);

}
