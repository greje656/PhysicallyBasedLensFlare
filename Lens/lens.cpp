#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <directxmath.h>
#include <directxcolors.h>
#include "resource.h"
#include "ray_trace.h"
#include <vector>
#include <string>

using namespace DirectX;

const std::string vertex_shader_source = R"(
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
)";

const std::string pixel_shader_source = R"(
cbuffer Uniforms : register(b0)
{
	float4 color;
	float4 placement;
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS( float4 Pos : SV_POSITION ) : SV_Target
{
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
};

struct SimpleVertex {
	XMFLOAT3 Pos;
};

struct Uniforms {
	XMFLOAT4 color;
	XMFLOAT4 placement;
};

namespace LensShapes {
	struct Line {
		float x;
		ID3D11Buffer* vertices;
	};

	struct Rectangle {
		ID3D11Buffer* vertices;
		ID3D11Buffer* lines;
	};

	struct Circle {
		float x, y, r;
		ID3D11Buffer* triangles;
		ID3D11Buffer* lines;
	};
}

LensShapes::Circle unit_circle;
LensShapes::Rectangle unit_square;
std::vector<LensInterface> Nikon_28_75mm_lens_interface;

const float d6  = 53.142f;
const float d10 =  7.063f;
const float d14 =  1.532f;
const float dAp =  2.800f;
const float d20 = 16.889f;
const float Bf  = 39.683f;

std::vector<PatentFormat> Nikon_28_75mm_lens_components = {
	{    72.747f,  2.300f, 1.60300f, false, 0.2f, 29.0f },
	{    37.000f, 13.000f, 1.00000f, false, 0.2f, 29.0f },

	{  -172.809f,  2.100f, 1.58913f, false, 2.7f, 26.2f },
	{    39.894f,  1.000f, 1.00000f, false, 2.7f, 26.2f },

	{    49.820f,  4.400f, 1.86074f, false, 0.5f, 20.0f },
	{    74.750f,      d6, 1.00000f, false, 0.5f, 20.0f },

	{    63.402f,  1.600f, 1.86074f, false, 0.5f, 16.1f },
	{    37.530f,  8.600f, 1.51680f, false, 0.5f, 16.1f },

	{   -75.887f,  1.600f, 1.80458f, false, 0.5f, 16.0f },
	{   -97.792f,     d10, 1.00000f, false, 0.5f, 16.5f },

	{    96.034f,  3.600f, 1.62041f, false, 0.5f, 18.0f },
	{   261.743f,  0.100f, 1.00000f, false, 0.5f, 18.0f },

	{    54.262f,  6.000f, 1.69680f, false, 0.5f, 18.0f },
	{ -5995.277f,     d14, 1.00000f, false, 0.5f, 18.0f },

	{       0.0f,     dAp, 1.00000f, true,  18.f, 10.0f },

	{   -74.414f,  2.200f, 1.90265f, false, 0.5f, 13.0f },

	{   -62.929f,  1.450f, 1.51680f, false, 0.5f, 13.0f },
	{   121.380f,  2.500f, 1.00000f, false, 4.0f, 13.1f },

	{   -85.723f,  1.400f, 1.49782f, false, 4.0f, 13.0f },

	{    31.093f,  2.600f, 1.80458f, false, 4.0f, 13.1f },
	{    84.758f,     d20, 1.00000f, false, 0.5f, 13.0f },

	{   459.690f,  1.400f, 1.86074f, false, 1.0f, 15.0f },

	{    40.240f,  7.300f, 1.49782f, false, 1.0f, 15.0f },
	{   -49.771f,  0.100f, 1.00000f, false, 1.0f, 15.2f },

	{    62.369f,  7.000f, 1.67025f, false, 1.0f, 16.0f },
	{   -76.454f,  5.200f, 1.00000f, false, 1.0f, 16.0f },

	{   -32.524f,  2.000f, 1.80454f, false, 0.5f, 17.0f },
	{   -50.194f,      Bf, 1.00000f, false, 0.5f, 17.0f },

	{        0.f,     5.f, 1.00000f,  true, 17.f,  0.0f },
};

int num_of_rays = 51;
int num_of_intersections = (int)Nikon_28_75mm_lens_components.size() + 1;
int num_points_per_cirlces = 200;
int num_vertices_per_cirlces = num_points_per_cirlces * 3;
float backbuffer_width = 1800;
float backbuffer_height = 900;
float ratio = backbuffer_height / backbuffer_width;
float min_ior = 1000.f;
float max_ior = -1000.f;
float global_scale = 0.009;
float total_lens_distance = 0.f;

INT sampleMask = 0x0F;
UINT offset = 0;
UINT stride = sizeof(SimpleVertex);
float blendFactor[4] = { 1.f, 1.f, 1.f, 1.f };

XMFLOAT4 fill_color1        = {  64.f / 255.f, 215.f / 255.f, 242.f / 255.f, 0.35f };
XMFLOAT4 fill_color2        = { 179.f / 255.f, 178.f / 255.f, 210.f / 255.f, 0.45f };
XMFLOAT4 stroke_color       = {  64.f / 255.f,  64.f / 255.f,  64.f / 255.f, 1.00f };
XMFLOAT4 flat_fill_color    = { 180.f / 255.f, 180.f / 255.f, 180.f / 255.f, 1.00f };
XMFLOAT4 intersection_color = {  64.f / 255.f, 215.f / 255.f, 242.f / 255.f, 0.35f };

XMFLOAT3 point_to_d3d(vec3& point) {
	float x = point.x * global_scale;
	float y = point.y / ratio* global_scale;
	float z = point.z * global_scale;
	return XMFLOAT3(-(z - 1.f), y, x);
}

float sign(float v) {
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
HINSTANCE                g_hInst = nullptr;
HWND                     g_hWnd = nullptr;
D3D_DRIVER_TYPE          g_driverType = D3D_DRIVER_TYPE_NULL;
D3D_FEATURE_LEVEL        g_featureLevel = D3D_FEATURE_LEVEL_11_0;
ID3D11Device*            g_pd3dDevice = nullptr;
ID3D11Device1*           g_pd3dDevice1 = nullptr;
ID3D11DeviceContext*     g_pImmediateContext = nullptr;
ID3D11DeviceContext1*    g_pImmediateContext1 = nullptr;
IDXGISwapChain*          g_pSwapChain = nullptr;
IDXGISwapChain1*         g_pSwapChain1 = nullptr;
ID3D11RenderTargetView*  g_pRenderTargetView = nullptr;
ID3D11VertexShader*      g_pVertexShader = nullptr;
ID3D11PixelShader*       g_pPixelShader = nullptr;
ID3D11InputLayout*       g_pVertexLayout = nullptr;
ID3D11Buffer*            g_pVertexBuffer = nullptr;
ID3D11Buffer*            g_Uniforms = nullptr;
ID3D11Buffer*            g_IntersectionPoints = nullptr;
ID3D11Texture2D*         g_pDepthStencil = nullptr;
ID3D11DepthStencilView*  g_pDepthStencilView = nullptr;
ID3D11BlendState*        g_pBlendStateBlend = NULL;
ID3D11BlendState*        g_pBlendStateMask = NULL;
ID3D11DepthStencilState* g_pDepthStencilState = NULL;
ID3D11DepthStencilState* g_pDepthStencilStateFill = NULL;
ID3D11DepthStencilState* g_pDepthStencilStateGreaterOrEqualIncr = NULL;
ID3D11DepthStencilState* g_pDepthStencilStateGreaterOrEqualDecr = NULL;
ID3D11DepthStencilState* g_pDepthStencilStateGreaterOrEqualRead = NULL;

//--------------------------------------------------------------------------------------
// Forward declarations
//--------------------------------------------------------------------------------------
HRESULT InitWindow( HINSTANCE hInstance, int nCmdShow );
HRESULT InitDevice();
void CleanupDevice();
LRESULT CALLBACK    WndProc( HWND, UINT, WPARAM, LPARAM );
void Render();


//--------------------------------------------------------------------------------------
// Entry point to the program. Initializes everything and goes into a message processing 
// loop. Idle time is used to render the scene.
//--------------------------------------------------------------------------------------
int WINAPI wWinMain( _In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow )
{
	UNREFERENCED_PARAMETER( hPrevInstance );
	UNREFERENCED_PARAMETER( lpCmdLine );

	if( FAILED( InitWindow( hInstance, nCmdShow ) ) )
		return 0;

	if( FAILED( InitDevice() ) ) {
		CleanupDevice();
		return 0;
	}

	// Main message loop
	MSG msg = {0};
	while( WM_QUIT != msg.message ) {
		if( PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) ) {
			TranslateMessage( &msg );
			DispatchMessage( &msg );
		} else {
			Render();
		}
	}

	CleanupDevice();

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

HRESULT CompileShaderFromSource(std::string shaderSource, LPCSTR szEntryPoint, LPCSTR szShaderModel, ID3DBlob** ppBlobOut)
{
	HRESULT hr = S_OK;

	DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
	#ifdef _DEBUG
		// Set the D3DCOMPILE_DEBUG flag to embed debug information in the shaders.
		// Setting this flag improves the shader debugging experience, but still allows 
		// the shaders to be optimized and to run exactly the way they will run in 
		// the release configuration of this program.
		dwShaderFlags |= D3DCOMPILE_DEBUG;

		// Disable optimizations to further improve shader debugging
		dwShaderFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
	#endif

	ID3DBlob* pErrorBlob = nullptr;
	hr = D3DCompile(shaderSource.c_str(), shaderSource.length(), nullptr, nullptr, nullptr, szEntryPoint, szShaderModel,
		dwShaderFlags, 0, ppBlobOut, &pErrorBlob);
	if (FAILED(hr)) {
		if (pErrorBlob) {
			OutputDebugStringA(reinterpret_cast<const char*>(pErrorBlob->GetBufferPointer()));
			pErrorBlob->Release();
		}
		return hr;
	}
	if (pErrorBlob) pErrorBlob->Release();

	return S_OK;
}

LensShapes::Rectangle CreateUnitRectangle() {

	float l = -1.f;
	float r =  1.f;
	float b = -1.f / ratio;
	float t =  1.f / ratio;

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
		to_add1.Pos = XMFLOAT3(x1, y1, 0.f);
		to_add2.Pos = XMFLOAT3(x2, y2, 0.f);
		to_add3.Pos = XMFLOAT3(0.f, 0.f, 0.f);

		triangle_vertices.push_back(to_add1);
		triangle_vertices.push_back(to_add2);
		triangle_vertices.push_back(to_add3);

		line_vertices.push_back(to_add1);
	}
	SimpleVertex to_add;
	to_add.Pos = XMFLOAT3(0.f, 1.f / ratio, 0.f);
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
	circle.x = 0;
	circle.y = 0;
	circle.r = 1.f;

	g_pd3dDevice->CreateBuffer(&bd1, &InitData1, &circle.triangles);
	g_pd3dDevice->CreateBuffer(&bd2, &InitData2, &circle.lines);

	return circle;
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

		if( SUCCEEDED( hr ) )
			break;
	}
	if( FAILED( hr ) )
		return hr;

	// Obtain DXGI factory from device (since we used nullptr for pAdapter above)
	IDXGIFactory1* dxgiFactory = nullptr;{
		IDXGIDevice* dxgiDevice = nullptr;
		hr = g_pd3dDevice->QueryInterface( __uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice) );
		if (SUCCEEDED(hr)) {
			IDXGIAdapter* adapter = nullptr;
			hr = dxgiDevice->GetAdapter(&adapter);
			if (SUCCEEDED(hr)) {
				hr = adapter->GetParent( __uuidof(IDXGIFactory1), reinterpret_cast<void**>(&dxgiFactory) );
				adapter->Release();
			}
			dxgiDevice->Release();
		}
	}

	if (FAILED(hr))
		return hr;

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

	// Note this tutorial doesn't handle full-screen swapchains so we block the ALT+ENTER shortcut
	dxgiFactory->MakeWindowAssociation( g_hWnd, DXGI_MWA_NO_ALT_ENTER );

	dxgiFactory->Release();

	if (FAILED(hr))
		return hr;

	// Create depth stencil texture
	D3D11_TEXTURE2D_DESC descDepth;
	ZeroMemory(&descDepth, sizeof(descDepth));
	descDepth.Width = width;
	descDepth.Height = height;
	descDepth.MipLevels = 1;
	descDepth.ArraySize = 1;
	descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	descDepth.SampleDesc.Count = 1;
	descDepth.SampleDesc.Quality = 0;
	descDepth.Usage = D3D11_USAGE_DEFAULT;
	descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	descDepth.CPUAccessFlags = 0;
	descDepth.MiscFlags = 0;
	hr = g_pd3dDevice->CreateTexture2D(&descDepth, nullptr, &g_pDepthStencil);
	
	if (FAILED(hr))
		return hr;

	// Create the depth stencil view
	D3D11_DEPTH_STENCIL_VIEW_DESC descDSV;
	ZeroMemory(&descDSV, sizeof(descDSV));
	descDSV.Format = descDepth.Format;
	descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	descDSV.Texture2D.MipSlice = 0;
	hr = g_pd3dDevice->CreateDepthStencilView(g_pDepthStencil, &descDSV, &g_pDepthStencilView);
	if (FAILED(hr))
		return hr;

	// Create a render target view
	ID3D11Texture2D* pBackBuffer = nullptr;
	hr = g_pSwapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), reinterpret_cast<void**>( &pBackBuffer ) );
	if( FAILED( hr ) )
		return hr;

	hr = g_pd3dDevice->CreateRenderTargetView( pBackBuffer, nullptr, &g_pRenderTargetView );
	pBackBuffer->Release();
	if( FAILED( hr ) )
		return hr;

	g_pImmediateContext->OMSetRenderTargets( 1, &g_pRenderTargetView, g_pDepthStencilView);

	// Setup the viewport
	D3D11_VIEWPORT vp;
	vp.Width = (FLOAT)width;
	vp.Height = (FLOAT)height;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	g_pImmediateContext->RSSetViewports( 1, &vp );

	// Compile the vertex shader
	ID3DBlob* pVSBlob = nullptr;
	
	hr = CompileShaderFromSource(vertex_shader_source, "VS", "vs_4_0", &pVSBlob);
	if( FAILED( hr ) ) {
		MessageBox( nullptr, L"The source cannot be compiled.", L"Error", MB_OK );
		return hr;
	}

	// Create the vertex shader
	hr = g_pd3dDevice->CreateVertexShader( pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &g_pVertexShader );
	if( FAILED( hr ) ) {
		pVSBlob->Release();
		return hr;
	}

	// Define the input layout
	D3D11_INPUT_ELEMENT_DESC layout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	UINT numElements = ARRAYSIZE( layout );

	// Create the input layout
	hr = g_pd3dDevice->CreateInputLayout( layout, numElements, pVSBlob->GetBufferPointer(),
											pVSBlob->GetBufferSize(), &g_pVertexLayout );
	pVSBlob->Release();
	if( FAILED( hr ) )
		return hr;

	// Set the input layout
	g_pImmediateContext->IASetInputLayout( g_pVertexLayout );

	// Compile the pixel shader
	ID3DBlob* pPSBlob = nullptr;
	hr = CompileShaderFromSource(pixel_shader_source, "PS", "ps_4_0", &pPSBlob);
	if( FAILED( hr ) ) {
		MessageBox(nullptr, L"The source cannot be compiled.", L"Error", MB_OK);
		return hr;
	}

	// Create the pixel shader
	hr = g_pd3dDevice->CreatePixelShader( pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &g_pPixelShader );
	pPSBlob->Release();
	if( FAILED( hr ) )
		return hr;

	D3D11_BLEND_DESC BlendState;
	D3D11_BLEND_DESC MaskedBlendState;
	ZeroMemory(&BlendState, sizeof(D3D11_BLEND_DESC));
	ZeroMemory(&MaskedBlendState, sizeof(D3D11_BLEND_DESC));

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

	g_pd3dDevice->CreateBlendState(&BlendState, &g_pBlendStateBlend);
	g_pd3dDevice->CreateBlendState(&MaskedBlendState, &g_pBlendStateMask);

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

	g_pd3dDevice->CreateDepthStencilState(&DepthStencilState, &g_pDepthStencilState);
	g_pd3dDevice->CreateDepthStencilState(&DepthStencilStateFill, &g_pDepthStencilStateFill);
	g_pd3dDevice->CreateDepthStencilState(&DepthStencilStateGreaterOrEqualIncr, &g_pDepthStencilStateGreaterOrEqualIncr);
	g_pd3dDevice->CreateDepthStencilState(&DepthStencilStateGreaterOrEqualDecr, &g_pDepthStencilStateGreaterOrEqualDecr);
	g_pd3dDevice->CreateDepthStencilState(&DepthStencilStateGreaterOrEqualRead, &g_pDepthStencilStateGreaterOrEqualRead);

	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	bd.ByteWidth = sizeof(Uniforms);
	hr = g_pd3dDevice->CreateBuffer(&bd, nullptr, &g_Uniforms);

	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd.CPUAccessFlags = 0;
	bd.ByteWidth = sizeof(SimpleVertex) * num_of_intersections;
	hr = g_pd3dDevice->CreateBuffer(&bd, nullptr, &g_IntersectionPoints);

	// Parse the lens components into the LensInterface the ray_trace routine expects
	Nikon_28_75mm_lens_interface.resize(Nikon_28_75mm_lens_components.size());
	for (int i = (int)Nikon_28_75mm_lens_components.size() - 1; i >= 0; --i) {
		PatentFormat& entry = Nikon_28_75mm_lens_components[i];
		total_lens_distance += entry.d;

		float left_ior  = i == 0 ? 1.f : Nikon_28_75mm_lens_components[i - 1].n;
		float right_ior = entry.n;

		if (right_ior != 1.f) {
			min_ior = min(min_ior, right_ior);
			max_ior = max(max_ior, right_ior);
		}

		vec3 center = { 0.f, 0.f, total_lens_distance - entry.r};
		vec3 n = { left_ior, 1.f, right_ior };

		LensInterface component = { total_lens_distance, center, entry.r, n, 1.f, 1.f, entry.flat, entry.w, entry.h };
		Nikon_28_75mm_lens_interface[i] = component;
	}

	unit_circle = CreateUnitCircle();
	unit_square = CreateUnitRectangle();

	return S_OK;
}


