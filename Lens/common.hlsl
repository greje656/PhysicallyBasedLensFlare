#define PI 3.14159265359f
#define TWOPI 6.28318530718f
#define NANO_METER 0.0000001
#define INCOMING_LIGHT_TEMP 6000.f

SamplerState LinearSampler {
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Wrap;
    AddressV = Wrap;
};

Texture2D input_texture1 : register(t1);
Texture2D input_texture2 : register(t2);

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

cbuffer PerformanceData : register(b2) {
	float4 performance_data;
}

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

float2 Rotate(float2 p, float a) {
	float x = p.x;
	float y = p.y;

	float cosa = cos(a);
	float sina = sin(a);

	float x1 = x * cosa - y * sina;
	float y1 = y * cosa + x * sina;

	return float2(x1, y1);
}
