#include "common.hlsl"

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