//--------------------------------------------------------------------------------------
// Clean up the objects we've created
//--------------------------------------------------------------------------------------
void CleanupDevice()
{
	if( g_pImmediateContext ) g_pImmediateContext->ClearState();

	if( g_pVertexBuffer ) g_pVertexBuffer->Release();
	if( g_pVertexLayout ) g_pVertexLayout->Release();
	if( g_pVertexShader ) g_pVertexShader->Release();
	if( g_pPixelShader ) g_pPixelShader->Release();
	if( g_pRenderTargetView ) g_pRenderTargetView->Release();
	if( g_pSwapChain1 ) g_pSwapChain1->Release();
	if( g_pSwapChain ) g_pSwapChain->Release();
	if( g_pImmediateContext1 ) g_pImmediateContext1->Release();
	if( g_pImmediateContext ) g_pImmediateContext->Release();
	if( g_pd3dDevice1 ) g_pd3dDevice1->Release();
	if (g_pd3dDevice) g_pd3dDevice->Release();
	if (g_Uniforms) g_Uniforms->Release();

	if (g_IntersectionPoints) g_IntersectionPoints->Release();
	if (g_pDepthStencil) g_pDepthStencil->Release();
	if (g_pDepthStencilView) g_pDepthStencilView->Release();
	if (g_pBlendStateBlend) g_pBlendStateBlend->Release();
	if (g_pBlendStateMask) g_pBlendStateMask->Release();
	if (g_pDepthStencilState) g_pDepthStencilState->Release();
	if (g_pDepthStencilStateFill) g_pDepthStencilStateFill->Release();
	if (g_pDepthStencilStateGreaterOrEqualIncr) g_pDepthStencilStateGreaterOrEqualIncr->Release();
	if (g_pDepthStencilStateGreaterOrEqualDecr) g_pDepthStencilStateGreaterOrEqualDecr->Release();
	if (g_pDepthStencilStateGreaterOrEqualRead) g_pDepthStencilStateGreaterOrEqualRead->Release();
}

