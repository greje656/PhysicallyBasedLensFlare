//--------------------------------------------------------------------------------------
// Copyright 2014 Intel Corporation
// All Rights Reserved
//
// Permission is granted to use, copy, distribute and prepare derivative works of this
// software for any purpose and without fee, provided, that the above copyright notice
// and this statement appear in all copies.  Intel makes no representations about the
// suitability of this software for any purpose.  THIS SOFTWARE IS PROVIDED "AS IS."
// INTEL SPECIFICALLY DISCLAIMS ALL WARRANTIES, EXPRESS OR IMPLIED, AND ALL LIABILITY,
// INCLUDING CONSEQUENTIAL AND OTHER INDIRECT DAMAGES, FOR THE USE OF THIS SOFTWARE,
// INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PROPRIETARY RIGHTS, AND INCLUDING THE
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  Intel does not
// assume any responsibility for any errors which may appear in this software nor any
// responsibility to update it.
//--------------------------------------------------------------------------------------

// Input Preprocess Defines:
// TRANSFORM_INVERSE: Defined for inverse fft only
// BUTTERFLY_COUNT: number of passes to perform
// ROWPASS: defined for tranformation along the x axis
// LENGTH: pixel length of row or column

Texture2D<float3> TextureSourceR  : register(t0);
Texture2D<float3> TextureSourceI  : register(t1);
RWTexture2D<float3> TextureTargetR  : register(u0);
RWTexture2D<float3> TextureTargetI  : register(u1);

static const float PI = 3.14159265f;

void GetButterflyValues(uint passIndex, uint x, out uint2 indices, out float2 weights)
{
	int sectionWidth = 2 << passIndex;
	int halfSectionWidth = sectionWidth / 2;

	int sectionStartOffset = x & ~(sectionWidth - 1);
	int halfSectionOffset = x & (halfSectionWidth - 1);
	int sectionOffset = x & (sectionWidth - 1);

	sincos( 2.0*PI*sectionOffset / (float)sectionWidth, weights.y, weights.x );
	weights.y = -weights.y;

	indices.x = sectionStartOffset + halfSectionOffset;
	indices.y = sectionStartOffset + halfSectionOffset + halfSectionWidth;

	if (passIndex == 0) {
		indices = reversebits(indices) >> (32 - BUTTERFLY_COUNT) & (LENGTH - 1);
	}
}

groupshared float3 pingPongArray[4][LENGTH];
void ButterflyPass(int passIndex, uint x, uint t0, uint t1, out float3 resultR, out float3 resultI)
{
	uint2 Indices;
	float2 Weights;
	GetButterflyValues(passIndex, x, Indices, Weights);
	
	float3 inputR1 = pingPongArray[t0][Indices.x];
	float3 inputI1 = pingPongArray[t1][Indices.x];

	float3 inputR2 = pingPongArray[t0][Indices.y];
	float3 inputI2 = pingPongArray[t1][Indices.y];

	resultR = inputR1 + Weights.x * inputR2 - Weights.y * inputI2;
	resultI = inputI1 + Weights.y * inputR2 + Weights.x * inputI2;
}

[numthreads( LENGTH, 1, 1 )]
void ButterflySLM(uint3 position : SV_DispatchThreadID)
{
#ifdef ROWPASS
	uint2 texturePos = uint2( position.xy );
#else
	uint2 texturePos = uint2( position.yx );
#endif

	// Load entire row or column into scratch array
	pingPongArray[0][position.x].xyz = TextureSourceR[texturePos];
#if defined(ROWPASS)
	// don't load values from the imaginary texture when loading the original texture
	pingPongArray[1][position.x].xyz = 0;
#else
	pingPongArray[1][position.x].xyz = TextureSourceI[texturePos];
#endif
	
	uint4 textureIndices = uint4(0, 1, 2, 3);

	
	for (int i = 0; i < BUTTERFLY_COUNT-1; i++)
	{
		GroupMemoryBarrierWithGroupSync();
		ButterflyPass( i, position.x, textureIndices.x, textureIndices.y, pingPongArray[textureIndices.z][position.x].xyz, pingPongArray[textureIndices.w][position.x].xyz );
		textureIndices.xyzw = textureIndices.zwxy;
	}

	// Final butterfly will write directly to the target texture
	GroupMemoryBarrierWithGroupSync();

	// The final pass writes to the output UAV texture
	ButterflyPass(BUTTERFLY_COUNT - 1, position.x, textureIndices.x, textureIndices.y, TextureTargetR[texturePos], TextureTargetI[texturePos]);

}
