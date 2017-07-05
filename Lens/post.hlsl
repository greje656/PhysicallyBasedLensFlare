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
	#if defined(DISPLAY_PERF_DATA)
		int s = 10;
		int ms_compute = performance_data.y * s;
		int ms_draw = performance_data.z * s;

		int2 padding = int2(20, 20);
		int2 inv_pos = int2(pos.x, backbuffer_size.y - pos.y) - padding;

		if(inv_pos.y == -1)
			if(inv_pos.x >= 0 && inv_pos.x < (ms_compute + ms_draw + 2))
				return float4(1,1,1,1);

		if(inv_pos.y > -8 && inv_pos.y < 0) {
			if(inv_pos.x >= 0 && inv_pos.x < (ms_compute + ms_draw + 2)) {
				if(inv_pos.y > -4 && (inv_pos.x % s) == 0)
					return float4(1,1,1,1);

				if((inv_pos.x % (5 * s)) == 0)
					return float4(1,1,1,1);
			}
		}

		if(inv_pos.y > 0 && inv_pos.y < padding.y) {
			if(inv_pos.x >= 0 && inv_pos.x < ms_compute)
				return float4(200.f/255.f, 1, 0, 1);
		
			if(inv_pos.x > (ms_compute + 1) && inv_pos.x < (ms_compute + ms_draw + 1))
				return float4(0, 1, 200.f/255.f, 1);
		}
	#endif	

	float3 c = input_texture1.Load(int3(pos.xy,0)).rgb;
	c = ACESFilm(c);
	return float4(c, 1);
}