//--------------------------------------------------------------------------------------
// Called every time the application receives a message
//--------------------------------------------------------------------------------------
LRESULT CALLBACK WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam ) {
	PAINTSTRUCT ps;
	HDC hdc;

	switch( message )
	{
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
	Uniforms cb = { color, placement };
	context->UpdateSubresource(g_Uniforms, 0, nullptr, &cb, 0, 0);
	if (filled) {
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		context->IASetVertexBuffers(0, 1, &rectangle.vertices, &stride, &offset);
		context->Draw(6, 0);
	}
	else {
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
		context->IASetVertexBuffers(0, 1, &rectangle.lines, &stride, &offset);
		context->Draw(5, 0);
	}
}

void DrawCircle(ID3D11DeviceContext* context, LensShapes::Circle& circle, XMFLOAT4& color, XMFLOAT4& placement, bool filled) {
	Uniforms cb = { color, placement };
	context->UpdateSubresource(g_Uniforms, 0, nullptr, &cb, 0, 0);
	if (filled) {
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		context->IASetVertexBuffers(0, 1, &circle.triangles, &stride, &offset);
		context->Draw(num_vertices_per_cirlces, 0);
	}
	else {
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
		context->IASetVertexBuffers(0, 1, &circle.lines, &stride, &offset);
		context->Draw(num_points_per_cirlces, 0);
	}
}

void DrawFlat(LensInterface& right) {
	float mx = -(right.pos * global_scale - 1.f);
	float mw = global_scale * 0.3f;
	
	XMFLOAT4 mask_placement1 = { mx, 1.f, mw * 1.00f, global_scale * right.w };
	XMFLOAT4 mask_placement2 = { mx, 1.f, mw * 1.01f, global_scale * right.h };
	XMFLOAT4 mask_placement3 = { mx + 0.0001f, 1.f, mw * 0.9f, global_scale * right.w * 0.9f };

	g_pImmediateContext->OMSetBlendState(g_pBlendStateMask, blendFactor, sampleMask);
	g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateFill, 1);
	DrawRectangle(g_pImmediateContext, unit_square, flat_fill_color, mask_placement1, true);

	g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateFill, 0);
	DrawRectangle(g_pImmediateContext, unit_square, flat_fill_color, mask_placement2, true);

	g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateGreaterOrEqualRead, 1);
	g_pImmediateContext->OMSetBlendState(g_pBlendStateBlend, blendFactor, sampleMask);
	DrawRectangle(g_pImmediateContext, unit_square, flat_fill_color, mask_placement3, true);
	DrawRectangle(g_pImmediateContext, unit_square, stroke_color, mask_placement3, false);
}

