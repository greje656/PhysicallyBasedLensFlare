#define PI 3.14159265359f
#define TWOPI PI * 2.f

#define NANO_METER 0.0000001
#define NUM_THREADS 32

#define AP_IDX 14
#define PATCH_TESSELATION 32

SamplerState LinearSampler {
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Wrap;
    AddressV = Wrap;
};

struct PSInput {
	float4 pos : SV_POSITION;
	float4 color : TEXCOORD0;
	float4 mask : TEXCOORD1;
	float4 reflectance : TEXCOORD2;
};

struct StarbustInput {
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD0;
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

// Constant buffers
cbuffer GlobalData : register(b1) {
	float time;
	float spread;
	float plate_size;
	float aperture_id;

	float num_interfaces;
	float coating_quality;
	float2 backbuffer_size;

	float3 light_dir;
	float aperture_resolution;

	float aperture_opening;
	float number_of_blades;
	float starburst_resolution;
	float padding;
};

cbuffer GhostData : register(b2) {
	float bounce1;
	float bounce2;
};

// Bounded buffers
Texture2D input_texture1 : register(t1);
Texture2D input_texture2 : register(t2);
StructuredBuffer<PSInput> vertices_buffer : register(t0);
RWStructuredBuffer<PSInput> uav_buffer : register(u0);
RWStructuredBuffer<LensInterface> lens_interface : register(u1);

static float4 temperature_color_map[25] = {
	float4(    0.0, 0.000, 0.000, 0.000),
	float4( 1000.0, 1.000, 0.007, 0.000),
	float4( 1500.0, 1.000, 0.126, 0.000),
	float4( 2000.0, 1.000, 0.234, 0.010),
	float4( 2500.0, 1.000, 0.349, 0.067),
	float4( 3000.0, 1.000, 0.454, 0.151),
	float4( 3500.0, 1.000, 0.549, 0.254),
	float4( 4000.0, 1.000, 0.635, 0.370),
	float4( 4500.0, 1.000, 0.710, 0.493),
	float4( 5000.0, 1.000, 0.778, 0.620),
	float4( 5500.0, 1.000, 0.837, 0.746),
	float4( 6000.0, 1.000, 0.890, 0.869),
	float4( 6500.0, 1.000, 0.937, 0.988),
	float4( 7000.0, 0.907, 0.888, 1.000),
	float4( 7500.0, 0.827, 0.839, 1.000),
	float4( 8000.0, 0.762, 0.800, 1.000),
	float4( 8500.0, 0.711, 0.766, 1.000),
	float4( 9000.0, 0.668, 0.738, 1.000),
	float4( 9500.0, 0.632, 0.714, 1.000),
	float4(10000.0, 0.602, 0.693, 1.000),
	float4(12000.0, 0.518, 0.632, 1.000),
	float4(14000.0, 0.468, 0.593, 1.000),
	float4(16000.0, 0.435, 0.567, 1.000),
	float4(18000.0, 0.411, 0.547, 1.000),
	float4(20000.0, 0.394, 0.533, 1.000)
};

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

float FresnelReflectance(float lambda, float d, float theta1, float n1, float n2, float n3) {
	float ni = n1;
	float nt = n3;

	float Oi = theta1;
	float Ot = asin((ni/nt) * sin(Oi));

	float Rs = ( ni*cos(Oi) - nt*cos(Ot) ) / ( ni*cos(Oi) + nt*cos(Ot) );
	float Rp = ( ni*cos(Ot) - nt*cos(Oi) ) / ( ni*cos(Ot) + nt*cos(Oi) );

	return (Rs + Rp) * 0.5;
}

float FresnelAR(float theta0, float lambda, float d1, float n0, float n1, float n2) {
	float theta1 = asin(sin(theta0) *n0 / n1);
	float theta2 = asin(sin(theta0) *n0 / n2);

	float rs01 = -sin(theta0-theta1) / sin(theta0+theta1);
	float rp01 = tan(theta0-theta1) / tan(theta0+theta1);
	float ts01 = 2*sin(theta1) *cos(theta0) / sin(theta0+theta1);
	float tp01 = ts01*cos(theta0-theta1);

	float rs12 = -sin(theta1-theta2) / sin(theta1+theta2);
	float rp12 = +tan(theta1-theta2) / tan(theta1+theta2);

	float ris = ts01*ts01*rs12;
	float rip = tp01*tp01*rp12;

	float dy = d1*n1 ;
	float dx = tan(theta1) *dy;
	float delay = sqrt(dx*dx+dy*dy);
	float relPhase = 4*PI / lambda*(delay-dx*sin(theta0) );

	float out_s2 = rs01*rs01 + ris*ris + 2*rs01*ris*cos(relPhase);
	float out_p2 = rp01*rp01 + rip*rip + 2*rp01*rip*cos(relPhase);

	return (out_s2+out_p2) / 2 ;
}


Ray Trace(Ray r, float lambda, int2 bounce_pair) {

	int LEN = bounce_pair.x + (bounce_pair.x - bounce_pair.y) + (num_interfaces - bounce_pair.y) - 1;
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
			i = TestFlat(r, lens_interface[T]);
		else 
			i = TestSphere(r, lens_interface[T]);

		[branch]
		if (!i.hit){
			r.pos = 0;
			r.tex.a = 0;
			break; // exit upon miss
		}

		// record texture coord . or max. rel . radius
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
		// float n1 = F.n.y;
		float n2 = r.dir.z < 0.f ? F.n.x : F.n.z;

		if (!bReflect) { // refraction
			r.dir = refract(r.dir,i.norm,n0/n2);

			[branch]
			if(length(r.dir) == 0){
				r.pos = 0;
				r.tex.a = 0;
				break; // total reflection
			}
		} else { // reflection with AR Coating
			r.dir = reflect(r.dir,i.norm);

			float n1 = max(sqrt(n0*n2) , 1.38 + coating_quality);
			float d1 = (F.d1 * NANO_METER);
			float R = FresnelAR(i.theta + 0.001, lambda, d1, n0, n1, n2);
			R = saturate(R);

			r.tex.a *= R; // update ray intensity
		}
	}

