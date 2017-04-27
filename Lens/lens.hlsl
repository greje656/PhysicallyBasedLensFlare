cbuffer Uniforms : register(b0) {
	float4 color;
	float4 direction;
};

struct PS_INPUT {
	float4 Position : SV_POSITION;
	float2 Texture  : TEXCOORD0;
};

//--------------------------------------------------------------------------------------
// Flare Vertex Shader
//--------------------------------------------------------------------------------------
PS_INPUT VS( float4 Pos : POSITION ) {
	PS_INPUT res;
	res.Position = Pos + float4(0.f, sin(direction.r) * 0.25, 0.f, 0.f);
	res.Texture = Pos.xy + 0.5;
	return res;
}

//--------------------------------------------------------------------------------------
// Flare Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS( in PS_INPUT In ) : SV_Target {
    return float4(In.Texture, 0.f, 0.5f);
}
