#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <directxmath.h>
#include <directxcolors.h>

#include <vector>
#include <string>
#include <sstream>

#include "resource.h"
#include "ray_trace.h"

//#define DRAW2D
#define DRAWLENSFLARE

using namespace DirectX;

//--------------------------------------------------------------------------------------
// DEFAULT
//--------------------------------------------------------------------------------------
const std::string vertex_shader_source = R"(
cbuffer Uniforms : register(b0) {
	float4 color;
	float4 placement;
};

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
float4 VS( float4 Pos : POSITION ) : SV_POSITION {
	float4 p = Pos;
	if (placement.y == 1.f) {
		p.xy *= placement.zw;
		p.x += placement.z;
		p.x += placement.x;
	}
	return p;
}
)";

const std::string pixel_shader_source = R"(
cbuffer Uniforms : register(b0) {
	float4 color;
	float4 placement;
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS( float4 Pos : SV_POSITION ) : SV_Target {
    return color;
}
)";

struct PatentFormat {
	float r;
	float d;
	float n;
	bool flat;
	float w;
	float h;
	float coating_thickness;
};

struct SimpleVertex {
	XMFLOAT3 pos;
};

struct PS_INPUT {
	XMFLOAT4 position;
	XMFLOAT4 texture;
	XMFLOAT4 mask;
	XMFLOAT4 ref;
};

struct InstanceUniforms {
	XMFLOAT4 color;
	XMFLOAT4 placement;
};

struct GlobalData {
	float time;
	float spread;
	float plate_size;
	float aperture_id;
	float num_interfaces;
	float aperture_resolution;

	XMFLOAT2 backbuffer_size;
	XMFLOAT4 direction;
	XMFLOAT4 aperture_opening;
};

struct GhostData {
	XMFLOAT4 bounces;
};

namespace LensShapes {
	struct Rectangle {
		ID3D11Buffer* vertices;
		ID3D11Buffer* lines;
	};

	struct Circle {
		float x, y, r;
		ID3D11Buffer* triangles;
		ID3D11Buffer* lines;
	};

	struct Patch {
		int subdiv;
		ID3D11Buffer* indices;
		ID3D11Buffer* vs_vertices;
		ID3D11Buffer* cs_vertices;
		ID3D11ShaderResourceView* vertices_resource_view;
		ID3D11UnorderedAccessView* ua_vertices_resource_view;
	};
}

INT sampleMask = 0x0F;
UINT offset = 0;
UINT stride = sizeof(SimpleVertex);
float blendFactor[4] = { 1.f, 1.f, 1.f, 1.f };

LensShapes::Circle unit_circle;
LensShapes::Rectangle unit_square;
LensShapes::Patch unit_patch;
std::vector<LensInterface> nikon_28_75mm_lens_interface;

float x_dir = 0.f;
float y_dir = 0.f;
float aperture_opening = 5.f;
float number_of_blades = 5.f;
vec3 direction(0.f, 0.f, -1.f);

bool left_mouse_down = false;
bool spacebar_down = false;
bool key_down = false;
bool editing_aperture = false;
bool editing_no_blades = false;
bool editing_spread = false;
bool editing_coating_quality = false;
bool overlay_wireframe = false;
bool draw2d = true;

const float d6  = 53.142f;
const float d10 =  7.063f;
const float d14 =  1.532f;
const float dAp =  2.800f;
const float d20 = 16.889f;
const float Bf  = 39.683f;
const int aperture_id = 14;

std::vector<PatentFormat> nikon_28_75mm_lens_components = {
	{    72.747f,  2.300f, 1.60300f, false, 0.2f, 29.0f, 530 },
	{    37.000f, 13.000f, 1.00000f, false, 0.2f, 29.0f, 600 }, 

	{  -172.809f,  2.100f, 1.58913f, false, 2.7f, 26.2f, 570 }, 
	{    39.894f,  1.000f, 1.00000f, false, 2.7f, 26.2f, 660 }, 

	{    49.820f,  4.400f, 1.86074f, false, 0.5f, 20.0f, 330 }, 
	{    74.750f,      d6, 1.00000f, false, 0.5f, 20.0f, 544 }, 

	{    63.402f,  1.600f, 1.86074f, false, 0.5f, 16.1f, 740 }, 
	{    37.530f,  8.600f, 1.51680f, false, 0.5f, 16.1f, 411 }, 

	{   -75.887f,  1.600f, 1.80458f, false, 0.5f, 16.0f, 580 }, 
	{   -97.792f,     d10, 1.00000f, false, 0.5f, 16.5f, 730 }, 

	{    96.034f,  3.600f, 1.62041f, false, 0.5f, 18.0f, 700 }, 
	{   261.743f,  0.100f, 1.00000f, false, 0.5f, 18.0f, 440 }, 

	{    54.262f,  6.000f, 1.69680f, false, 0.5f, 18.0f, 800 }, 
	{ -5995.277f,     d14, 1.00000f, false, 0.5f, 18.0f, 300 }, 

	{       0.0f,     dAp, 1.00000f, true,  18.f, aperture_opening, 440 },

	{   -74.414f,  2.200f, 1.90265f, false, 0.5f, 13.0f, 500 }, 

	{   -62.929f,  1.450f, 1.51680f, false, 0.1f, 13.0f, 770 }, 
	{   121.380f,  2.500f, 1.00000f, false, 4.0f, 13.1f, 820 }, 

	{   -85.723f,  1.400f, 1.49782f, false, 4.0f, 13.0f, 200 }, 

	{    31.093f,  2.600f, 1.80458f, false, 4.0f, 13.1f, 540 }, 
	{    84.758f,     d20, 1.00000f, false, 0.5f, 13.0f, 580 }, 

	{   459.690f,  1.400f, 1.86074f, false, 1.0f, 15.0f, 533 }, 

	{    40.240f,  7.300f, 1.49782f, false, 1.0f, 15.0f, 666 }, 
	{   -49.771f,  0.100f, 1.00000f, false, 1.0f, 15.2f, 500 }, 

	{    62.369f,  7.000f, 1.67025f, false, 1.0f, 16.0f, 487 }, 
	{   -76.454f,  5.200f, 1.00000f, false, 1.0f, 16.0f, 671 }, 

	{   -32.524f,  2.000f, 1.80454f, false, 0.5f, 17.0f, 487 }, 
	{   -50.194f,      Bf, 1.00000f, false, 0.5f, 17.0f, 732 }, 

	{        0.f,     5.f, 1.00000f,  true, 10.f,  10.f, 500 }
};

int patch_tesselation = 32;
int num_threads = patch_tesselation;
int num_groups = patch_tesselation / num_threads;
int num_of_rays = patch_tesselation;
int num_of_lens_components = (int)nikon_28_75mm_lens_components.size() + 1;
int ghost_bounce_1 = 21;
int ghost_bounce_2 = 10;
int num_of_intersections_1 = num_of_lens_components;
int num_of_intersections_2 = num_of_lens_components;
int num_of_intersections_3 = num_of_lens_components;
int num_points_per_cirlces = 200;
int num_vertices_per_cirlces = num_points_per_cirlces * 3;
int num_vertices_per_bundle = (patch_tesselation - 1) * (patch_tesselation - 1);

float backbuffer_width = 1800;
float backbuffer_height = 900;
float aperture_resolution = 512;
float starburst_resolution = 2056;
float coating_quality = 0.0;
float ratio = backbuffer_height / backbuffer_width;
float min_ior = 1000.f;
float max_ior = -1000.f;
float global_scale = 0.009;
float total_lens_distance = 0.f;
float time = (float)ghost_bounce_1;
float speed = 0.1f;
float rays_spread = 2.0f;

#ifdef SAVE_BACK_BUFFER_TO_DISK
#include <DirectXTex.h>
void SaveBackBuffer() {
	ScratchImage scratch_image;
	ID3D11Resource* resource = nullptr;
	Views::renderTargetView->GetResource(&resource);
	CaptureTexture(g_pd3dDevice, g_pImmediateContext, resource, scratch_image);

	static int frame_number = 0;
	frame_number++;
	std::wstringstream finle_name;
	finle_name << "Lens";
	if (frame_number < 10)
		finle_name << "00";
	else if (frame_number < 100)
		finle_name << "0";

	finle_name << frame_number;
	finle_name << ".tga";

	const Image* image = scratch_image.GetImage(0, 0, 0);
	SaveToTGAFile(*image, finle_name.str().c_str());
}
#endif

XMFLOAT4 sRGB(XMFLOAT4 c) {
	XMFLOAT4 rgb = c;
	rgb.x /= 255.f;
	rgb.y /= 255.f;
	rgb.z /= 255.f;
	return rgb;
};

#define COLOR_THEME1
#ifdef COLOR_THEME1
	float opaque_alpha           = 0.65;
	XMFLOAT4 fill_color1         = sRGB({  64.f, 215.f, 242.f, 0.2f });
	XMFLOAT4 fill_color2         = sRGB({ 179.f, 178.f, 210.f, 0.2f });
	XMFLOAT4 flat_fill_color     = sRGB({ 190.f, 190.f, 190.f, 1.0f });
	XMFLOAT4 stroke_color        = sRGB({ 115.f, 115.f, 115.f, 1.0f });
	XMFLOAT4 stroke_color1       = sRGB({ 115.f, 115.f, 115.f, 1.0f });
	XMFLOAT4 stroke_color2       = sRGB({ 165.f, 165.f, 165.f, 1.0f });
	XMFLOAT4 background_color1   = sRGB({ 240.f, 240.f, 240.f, 1.0f });
	XMFLOAT4 background_color2   = sRGB({   0.f,   0.f,   0.f, 1.0f });

	XMFLOAT4 intersection_color1 = sRGB({   0.f,   0.f,   0.f, 0.1f });
	XMFLOAT4 intersection_color2 = sRGB({  64.f, 215.f, 242.f, 0.5f });
	XMFLOAT4 intersection_color3 = sRGB({ 179.f, 178.f, 210.f, 0.5f });
	XMFLOAT4 black               = sRGB({   0.f,   0.f,   0.f, 0.5f });
#endif

inline XMFLOAT3 point_to_d3d(vec3& point) {
	float x = point.x;
	float y = point.y / ratio* global_scale;
	float z = point.z * global_scale;
	return XMFLOAT3(-(z - 1.f), y, x);
}

inline float sign(float v) {
	return v < 0.f ? -1.f : 1.f;
}

inline float lerp(float a, float b, float l) {
	return a * (1.f - l) + b * l;
}

XMFLOAT4 lerp(XMFLOAT4& a, XMFLOAT4& b, float l) {
	float x = lerp(a.x, b.x, l);
	float y = lerp(a.y, b.y, l);
	float z = lerp(a.z, b.z, l);
	float w = lerp(a.w, b.w, l);
	return{ x, y, z, w };
}

//--------------------------------------------------------------------------------------
// Global Variables
//--------------------------------------------------------------------------------------
HINSTANCE                  g_hInst = nullptr;
HWND                       g_hWnd = nullptr;
D3D_DRIVER_TYPE            g_driverType = D3D_DRIVER_TYPE_NULL;
D3D_FEATURE_LEVEL          g_featureLevel = D3D_FEATURE_LEVEL_11_0;
ID3D11Device*              g_pd3dDevice = nullptr;
ID3D11Device1*             g_pd3dDevice1 = nullptr;
ID3D11DeviceContext*       g_pImmediateContext = nullptr;
ID3D11DeviceContext1*      g_pImmediateContext1 = nullptr;
IDXGISwapChain*            g_pSwapChain = nullptr;
IDXGISwapChain1*           g_pSwapChain1 = nullptr;