	[branch]
	if (k<LEN) {
		r.pos = 0;
		r.tex.a = 0; // early-exit rays = invalid
	}
	
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

	float energy = 1.f;
	float area = (Oa/(Na + 0.00001)) * energy;

	return isnan(area) ? 0.f : area;
}

PSInput getTraceResult(float2 ndc, float wavelength){
	float3 starting_pos = float3(ndc * spread, 1000.f);
	
	// Project all starting points in the entry lens
	Ray c = { starting_pos, float3(0, 0, -1.f), float4(0,0,0,0) };
	Intersection i = TestSphere(c, lens_interface[0]);
	starting_pos = i.pos - light_dir.xyz;

	Ray r = { starting_pos, light_dir.xyz, float4(0,0,0,1) };
	Ray g = Trace(r, wavelength, int2(bounce1, bounce2));

	PSInput result;
	result.pos = float4(g.pos.xyz, 1.f);
	result.mask = float4(ndc, 0, 1);
	result.color = g.tex;
	result.reflectance = float4(0,0,0, g.tex.a);

	return result;
}

// Compute
// ----------------------------------------------------------------------------------
[numthreads(NUM_THREADS, NUM_THREADS, 1)]
void CS(int3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint gi : SV_GroupIndex) {
	int2 pos = gid.xy * NUM_THREADS + gtid.xy;
	float2 uv = pos / float(PATCH_TESSELATION - 1);
	float2 ndc = (uv - 0.5f) * 2.f;

	float color_spectrum[3] = {650.f, 510.f, 475.f};
	float wavelength = color_spectrum[gid.z] * NANO_METER;

	PSInput result = getTraceResult(ndc, wavelength);
	
	uint offset = PosToOffset(pos);
	uav_buffer[offset].reflectance.a = 0;

	AllMemoryBarrierWithGroupSync();

	uav_buffer[offset].pos = result.pos;
	uav_buffer[offset].color = result.color;
	uav_buffer[offset].mask = result.mask;

	if(gid.z == 0) {
		uav_buffer[offset].reflectance.r = result.reflectance.a;
	}else if(gid.z == 1){
		uav_buffer[offset].reflectance.g = result.reflectance.a;
	}else if(gid.z == 2){
		uav_buffer[offset].reflectance.b = result.reflectance.a;
	}

	uav_buffer[offset].mask.w = GetArea(pos);

}

// Vertex Shader
// ----------------------------------------------------------------------------------
PSInput VS(uint id : SV_VertexID) {
	PSInput vertex  = vertices_buffer[id];
	
	float ratio = backbuffer_size.x / backbuffer_size.y;
	float scale = 1.f / plate_size;
	vertex.pos.xy *= scale * float2(1.f, ratio);
	vertex.pos.w = 1;
	
	return vertex;
}

