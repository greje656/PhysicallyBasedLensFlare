#pragma once

#include <d3d11_1.h>
#include <directxmath.h>

using namespace DirectX;

namespace FFT {
	enum FFTTextures {
		FFTTexture_Real0,
		FFTTexture_Imaginary0,
		FFTTexture_Real1,
		FFTTexture_Imaginary1,
		FFTTexture_Count
	};

	ID3D11Texture2D *mTextures[FFTTexture_Count];
	ID3D11UnorderedAccessView *mTextureUAV[FFTTexture_Count];
	ID3D11ShaderResourceView *mTextureSRV[FFTTexture_Count];
	ID3D11RenderTargetView *mRenderTargetViews[FFTTexture_Count];

	void InitFFTTetxtures(ID3D11Device *device, int aperture_resolution) {
		D3D11_TEXTURE2D_DESC textureDesc;
		memset(&textureDesc, 0, sizeof(textureDesc));
		textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		textureDesc.Width = (UINT)aperture_resolution;
		textureDesc.Height = (UINT)aperture_resolution;
		textureDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = 1;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;
		textureDesc.CPUAccessFlags = 0;
		textureDesc.Usage = D3D11_USAGE_DEFAULT;

		for (int i = 0; i < FFTTexture_Count; i++) {
			textureDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
			if (i == FFTTexture_Real0 || i == FFTTexture_Imaginary0) {
				textureDesc.BindFlags = textureDesc.BindFlags | D3D11_BIND_RENDER_TARGET;
			}

			device->CreateTexture2D(&textureDesc, NULL, &mTextures[i]);
			device->CreateUnorderedAccessView(mTextures[i], NULL, &mTextureUAV[i]);
			device->CreateShaderResourceView(mTextures[i], NULL, &mTextureSRV[i]);

		}

		device->CreateRenderTargetView(mTextures[FFTTexture_Real0], NULL, &mRenderTargetViews[FFTTexture_Real0]);
		device->CreateRenderTargetView(mTextures[FFTTexture_Imaginary0], NULL, &mRenderTargetViews[FFTTexture_Imaginary0]);
	}

	#define roundup(x,y) ( (int)y * (int)((x + y - 1)/y))
	void CopyTextureNoStretch(ID3D11DeviceContext *context, ID3D11ShaderResourceView *srcSRV, ID3D11UnorderedAccessView *dstUAV, int size, ID3D11ComputeShader* copyShader) {
		ID3D11UnorderedAccessView* pClearUAVs[] = { NULL, NULL };
		ID3D11ShaderResourceView* pClearSRVs[] = { NULL, NULL, NULL };

		context->PSSetShaderResources(0, 3, pClearSRVs);
		context->CSSetShaderResources(0, 1, pClearSRVs);
		context->CSSetUnorderedAccessViews(0, 2, pClearUAVs, NULL);
		context->CSSetShader(copyShader, NULL, 0);

		float values[] = { 0.0f, 0.0f, 0.0f, 0.0f };
		context->ClearUnorderedAccessViewFloat(dstUAV, values);
		context->CSSetShaderResources(0, 1, &srcSRV);
		context->CSSetUnorderedAccessViews(0, 1, &dstUAV, NULL);
		int dispatchX = roundup(min(size, size) / 16, 16);
		int dispatchY = roundup(min(size, size) / 16, 16);
		context->Dispatch(dispatchX, dispatchY, 1);

		context->CSSetShaderResources(0, 1, pClearSRVs);
		context->CSSetUnorderedAccessViews(0, 2, pClearUAVs, NULL);
	}

	void RunDispatchSLM(ID3D11DeviceContext *context, int pass, ID3D11ComputeShader* shader, int aperture_resolution) {
		ID3D11UnorderedAccessView* pUAVs[] = { NULL, NULL };
		ID3D11ShaderResourceView* pSRVs[] = { NULL, NULL, NULL };

		context->CSSetShaderResources(0, 3, pSRVs);
		context->CSSetUnorderedAccessViews(0, 2, pUAVs, NULL);
		context->CSSetShader(shader, NULL, 0);

		pSRVs[0] = mTextureSRV[FFTTexture_Real0 + 2 * pass];
		pSRVs[1] = mTextureSRV[FFTTexture_Imaginary0 + 2 * pass];
		pUAVs[0] = mTextureUAV[FFTTexture_Real0 + 2 * !pass];
		pUAVs[1] = mTextureUAV[FFTTexture_Imaginary0 + 2 * !pass];

		context->CSSetUnorderedAccessViews(0, 2, pUAVs, NULL);
		context->CSSetShaderResources(0, 3, pSRVs);

		context->Dispatch(1, (UINT)aperture_resolution, 1);

		pSRVs[0] = NULL; pSRVs[1] = NULL; pSRVs[2] = NULL;
		context->CSSetShaderResources(0, 3, pSRVs);
		pUAVs[0] = NULL; pUAVs[1] = NULL;
		context->CSSetUnorderedAccessViews(0, 2, pUAVs, NULL);
	}
}