ID3D11InputLayout*         g_pVertexLayout2d = nullptr;
ID3D11InputLayout*         g_pVertexLayout3d = nullptr;

namespace Views {
	ID3D11UnorderedAccessView* null_ua_view[1] = { NULL };
	ID3D11ShaderResourceView*  null_sr_view[1] = { NULL };
	ID3D11UnorderedAccessView* lensInterfaceResourceView;
	ID3D11ShaderResourceView*  shaderHDRView = nullptr;
	ID3D11DepthStencilView*    depthStencilView = nullptr;
	ID3D11RenderTargetView*    renderTargetView = nullptr;
	ID3D11RenderTargetView*    HDRView = nullptr;
}

namespace Textures {
	ID3D11Texture2D*          depthStencil = nullptr;
	ID3D11Texture2D*          HDRTexture = nullptr;
	ID3D11Texture2D*          dust = nullptr;
	ID3D11Texture2D*          aperture = nullptr;
	ID3D11Texture2D*          starburst = nullptr;
	ID3D11Texture2D*          starburst_filtered = nullptr;
	ID3D11Texture2D*          aperture_depth_buffer = nullptr;

	ID3D11RenderTargetView*   dust_rt_view = nullptr;
	ID3D11ShaderResourceView* dust_sr_view = nullptr;
	ID3D11RenderTargetView*   aperture_rt_view = nullptr;
	ID3D11ShaderResourceView* aperture_sr_view = nullptr;
	ID3D11DepthStencilView*   aperture_depth_buffer_view = nullptr;

	ID3D11RenderTargetView*   starburst_rt_view = nullptr;
	ID3D11ShaderResourceView* starburst_sr_view = nullptr;
	ID3D11RenderTargetView*   starburst_filtered_rt_view = nullptr;
	ID3D11ShaderResourceView* starburst_filtered_sr_view = nullptr;
	ID3D11DepthStencilView*   starburst_depth_buffer_view = nullptr;

	ID3D11SamplerState*       linear_clamp_sampler = nullptr;
	ID3D11SamplerState*       linear_wrap_sampler = nullptr;

	void InitializeSamplers() {
		D3D11_SAMPLER_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		g_pd3dDevice->CreateSamplerState(&desc, &Textures::linear_clamp_sampler);

		desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		g_pd3dDevice->CreateSamplerState(&desc, &Textures::linear_wrap_sampler);
	}
	

	void initTexture(int width, int height, DXGI_FORMAT format, ID3D11Texture2D*& texture,
		ID3D11ShaderResourceView*& sr_view, ID3D11RenderTargetView*& rt_view, D3D11_SUBRESOURCE_DATA* data = nullptr) {
		
		HRESULT hr;

		D3D11_TEXTURE2D_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = format;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		hr = g_pd3dDevice->CreateTexture2D(&desc, data, &texture);

		D3D11_RENDER_TARGET_VIEW_DESC rt_view_desc;
		ZeroMemory(&rt_view_desc, sizeof(rt_view_desc));
		rt_view_desc.Format = rt_view_desc.Format;
		rt_view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rt_view_desc.Texture2D.MipSlice = 0;
		hr = g_pd3dDevice->CreateRenderTargetView(texture, &rt_view_desc, &rt_view);

		D3D11_SHADER_RESOURCE_VIEW_DESC sr_view_desc;
		ZeroMemory(&sr_view_desc, sizeof(sr_view_desc));
		sr_view_desc.Format = desc.Format;
		sr_view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		sr_view_desc.Texture2D.MostDetailedMip = 0;
		sr_view_desc.Texture2D.MipLevels = 1;
		hr = g_pd3dDevice->CreateShaderResourceView(texture, &sr_view_desc, &sr_view);

	}

	void InitializeDepthBuffer(int width, int height, ID3D11Texture2D*& buffer, ID3D11DepthStencilView*& buffer_view) {
		HRESULT hr;

		// Create depth stencil texture
		D3D11_TEXTURE2D_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		hr = g_pd3dDevice->CreateTexture2D(&desc, nullptr, &buffer);

		// Create the depth stencil view
		D3D11_DEPTH_STENCIL_VIEW_DESC view_desc;
		ZeroMemory(&view_desc, sizeof(view_desc));
		view_desc.Format = desc.Format;
		view_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		view_desc.Texture2D.MipSlice = 0;
		hr = g_pd3dDevice->CreateDepthStencilView(buffer, &view_desc, &buffer_view);
	}

	void InitTextures() {
		ID3D11Texture2D* backBuffer = nullptr;
		g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
		g_pd3dDevice->CreateRenderTargetView(backBuffer, nullptr, &Views::renderTargetView);

		Textures::initTexture((int)backbuffer_width, (int)backbuffer_height, DXGI_FORMAT_R16G16B16A16_FLOAT, Textures::HDRTexture, Views::shaderHDRView, Views::HDRView);
		Textures::initTexture((int)aperture_resolution, (int)aperture_resolution, DXGI_FORMAT_R16G16B16A16_FLOAT, Textures::aperture, Textures::aperture_sr_view, Textures::aperture_rt_view);
		Textures::initTexture((int)starburst_resolution, (int)starburst_resolution, DXGI_FORMAT_R16G16B16A16_FLOAT, Textures::starburst, Textures::starburst_sr_view, Textures::starburst_rt_view);
		Textures::initTexture((int)starburst_resolution, (int)starburst_resolution, DXGI_FORMAT_R16G16B16A16_FLOAT, Textures::starburst_filtered, Textures::starburst_filtered_sr_view, Textures::starburst_filtered_rt_view);
		Textures::InitializeDepthBuffer((int)backbuffer_width, (int)backbuffer_height, Textures::depthStencil, Views::depthStencilView);
		Textures::InitializeDepthBuffer((int)aperture_resolution, (int)aperture_resolution, Textures::aperture_depth_buffer, Textures::aperture_depth_buffer_view);
		Textures::InitializeDepthBuffer((int)starburst_resolution, (int)starburst_resolution, Textures::aperture_depth_buffer, Textures::starburst_depth_buffer_view);

		HBITMAP bitmap = LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_BITMAP1));
		int size = 512 * 512 * 4;
		void* bitmap_data = malloc(size);
		GetBitmapBits(bitmap, size, bitmap_data);
		
		D3D11_SUBRESOURCE_DATA resource_data;
		resource_data.pSysMem = bitmap_data;
		resource_data.SysMemPitch = 512 * 4;
		resource_data.SysMemSlicePitch = 512* 512 * 4;
		
		Textures::initTexture(512, 512, DXGI_FORMAT_R8G8B8A8_UNORM, Textures::dust, Textures::dust_sr_view, Textures::dust_rt_view, &resource_data);
	}
}

namespace Shaders {
	ID3D11VertexShader*   vertexShader = nullptr;
	ID3D11PixelShader*    pixelShader = nullptr;
	ID3D11PixelShader*    toneMapPixelShader = nullptr;

	ID3D11VertexShader*   starburstVertexShader = nullptr;
	ID3D11PixelShader*    starburstPixelShader = nullptr;
	ID3D11PixelShader*    starburstFromFFTPixelShader = nullptr;
	ID3D11PixelShader*    starburstFilterPixelShader = nullptr;

	ID3D11VertexShader*   flareVertexShader = nullptr;
	ID3D11PixelShader*    flarePixelShader = nullptr;
	ID3D11PixelShader*    flarePixelShaderDebug = nullptr;
	ID3D11PixelShader*    flarePixelShaderDebugWireframe = nullptr;
	ID3D11ComputeShader*  flareComputeShader = nullptr;

	ID3D11ComputeShader*  fftRowComputeShader = nullptr;
	ID3D11ComputeShader*  fftColComputeShader = nullptr;
	ID3D11ComputeShader*  fftCopyShader = nullptr;

	ID3D11PixelShader*    aperture_ps_shader;

	HRESULT CompileShaderFromSource(std::string shaderSource, LPCSTR szEntryPoint, LPCSTR szShaderModel, ID3DBlob** ppBlobOut) {
		ID3DBlob* temp = nullptr;
		HRESULT hr = D3DCompile(shaderSource.c_str(), shaderSource.length(), nullptr, nullptr, nullptr, szEntryPoint, szShaderModel, D3DCOMPILE_ENABLE_STRICTNESS, 0, ppBlobOut, &temp);
		char* msg = temp ? (char*)temp : nullptr; msg;
		return hr;
	}

	HRESULT CompileShaderFromFile(LPCWSTR shaderFile, LPCSTR szEntryPoint, LPCSTR szShaderModel, ID3DBlob** ppBlobOut, D3D_SHADER_MACRO* defines = nullptr) {
		ID3DBlob* temp = nullptr;
		HRESULT hr = D3DCompileFromFile(shaderFile, defines, nullptr, szEntryPoint, szShaderModel, D3DCOMPILE_ENABLE_STRICTNESS, 0, ppBlobOut, &temp);
		char* msg = temp ? (char*)temp->GetBufferPointer() : nullptr; msg;
		return hr;
	}

	void InitShaders() {
		HRESULT hr;

		ID3DBlob* blob = nullptr;
		D3D11_INPUT_ELEMENT_DESC layout[] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, };
		UINT numElements = ARRAYSIZE(layout);

