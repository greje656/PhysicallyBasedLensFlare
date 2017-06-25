#include "common.hlsl"

struct PSInput {
	float4 pos : SV_POSITION;
	float4 color : TEXCOORD0;
	float4 coordinates : TEXCOORD1;
	float4 reflectance : TEXCOORD2;
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

struct GhostData{
	float bounce1;
	float bounce2;
	float2 padding;
};

StructuredBuffer<PSInput> vertices_buffer : register(t0);
RWStructuredBuffer<PSInput> uav_buffer : register(u0);
RWStructuredBuffer<LensInterface> lens_interface : register(u1);
RWStructuredBuffer<GhostData> ghostdata_buffer : register(u2);

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
		float n0 = r.dir.z < 0.f ? F.n.x : F.n.z;
		// float n1 = F.n.y;
		float n2 = r.dir.z < 0.f ? F.n.z : F.n.x;

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

float GetArea(int2 pos, int offset) {

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

	float4 pa = uav_buffer[a + offset].pos;
	float4 pb = uav_buffer[b + offset].pos;
	float4 pc = uav_buffer[c + offset].pos;
	float4 pd = uav_buffer[d + offset].pos;
	float4 pe = uav_buffer[e + offset].pos;
	float4 pf = uav_buffer[f + offset].pos;
	float4 pg = uav_buffer[g + offset].pos;
	float4 ph = uav_buffer[h + offset].pos;
	float4 pi = uav_buffer[i + offset].pos;

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

	float energy = 4.f;
	float area = (Oa/(Na + 0.00001)) * energy;

	return isnan(area) ? 0.f : area;
}

PSInput getTraceResult(float2 ndc, float wavelength, int2 bounces){
	float3 starting_pos = float3(ndc * spread, 1000.f);
	
	// Project all starting points in the entry lens
	Ray c = { starting_pos, float3(0, 0, -1.f), float4(0,0,0,0) };
	Intersection i = TestSphere(c, lens_interface[0]);
	starting_pos = i.pos - light_dir.xyz;

	Ray r = { starting_pos, light_dir.xyz, float4(0,0,0,1) };
	Ray g = Trace(r, wavelength, bounces);

	PSInput result;
	result.pos = float4(g.pos.xyz, 1.f);
	result.coordinates = float4(ndc, g.tex.xy);
	result.color = g.tex;
	result.reflectance = float4(0,0,0, g.tex.a);

	return result;
}

// Compute
// ----------------------------------------------------------------------------------
[numthreads(NUM_THREADS, NUM_THREADS, 1)]
void CS(int3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint gi : SV_GroupIndex) {
	int2 pos = gtid.xy;
	float2 uv = pos / float(PATCH_TESSELATION - 1);
	float2 ndc = (uv - 0.5f) * 2.f;

	float color_spectrum[3] = {650.f, 510.f, 475.f};
	float wavelength = color_spectrum[gid.z] * NANO_METER;

	int2 bounces = int2(ghostdata_buffer[gid.x].bounce1, ghostdata_buffer[gid.x].bounce2);
	PSInput result = getTraceResult(ndc, wavelength, bounces);
	
	uint offset = PosToOffset(pos) + (gid.x * PATCH_TESSELATION * PATCH_TESSELATION);
	uav_buffer[offset].reflectance.a = 0;

	//AllMemoryBarrierWithGroupSync();

	uav_buffer[offset].pos = result.pos;
	uav_buffer[offset].color = result.color;
	uav_buffer[offset].coordinates = result.coordinates;

	if(gid.z == 0) {
		uav_buffer[offset].reflectance.r = result.reflectance.a;
	}else if(gid.z == 1){
		uav_buffer[offset].reflectance.g = result.reflectance.a;
	}else if(gid.z == 2){
		uav_buffer[offset].reflectance.b = result.reflectance.a;
	}

	uav_buffer[offset].color.w = GetArea(pos, (gid.x * PATCH_TESSELATION * PATCH_TESSELATION));

}

// Vertex Shader
// ----------------------------------------------------------------------------------
PSInput VS(uint id : SV_VertexID, uint instance_id : SV_InstanceID) {
	PSInput vertex  = vertices_buffer[id + instance_id * PATCH_TESSELATION * PATCH_TESSELATION];
	
	float ratio = backbuffer_size.x / backbuffer_size.y;
	float scale = 1.f / plate_size;
	vertex.pos.xy *= scale * float2(1.f, ratio);
	vertex.pos.w = 1;
	
	return vertex;
}

// Pixel Shader
// ----------------------------------------------------------------------------------
float4 PS(in PSInput input) : SV_Target {

	float4 color = input.color;
	float4 coordinates = input.coordinates;

	float2 aperture_uv = (coordinates.zw + 1.f)/2.f;
	float aperture = input_texture1.Sample(LinearSampler, aperture_uv).g;
	
	float fade = 0.2;
	float lens_distance = length(coordinates.xy);
	float sun_disk = 1 - saturate((lens_distance - 1.f + fade)/fade);
	sun_disk = smoothstep(0, 1, sun_disk);
	sun_disk *= lerp(0.5, 1, saturate(lens_distance));

	float aperture_disk = saturate(length(aperture_uv - 0.5) * 0.5);
	aperture_disk = smoothstep(0, 1, aperture_disk);
	aperture_disk = lerp(0.5, 1, aperture_disk);

	float alpha1 = color.z < 1.0f;
	float alpha2 = sun_disk;
	float alpha3 = color.w;
	float alpha4 = aperture;
	float alpha5 = aperture_disk;
	float alpha = alpha1 * alpha2 * alpha3 * alpha4 * alpha5;

	[branch]
	if(alpha == 0.f)
		discard;

	#if defined(DEBUG_WIREFRAME)
		return float4(aperture_uv, 0.0, 1);
	#endif

	#if defined(DEBUG_VALUES)
		return float4(alpha, alpha, alpha ,1);
	#endif

	float3 v = alpha * input.reflectance.xyz * TemperatureToColor(INCOMING_LIGHT_TEMP);
	
	return float4(v, 1.f);
}