float3 TemperatureToColor(float t) {
	int index;
	for(int i=0; i < 25; ++i){
		if(t < temperature_color_map[i].x) {
			index = i;
			break;
		}
	}

	if(index > 0) {
		float4 lower = temperature_color_map[index - 1];
		float4 upper = temperature_color_map[index];
		float l = (t - lower.x)/(upper.x - lower.x);
		return lerp(lower.yzw, upper.yzw, l);
	} else {
		return 0;
	}
}

// Pixel Shader
// ----------------------------------------------------------------------------------
float4 PS(in PSInput input) : SV_Target {

	float4 color = input.color;
	float4 mask = input.mask;

	float2 aperture_uv = (color.xy + 1.f)/2.f;
	float aperture = input_texture1.Sample(LinearSampler, aperture_uv).g;
	
	float fade = 0.2;
	float lens_distance = length(mask.xy);
	float sun_disk = 1 - saturate((lens_distance - 1.f + fade)/fade);
	sun_disk = smoothstep(0, 1, sun_disk);
	sun_disk *= lerp(0.5, 1, saturate(lens_distance));

	float alpha1 = color.z < 1.0f;
	float alpha2 = sun_disk;
	float alpha3 = mask.w;
	float alpha4 = aperture;
	float alpha = alpha1 * alpha2 * alpha3 * alpha4;

	[branch]
	if(alpha == 0.f)
		discard;

	#if defined(DEBUG_WIREFRAME)
		return float4(aperture_uv, 0.0, 1);
	#endif

	#if defined(DEBUG_VALUES)
		return float4(alpha, alpha, alpha ,1);
	#endif

	float3 v = alpha * input.reflectance.xyz * TemperatureToColor(6000);
	
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
	float3 c = input_texture1.Load(int3(pos.xy,0)).rgb;
	c = ACESFilm(c);
	return float4(c, 1);
}

float3 wl2rgbTannenbaum(float w){
	float3 r;

	if(w < 350.0)
		r = float3(0.5, 0.0, 1.0);
	else if((w >= 350.0) && (w < 440.0))
		r = float3((440.0 - w) / 90.0, 0.0, 1.0);
	else if((w >= 440.0) && (w <= 490.0))
		r = float3(0.0, (w - 440.0) / 50.0, 1.0);
	else if((w >= 490.0) && (w < 510.0))
		r = float3(0.0, 1.0, (-(w - 510.0)) / 20.0);
	else if ((w >= 510.0) && (w < 580.0))
		r = float3((w - 510.0) / 70.0, 1.0, 0.0);
	else if((w >= 580.0) && (w < 645.0))
		r = float3(1.0, (-(w - 645.0)) / 65.0, 0.0);
	else
		r = float3(1.0, 0.0, 0.0);
	
	if(w < 350.0)
		r *= 0.3;
	else if((w >= 350.0) && (w < 420.0))
		r *= 0.3 + (0.7 * ((w - 350.0) / 70.0));
	else if((w >= 420.0) && (w <= 700.0))
		r *= 1.0;
	else if((w > 700.0) && (w <= 780.0))
		r *= 0.3 + (0.7 * ((780.0 - w) / 80.0));
	else
		r *= 0.3;

	return r;
}

float nrand( float2 n ) {
	return frac(sin(dot(n.xy, float2(12.9898, 78.233)))* 43758.5453);
}

float n1rand( float2 n ) {
	float t = frac(0);
	float nrnd0 = nrand(n + 0.07f * t);
	return nrnd0;
}

float3 intersectPlane(float3 n, float3 p0, float3 l0, float3 l) { 
    // assuming vectors are all normalized
    float denom = dot(n, l); 
    if (denom > 1e-6) { 
        float3 p0l0 = p0 - l0; 
        float t = dot(p0l0, n) / denom; 
        return p0 + l * t;
    } 
 
    return 0; 
} 

StarbustInput VSStarburst(float4 pos : POSITION ) {
	StarbustInput result;
	
	result.pos = float4(pos.xy * 0.5f, 0.f, 1.f);
	result.pos.xy *= 0.5;

	float3 n = float3(0, 0, -1);
	float3 p0 = float3(0, 0, 10);
	float3 l0 = float3(0, 0, 0);
	float3 c = intersectPlane(n, p0, l0, light_dir);

	result.pos.xy += c.xy;

	result.uv = (pos.xy + float2(1.f, 2.f)) * float2(0.5f, 0.25f);

	return result;
}