		hr = CompileShaderFromSource(vertex_shader_source, "VS", "vs_5_0", &blob);
		hr = g_pd3dDevice->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &Shaders::vertexShader);
		hr = g_pd3dDevice->CreateInputLayout(layout, numElements, blob->GetBufferPointer(), blob->GetBufferSize(), &g_pVertexLayout2d);
		blob->Release();

		hr = CompileShaderFromSource(pixel_shader_source, "PS", "ps_5_0", &blob);
		hr = g_pd3dDevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &Shaders::pixelShader);
		blob->Release();

		hr = CompileShaderFromFile(L"lens.hlsl", "VS", "vs_5_0", &blob);
		hr = g_pd3dDevice->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &Shaders::flareVertexShader);
		hr = g_pd3dDevice->CreateInputLayout(layout, numElements, blob->GetBufferPointer(), blob->GetBufferSize(), &g_pVertexLayout3d);
		blob->Release();

		hr = CompileShaderFromFile(L"lens.hlsl", "PS", "ps_5_0", &blob);
		hr = g_pd3dDevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &Shaders::flarePixelShader);
		blob->Release();	

		D3D_SHADER_MACRO debug_flags[] = { "DEBUG_VALUES", "", 0, 0 };
		hr = CompileShaderFromFile(L"lens.hlsl", "PS", "ps_5_0", &blob, debug_flags);
		hr = g_pd3dDevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &Shaders::flarePixelShaderDebug);
		blob->Release();

		D3D_SHADER_MACRO wireframe_debug_flags[] = { "DEBUG_WIREFRAME", "", 0, 0 };
		hr = CompileShaderFromFile(L"lens.hlsl", "PS", "ps_5_0", &blob, wireframe_debug_flags);
		hr = g_pd3dDevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &Shaders::flarePixelShaderDebugWireframe);
		blob->Release();

		hr = CompileShaderFromFile(L"lens.hlsl", "CS", "cs_5_0", &blob);
		hr = g_pd3dDevice->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &Shaders::flareComputeShader);
		blob->Release();

		hr = CompileShaderFromFile(L"lens.hlsl", "PSToneMapping", "ps_5_0", &blob);
		hr = g_pd3dDevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &Shaders::toneMapPixelShader);
		blob->Release();

		hr = CompileShaderFromFile(L"lens.hlsl", "VSStarburst", "vs_5_0", &blob);
		hr = g_pd3dDevice->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &Shaders::starburstVertexShader);
		blob->Release();

		hr = CompileShaderFromFile(L"lens.hlsl", "PSStarburst", "ps_5_0", &blob);
		hr = g_pd3dDevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &Shaders::starburstPixelShader);
		blob->Release();

		hr = CompileShaderFromFile(L"lens.hlsl", "PSStarburstFromFFT", "ps_5_0", &blob);
		hr = g_pd3dDevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &Shaders::starburstFromFFTPixelShader);
		blob->Release();

		hr = CompileShaderFromFile(L"lens.hlsl", "PSStarburstFilter", "ps_5_0", &blob);
		hr = g_pd3dDevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &Shaders::starburstFilterPixelShader);
		blob->Release();

		hr = CompileShaderFromFile(L"lens.hlsl", "PSAperture", "ps_5_0", &blob);
		hr = g_pd3dDevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &Shaders::aperture_ps_shader);
		blob->Release();

		int butterfly_count = (int)(logf(aperture_resolution) / logf(2.0));
		std::string resolution_string = std::to_string((int)aperture_resolution);
		std::string butterfly_string = std::to_string(butterfly_count);
		D3D_SHADER_MACRO fft_defines_row[] = {
			"LENGTH", resolution_string.c_str(),
			"BUTTERFLY_COUNT", butterfly_string.c_str(),
			"ROWPASS", "", 0, 0 };
		hr = CompileShaderFromFile(L"fftslm.hlsl", "ButterflySLM", "cs_5_0", &blob, fft_defines_row);
		hr = g_pd3dDevice->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &Shaders::fftRowComputeShader);
		blob->Release();

		D3D_SHADER_MACRO fft_defines_col[] = {
			"LENGTH", resolution_string.c_str(),
			"BUTTERFLY_COUNT", butterfly_string.c_str(),
			"ROWCOL", "", 0, 0 };
		hr = CompileShaderFromFile(L"fftslm.hlsl", "ButterflySLM", "cs_5_0", &blob, fft_defines_col);
		hr = g_pd3dDevice->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &Shaders::fftColComputeShader);
		blob->Release();

		hr = CompileShaderFromFile(L"copycs.hlsl", "CopyTextureCS", "cs_5_0", &blob);
		hr = g_pd3dDevice->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &Shaders::fftCopyShader);
		blob->Release();
	}
}

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

	void InitFFTTetxtures() {
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

			g_pd3dDevice->CreateTexture2D(&textureDesc, NULL, &mTextures[i]);
			g_pd3dDevice->CreateUnorderedAccessView(mTextures[i], NULL, &mTextureUAV[i]);
			g_pd3dDevice->CreateShaderResourceView(mTextures[i], NULL, &mTextureSRV[i]);

		}

		g_pd3dDevice->CreateRenderTargetView(mTextures[FFTTexture_Real0], NULL, &mRenderTargetViews[FFTTexture_Real0]);
		g_pd3dDevice->CreateRenderTargetView(mTextures[FFTTexture_Imaginary0], NULL, &mRenderTargetViews[FFTTexture_Imaginary0]);
	}

	#define roundup(x,y) ( (int)y * (int)((x + y - 1)/y))
	void CopyTextureNoStretch(ID3D11DeviceContext *context, ID3D11ShaderResourceView *srcSRV, ID3D11UnorderedAccessView *dstUAV, int size) {
		ID3D11UnorderedAccessView* pClearUAVs[] = { NULL, NULL };
		ID3D11ShaderResourceView* pClearSRVs[] = { NULL, NULL, NULL };

		context->PSSetShaderResources(0, 3, pClearSRVs);
		context->CSSetShaderResources(0, 1, pClearSRVs);
		context->CSSetUnorderedAccessViews(0, 2, pClearUAVs, NULL);
		context->CSSetShader(Shaders::fftCopyShader, NULL, 0);

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

	void RunDispatchSLM(ID3D11DeviceContext *context, int pass, ID3D11ComputeShader* shader) {
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

namespace Buffers {
	ID3D11Buffer* globalData = nullptr;
	ID3D11Buffer* ghostData = nullptr;
	ID3D11Buffer* instanceUniforms = nullptr;
	ID3D11Buffer* intersectionPoints1 = nullptr;
	ID3D11Buffer* intersectionPoints2 = nullptr;
	ID3D11Buffer* intersectionPoints3 = nullptr;
	ID3D11Buffer* lensInterface = nullptr;

	void InitBuffers() {
		HRESULT hr;

		D3D11_BUFFER_DESC bd;
		ZeroMemory(&bd, sizeof(bd));
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = 0;
		bd.ByteWidth = sizeof(InstanceUniforms);
		hr = g_pd3dDevice->CreateBuffer(&bd, nullptr, &Buffers::instanceUniforms);

		ZeroMemory(&bd, sizeof(bd));
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = 0;
		bd.ByteWidth = sizeof(GlobalData);
		hr = g_pd3dDevice->CreateBuffer(&bd, nullptr, &Buffers::globalData);

		ZeroMemory(&bd, sizeof(bd));
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = 0;
		bd.ByteWidth = sizeof(GhostData);
		hr = g_pd3dDevice->CreateBuffer(&bd, nullptr, &Buffers::ghostData);

		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bd.ByteWidth = sizeof(SimpleVertex) * num_of_intersections_1;
		hr = g_pd3dDevice->CreateBuffer(&bd, nullptr, &Buffers::intersectionPoints1);

		bd.ByteWidth = sizeof(SimpleVertex) * num_of_intersections_2;
		hr = g_pd3dDevice->CreateBuffer(&bd, nullptr, &Buffers::intersectionPoints2);

		bd.ByteWidth = sizeof(SimpleVertex) * num_of_intersections_3;
		hr = g_pd3dDevice->CreateBuffer(&bd, nullptr, &Buffers::intersectionPoints3);
	}
}

namespace States {
	ID3D11BlendState*        blendStateBlend = nullptr;
	ID3D11BlendState*        blendStateNoBlend = nullptr;
	ID3D11BlendState*        blendStateMask = nullptr;
	ID3D11BlendState*        blendStateAdd = nullptr;
	ID3D11RasterizerState*   rasterStateCull = nullptr;
	ID3D11RasterizerState*   rasterStateNoCull = nullptr;
	ID3D11RasterizerState*   rasterStateNoCullWireframe = nullptr;
	ID3D11DepthStencilState* depthStencilState = nullptr;
	ID3D11DepthStencilState* depthStencilStateFill = nullptr;
	ID3D11DepthStencilState* depthStencilStateGreaterOrEqualIncr = nullptr;
	ID3D11DepthStencilState* depthStencilStateGreaterOrEqualDecr = nullptr;
	ID3D11DepthStencilState* depthStencilStateGreaterOrEqualRead = nullptr;

	void InitStates() {
		D3D11_RASTERIZER_DESC rasterState;
		ZeroMemory(&rasterState, sizeof(D3D11_RASTERIZER_DESC));
		rasterState.FillMode = D3D11_FILL_SOLID;
		rasterState.CullMode = D3D11_CULL_BACK;
		g_pd3dDevice->CreateRasterizerState(&rasterState, &States::rasterStateCull);

		rasterState.CullMode = D3D11_CULL_NONE;
		g_pd3dDevice->CreateRasterizerState(&rasterState, &States::rasterStateNoCull);

		rasterState.FillMode = D3D11_FILL_WIREFRAME;
		g_pd3dDevice->CreateRasterizerState(&rasterState, &States::rasterStateNoCullWireframe);

		D3D11_BLEND_DESC BlendState;
		D3D11_BLEND_DESC MaskedBlendState;
		D3D11_BLEND_DESC AdditiveBlendState;
		D3D11_BLEND_DESC BlendStateNoBlend;
		ZeroMemory(&BlendState, sizeof(D3D11_BLEND_DESC));
		ZeroMemory(&BlendStateNoBlend, sizeof(D3D11_BLEND_DESC));
		ZeroMemory(&MaskedBlendState, sizeof(D3D11_BLEND_DESC));
		ZeroMemory(&AdditiveBlendState, sizeof(D3D11_BLEND_DESC));

		BlendStateNoBlend.RenderTarget[0].BlendEnable = FALSE;

		BlendState.RenderTarget[0].BlendEnable = TRUE;
		BlendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		BlendState.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		BlendState.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		BlendState.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		BlendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
		BlendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		BlendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;

		MaskedBlendState.RenderTarget[0] = BlendState.RenderTarget[0];
		MaskedBlendState.RenderTarget[0].RenderTargetWriteMask = 0x0;

		AdditiveBlendState.RenderTarget[0].BlendEnable = TRUE;
		AdditiveBlendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		AdditiveBlendState.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		AdditiveBlendState.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_ALPHA;
		AdditiveBlendState.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		AdditiveBlendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		AdditiveBlendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		AdditiveBlendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;

		g_pd3dDevice->CreateBlendState(&BlendState, &States::blendStateBlend);
		g_pd3dDevice->CreateBlendState(&BlendState, &States::blendStateNoBlend);
		g_pd3dDevice->CreateBlendState(&MaskedBlendState, &States::blendStateMask);
		g_pd3dDevice->CreateBlendState(&AdditiveBlendState, &States::blendStateAdd);

		D3D11_DEPTH_STENCIL_DESC DepthStencilState;
		ZeroMemory(&DepthStencilState, sizeof(D3D11_DEPTH_STENCIL_DESC));
		DepthStencilState.DepthEnable = FALSE;
		DepthStencilState.StencilEnable = FALSE;
		DepthStencilState.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		DepthStencilState.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		DepthStencilState.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		DepthStencilState.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		DepthStencilState.BackFace = DepthStencilState.FrontFace;

		D3D11_DEPTH_STENCIL_DESC DepthStencilStateFill;
		D3D11_DEPTH_STENCIL_DESC DepthStencilStateGreaterOrEqualIncr;
		D3D11_DEPTH_STENCIL_DESC DepthStencilStateGreaterOrEqualDecr;
		D3D11_DEPTH_STENCIL_DESC DepthStencilStateGreaterOrEqualRead;

		ZeroMemory(&DepthStencilStateFill, sizeof(D3D11_DEPTH_STENCIL_DESC));
		ZeroMemory(&DepthStencilStateGreaterOrEqualIncr, sizeof(D3D11_DEPTH_STENCIL_DESC));
		ZeroMemory(&DepthStencilStateGreaterOrEqualDecr, sizeof(D3D11_DEPTH_STENCIL_DESC));
		ZeroMemory(&DepthStencilStateGreaterOrEqualRead, sizeof(D3D11_DEPTH_STENCIL_DESC));

		DepthStencilStateFill.DepthEnable = FALSE;
		DepthStencilStateFill.StencilEnable = TRUE;
		DepthStencilStateFill.StencilWriteMask = 0xFF;
		DepthStencilStateFill.StencilReadMask = 0xFF;
		DepthStencilStateFill.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_REPLACE;
		DepthStencilStateFill.FrontFace.StencilFailOp = D3D11_STENCIL_OP_REPLACE;
		DepthStencilStateFill.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
		DepthStencilStateFill.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		DepthStencilStateFill.BackFace = DepthStencilStateFill.FrontFace;

		DepthStencilStateGreaterOrEqualIncr.DepthEnable = FALSE;
		DepthStencilStateGreaterOrEqualIncr.StencilEnable = TRUE;
		DepthStencilStateGreaterOrEqualIncr.StencilWriteMask = 0xFF;
		DepthStencilStateGreaterOrEqualIncr.StencilReadMask = 0xFF;
		DepthStencilStateGreaterOrEqualIncr.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		DepthStencilStateGreaterOrEqualIncr.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		DepthStencilStateGreaterOrEqualIncr.FrontFace.StencilPassOp = D3D11_STENCIL_OP_INCR;
		DepthStencilStateGreaterOrEqualIncr.FrontFace.StencilFunc = D3D11_COMPARISON_LESS_EQUAL;
		DepthStencilStateGreaterOrEqualIncr.BackFace = DepthStencilStateGreaterOrEqualIncr.FrontFace;

		DepthStencilStateGreaterOrEqualDecr = DepthStencilStateGreaterOrEqualIncr;
		DepthStencilStateGreaterOrEqualDecr.FrontFace.StencilPassOp = D3D11_STENCIL_OP_DECR;
		DepthStencilStateGreaterOrEqualDecr.BackFace = DepthStencilStateGreaterOrEqualDecr.FrontFace;

		DepthStencilStateGreaterOrEqualRead = DepthStencilStateGreaterOrEqualIncr;
		DepthStencilStateGreaterOrEqualRead.StencilWriteMask = 0X00;
		DepthStencilStateGreaterOrEqualRead.FrontFace.StencilFunc = D3D11_COMPARISON_LESS_EQUAL;
		DepthStencilStateGreaterOrEqualRead.BackFace = DepthStencilStateGreaterOrEqualRead.FrontFace;

		g_pd3dDevice->CreateDepthStencilState(&DepthStencilState, &States::depthStencilState);
		g_pd3dDevice->CreateDepthStencilState(&DepthStencilStateFill, &States::depthStencilStateFill);
		g_pd3dDevice->CreateDepthStencilState(&DepthStencilStateGreaterOrEqualIncr, &States::depthStencilStateGreaterOrEqualIncr);
		g_pd3dDevice->CreateDepthStencilState(&DepthStencilStateGreaterOrEqualDecr, &States::depthStencilStateGreaterOrEqualDecr);
		g_pd3dDevice->CreateDepthStencilState(&DepthStencilStateGreaterOrEqualRead, &States::depthStencilStateGreaterOrEqualRead);
	}
}

void UpdateLensComponents() {
	D3D11_BOX box = { 0 };
	box.left = aperture_id * sizeof(LensInterface);
	box.right = (aperture_id + 1) * sizeof(LensInterface);
	box.back = 1;
	box.bottom = 1;

	nikon_28_75mm_lens_interface[aperture_id].sa = aperture_opening;
	LensInterface aperture_component = nikon_28_75mm_lens_interface[aperture_id];

	g_pImmediateContext->UpdateSubresource(Buffers::lensInterface, 0, &box, &aperture_component, 0, 0);
}

void ParseLensComponents() {
	// Parse the lens components into the LensInterface the ray_trace routine expects
	nikon_28_75mm_lens_interface.resize(nikon_28_75mm_lens_components.size());

	for (int i = (int)nikon_28_75mm_lens_components.size() - 1; i >= 0; --i) {
		PatentFormat& entry = nikon_28_75mm_lens_components[i];
		total_lens_distance += entry.d;

		float left_ior = i == 0 ? 1.f : nikon_28_75mm_lens_components[i - 1].n;
		float right_ior = entry.n;

		if (right_ior != 1.f) {
			min_ior = min(min_ior, right_ior);
			max_ior = max(max_ior, right_ior);
		}

		vec3 center = { 0.f, 0.f, total_lens_distance - entry.r };
		vec3 n = { left_ior, 1.f, right_ior };

		LensInterface component = { center, entry.r, n, entry.h, entry.coating_thickness, (float)entry.flat, total_lens_distance, entry.w };
		nikon_28_75mm_lens_interface[i] = component;
	}

	HRESULT hr;
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bd.CPUAccessFlags = 0;
	bd.ByteWidth = (int)nikon_28_75mm_lens_interface.size() * (int)sizeof(LensInterface);
	bd.StructureByteStride = sizeof(LensInterface);

	D3D11_SUBRESOURCE_DATA data;
	data.pSysMem = &nikon_28_75mm_lens_interface.front();
	hr = g_pd3dDevice->CreateBuffer(&bd, &data, &Buffers::lensInterface);

	D3D11_UNORDERED_ACCESS_VIEW_DESC uaDescView;
	ZeroMemory(&uaDescView, sizeof(uaDescView));
	uaDescView.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uaDescView.Buffer.FirstElement = 0;
	uaDescView.Format = DXGI_FORMAT_UNKNOWN;
	uaDescView.Buffer.NumElements = bd.ByteWidth / bd.StructureByteStride;

	hr = g_pd3dDevice->CreateUnorderedAccessView(Buffers::lensInterface, &uaDescView, &Views::lensInterfaceResourceView);
}

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
HRESULT InitWindow( HINSTANCE hInstance, int nCmdShow );
HRESULT InitDevice();
void Render();

//--------------------------------------------------------------------------------------
// Entry point to the program. Initializes everything and goes into a message processing 
// loop. Idle time is used to render the scene.
//--------------------------------------------------------------------------------------
int WINAPI wWinMain( _In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow ) {
	UNREFERENCED_PARAMETER( hPrevInstance );
	UNREFERENCED_PARAMETER( lpCmdLine );

	if( FAILED( InitWindow( hInstance, nCmdShow ) ) )
		return 0;

	if( FAILED( InitDevice() ) ) {
		return 0;
	}

	// Main message loop
	MSG msg = {0};
	while( WM_QUIT != msg.message ) {

		if (msg.message == WM_KEYDOWN) {
			if (!key_down && msg.wParam == 65)
				overlay_wireframe = !overlay_wireframe;

			if (!key_down && msg.wParam == 69)
				editing_coating_quality = !editing_coating_quality;

			if (!key_down && msg.wParam == 81)
				editing_aperture = true;

			if (!key_down && msg.wParam == 82)
				editing_no_blades = true;

			if (!key_down && msg.wParam == 87)
				editing_spread = true;

			if (!key_down && msg.wParam == 32)
				draw2d = !draw2d;

			if (!key_down && msg.wParam == 37)
				ghost_bounce_1 = std::max<int>(2, ghost_bounce_1 - 1);

			if (!key_down && msg.wParam == 39)
				ghost_bounce_1 = std::min<int>((int)nikon_28_75mm_lens_interface.size() - 2, ghost_bounce_1 + 1);

			if (!key_down && msg.wParam == 40)
				ghost_bounce_2 = std::max<int>(1, ghost_bounce_2 - 1);

			if (!key_down && msg.wParam == 38) {
				ghost_bounce_2 = std::min<int>((int)nikon_28_75mm_lens_interface.size() - 2, ghost_bounce_2 + 1);
				ghost_bounce_2 = std::min<int>(ghost_bounce_1 - 2, ghost_bounce_2);
			}
		
			ghost_bounce_1 = std::max<int>(ghost_bounce_2 + 2, ghost_bounce_1);
			key_down = true;
		}

		if (msg.message == WM_KEYUP) {
			key_down = false;
			editing_aperture = false;
			editing_no_blades = false;
			editing_spread = false;
			editing_coating_quality = false;
		}

		if (msg.message == WM_LBUTTONDOWN) {
			left_mouse_down = true;
			//stringstream mouse_msg;
			//mouse_msg << y_to_print << "\n";
			//WCHAR str[16]; 
			//MultiByteToWideChar(0, 0, mouse_msg.str().c_str() + 1, mouse_msg.str().length(), str, 16);
			//OutputDebugString(str);
		}

		if (msg.message == WM_LBUTTONUP) {
			left_mouse_down = false;
		}

		if (msg.message == WM_MOUSEMOVE) {
			POINT p;
			GetCursorPos(&p);
			ScreenToClient(g_hWnd, &p);

			float nx = ((p.x / backbuffer_width) * 2.f - 1.f);
			float ny = ((p.y / backbuffer_height) * 2.f - 1.f);

			if (!key_down && left_mouse_down) {
				x_dir = nx * 0.2f;
				y_dir = ny * 0.2f;
			}

			if (editing_aperture) {
				aperture_opening = 5.f + ny * 5.f;
				UpdateLensComponents();
			}

			if (editing_no_blades) {
				number_of_blades = 10.f + ny * 5.f;
			}

			if (editing_spread) {
				rays_spread = 5.f + ny * 5.f;
			}

			if (editing_coating_quality) {
				coating_quality = 0.5f + ny;
			}
		}

		if( PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) ) {
			TranslateMessage( &msg );
			DispatchMessage( &msg );
		} else {
			Render();
		}
	}

	return ( int )msg.wParam;
}


//--------------------------------------------------------------------------------------
// Register class and create window
//--------------------------------------------------------------------------------------
HRESULT InitWindow( HINSTANCE hInstance, int nCmdShow ) {
	// Register class
	WNDCLASSEX wcex;
	wcex.cbSize = sizeof( WNDCLASSEX );
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon( hInstance, ( LPCTSTR )IDI_TUTORIAL1 );
	wcex.hCursor = LoadCursor( nullptr, IDC_ARROW );
	wcex.hbrBackground = ( HBRUSH )( COLOR_WINDOW + 1 );
	wcex.lpszMenuName = nullptr;
	wcex.lpszClassName = L"LensClass";
	wcex.hIconSm = LoadIcon( wcex.hInstance, ( LPCTSTR )IDI_TUTORIAL1 );
	if( !RegisterClassEx( &wcex ) )
		return E_FAIL;

	// Create window
	g_hInst = hInstance;
	RECT rc = { 0, 0, (LONG)backbuffer_width, (LONG)backbuffer_height };
	AdjustWindowRect( &rc, WS_OVERLAPPEDWINDOW, FALSE );
	g_hWnd = CreateWindow( L"LensClass", L"Lens Interface",
							WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
							CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInstance,
							nullptr );
	if( !g_hWnd )
		return E_FAIL;

	ShowWindow( g_hWnd, nCmdShow );

	return S_OK;
}

LensShapes::Rectangle CreateUnitRectangle() {

	float l = -1.f;
	float r =  1.f;
	float b = -1.f;
	float t =  1.f;

	SimpleVertex vertices[] = {
		XMFLOAT3(l, b, 0.f),
		XMFLOAT3(l, t, 0.f),
		XMFLOAT3(r, t, 0.f),
		XMFLOAT3(l, b, 0.f),
		XMFLOAT3(r, b, 0.f),
		XMFLOAT3(r, t, 0.f),
	};

	SimpleVertex lines[] = {
		XMFLOAT3(l, b, 0.f),
		XMFLOAT3(l, t, 0.f),
		XMFLOAT3(r, t, 0.f),
		XMFLOAT3(r, b, 0.f),
		XMFLOAT3(l, b, 0.f),
	};

	LensShapes::Rectangle rectangle;

	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(SimpleVertex) * 6;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd.CPUAccessFlags = 0;
	D3D11_SUBRESOURCE_DATA InitData;
	ZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = vertices;

	g_pd3dDevice->CreateBuffer(&bd, &InitData, &rectangle.vertices);

	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(SimpleVertex) * 5;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd.CPUAccessFlags = 0;
	D3D11_SUBRESOURCE_DATA InitData2;
	ZeroMemory(&InitData2, sizeof(InitData2));
	InitData2.pSysMem = lines;

	g_pd3dDevice->CreateBuffer(&bd, &InitData2, &rectangle.lines);

	return rectangle;
}

LensShapes::Circle CreateUnitCircle() {

	std::vector<SimpleVertex> triangle_vertices;
	std::vector<SimpleVertex> line_vertices;

	for (int i = 0; i < num_points_per_cirlces - 1; i++) {

		float t1 = (float)i / (float)(num_points_per_cirlces - 1);
		float a1 = t1 * 2.f * PI;
		float x1 = sin(a1);
		float y1 = cos(a1) / ratio;

		float t2 = (float)(i + 1) / (float)(num_points_per_cirlces - 1);
		float a2 = t2 * 2.f * PI;
		float x2 = sin(a2);
		float y2 = cos(a2) / ratio;

		SimpleVertex to_add1;
		SimpleVertex to_add2;
		SimpleVertex to_add3;
		to_add1.pos = XMFLOAT3(x1, y1, 0.f);
		to_add2.pos = XMFLOAT3(x2, y2, 0.f);
		to_add3.pos = XMFLOAT3(0.f, 0.f, 0.f);

		triangle_vertices.push_back(to_add1);
		triangle_vertices.push_back(to_add2);
		triangle_vertices.push_back(to_add3);

		line_vertices.push_back(to_add1);
	}

	SimpleVertex to_add;
	to_add.pos = XMFLOAT3(0.f, 1.f / ratio, 0.f);
	line_vertices.push_back(to_add);

	D3D11_BUFFER_DESC bd1;
	ZeroMemory(&bd1, sizeof(bd1));
	bd1.Usage = D3D11_USAGE_DEFAULT;
	bd1.ByteWidth = sizeof(SimpleVertex) * num_vertices_per_cirlces;
	bd1.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd1.CPUAccessFlags = 0;
	D3D11_SUBRESOURCE_DATA InitData1;
	ZeroMemory(&InitData1, sizeof(InitData1));
	InitData1.pSysMem = (float*)&triangle_vertices[0];

	D3D11_BUFFER_DESC bd2;
	ZeroMemory(&bd2, sizeof(bd2));
	bd2.Usage = D3D11_USAGE_DEFAULT;
	bd2.ByteWidth = sizeof(SimpleVertex) * num_points_per_cirlces;
	bd2.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd2.CPUAccessFlags = 0;
	D3D11_SUBRESOURCE_DATA InitData2;
	ZeroMemory(&InitData2, sizeof(InitData2));
	InitData2.pSysMem = (float*)&line_vertices[0];

	LensShapes::Circle circle;
	circle.x = 0.f;
	circle.y = 0.f;
	circle.r = 1.f;

	g_pd3dDevice->CreateBuffer(&bd1, &InitData1, &circle.triangles);
	g_pd3dDevice->CreateBuffer(&bd2, &InitData2, &circle.lines);

	return circle;
}

LensShapes::Patch CreateUnitPatch(int subdiv) {

	float l = -1.0f;
	float r = 1.0f;
	float b = -1.0f;
	float t = 1.0f;

	std::vector<SimpleVertex> vertices;
	for (int y = 0; y < subdiv; ++y) {
		float ny = (float)y / (float)(subdiv - 1);
		for (int x = 0; x < subdiv; ++x) {
			float nx = (float)x / (float)(subdiv - 1);
			float x_pos = lerp(l, r, nx);
			float y_pos = lerp(t, b, ny);
			vertices.push_back({ XMFLOAT3(x_pos, y_pos, 0.f) });
		}
	}

	std::vector<int> indices;
	int current_corner = 0;
	for (int y = 0; y < (subdiv - 1); ++y) {
		for (int x = 0; x < (subdiv - 1); ++x) {
			int i1 = current_corner;
			int i2 = i1 + 1;
			int i3 = i1 + subdiv;
			int i4 = i2 + subdiv;

			indices.push_back(i3);
			indices.push_back(i1);
			indices.push_back(i2);

			indices.push_back(i2);
			indices.push_back(i4);
			indices.push_back(i3);
			
			current_corner++;
		}
		current_corner++;
	}

	LensShapes::Patch patch;
	patch.subdiv = subdiv;

	D3D11_BUFFER_DESC bd;
	D3D11_SUBRESOURCE_DATA InitData;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(SimpleVertex) * (subdiv * subdiv);
	bd.StructureByteStride = sizeof(SimpleVertex);
	bd.CPUAccessFlags = 0;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	ZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = (float*)&vertices[0];
	g_pd3dDevice->CreateBuffer(&bd, &InitData, &patch.vs_vertices);

	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = 3 * 2 * sizeof(int) * ((subdiv - 1) * (subdiv - 1));
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bd.CPUAccessFlags = 0;
	ZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = (int*)&indices[0];
	g_pd3dDevice->CreateBuffer(&bd, &InitData, &patch.indices);

	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(PS_INPUT) * (subdiv * subdiv);
	bd.StructureByteStride = sizeof(PS_INPUT);
	bd.CPUAccessFlags = 0;
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	ZeroMemory(&InitData, sizeof(InitData));
	void* vertex_data = malloc(sizeof(PS_INPUT) * subdiv * subdiv);
	InitData.pSysMem = vertex_data;
	g_pd3dDevice->CreateBuffer(&bd, &InitData, &patch.cs_vertices);

	ZeroMemory(&bd, sizeof(bd));
	patch.cs_vertices->GetDesc(&bd);

	D3D11_SHADER_RESOURCE_VIEW_DESC descView;
	ZeroMemory(&descView, sizeof(descView));
	descView.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	descView.BufferEx.FirstElement = 0;
	descView.Format = DXGI_FORMAT_UNKNOWN;
	descView.BufferEx.NumElements = bd.ByteWidth / bd.StructureByteStride;

	D3D11_UNORDERED_ACCESS_VIEW_DESC uaDescView;
	ZeroMemory(&uaDescView, sizeof(uaDescView));
	uaDescView.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uaDescView.Buffer.FirstElement = 0;
	uaDescView.Format = DXGI_FORMAT_UNKNOWN;
	uaDescView.Buffer.NumElements = bd.ByteWidth / bd.StructureByteStride;

	g_pd3dDevice->CreateShaderResourceView(patch.cs_vertices, &descView, &patch.vertices_resource_view);
	g_pd3dDevice->CreateUnorderedAccessView(patch.cs_vertices, &uaDescView, &patch.ua_vertices_resource_view);

	return patch;
}

//--------------------------------------------------------------------------------------
// Create Direct3D device and swap chain
//--------------------------------------------------------------------------------------
HRESULT InitDevice()
{
	HRESULT hr = S_OK;

	RECT rc;
	GetClientRect( g_hWnd, &rc );
	UINT width = rc.right - rc.left;
	UINT height = rc.bottom - rc.top;

	UINT createDeviceFlags = 0;
	#ifdef _DEBUG
		createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
	#endif

	D3D_DRIVER_TYPE driverTypes[] = {
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
	};

	UINT numDriverTypes = ARRAYSIZE( driverTypes );

	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
	};

	UINT numFeatureLevels = ARRAYSIZE( featureLevels );

	for( UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++ ) {
		g_driverType = driverTypes[driverTypeIndex];
		hr = D3D11CreateDevice( nullptr, g_driverType, nullptr, createDeviceFlags, featureLevels, numFeatureLevels,
								D3D11_SDK_VERSION, &g_pd3dDevice, &g_featureLevel, &g_pImmediateContext );

		if ( hr == E_INVALIDARG ) {
			// DirectX 11.0 platforms will not recognize D3D_FEATURE_LEVEL_11_1 so we need to retry without it
			hr = D3D11CreateDevice( nullptr, g_driverType, nullptr, createDeviceFlags, &featureLevels[1], numFeatureLevels - 1,
									D3D11_SDK_VERSION, &g_pd3dDevice, &g_featureLevel, &g_pImmediateContext );
		}

		if(SUCCEEDED(hr))
			break;
	}

	// Obtain DXGI factory from device (since we used nullptr for pAdapter above)
	IDXGIFactory1* dxgiFactory = nullptr;
	IDXGIDevice* dxgiDevice = nullptr;
	IDXGIAdapter* adapter = nullptr;
	hr = g_pd3dDevice->QueryInterface( __uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice) );
	hr = dxgiDevice->GetAdapter(&adapter);
	hr = adapter->GetParent( __uuidof(IDXGIFactory1), reinterpret_cast<void**>(&dxgiFactory) );
	adapter->Release();

	// Create swap chain
	IDXGIFactory2* dxgiFactory2 = nullptr;
	hr = dxgiFactory->QueryInterface( __uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory2) );
	if ( dxgiFactory2 ) {
		// DirectX 11.1 or later
		hr = g_pd3dDevice->QueryInterface( __uuidof(ID3D11Device1), reinterpret_cast<void**>(&g_pd3dDevice1) );
		if (SUCCEEDED(hr)) {
			(void) g_pImmediateContext->QueryInterface( __uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&g_pImmediateContext1) );
		}

		DXGI_SWAP_CHAIN_DESC1 sd;
		ZeroMemory(&sd, sizeof(sd));
		sd.Width = width;
		sd.Height = height;
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount = 1;

		hr = dxgiFactory2->CreateSwapChainForHwnd( g_pd3dDevice, g_hWnd, &sd, nullptr, nullptr, &g_pSwapChain1 );
		if (SUCCEEDED(hr)) {
			hr = g_pSwapChain1->QueryInterface( __uuidof(IDXGISwapChain), reinterpret_cast<void**>(&g_pSwapChain) );
		}

		dxgiFactory2->Release();
	} else {
		// DirectX 11.0 systems
		DXGI_SWAP_CHAIN_DESC sd;
		ZeroMemory(&sd, sizeof(sd));
		sd.BufferCount = 1;
		sd.BufferDesc.Width = width;
		sd.BufferDesc.Height = height;
		sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.BufferDesc.RefreshRate.Numerator = 60;
		sd.BufferDesc.RefreshRate.Denominator = 1;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.OutputWindow = g_hWnd;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.Windowed = TRUE;

		hr = dxgiFactory->CreateSwapChain( g_pd3dDevice, &sd, &g_pSwapChain );
	}

	// Setup the viewport
	D3D11_VIEWPORT vp;
	vp.Width = (FLOAT)width;
	vp.Height = (FLOAT)height;
	vp.MinDepth = 0.f;
	vp.MaxDepth = 1.f;
	vp.TopLeftX = 0.f;
	vp.TopLeftY = 0.f;
	g_pImmediateContext->RSSetViewports(1, &vp);

	// Note this tutorial doesn't handle full-screen swapchains so we block the ALT+ENTER shortcut
	dxgiFactory->MakeWindowAssociation( g_hWnd, DXGI_MWA_NO_ALT_ENTER );
	dxgiFactory->Release();

	Textures::InitTextures();
	Textures::InitializeSamplers();
	Shaders::InitShaders();
	Buffers::InitBuffers();
	States::InitStates();

	FFT::InitFFTTetxtures();

	unit_circle = CreateUnitCircle();
	unit_square = CreateUnitRectangle();
	unit_patch = CreateUnitPatch(patch_tesselation);

	ParseLensComponents();

	return S_OK;
}

