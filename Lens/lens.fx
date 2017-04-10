//--------------------------------------------------------------------------------------
// File: lens.fx
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

cbuffer Uniforms : register(b0)
{
	float4 color;
	float4 placement;
};

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
float4 VS( float4 Pos : POSITION ) : SV_POSITION
{
	float4 p = Pos;
	if (placement.y == 1.f) {
		p.xy *= placement.zw;
		p.x += placement.z;
		p.x += placement.x;
	}
	return p;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS( float4 Pos : SV_POSITION ) : SV_Target
{
    return color;
}
