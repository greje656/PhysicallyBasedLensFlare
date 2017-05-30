#define PI 3.14159265359f
#define NANO_METER 0.0000001
#define NUM_THREADS 32

#define AP_IDX 14
#define PLATE_SIZE 10.f
#define NUM_INTERFACE 29
#define PATCH_TESSELATION 32

struct PSInput {
	float4 pos : SV_POSITION;
	float4 color : TEXCOORD0;
	float4 mask : TEXCOORD1;
};

struct Intersection {
	float3 pos;
	float3 norm;
	float theta;
	bool hit;
	bool inverted;
};

struct LensInterface {
	float3 center;
	float radius;

	float3 n;
	float sa;

	float d1;
	float flat;
	float pos;
	float w;
};

struct Ray {	
	float3 pos;
	float3 dir;
	float4 tex;
};

cbuffer GlobalData : register(b1) {
	float time;
	float spread;
	float2 backbuffer_size;
	float4 light_dir;
};

cbuffer GhostData : register(b2) {
	float bounce1;
	float bounce2;
};

Texture2D hdr_texture : register(t1);
StructuredBuffer<PSInput> vertices_buffer : register(t0);
RWStructuredBuffer<PSInput> uav_buffer : register(u0);
RWStructuredBuffer<LensInterface> lens_interface : register(u1);

Intersection TestFlat(Ray r, LensInterface F) {
	Intersection i;
	i.pos = r.pos + r.dir * ((F.center.z - r.pos.z) / r.dir.z);
	i.norm = r.dir.z > 0 ? float3(0, 0, -1) : float3(0, 0, 1);
	i.theta = 0;
	i.hit = true;
	i.inverted = false;
	return i;
}

Intersection TestSphere(Ray r, LensInterface F) {
	Intersection i;
	
	float3 D = r.pos - F.center;
	float B = dot(D, r.dir);
	float C = dot(D, D) - F.radius * F.radius;
	float B2_C = B*B-C;

	if (B2_C < 0) {
		i.hit = false;
		return i; 
	}

	float sgn = (F.radius * r.dir.z) > 0 ? 1.f : -1.f;
	float t = sqrt(B2_C) * sgn - B;
	i.pos = r.dir * t + r.pos;
	i.norm = normalize(i.pos - F.center);
	
	if (dot(i.norm, r.dir) > 0)
		i.norm = -i.norm;

	float d = clamp(-1, 1, dot(-r.dir, i.norm));
	i.theta = acos(d);
	i.hit = true;
	i.inverted = t < 0;
	
	return i;
}