//--------------------------------------------------------------------------------------
// Called every time the application receives a message
//--------------------------------------------------------------------------------------
LRESULT CALLBACK WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam ) {
	PAINTSTRUCT ps;
	HDC hdc;

	switch( message ) {
		case WM_PAINT:
			hdc = BeginPaint( hWnd, &ps );
			EndPaint( hWnd, &ps );
			break;

		case WM_DESTROY:
			PostQuitMessage( 0 );
			break;

		default:
			return DefWindowProc( hWnd, message, wParam, lParam );
	}

	return 0;
}

void DrawRectangle(ID3D11DeviceContext* context, LensShapes::Rectangle& rectangle, XMFLOAT4& color, XMFLOAT4& placement, bool filled) {
	InstanceUniforms cb = { color, placement };
	context->UpdateSubresource(Buffers::instanceUniforms, 0, nullptr, &cb, 0, 0);
	if (filled) {
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		context->IASetVertexBuffers(0, 1, &rectangle.vertices, &stride, &offset);
		context->Draw(6, 0);
	} else {
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
		context->IASetVertexBuffers(0, 1, &rectangle.lines, &stride, &offset);
		context->Draw(5, 0);
	}
}

void DrawFullscreenQuad(ID3D11DeviceContext* context, LensShapes::Rectangle& rectangle, XMFLOAT4& color,
	ID3D11RenderTargetView*& rt_view, ID3D11DepthStencilView*& depth_buffer_view) {

	InstanceUniforms cb = { color, XMFLOAT4(0.f, 0.f, 0.f, 0.f) };
	context->UpdateSubresource(Buffers::instanceUniforms, 0, nullptr, &cb, 0, 0);

	context->RSSetState(States::rasterStateCull);

	context->OMSetBlendState(States::blendStateNoBlend, blendFactor, sampleMask);
	context->OMSetDepthStencilState(States::depthStencilState, 0);
	context->OMSetRenderTargets(1, &rt_view, depth_buffer_view);
	context->ClearRenderTargetView(rt_view, XMVECTORF32{ 0, 0, 0, 0 });
	context->ClearDepthStencilView(depth_buffer_view, D3D11_CLEAR_DEPTH, 1.0f, 0);

	context->IASetInputLayout(g_pVertexLayout2d);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	context->IASetVertexBuffers(0, 1, &rectangle.vertices, &stride, &offset);
	context->IASetIndexBuffer(unit_patch.indices, DXGI_FORMAT_R32_UINT, 0);

	context->Draw(6, 0);

	g_pImmediateContext->OMSetRenderTargets(1, &Views::renderTargetView, Views::depthStencilView);

}