void DrawLens(LensInterface& left, LensInterface& right) {
	
	float normalized_ior = (right.n.x - min_ior) / (max_ior - min_ior);
	XMFLOAT4 fill_color = normalized_ior < 0.5f ? fill_color1 : fill_color2;
	//XMFLOAT4 fill_color = lerp(fill_color1, fill_color2, normalized_ior);

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
		float mh = global_scale * right.h;
		XMFLOAT4 mask_placement = { mx, 1.f, mw, mh * 1.001f };
		XMFLOAT4 mask_placement2 = { mx, 1.f, mw * 0.995f, mh * 0.997f };

		float rx = -(_right.pos * global_scale - 1.f);
		float rr = _right.radius * global_scale;
		XMFLOAT4 right_placement = { rx, 1.f, rr, rr };

		float lx = -(_left.pos * global_scale - 1.f);
		float lr = _left.radius * global_scale;
		XMFLOAT4 left_placement = { lx, 1.f, lr, lr };

		g_pImmediateContext->ClearDepthStencilView(g_pDepthStencilView, D3D11_CLEAR_STENCIL, 1.0f, 0);
		g_pImmediateContext->OMSetBlendState(g_pBlendStateMask, blendFactor, sampleMask);
		g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateFill, 1);
		DrawRectangle(g_pImmediateContext, unit_square, stroke_color, mask_placement, true);

		g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateGreaterOrEqualIncr, 1);
		DrawCircle(g_pImmediateContext, unit_circle, fill_color, right_placement, true);

		g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateGreaterOrEqualDecr, 2);
		DrawCircle(g_pImmediateContext, unit_circle, fill_color, left_placement, true);

		g_pImmediateContext->OMSetBlendState(g_pBlendStateBlend, blendFactor, sampleMask);
		g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateGreaterOrEqualRead, 2);
		DrawRectangle(g_pImmediateContext, unit_square, fill_color, mask_placement, true);
		DrawRectangle(g_pImmediateContext, unit_square, stroke_color, mask_placement2, false);

		g_pImmediateContext->OMSetBlendState(g_pBlendStateMask, blendFactor, sampleMask);
		g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateFill, 1);
		DrawRectangle(g_pImmediateContext, unit_square, stroke_color, mask_placement, true);

		g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateGreaterOrEqualRead, 1);
		g_pImmediateContext->OMSetBlendState(g_pBlendStateBlend, blendFactor, sampleMask);
		DrawCircle(g_pImmediateContext, unit_circle, stroke_color, left_placement, false);
		DrawCircle(g_pImmediateContext, unit_circle, stroke_color, right_placement, false);
	}
	//     / \
	//    |   |
	//    |   |
	//    |   |
	//     \ /
	else if (left.radius > 0.f && right.radius < 0.f)
	{
		float eps = 0.001f;
		float delta = abs(right.pos - left.pos);
		float mx = -(right.pos * global_scale - 1.f) + eps;
		float mw = -delta * global_scale * right.w - eps;
		float mh = global_scale * right.h;
		XMFLOAT4 mask_placement = { mx, 1.f, mw, mh };
		XMFLOAT4 mask_placement2 = { mx, 1.f, mw * 0.995f, mh * 0.997f };

		float lx = -(left.pos * global_scale - 1.f);
		float lr = left.radius * global_scale;
		XMFLOAT4 left_placement = { lx, 1.f, lr, lr };

		float rx = -(right.pos * global_scale - 1.f);
		float rr = right.radius * global_scale;
		XMFLOAT4 right_placement = { rx, 1.f, rr, rr };

		g_pImmediateContext->ClearDepthStencilView(g_pDepthStencilView, D3D11_CLEAR_STENCIL, 1.0f, 0);
		g_pImmediateContext->OMSetBlendState(g_pBlendStateMask, blendFactor, sampleMask);
		g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateFill, 1);
		DrawRectangle(g_pImmediateContext, unit_square, stroke_color, mask_placement, true);
		g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateGreaterOrEqualIncr, 1);
		DrawCircle(g_pImmediateContext, unit_circle, stroke_color, left_placement, true);

		g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateGreaterOrEqualIncr, 2);
		g_pImmediateContext->OMSetBlendState(g_pBlendStateBlend, blendFactor, sampleMask);
		DrawCircle(g_pImmediateContext, unit_circle, fill_color, right_placement, true);

		g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateGreaterOrEqualRead, 3);
		DrawRectangle(g_pImmediateContext, unit_square, stroke_color, mask_placement2, false);

		g_pImmediateContext->ClearDepthStencilView(g_pDepthStencilView, D3D11_CLEAR_STENCIL, 1.0f, 0);
		g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateFill, 1);
		g_pImmediateContext->OMSetBlendState(g_pBlendStateMask, blendFactor, sampleMask);
		DrawRectangle(g_pImmediateContext, unit_square, stroke_color, mask_placement, true);

		g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateGreaterOrEqualRead, 1);
		g_pImmediateContext->OMSetBlendState(g_pBlendStateBlend, blendFactor, sampleMask);
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
		float mh = global_scale * right.h;
		XMFLOAT4 mask_placement = { mx, 1.f, mw, mh };
		XMFLOAT4 mask_placement2 = { mx, 1.f, mw * 0.995f, mh * 0.995f };

		float lx = -(left.pos * global_scale - 1.f);
		float lr = left.radius * global_scale;
		XMFLOAT4 left_placement = { lx, 1.f, lr, lr };

		float rx = -(right.pos * global_scale - 1.f);
		float rr = right.radius * global_scale;
		XMFLOAT4 right_placement = { rx, 1.f, rr, rr };

		g_pImmediateContext->ClearDepthStencilView(g_pDepthStencilView, D3D11_CLEAR_STENCIL, 1.0f, 0);
		g_pImmediateContext->OMSetBlendState(g_pBlendStateMask, blendFactor, sampleMask);
		g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateFill, 1);
		DrawRectangle(g_pImmediateContext, unit_square, fill_color, mask_placement, true);

		g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateFill, 0);
		DrawCircle(g_pImmediateContext, unit_circle, fill_color, left_placement, true);
		DrawCircle(g_pImmediateContext, unit_circle, fill_color, right_placement, true);

		g_pImmediateContext->OMSetBlendState(g_pBlendStateBlend, blendFactor, sampleMask);
		g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateGreaterOrEqualRead, 1);
		DrawRectangle(g_pImmediateContext, unit_square, fill_color, mask_placement, true);
		DrawRectangle(g_pImmediateContext, unit_square, stroke_color, mask_placement2, false);

		g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateFill, 1);
		g_pImmediateContext->OMSetBlendState(g_pBlendStateMask, blendFactor, sampleMask);
		DrawRectangle(g_pImmediateContext, unit_square, fill_color, mask_placement, true);

		g_pImmediateContext->OMSetBlendState(g_pBlendStateBlend, blendFactor, sampleMask);
		g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilStateGreaterOrEqualRead, 1);
		DrawCircle(g_pImmediateContext, unit_circle, stroke_color, left_placement, false);
		DrawCircle(g_pImmediateContext, unit_circle, stroke_color, right_placement, false);
	}
}