float Reflectance(float lambda, float d, float theta1, float n1, float n2, float n3) {

	// Apply Snell's law to get the other angles
	float theta2 = asin(n1 * sin(theta1) / n2);
	float theta3 = asin(n1 * sin(theta1) / n3);

	float cos1 = cos(theta1);
	float cos2 = cos(theta2);
	float cos3 = cos(theta3);

	float beta = (2.0f * PI) / lambda * n2 * d * cos2;

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

Ray Trace(Ray r, float lambda, int2 bounce_pair) {

	int LEN = bounce_pair.x + (bounce_pair.x - bounce_pair.y) + (NUM_INTERFACE - bounce_pair.y) - 1;
	int PHASE = 0;
	int DELTA = 1;
	int T = 1;

	int k;
	for(k=0; k < LEN; k++, T += DELTA) {
		
		LensInterface F = lens_interface[T];

		bool bReflect = (T == bounce_pair[PHASE]) ? true : false;
		if (bReflect) { DELTA = -DELTA; PHASE++; } // intersection test

		Intersection i;
		if(F.flat)
			i = TestFlat(r,lens_interface[T]);
		else 
			i = TestSphere(r,lens_interface[T]);

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
			r.tex.xy = i.pos.xy / lens_interface[AP_IDX].sa; // update ray light_dir and position

		r.dir = normalize(i.pos- r.pos);

		if (i.inverted) r.dir *= -1.f; // correct an ← inverted ray
		
		r.pos = i.pos;

		// skip reflection / refraction for flat surfaces
		if (F.flat)
			continue;

		// do reflection / refraction for spher . surfaces
		float n0 = r.dir.z < 0.f ? F.n.z : F.n.x;
		float n1 = F.n.y;
		float n2 = r.dir.z < 0.f ? F.n.x : F.n.z;

		if (!bReflect) { // refraction
			r.dir = refract(r.dir,i.norm,n0/n2);
			if(length(r.dir) == 0)
				break; // total reflection
		} else { // reflection with AR Coating
			r.dir = reflect(r.dir,i.norm);

			float _n1 = n0;
			float _n2 = n1; _n2 = max(sqrt(_n1*_n2) , 1.38);
			float _n3 = n2;
			float d1 = (560.0 * NANO_METER) / 4.f / _n1;
			float R = Reflectance(i.theta, lambda, d1, _n1, _n2, _n3);

			R = saturate(R);
			r.tex.a *= R; // update ray intensity
		}
	}

	if (k<LEN)
		r.tex.a = 0; // early−exit rays = invalid
	
	return r;
}

uint PosToOffset(int2 pos) {
	return pos.x + pos.y * PATCH_TESSELATION;
}

uint PosToOffsetClamped(int2 pos) {
	int x = clamp(pos.x, 0, PATCH_TESSELATION - 1);
	int y = clamp(pos.y, 0, PATCH_TESSELATION - 1);
	return x + y * PATCH_TESSELATION;
}

float GetArea(int2 pos) {

	// a----b----c
	// |  A |  B |
	// d----e----f
	// |  C |  D |
	// g----h----i

	int a = PosToOffsetClamped(pos + int2(-1, 1));
	int b = PosToOffsetClamped(pos + int2( 0, 1));
	int c = PosToOffsetClamped(pos + int2( 1, 1));
	int d = PosToOffsetClamped(pos + int2(-1, 0));
	int e = PosToOffsetClamped(pos + int2( 0, 0));
	int f = PosToOffsetClamped(pos + int2( 1, 0));
	int g = PosToOffsetClamped(pos + int2(-1,-1));
	int h = PosToOffsetClamped(pos + int2( 0,-1));
	int i = PosToOffsetClamped(pos + int2( 1,-1));

	float4 pa = uav_buffer[a].pos;
	float4 pb = uav_buffer[b].pos;
	float4 pc = uav_buffer[c].pos;
	float4 pd = uav_buffer[d].pos;
	float4 pe = uav_buffer[e].pos;
	float4 pf = uav_buffer[f].pos;
	float4 pg = uav_buffer[g].pos;
	float4 ph = uav_buffer[h].pos;
	float4 pi = uav_buffer[i].pos;

	float ab = length(pa.xy - pb.xy);
	float bc = length(pb.xy - pc.xy);
	float ad = length(pa.xy - pd.xy);
	float be = length(pb.xy - pe.xy);
	float cf = length(pc.xy - pf.xy);
	float de = length(pd.xy - pe.xy);
	float ef = length(pe.xy - pf.xy);
	float dg = length(pd.xy - pg.xy);
	float eh = length(pe.xy - ph.xy);
	float fi = length(pf.xy - pi.xy);
	float gh = length(pg.xy - ph.xy);
	float hi = length(ph.xy - pi.xy);

	bool left_edge   = (pos.x == 0);
	bool right_edge  = (pos.x == (PATCH_TESSELATION - 1));
	bool bottom_edge = (pos.y == 0);
	bool top_edge    = (pos.y == (PATCH_TESSELATION - 1));

	float A = lerp(ab, de, 0.5f) * lerp(ad, be, 0.5f) * (!left_edge  && !top_edge);
	float B = lerp(bc, ef, 0.5f) * lerp(be, cf, 0.5f) * (!right_edge && !top_edge);
	float C = lerp(de, gh, 0.5f) * lerp(dg, eh, 0.5f) * (!left_edge  && !bottom_edge);
	float D = lerp(ef, hi, 0.5f) * lerp(eh, fi, 0.5f) * (!right_edge && !bottom_edge);

	bool is_edge = (left_edge || right_edge) || (bottom_edge || top_edge);
	bool is_corner = (left_edge || right_edge) && (bottom_edge || top_edge);
	float no_area_contributors = is_corner ? 1.f : is_edge ? 2.f : 4.f;

	float unit_patch_length = spread / (float)PATCH_TESSELATION;
	float Oa = unit_patch_length * unit_patch_length * no_area_contributors;
	float Na = (A + B + C + D) / no_area_contributors;

	float energy = 1;
	
	return (Oa/Na) * energy;

}

PSInput getTraceResult(float2 ndc){
	float3 starting_pos = float3(ndc * spread, 400.f);
	
	// Project all starting points in the entry lens
	Ray c = { starting_pos, float3(0, 0, -1.f), float4(0,0,0,0) };
	Intersection i = TestSphere(c, lens_interface[0]);
	starting_pos = i.pos - light_dir.xyz;

	Ray r = { starting_pos, light_dir.xyz, float4(0,0,0,1) };
	Ray g = Trace(r, 450.f * NANO_METER, int2(bounce1, bounce2));

	PSInput result;
	result.pos = float4(g.pos.xyz, 1.f);
	result.mask = float4(ndc, 0, 1);
	result.color = g.tex;

	return result;
}

// Compute
// ----------------------------------------------------------------------------------
[numthreads(NUM_THREADS, NUM_THREADS, 1)]
void CS(int3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID) {
	int2 pos = gid.xy * NUM_THREADS + gtid.xy;
	float2 uv = pos / float(PATCH_TESSELATION - 1);
	float2 ndc = (uv - 0.5f) * 2.f;

	PSInput result = getTraceResult(ndc);
	
	uint offset = PosToOffset(pos);
	uav_buffer[offset] = result;

	AllMemoryBarrierWithGroupSync();

	uav_buffer[offset].mask.w = GetArea(pos);

}

// Vertex Shader
// ----------------------------------------------------------------------------------
PSInput VS(uint id : SV_VertexID) {
	PSInput vertex  = vertices_buffer[id];
	
	float ratio = backbuffer_size.x / backbuffer_size.y;
	float scale = 1.f / PLATE_SIZE;
	vertex.pos.xy *= scale * float2(1.f, ratio);
	vertex.pos.w = 1;
	
	return vertex;
}

// Pixel Shader
// ----------------------------------------------------------------------------------
float4 PS(in PSInput input) : SV_Target {
	float4 color = input.color;
	float4 mask = input.mask;
	
	float alpha1 = color.a;
	float alpha2 = color.z < 1.0f;
	float alpha3 = length(mask.xy) < 1.0;
	float alpha4 = length(color.xy) < 1.0f;
	float alpha5 = mask.w;
	float alpha = alpha1 * alpha2 * alpha3 * alpha4 * alpha5;

	[branch]
	if(alpha == 0.f)
		discard;

	float3 v = alpha * float3(232, 127, 61)/255.f;
	
	return float4(v, 1.f);
}

float3 ACESFilm(float3 x) {
	float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
	float d = 0.59f;
	float e = 0.14f;
	return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}

float4 PSToneMapping(float4 pos : SV_POSITION ) : SV_Target {
	float2 uv = pos.xy / backbuffer_size;
	float3 c = hdr_texture.Load(int3(pos.xy,0)).rgb;
	//c = ACESFilm(c);
	return float4(c, 1);
}

float4 PSAperture(float4 pos : SV_POSITION) : SV_Target {
	float2 uv = pos.xy / 1;//aperture_resolution;
	float2 ndc = (uv - 0.5f) * 2.f;
	float c = length(ndc) < 1.0f;

	return float4(c ,c ,c, 1);
}
