Texture2D<float3> SourceTexture : register(t0);
RWTexture2D<float3> TargetTexture : register(u0);

[numthreads(16, 16, 1)]
void CopyTextureCS(
	uint3 position : SV_DispatchThreadID
	)
{
	TargetTexture[position.xy] = SourceTexture[position.xy];
}