void DrawLensInterface(std::vector<LensInterface>& lens_interface) {

	g_pImmediateContext->ClearDepthStencilView(g_pDepthStencilView, D3D11_CLEAR_STENCIL, 1.0f, 0);
	g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilState, 0);
	g_pImmediateContext->OMSetBlendState(g_pBlendStateBlend, blendFactor, sampleMask);

	int i = 0;
	while (i < (int)lens_interface.size()) {
		if (lens_interface[i].flat) {
			DrawFlat(lens_interface[i]);
			i += 1;
		} else if (lens_interface[i].n.x == 1.f) {
			DrawLens(lens_interface[i], lens_interface[i + 1]);
			i += 2;
		} else {
			DrawLens(lens_interface[i - 1], lens_interface[i]);
			i += 1;
		}
	}
}

void DrawIntersections(ID3D11DeviceContext* context, std::vector<vec3>& intersectios, XMFLOAT4& color) {
	Uniforms cb;
	cb.color = color;
	cb.placement = XMFLOAT4(0.f, 0.f, 0.f, 0.f);

	std::vector<XMFLOAT3> points(num_of_intersections);
	for (int i = 0; i < (int)intersectios.size(); ++i) {
		points[i] = (point_to_d3d(intersectios[i]));
	}

	void* ptr = &points.front();
	context->UpdateSubresource(g_IntersectionPoints, 0, nullptr, ptr, 0, 0);
	context->UpdateSubresource(g_Uniforms, 0, nullptr, &cb, 0, 0);
	
	g_pImmediateContext->OMSetDepthStencilState(g_pDepthStencilState, 0);
	g_pImmediateContext->OMSetBlendState(g_pBlendStateBlend, blendFactor, sampleMask);

	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
	context->IASetVertexBuffers(0, 1, &g_IntersectionPoints, &stride, &offset);
	context->Draw((int)intersectios.size(), 0);

}

