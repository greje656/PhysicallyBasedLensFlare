#include "common.hlsl"

float fade_aperture_edge(float radius, float fade, float signed_distance) {
	float l = radius;
	float u = radius + fade;
	float s = u - l;
	float c = 1.f - saturate(saturate(signed_distance - l)/s);
	return smoothstep(0, 1, c);
}
 
float smax(float a, float b, float k) {
	float diff = a - b;
	float h = saturate(0.5 + 0.5 * diff / k);
	return b + h * (diff + k * (1.0f - h));
}

float4 PSAperture(float4 pos : SV_POSITION) : SV_Target {

	float2 uv = pos.xy / aperture_resolution;
	float2 ndc = ((uv - 0.5f) * 2.f);

	int num_of_blades = int(number_of_blades);

	float a = (atan2(ndc.x, ndc.y) + aperture_opening)/TWOPI + 3.f/4.f;
	float o = frac(a * num_of_blades + 0.5);
	float w1 = lerp(0.010, 0.001f, saturate((num_of_blades - 4)/10.f));
	float w2 = lerp(0.025, 0.001f, saturate((num_of_blades - 4)/10.f));
	float s0 = sin(o * 2 * PI);
	float s1 = s0 * w1;
	float s2 = s0 * w2;

	// fft aperture shape
	float signed_distance = 0.f;
	for(int i = 0; i < num_of_blades; ++i) {
		float angle = (i/float(num_of_blades)) * TWOPI;
		float2 axis = float2(cos(angle), sin(angle));
		signed_distance = max(signed_distance, dot(axis, ndc));
	}

	//signed_distance += s1;
	float aperture_fft = fade_aperture_edge(0.7, 0.00001, signed_distance);

	// camera aperture shape
	signed_distance = 0.f;
	for(int i = 0; i < num_of_blades; ++i) {
		float angle = aperture_opening + (i/float(num_of_blades)) * TWOPI;
		float2 axis = float2(cos(angle), sin(angle));
		signed_distance = smax(signed_distance, dot(axis, ndc), 0.1);
	}

	signed_distance += s2;
	float aperture_mask = fade_aperture_edge(0.7, 0.1, signed_distance);

	{ // Diffraction rings
		float w = 0.2;
		float s = signed_distance + 0.05;
		float n = saturate(saturate(s + w) - (1.f - w));
		
		float x = n/w;
		float a = x;
		float b = -x + 1.f;
		float c = min(a,b) * 2.f;
		float t = (sin(x * 6.f * PI - 1.5f) + 1.f) * 0.5f;
		float rings = pow(t*c, 1.f);
		aperture_mask = aperture_mask + rings * 0.125;
	}

	float dust_fft = 0.f;
	{ // Dust
		dust_fft = input_texture1.Sample(LinearSampler, uv).r;
		aperture_mask *= saturate(dust_fft + 0.9);
	}

	float3 rgb = float3(aperture_fft, dust_fft, aperture_mask);

	return float4(rgb, 1);
}