void DrawCircle(ID3D11DeviceContext* context, LensShapes::Circle& circle, XMFLOAT4& color, XMFLOAT4& placement, bool filled) {
	InstanceUniforms cb = { color, placement };
	context->UpdateSubresource(Buffers::instanceUniforms, 0, nullptr, &cb, 0, 0);
	if (filled) {
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		context->IASetVertexBuffers(0, 1, &circle.triangles, &stride, &offset);
		context->Draw(num_vertices_per_cirlces, 0);
	} else {
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
		context->IASetVertexBuffers(0, 1, &circle.lines, &stride, &offset);
		context->Draw(num_points_per_cirlces, 0);
	}
}

void DrawFlat(LensInterface& right) {
	float mx = -(right.pos * global_scale - 1.f);
	float mw = global_scale * 0.4f;
	
	XMFLOAT4 mask_placement1 = { mx, 1.f, mw * 1.00f, global_scale * right.w };
	XMFLOAT4 mask_placement2 = { mx, 1.f, mw * 1.01f, global_scale * right.sa };
	XMFLOAT4 mask_placement3 = { mx + 0.0001f, 1.f, mw * 0.9f, global_scale * right.w };

	g_pImmediateContext->OMSetBlendState(States::blendStateMask, blendFactor, sampleMask);
	g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateFill, 1);
	DrawRectangle(g_pImmediateContext, unit_square, flat_fill_color, mask_placement1, true);

	g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateFill, 0);
	DrawRectangle(g_pImmediateContext, unit_square, flat_fill_color, mask_placement2, true);

	g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateGreaterOrEqualRead, 1);
	g_pImmediateContext->OMSetBlendState(States::blendStateBlend, blendFactor, sampleMask);
	DrawRectangle(g_pImmediateContext, unit_square, flat_fill_color, mask_placement3, true);
	DrawRectangle(g_pImmediateContext, unit_square, stroke_color, mask_placement3, false);
}