float4 PSStarburst(StarbustInput input) : SV_Target {
	float3 starburst = input_texture1.Sample(LinearSampler, input.uv).rgb;
	return float4(starburst, 1.f);
}

float4 PSStarburstFromFFT(float4 pos : SV_POSITION ) : SV_Target {

	float2 uv = pos.xy / starburst_resolution - 0.5f;

	float nvalue = n1rand(uv);

	float3 result = 0;
	int num_steps = 256;

	// -ve violet, +v reds
	float scale = 1 + nvalue * 0.1;
	for(int i = 0; i <= num_steps; ++i) {
		float n = (float)i/(float)num_steps;
		float2 scaled_uv = uv * lerp(1.0 + scale, 1.0, n);
		bool clamped = scaled_uv.x < -0.5 || scaled_uv.x > 0.5 || scaled_uv.y < -0.5 || scaled_uv.y > 0.5;

		float r = input_texture1.Sample(LinearSampler, scaled_uv).r * !clamped;
		float i = input_texture2.Sample(LinearSampler, scaled_uv).r * !clamped;
		float2 p = float2(r, i);

		float v = pow(length(p), 2.f);

		float lambda = lerp(350.f, 650.f, n);
		float3 rgb = wl2rgbTannenbaum(lambda);
		result += v * rgb;
	}

	result /= (float)num_steps;

	return float4(result, 1);
}

float2 rotate(float2 p, float a) {
	float x = p.x;
	float y = p.y;

	float cosa = cos(a);
	float sina = sin(a);

	float x1 = x * cosa - y * sina;
	float y1 = y * cosa + x * sina;

	return float2(x1, y1);
}

float4 PSStarburstFilter(float4 pos : SV_POSITION ) : SV_Target {
	float2 uv = pos.xy / starburst_resolution - 0.5;

	float3 result = 0;
	int num_steps = 256;
	
	for(int i = 0; i <= num_steps; ++i){
		float n = (float)i/(float)num_steps;
		float a = n * TWOPI * 5;
		float2 spiral = float2(cos(a), sin(a)) * n * 0.0005;
		float2 jittered_uv = uv + spiral;
		float2 scaled_uv = jittered_uv;

		float2 rotated_uv = rotate(scaled_uv, n * 0.1);
		scaled_uv = rotated_uv;
		bool clamped = scaled_uv.x < -0.5 || scaled_uv.x > 0.5 || scaled_uv.y < -0.5 || scaled_uv.y > 0.5;

		float3 rgb = input_texture1.Sample(LinearSampler, scaled_uv + 0.5).rgb * !clamped;
		result += rgb;
	}

	result /= (float)num_steps;

	return float4(result, 1);
}

float fade_aperture_edge(float radius, float fade, float signed_distance) {
	float l = radius;
	float u = radius + fade;
	float s = u - l;
	float c = 1.f - saturate(saturate(signed_distance - l)/s);
	return smoothstep(0, 1, c);
}

float4 PSAperture(float4 pos : SV_POSITION) : SV_Target {

	float2 uv = pos.xy / aperture_resolution;
	float2 ndc = ((uv - 0.5f) * 2.f);

	int num_blades = number_of_blades;
	float angle_offset = aperture_opening;

	float signed_distance = 0.f;
	for(int i = 0; i < num_blades; ++i) {
		float angle = angle_offset + (i/float(num_blades)) * PI * 2;
		float2 axis = float2(cos(angle), sin(angle));
		signed_distance = max(signed_distance, dot(axis, ndc));
	}

	float aperture_fft = fade_aperture_edge(0.5, 0.2, signed_distance);
	float aperture_mask = fade_aperture_edge(0.75, 0.05, signed_distance);

	{ // Diffraction rings
		float w = 0.1;
		float s = signed_distance + 0.1;
		float n = saturate(saturate(s + w) - (1.f - w));
		
		float x = n/w;
		float a = x;
		float b = -x + 1.f;
		float c = min(a,b) * 2.f;
		float t = (sin(x * 6.f * PI - 1.5f) + 1.f) * 0.5f;
		float rings = pow(t*c, 1.f);
		aperture_mask = aperture_mask + rings * 0.125;
	}

	{ // Dust
		float dust = input_texture1.Sample(LinearSampler, uv).r;
		aperture_fft *= saturate(dust + 0.995);
		aperture_mask *= saturate(dust + 0.95);
	}

	float3 rgb = float3(aperture_fft, aperture_mask, 0);

	return float4(rgb, 1);
}
