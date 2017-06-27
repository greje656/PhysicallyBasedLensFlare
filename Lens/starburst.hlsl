#include "common.hlsl"

struct StarbustInput {
	float4 pos : SV_POSITION;
	float3 uv : TEXCOORD0;
};

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

float3 IntersectPlane(float3 n, float3 p0, float3 l0, float3 l) { 
    float denom = dot(n, l); 

    if (denom > 1e-6) { 
        float3 p0l0 = p0 - l0; 
        float t = dot(p0l0, n) / denom; 
        return p0 + l * t;
    } 
 
    return 0; 
} 

float2 Rotate(float2 p, float a) {
	float x = p.x;
	float y = p.y;

	float cosa = cos(a);
	float sina = sin(a);

	float x1 = x * cosa - y * sina;
	float y1 = y * cosa + x * sina;

	return float2(x1, y1);
}

StarbustInput VSStarburst(float4 pos : POSITION, uint id : SV_VertexID) {
	StarbustInput result;

	float ratio = backbuffer_size.x / backbuffer_size.y;

	float3 n  = float3(0, 0, -1);
	float3 p0 = float3(0, 0, 20);
	float3 l0 = float3(0, 0,  0);
	float3 c  = IntersectPlane(n, p0, l0, light_dir);

	float intensity = 1.f - saturate(abs(light_dir.x * 8.f));

	float oo1 = 1.f - (sin(time * 5.f) + 1.f) * 0.025f;
	float oo2 = 1.f - (sin(time * 1.f) + 1.f) * 0.0125f;
	float oo = oo1 * oo2;
	intensity *= oo;

	float opening = 0.2f + saturate(aperture_opening/10.f);
	result.pos = float4(pos.xy * opening, 0.f, 1.f);
	result.pos.xy += c.xy * float2(0.5f, 1.f);

	//bool c1 = (id == 0) || (id == 3);
	//bool c4 = (id == 4);
	//float distance_to_edge = (light_dir.x * 3.f * opening);
	//if(c1)
	//	result.pos.x += clamp(distance_to_edge, -20, 0);
	//else if(c4)
	//	result.pos.x += clamp(distance_to_edge, 0, 20);

	result.uv.xy = (pos.xy * float2(1.f, 0.5f) + float2(1.f, 1.f)) * 0.5f;
	result.uv.z = intensity;
	return result;
}

float4 PSStarburst(StarbustInput input) : SV_Target {

	float2 uv = input.uv.xy;
	float intensity = (input.uv.z);// * lerp(1.f, 2.f, length(uv-0.5));
	float3 starburst = input_texture1.Sample(LinearSampler, input.uv.xy).rgb * intensity;
	starburst *= TemperatureToColor(INCOMING_LIGHT_TEMP);

	return float4(starburst, 1.f);
}

float4 PSStarburstFromFFT(float4 pos : SV_POSITION ) : SV_Target {

	float2 uv = pos.xy / starburst_resolution - 0.5;
	float nvalue = n1rand(uv);

	float3 result = 0.f;
	int num_steps = 256;

	float d = length(uv) * 2;
	
	// -ve violet, +v reds
	float scale1 =  0.5f;// + nvalue * 0.5;
	float scale2 = -0.75f;// + nvalue * 0.5;
	for(int i = 0; i <= num_steps; ++i) {
		float n = (float)i/(float)num_steps;
		
		float2 scaled_uv1 = uv * lerp(1.0 + scale1, 1.0, n);
		float2 scaled_uv2 = uv * lerp(1.0 + scale2, 1.0, n);
		bool clamped1 = scaled_uv1.x < -0.5 || scaled_uv1.x > 0.5 || scaled_uv1.y < -0.5 || scaled_uv1.y > 0.5;
		bool clamped2 = scaled_uv2.x < -0.5 || scaled_uv2.x > 0.5 || scaled_uv2.y < -0.5 || scaled_uv2.y > 0.5;

		float r1 = input_texture1.Sample(LinearSampler, scaled_uv1).r * !clamped1;
		float i1 = input_texture2.Sample(LinearSampler, scaled_uv1).r * !clamped1;
		float2 p1 = float2(r1, i1);

		float r2 = input_texture1.Sample(LinearSampler, scaled_uv2).b * !clamped2;
		float i2 = input_texture2.Sample(LinearSampler, scaled_uv2).b * !clamped2;
		float2 p2 = float2(r2, i2);

		float v1 = pow(length(p1), 2.f) * 0.00001 * lerp(1.0, 25, d);
		float v2 = pow(length(p2), 2.f) * 0.00001 * lerp(0.5,  0, d);

		float lambda = lerp(380.f, 700.f, n);
		float3 rgb = wl2rgbTannenbaum(lambda);
		rgb = lerp(1.f, rgb, 0.75f);

		result += (v1 + v2 * rgb * 0.25f);
	}

	result /= (float)num_steps;
	return float4(result, 1);
}

float4 PSStarburstFilter(float4 pos : SV_POSITION ) : SV_Target {
	float2 uv = pos.xy / starburst_resolution - 0.5;

	float3 result = 0;
	int num_steps = 256;
		
	for(int i = 0; i <= num_steps; ++i){
		float n = (float)i/(float)num_steps;
		float a = n * TWOPI * 2;
		float2 spiral = float2(cos(a), sin(a)) * n * 0.002;
		float2 jittered_uv = uv + spiral;

		float2 rotated_uv = Rotate(jittered_uv, n * 0.05);
		bool clamped = rotated_uv.x < -0.5 || rotated_uv.x > 0.5 || rotated_uv.y < -0.5 || rotated_uv.y > 0.5;

		float3 v = input_texture1.Sample(LinearSampler, rotated_uv + 0.5).rgb * !clamped;

		float lambda = lerp(380.f, 700.f, (i % 80)/80.f);
		float3 rgb = wl2rgbTannenbaum(lambda);
		rgb = lerp(rgb, 1.f, 0.5f);

		result += v * rgb;
	}

	result /= (float)num_steps;

	return float4(result, 1);
}