XMFLOAT4 LensColor(float ior, XMFLOAT4& c1, XMFLOAT4&c2) {
	float normalized_ior = (ior - min_ior) / (max_ior - min_ior);
	return lerp(c1, c2, normalized_ior);
	//return normalized_ior < 0.5f ? c1 : c2;
}

XMFLOAT4 IntersectionColor(int i) {
	float ior1 = nikon_28_75mm_lens_interface[i].n.x == 1.f ? nikon_28_75mm_lens_interface[i].n.z : nikon_28_75mm_lens_interface[i].n.x;
	return LensColor(ior1, intersection_color2, intersection_color3);
}

void DrawLens(LensInterface& left, LensInterface& right, bool opaque) {
	
	XMFLOAT4 fill_color = LensColor(right.n.x, fill_color1, fill_color2);
	fill_color.w = opaque ? opaque_alpha : fill_color.w;
	
	if (opaque)
		stroke_color = stroke_color1;
	else
		stroke_color = stroke_color2;

	//  |\      /|
	//  | |    | |
	//  | | or | |
	//  | |    | |
	//  |/      \|
	if (sign(left.radius) == sign(right.radius)) {
		LensInterface _left = left;
		LensInterface _right = right;
		float eps = 0.001f;
		
		float min_radius = max(left.radius, right.radius);
		if ((right.radius) > 0 && (left.radius > 0.f)) {
			min_radius = min(left.radius, right.radius);
			eps *= -1.f;
			_left = right;
			_right = left;
		}

		float delta = abs(_right.pos - _left.pos) * 0.5f;
		float mx = -(_right.pos * global_scale - 1.f) + eps;
		float mw = (min_radius - delta) * global_scale * right.w;
		float mh = global_scale * right.sa;
		XMFLOAT4 mask_placement = { mx, 1.f, mw, mh * 1.001f };
		XMFLOAT4 mask_placement2 = { mx, 1.f, mw * 0.995f, mh * 0.997f };

		float rx = -(_right.pos * global_scale - 1.f);
		float rr = _right.radius * global_scale;
		XMFLOAT4 right_placement = { rx, 1.f, rr, rr };

		float lx = -(_left.pos * global_scale - 1.f);
		float lr = _left.radius * global_scale;
		XMFLOAT4 left_placement = { lx, 1.f, lr, lr };

		g_pImmediateContext->ClearDepthStencilView(Views::depthStencilView, D3D11_CLEAR_STENCIL, 1.0f, 0);
		g_pImmediateContext->OMSetBlendState(States::blendStateMask, blendFactor, sampleMask);
		g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateFill, 1);
		DrawRectangle(g_pImmediateContext, unit_square, stroke_color, mask_placement, true);

		g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateGreaterOrEqualIncr, 1);
		DrawCircle(g_pImmediateContext, unit_circle, fill_color, right_placement, true);

		g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateGreaterOrEqualDecr, 2);
		DrawCircle(g_pImmediateContext, unit_circle, fill_color, left_placement, true);

		g_pImmediateContext->OMSetBlendState(States::blendStateBlend, blendFactor, sampleMask);
		g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateGreaterOrEqualRead, 2);
		DrawRectangle(g_pImmediateContext, unit_square, fill_color, mask_placement, true);
		DrawRectangle(g_pImmediateContext, unit_square, stroke_color, mask_placement2, false);

		g_pImmediateContext->OMSetBlendState(States::blendStateMask, blendFactor, sampleMask);
		g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateFill, 1);
		DrawRectangle(g_pImmediateContext, unit_square, stroke_color, mask_placement, true);

		g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateGreaterOrEqualRead, 1);
		g_pImmediateContext->OMSetBlendState(States::blendStateBlend, blendFactor, sampleMask);
		DrawCircle(g_pImmediateContext, unit_circle, stroke_color, left_placement, false);
		DrawCircle(g_pImmediateContext, unit_circle, stroke_color, right_placement, false);
	}
	//     / \
	//    |   |
	//    |   |
	//    |   |
	//     \ /
	else if (left.radius > 0.f && right.radius < 0.f) {
		float eps = 0.001f;
		float delta = abs(right.pos - left.pos);
		float mx = -(right.pos * global_scale - 1.f) + eps;
		float mw = -delta * global_scale * right.w - eps;
		float mh = global_scale * right.sa;
		XMFLOAT4 mask_placement = { mx, 1.f, mw, mh };
		XMFLOAT4 mask_placement2 = { mx, 1.f, mw * 0.995f, mh * 0.997f };

		float lx = -(left.pos * global_scale - 1.f);
		float lr = left.radius * global_scale;
		XMFLOAT4 left_placement = { lx, 1.f, lr, lr };

		float rx = -(right.pos * global_scale - 1.f);
		float rr = right.radius * global_scale;
		XMFLOAT4 right_placement = { rx, 1.f, rr, rr };

		g_pImmediateContext->ClearDepthStencilView(Views::depthStencilView, D3D11_CLEAR_STENCIL, 1.0f, 0);
		g_pImmediateContext->OMSetBlendState(States::blendStateMask, blendFactor, sampleMask);
		g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateFill, 1);
		DrawRectangle(g_pImmediateContext, unit_square, stroke_color, mask_placement, true);
		g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateGreaterOrEqualIncr, 1);
		DrawCircle(g_pImmediateContext, unit_circle, stroke_color, left_placement, true);

		g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateGreaterOrEqualIncr, 2);
		g_pImmediateContext->OMSetBlendState(States::blendStateBlend, blendFactor, sampleMask);
		DrawCircle(g_pImmediateContext, unit_circle, fill_color, right_placement, true);

		g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateGreaterOrEqualRead, 3);
		DrawRectangle(g_pImmediateContext, unit_square, stroke_color, mask_placement2, false);

		g_pImmediateContext->ClearDepthStencilView(Views::depthStencilView, D3D11_CLEAR_STENCIL, 1.0f, 0);
		g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateFill, 1);
		g_pImmediateContext->OMSetBlendState(States::blendStateMask, blendFactor, sampleMask);
		DrawRectangle(g_pImmediateContext, unit_square, stroke_color, mask_placement, true);

		g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateGreaterOrEqualRead, 1);
		g_pImmediateContext->OMSetBlendState(States::blendStateBlend, blendFactor, sampleMask);
		DrawCircle(g_pImmediateContext, unit_circle, stroke_color, left_placement, false);
		DrawCircle(g_pImmediateContext, unit_circle, stroke_color, right_placement, false);
	}
	//   \    /
	//    |  |
	//    |  |
	//    |  |
	//   /    \ 
	else if (left.radius < 0.f && right.radius > 0.f) {
		float delta = abs(right.pos - left.pos);
		float w = delta * right.w;
		float mx = -((right.pos + delta * 0.5f + w) * global_scale - 1.f);
		float mw = global_scale * w;
		float mh = global_scale * right.sa;
		XMFLOAT4 mask_placement = { mx, 1.f, mw, mh };
		XMFLOAT4 mask_placement2 = { mx, 1.f, mw * 0.995f, mh * 0.995f };

		float lx = -(left.pos * global_scale - 1.f);
		float lr = left.radius * global_scale;
		XMFLOAT4 left_placement = { lx, 1.f, lr, lr };

		float rx = -(right.pos * global_scale - 1.f);
		float rr = right.radius * global_scale;
		XMFLOAT4 right_placement = { rx, 1.f, rr, rr };

		g_pImmediateContext->ClearDepthStencilView(Views::depthStencilView, D3D11_CLEAR_STENCIL, 1.0f, 0);
		g_pImmediateContext->OMSetBlendState(States::blendStateMask, blendFactor, sampleMask);
		g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateFill, 1);
		DrawRectangle(g_pImmediateContext, unit_square, fill_color, mask_placement, true);

		g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateFill, 0);
		DrawCircle(g_pImmediateContext, unit_circle, fill_color, left_placement, true);
		DrawCircle(g_pImmediateContext, unit_circle, fill_color, right_placement, true);

		g_pImmediateContext->OMSetBlendState(States::blendStateBlend, blendFactor, sampleMask);
		g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateGreaterOrEqualRead, 1);
		DrawRectangle(g_pImmediateContext, unit_square, fill_color, mask_placement, true);
		DrawRectangle(g_pImmediateContext, unit_square, stroke_color, mask_placement2, false);

		g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateFill, 1);
		g_pImmediateContext->OMSetBlendState(States::blendStateMask, blendFactor, sampleMask);
		DrawRectangle(g_pImmediateContext, unit_square, fill_color, mask_placement, true);

		g_pImmediateContext->OMSetBlendState(States::blendStateBlend, blendFactor, sampleMask);
		g_pImmediateContext->OMSetDepthStencilState(States::depthStencilStateGreaterOrEqualRead, 1);
		DrawCircle(g_pImmediateContext, unit_circle, stroke_color, left_placement, false);
		DrawCircle(g_pImmediateContext, unit_circle, stroke_color, right_placement, false);
	}
}

