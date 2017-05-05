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
	float4 Mask : TEXCOORD1;
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
#define AP_IDX 14
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
			r.tex.z = max(r.tex.z, length(i.pos.xy) / F.h);
		else if(T==AP_IDX) // iris aperture plane
			r.tex.xy = i.pos.xy/interfaces[AP_IDX].radius; // update ray direction and position

		r.dir = normalize(i.pos- r.pos);

		if (i.inverted) r.dir *= -1.f; // correct an ← inverted ray
		
		r.pos = i.pos;

		// skip reflection / refraction for flat surfaces
		if (F.flat) continue;

		// do reflection / refraction for spher . surfaces
		float n0 = r.dir.z < 0.f ? F.n.x : F.n.z;
		float n1 = F.n.y;
		float n2 = r.dir.z < 0.f ? F.n.z : F.n.x;

		if (!bReflect) // refraction
		{
			r.dir = refract(r.dir,i.norm,n0/n2);
			if(length(r.dir)==0) break; // total reflection
		}
		else // reflection with AR Coating
		{
			r.dir = reflect(r.dir,i.norm);
			float _lambda = 5.4e-7;
			float _n1 = max(sqrt(n0*n2) , 1.38) ; // 1.38= lowest achievable d1 = lambda0 / 4 / n1; // phase delay
			float _Fd1 = _lambda / 4.f / _n1;
			//float R = Reflectance(i.theta, _lambda, _Fd1, n0, _n1, n2) * 0.1;
			float R = FresnelAR(i.theta, _lambda, _Fd1, n0, _n1, n2);
			r.tex.a *= R; // update ray intensity
		}
	}

	if (k<LEN) r.tex.a=0; // early−exit rays = invalid
	return r;
}

//--------------------------------------------------------------------------------------
// Flare Vertex Shader
//--------------------------------------------------------------------------------------
PS_INPUT VS( float4 Pos : POSITION ) {
	PS_INPUT res;
	res.Position = Pos;
	res.Mask = Pos;
	
	Ray r;
	r.tex = float4(0,0,0,1);
	r.pos = float3(Pos.xy * 1.f, 300.f);
	r.dir = normalize(float3(0.f, 0.F, -1.f));

	Ray g = Trace(r, 1.f, int2(g1, g2));

	res.Position.xyz = float3(g.pos.xy * 0.0009f *10.f, 0.f);
	res.Position.x *= 0.5f;

	res.Texture = g.tex;

	return res;
}

[maxvertexcount(3)]
void GS(triangle PS_INPUT input[3], inout TriangleStream<PS_INPUT> outputStream)
{

	PS_INPUT p0 = input[0];
	PS_INPUT p1 = input[1];
	PS_INPUT p2 = input[2];

	outputStream.Append(p0);
	outputStream.Append(p1);
	outputStream.Append(p2);

	outputStream.RestartStrip();
}

//--------------------------------------------------------------------------------------
// Flare Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS( in PS_INPUT In ) : SV_Target {

	float light_intensity = length(In.Mask.xy) < 1.0f ? 1.f : 0.f;
	float4 color = In.Texture;
	float alpha = color.a * (color.z <= 1.f);
	float v = 100.f;//saturate(color.z);
    return float4(v,v,v, alpha * light_intensity * 10);

}