//--------------------------------------------------------------------------------------
// Render a frame
//--------------------------------------------------------------------------------------
void Render()
{
	Uniforms cb;

	// Clear the back buffer 
	g_pImmediateContext->ClearRenderTargetView( g_pRenderTargetView, Colors::WhiteSmoke);
	g_pImmediateContext->ClearDepthStencilView(g_pDepthStencilView, D3D11_CLEAR_STENCIL, 1.0f, 0);

	g_pImmediateContext->VSSetShader( g_pVertexShader, nullptr, 0 );
	g_pImmediateContext->PSSetShader( g_pPixelShader, nullptr, 0 );
	g_pImmediateContext->VSSetConstantBuffers(0, 1, &g_Uniforms);
	g_pImmediateContext->PSSetConstantBuffers(0, 1, &g_Uniforms);

	static float time = 0.f;
	time += 0.01;
	
	float anim_ray_direction = sin(time) * 5.f;
	float anim_g4_lens = sin(time * 0.5f) * 0.01f;
	float rays_spread = 20.f;

	std::vector<vec3> intersections;
	for (int i = -num_of_rays; i <= num_of_rays; ++i) {
		float pos = (float)i / (float)num_of_rays * rays_spread;
		
		vec3 a = vec3(0.0f, pos, total_lens_distance);
		vec3 b = vec3(0.0f, pos + anim_ray_direction, total_lens_distance + 10.f);
		vec3 c = normalize(a - b);
		
		Ray ray = { a - c * 100.f, c };
		trace(0, ray, 1.f, Nikon_28_75mm_lens_interface, intersections);
		
		DrawIntersections(g_pImmediateContext, intersections, intersection_color);
	}

	for (int i = 6; i < 14; ++i) {
		Nikon_28_75mm_lens_interface[i].center.z += anim_g4_lens;
		Nikon_28_75mm_lens_interface[i].pos += anim_g4_lens;
	}

	DrawLensInterface(Nikon_28_75mm_lens_interface);

	g_pSwapChain->Present( 0, 0 );	
}