void DrawLensInterface(std::vector<LensInterface>& lens_interface) {

	g_pImmediateContext->ClearDepthStencilView(Views::depthStencilView, D3D11_CLEAR_STENCIL, 1.0f, 0);
	g_pImmediateContext->OMSetDepthStencilState(States::depthStencilState, 0);
	g_pImmediateContext->OMSetBlendState(States::blendStateBlend, blendFactor, sampleMask);

	int i = 0;
	while (i < (int)lens_interface.size()) {
		bool opaque1 = (i == ghost_bounce_1 - 1);
		bool opaque2 = (i == ghost_bounce_2 - 1);
		if (lens_interface[i].flat) {
			DrawFlat(lens_interface[i]);
			i += 1;
		} else if (lens_interface[i].n.x == 1.f) {
			opaque1 = opaque1 || (i == ghost_bounce_1 - 2);
			opaque2 = opaque2 || (i == ghost_bounce_2 - 2);
			DrawLens(lens_interface[i], lens_interface[i + 1], opaque1 || opaque2);
			i += 2;
		} else {
			DrawLens(lens_interface[i - 1], lens_interface[i], opaque1 || opaque2);
			i += 1;
		}
	}
}

void DrawIntersections(ID3D11DeviceContext* context, ID3D11Buffer* buffer, std::vector<vec3>& intersections, int max_points, XMFLOAT4& color) {
	InstanceUniforms cb;
	cb.color = color;
	cb.placement = XMFLOAT4(0.f, 0.f, 0.f, 0.f);

	std::vector<XMFLOAT3> points(max_points);
	for (int i = 0; i < (int)intersections.size(); ++i) {
		points[i] = (point_to_d3d(intersections[i]));
	}

	void* ptr = &points.front();
	context->UpdateSubresource(buffer, 0, nullptr, ptr, 0, 0);
	context->UpdateSubresource(Buffers::instanceUniforms, 0, nullptr, &cb, 0, 0);
	
	g_pImmediateContext->OMSetDepthStencilState(States::depthStencilState, 0);
	g_pImmediateContext->OMSetBlendState(States::blendStateBlend, blendFactor, sampleMask);

	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
	context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
	context->Draw((int)intersections.size(), 0);
}

void UpdateGlobals() {
	time += speed;
	
	#if defined(DRAWLENSFLARE)
		direction = normalize(vec3(-x_dir, y_dir, -1.f));
	#else
		direction = normalize(vec3(0, y_dir, -1.f));
	#endif

	GlobalData cb = {
		time,
		rays_spread,
		nikon_28_75mm_lens_interface[nikon_28_75mm_lens_interface.size() - 1].sa,

		aperture_id,
		(float)nikon_28_75mm_lens_interface.size(),
		coating_quality,

		XMFLOAT2(backbuffer_width, backbuffer_height),
		XMFLOAT4(direction.x, direction.y, direction.z, aperture_resolution),
		XMFLOAT4(aperture_opening, number_of_blades, starburst_resolution,0)
	};

	g_pImmediateContext->UpdateSubresource(Buffers::globalData, 0, nullptr, &cb, 0, 0);
}

void DrawAperture() {
	g_pImmediateContext->VSSetShader(Shaders::vertexShader, nullptr, 0);
	g_pImmediateContext->PSSetShader(Shaders::aperture_ps_shader, nullptr, 0);
	g_pImmediateContext->PSSetSamplers(0, 1, &Textures::linear_clamp_sampler);
	g_pImmediateContext->PSSetShaderResources(1, 1, &Textures::dust_sr_view);

	g_pImmediateContext->VSSetConstantBuffers(0, 1, &Buffers::instanceUniforms);
	g_pImmediateContext->PSSetConstantBuffers(0, 1, &Buffers::instanceUniforms);
	g_pImmediateContext->PSSetConstantBuffers(1, 1, &Buffers::globalData);

	DrawFullscreenQuad(g_pImmediateContext, unit_square, fill_color1, Textures::aperture_rt_view, Textures::aperture_depth_buffer_view);

	g_pImmediateContext->PSSetShaderResources(1, 1, Views::null_sr_view);
}

void DrawStarBurst() {
	float clear_color[] = { 0,0,0,0 };
	
	// Setup input
	FFT::CopyTextureNoStretch(g_pImmediateContext, Textures::aperture_sr_view, FFT::mTextureUAV[FFT::FFTTexture_Real0], (int)aperture_resolution);
	g_pImmediateContext->ClearRenderTargetView(FFT::mRenderTargetViews[FFT::FFTTexture_Imaginary0], clear_color);

	FFT::RunDispatchSLM(g_pImmediateContext, 0, Shaders::fftRowComputeShader);
	FFT::RunDispatchSLM(g_pImmediateContext, 1, Shaders::fftColComputeShader);

	D3D11_VIEWPORT vp;
	vp.Width = (FLOAT)starburst_resolution;
	vp.Height = (FLOAT)starburst_resolution;
	vp.MinDepth = 0.f;
	vp.MaxDepth = 1.f;
	vp.TopLeftX = 0.f;
	vp.TopLeftY = 0.f;
	g_pImmediateContext->RSSetViewports(1, &vp);

	g_pImmediateContext->PSSetShader(Shaders::starburstFromFFTPixelShader, nullptr, 0);
	g_pImmediateContext->PSSetShaderResources(1, 2, FFT::mTextureSRV);
	g_pImmediateContext->PSSetSamplers(0, 1, &Textures::linear_wrap_sampler);
	g_pImmediateContext->PSSetConstantBuffers(1, 1, &Buffers::globalData);
	DrawFullscreenQuad(g_pImmediateContext, unit_square, fill_color1, Textures::starburst_rt_view, Textures::starburst_depth_buffer_view);
	g_pImmediateContext->PSSetShaderResources(1, 1, Views::null_sr_view);

	g_pImmediateContext->PSSetShader(Shaders::starburstFilterPixelShader, nullptr, 0);
	g_pImmediateContext->PSSetShaderResources(1, 1, &Textures::starburst_sr_view);
	g_pImmediateContext->PSSetSamplers(0, 1, &Textures::linear_wrap_sampler);
	g_pImmediateContext->PSSetConstantBuffers(1, 1, &Buffers::globalData);
	DrawFullscreenQuad(g_pImmediateContext, unit_square, fill_color1, Textures::starburst_filtered_rt_view, Textures::starburst_depth_buffer_view);
	g_pImmediateContext->PSSetShaderResources(1, 1, Views::null_sr_view);

	vp.Width = (FLOAT)backbuffer_width;
	vp.Height = (FLOAT)backbuffer_height;
	g_pImmediateContext->RSSetViewports(1, &vp);
}

//--------------------------------------------------------------------------------------
// Render a frame
//--------------------------------------------------------------------------------------
void Render() {

	UpdateGlobals();

	static bool first = true;
	if (first) {
		DrawAperture();
		DrawStarBurst();
		first = false;
	}

	#if defined(DRAWLENSFLARE)
		g_pImmediateContext->IASetInputLayout(g_pVertexLayout3d);

		g_pImmediateContext->RSSetState(States::rasterStateNoCull);
		g_pImmediateContext->OMSetRenderTargets(1, &Views::HDRView, Views::depthStencilView);
		g_pImmediateContext->ClearRenderTargetView(Views::HDRView, XMVECTORF32{ 0, 0, 0, 0 });
		g_pImmediateContext->ClearDepthStencilView(Views::depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

		g_pImmediateContext->OMSetDepthStencilState(States::depthStencilState, 0);
		g_pImmediateContext->OMSetBlendState(States::blendStateAdd, blendFactor, sampleMask);

		g_pImmediateContext->IASetIndexBuffer(unit_patch.indices, DXGI_FORMAT_R32_UINT, 0);
		g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		g_pImmediateContext->VSSetShader(Shaders::flareVertexShader, nullptr, 0);
		g_pImmediateContext->VSSetConstantBuffers(1, 1, &Buffers::globalData);

		g_pImmediateContext->PSSetShader(Shaders::flarePixelShader, nullptr, 0);
		g_pImmediateContext->PSSetConstantBuffers(1, 1, &Buffers::globalData);
		g_pImmediateContext->PSSetShaderResources(1, 1, &Textures::aperture_sr_view);
		g_pImmediateContext->PSSetSamplers(0, 1, &Textures::linear_clamp_sampler);

		g_pImmediateContext->CSSetShader(Shaders::flareComputeShader, nullptr, 0);
		g_pImmediateContext->CSSetConstantBuffers(1, 1, &Buffers::globalData);
		g_pImmediateContext->CSSetConstantBuffers(2, 1, &Buffers::ghostData);
		
		int bounce1 = 2;
		int bounce2 = 1;
		while (true) {
			if (bounce1 >= (int)(nikon_28_75mm_lens_interface.size() - 1)) {
				bounce2++;
				bounce1 = bounce2 + 1;
			}

			if (bounce2 >= (int)(nikon_28_75mm_lens_interface.size() - 1)) {
				break;
			}

			GhostData cb = { XMFLOAT4((float)bounce1, (float)bounce2, 0, 0) };
			g_pImmediateContext->UpdateSubresource(Buffers::ghostData, 0, nullptr, &cb, 0, 0);

			g_pImmediateContext->CSSetUnorderedAccessViews(0, 1, &unit_patch.ua_vertices_resource_view, nullptr);
			g_pImmediateContext->CSSetUnorderedAccessViews(1, 1, &Views::lensInterfaceResourceView, nullptr);
			g_pImmediateContext->Dispatch(num_groups, num_groups, 3);
			g_pImmediateContext->CSSetUnorderedAccessViews(0, 1, Views::null_ua_view, nullptr);
			g_pImmediateContext->CSSetUnorderedAccessViews(1, 1, &Views::lensInterfaceResourceView, nullptr);

			g_pImmediateContext->VSSetShaderResources(0, 1, &unit_patch.vertices_resource_view);
			g_pImmediateContext->DrawIndexed(num_vertices_per_bundle * 3 * 2, 0, 0);
			g_pImmediateContext->VSSetShaderResources(0, 1, Views::null_sr_view);

			bounce1++;
		}




		g_pImmediateContext->VSSetShader(Shaders::starburstVertexShader, nullptr, 0);
		g_pImmediateContext->PSSetShaderResources(1, 1, &Textures::starburst_filtered_sr_view);
		g_pImmediateContext->PSSetSamplers(0, 1, &Textures::linear_wrap_sampler);
		g_pImmediateContext->PSSetShader(Shaders::starburstPixelShader, nullptr, 0);
		g_pImmediateContext->RSSetState(States::rasterStateCull);

		g_pImmediateContext->OMSetBlendState(States::blendStateAdd, blendFactor, sampleMask);

		g_pImmediateContext->IASetInputLayout(g_pVertexLayout2d);
		g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		g_pImmediateContext->IASetVertexBuffers(0, 1, &unit_square.vertices, &stride, &offset);
		g_pImmediateContext->IASetIndexBuffer(unit_patch.indices, DXGI_FORMAT_R32_UINT, 0);

		g_pImmediateContext->Draw(6, 0);




		g_pImmediateContext->RSSetState(States::rasterStateCull);
		g_pImmediateContext->IASetInputLayout(g_pVertexLayout2d);
		g_pImmediateContext->OMSetRenderTargets(1, &Views::renderTargetView, Views::depthStencilView);
		g_pImmediateContext->ClearRenderTargetView(Views::renderTargetView, XMVECTORF32{ 0,0,0,0});

		g_pImmediateContext->VSSetShader(Shaders::vertexShader, nullptr, 0);
		g_pImmediateContext->PSSetShader(Shaders::toneMapPixelShader, nullptr, 0);
		g_pImmediateContext->PSSetConstantBuffers(0, 1, &Buffers::instanceUniforms);
		g_pImmediateContext->VSSetConstantBuffers(0, 1, &Buffers::instanceUniforms);
		g_pImmediateContext->PSSetShaderResources(1, 1, &Views::shaderHDRView);

		DrawFullscreenQuad(g_pImmediateContext, unit_square, fill_color1, Views::renderTargetView, Views::depthStencilView);

		g_pImmediateContext->PSSetShaderResources(1, 1, Views::null_sr_view);

	#else
		if(!draw2d) {
			GhostData cb = { XMFLOAT4((float)ghost_bounce_1, (float)ghost_bounce_2, 0, 0) };
			g_pImmediateContext->UpdateSubresource(Buffers::ghostData, 0, nullptr, &cb, 0, 0);

			g_pImmediateContext->IASetInputLayout(g_pVertexLayout3d);
			g_pImmediateContext->IASetIndexBuffer(unit_patch.indices, DXGI_FORMAT_R32_UINT, 0);
			g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			g_pImmediateContext->OMSetRenderTargets(1, &Views::renderTargetView, Views::depthStencilView);
			g_pImmediateContext->OMSetDepthStencilState(States::depthStencilState, 0);
			g_pImmediateContext->OMSetBlendState(States::blendStateBlend, blendFactor, sampleMask);
			g_pImmediateContext->ClearRenderTargetView(Views::renderTargetView, XMVECTORF32{ background_color2.x, background_color2.y, background_color2.z, background_color2.w });
			g_pImmediateContext->ClearDepthStencilView(Views::depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

			g_pImmediateContext->CSSetShader(Shaders::flareComputeShader, nullptr, 0);
			g_pImmediateContext->CSSetConstantBuffers(1, 1, &Buffers::globalData);
			g_pImmediateContext->CSSetConstantBuffers(2, 1, &Buffers::ghostData);
			g_pImmediateContext->CSSetUnorderedAccessViews(0, 1, &unit_patch.ua_vertices_resource_view, nullptr);
			g_pImmediateContext->CSSetUnorderedAccessViews(1, 1, &Views::lensInterfaceResourceView, nullptr);

			g_pImmediateContext->VSSetShader(Shaders::flareVertexShader, nullptr, 0);
			g_pImmediateContext->VSSetConstantBuffers(1, 1, &Buffers::globalData);

			g_pImmediateContext->PSSetShader(Shaders::flarePixelShaderDebug, nullptr, 0);
			g_pImmediateContext->PSSetSamplers(0, 1, &Textures::linear_clamp_sampler);
			g_pImmediateContext->PSSetConstantBuffers(1, 1, &Buffers::globalData);
			g_pImmediateContext->PSSetShaderResources(1, 1, &Textures::aperture_sr_view);
			// Dispatch
			g_pImmediateContext->Dispatch(num_groups, num_groups, 3);
			g_pImmediateContext->CSSetUnorderedAccessViews(0, 1, Views::null_ua_view, nullptr);

			// Draw
			g_pImmediateContext->VSSetShaderResources(0, 1, &unit_patch.vertices_resource_view);
			g_pImmediateContext->RSSetState(States::rasterStateNoCull);
			g_pImmediateContext->DrawIndexed(num_vertices_per_bundle * 3 * 2, 0, 0);

			if (overlay_wireframe) {
				g_pImmediateContext->RSSetState(States::rasterStateNoCullWireframe);
				g_pImmediateContext->PSSetShader(Shaders::flarePixelShaderDebugWireframe, nullptr, 0);
				g_pImmediateContext->DrawIndexed(num_vertices_per_bundle * 3 * 2, 0, 0);
			}

			g_pImmediateContext->VSSetShaderResources(0, 1, Views::null_sr_view);
			g_pImmediateContext->PSSetShaderResources(1, 1, Views::null_sr_view);
		} else {
			g_pImmediateContext->RSSetState(States::rasterStateCull);
			g_pImmediateContext->IASetInputLayout(g_pVertexLayout2d);

			g_pImmediateContext->OMSetRenderTargets(1, &Views::renderTargetView, Views::depthStencilView);
			g_pImmediateContext->ClearRenderTargetView(Views::renderTargetView, XMVECTORF32{ background_color1.x, background_color1.y, background_color1.z, background_color1.w });
			g_pImmediateContext->ClearDepthStencilView(Views::depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

			g_pImmediateContext->VSSetShader(Shaders::vertexShader, nullptr, 0);
			g_pImmediateContext->VSSetConstantBuffers(0, 1, &Buffers::instanceUniforms);

			g_pImmediateContext->PSSetShader(Shaders::pixelShader, nullptr, 0);
			g_pImmediateContext->PSSetConstantBuffers(0, 1, &Buffers::instanceUniforms);

			// Trace all rays
			std::vector<std::vector<vec3>> intersections1(num_of_rays);
			std::vector<std::vector<vec3>> intersections2(num_of_rays);
			std::vector<std::vector<vec3>> intersections3(num_of_rays);
			for (int i = 0; i < num_of_rays; ++i) {
				float pos = lerp(-1.f, 1.f, (float)i / (float)(num_of_rays - 1)) * rays_spread;

				vec3 a1 = vec3(0.0f, pos, 400.f);
				vec3 d1 = vec3(0.0f, 0.0f, -1.f);
				Ray r1 = { a1, d1 };
				Intersection i1 = testSPHERE(r1, nikon_28_75mm_lens_interface[0]);

				vec3 a2 = i1.pos - direction;

				Ray r = { a2, direction };

				Trace(r, 1.f, nikon_28_75mm_lens_interface, intersections1[i], intersections2[i], intersections3[i], int2{ ghost_bounce_1, ghost_bounce_2 });
			}

			// Draw all rays
			XMFLOAT4 ghost_color1 = IntersectionColor(ghost_bounce_1 - 1);
			XMFLOAT4 ghost_color2 = IntersectionColor(ghost_bounce_2 - 1);
			for (int i = 0; i < num_of_rays; ++i)
				DrawIntersections(g_pImmediateContext, Buffers::intersectionPoints1, intersections1[i], num_of_intersections_1, intersection_color1);

			for (int i = 0; i < num_of_rays; ++i)
				DrawIntersections(g_pImmediateContext, Buffers::intersectionPoints2, intersections2[i], num_of_intersections_2, ghost_color1);

			for (int i = 0; i < num_of_rays; ++i)
				DrawIntersections(g_pImmediateContext, Buffers::intersectionPoints3, intersections3[i], num_of_intersections_3, ghost_color2);

			// Draw lenses
			DrawLensInterface(nikon_28_75mm_lens_interface);
		}
	#endif

	// Visualize the aperture texture:
	// g_pImmediateContext->PSSetShader(Shaders::toneMapPixelShader, nullptr, 0);
	// g_pImmediateContext->PSSetShaderResources(1, 1, &Textures::starburst_filtered_sr_view);
	// DrawFullscreenQuad(g_pImmediateContext, unit_square, fill_color1, Views::renderTargetView, Views::depthStencilView);
		
	// Visualize the dust texture:
	// g_pImmediateContext->PSSetShader(Shaders::toneMapPixelShader, nullptr, 0);
	// g_pImmediateContext->PSSetShaderResources(1, 1, &Textures::dust_sr_view);

	g_pSwapChain->Present(0, 0);
	// SaveBackBuffer();
}
