#include "Application.h"

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    HDC hdc;

    switch (message)
    {
        case WM_PAINT:
            hdc = BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}

Application::Application()
{
	_hInst = nullptr;
	_hWnd = nullptr;
	_driverType = D3D_DRIVER_TYPE_NULL;
	_featureLevel = D3D_FEATURE_LEVEL_11_0;
	_pd3dDevice = nullptr;
	_pImmediateContext = nullptr;
	_pSwapChain = nullptr;
	_pRenderTargetView = nullptr;
	_pVertexShader = nullptr;
	_pPixelShader = nullptr;
	_pVertexLayout = nullptr;

	_pConstantBuffer = nullptr;
}

Application::~Application()
{
	Cleanup();
}

HRESULT Application::Initialise(HINSTANCE hInstance, int nCmdShow)
{
    if (FAILED(InitWindow(hInstance, nCmdShow)))
	{
        return E_FAIL;
	}

    RECT rc;
    GetClientRect(_hWnd, &rc);
    _WindowWidth = rc.right - rc.left;
    _WindowHeight = rc.bottom - rc.top;

    if (FAILED(InitDevice()))
    {
        Cleanup();

        return E_FAIL;
    }

    //Load meshes, textures and materials
    LoadMeshes();
    LoadMaterials();
    LoadTextures();

    //initialise objects
    InitObjects();

    // Define the specifications for our sampler :
    D3D11_SAMPLER_DESC sampDesc;
    ZeroMemory(&sampDesc, sizeof(sampDesc));
    //Anisotropic filtering reduces the blur that is very visible when faces of objects relate to be near-parallel with line of sight
    sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    // Create the sample state
    _pd3dDevice->CreateSamplerState(&sampDesc, &_pSamplerLinear);

    // Tell DirectX which sampler to use in the texture shader, assigning it to sampler register one:
    _pImmediateContext->PSSetSamplers(0, 1, &_pSamplerLinear);



    // Initialize the view matrix
    XMVECTOR Eye = XMLoadFloat4(&_camera->GetEye());
    XMVECTOR At = XMLoadFloat4(&_camera->GetAt());
    XMVECTOR Up = XMLoadFloat4(&_camera->GetUp());

    // Initialize the world matrix
    XMStoreFloat4x4(&_world, XMMatrixIdentity());

	XMStoreFloat4x4(&_view, XMMatrixLookAtLH(Eye, At, Up));

    // Initialize the projection matrix
    /*XMMatrixPerspective(  Top-down field of view angle in radians,
                            Aspect ratio of view-space X:Y,
                            Distance to the near clipping plane > 0,
                            Distance to the far clipping plane > 0),
                                                                        Returns: the perspective projection matrix  */
	XMStoreFloat4x4(&_projection, XMMatrixPerspectiveFovLH(XM_PIDIV2, _WindowWidth / (float) _WindowHeight, 0.01f, 100.0f));

	return S_OK;
}

HRESULT Application::InitShadersAndInputLayout()    //Loads in shaders from the HLSL (high-level shader language) file and returns an error if it fails
{
	HRESULT hr;

    // Compile the vertex shader
    ID3DBlob* pVSBlob = nullptr;
    hr = CompileShaderFromFile(L"DX11 Framework.hlsl", "VS", "vs_4_0", &pVSBlob);

    if (FAILED(hr))
    {
        MessageBox(nullptr,
                   L"The HLSL file cannot be compiled. Check VS Outpot for Error Log.", L"Error", MB_OK);
        return hr;
    }

	// Create the vertex shader
	hr = _pd3dDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &_pVertexShader);

	if (FAILED(hr))
	{	
		pVSBlob->Release();
        return hr;
	}

	// Compile the pixel shader
	ID3DBlob* pPSBlob = nullptr;
    hr = CompileShaderFromFile(L"DX11 Framework.hlsl", "PS", "ps_4_0", &pPSBlob);

    if (FAILED(hr))
    {
        MessageBox(nullptr,
            L"The HLSL file cannot be compiled. Check VS Outpot for Error Log.", L"Error", MB_OK);
        return hr;
    }

	// Create the pixel shader
	hr = _pd3dDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &_pPixelShader);
	pPSBlob->Release();

    if (FAILED(hr))
        return hr;

    // Define the input layout, which is used by the vertex 
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
	};

	UINT numElements = ARRAYSIZE(layout);

    // Create the input layout
	hr = _pd3dDevice->CreateInputLayout(layout, numElements, pVSBlob->GetBufferPointer(),
                                        pVSBlob->GetBufferSize(), &_pVertexLayout);
	pVSBlob->Release();

	if (FAILED(hr))
        return hr;

    // Set the input layout
    _pImmediateContext->IASetInputLayout(_pVertexLayout);

	return hr;
}

HRESULT Application::InitWindow(HINSTANCE hInstance, int nCmdShow)
{
    // Register class
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, (LPCTSTR)IDI_TUTORIAL1);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW );
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = L"TutorialWindowClass";
    wcex.hIconSm = LoadIcon(wcex.hInstance, (LPCTSTR)IDI_TUTORIAL1);
    if (!RegisterClassEx(&wcex))
        return E_FAIL;

    // Create window
    _hInst = hInstance;
    RECT rc = {0, 0, 640, 480};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    _hWnd = CreateWindow(L"TutorialWindowClass", L"DX11 Framework", WS_OVERLAPPEDWINDOW,
                         CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInstance,
                         nullptr);
    if (!_hWnd)
		return E_FAIL;

    ShowWindow(_hWnd, nCmdShow);

    return S_OK;
}

HRESULT Application::CompileShaderFromFile(WCHAR* szFileName, LPCSTR szEntryPoint, LPCSTR szShaderModel, ID3DBlob** ppBlobOut)
{
    HRESULT hr = S_OK;

    DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(DEBUG) || defined(_DEBUG)
    // Set the D3DCOMPILE_DEBUG flag to embed debug information in the shaders.
    // Setting this flag improves the shader debugging experience, but still allows 
    // the shaders to be optimized and to run exactly the way they will run in 
    // the release configuration of this program.
    dwShaderFlags |= D3DCOMPILE_DEBUG;
#endif

    ID3DBlob* pErrorBlob;
    hr = D3DCompileFromFile(szFileName, nullptr, nullptr, szEntryPoint, szShaderModel, 
        dwShaderFlags, 0, ppBlobOut, &pErrorBlob);

    if (FAILED(hr))
    {
        if (pErrorBlob != nullptr)
            OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());

        if (pErrorBlob) pErrorBlob->Release();

        return hr;
    }

    if (pErrorBlob) pErrorBlob->Release();

    return S_OK;
}

HRESULT Application::InitDevice()
{
    HRESULT hr = S_OK;

    UINT createDeviceFlags = 0;

#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_DRIVER_TYPE driverTypes[] =
    {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
        D3D_DRIVER_TYPE_REFERENCE,
    };

    UINT numDriverTypes = ARRAYSIZE(driverTypes);

    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

	UINT numFeatureLevels = ARRAYSIZE(featureLevels);

    //Define the render target output buffer
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 1;
    sd.BufferDesc.Width = _WindowWidth;
    sd.BufferDesc.Height = _WindowHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = _hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;

    // define  the depth/stencil buffer
    D3D11_TEXTURE2D_DESC depthStencilDesc;
    // Texture width
    depthStencilDesc.Width = _WindowWidth;
    // Texture height
    depthStencilDesc.Height = _WindowHeight;
    // Maximum number of mipmap levels in the texture
    depthStencilDesc.MipLevels = 1;
    // Number of textures in the array
    depthStencilDesc.ArraySize = 1;
    // Texture format
    depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    //      structure that specifies multisampling parameters for the texture
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.SampleDesc.Quality = 0;
    // identifies how the texture is to be read from and written to
    depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
    // flags for binding to pipeline stages
    depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    // flags to specify the types of cpu access allowed. 0 if cpu access is not required
    depthStencilDesc.CPUAccessFlags = 0;
    // misc flags.
    depthStencilDesc.MiscFlags = 0;

    for (UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++)
    {
        _driverType = driverTypes[driverTypeIndex];
        hr = D3D11CreateDeviceAndSwapChain(nullptr, _driverType, nullptr, createDeviceFlags, featureLevels, numFeatureLevels,
                                           D3D11_SDK_VERSION, &sd, &_pSwapChain, &_pd3dDevice, &_featureLevel, &_pImmediateContext);
        if (SUCCEEDED(hr))
            break;
    }

    if (FAILED(hr))
        return hr;

    // Create a render target view
    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = _pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);

    if (FAILED(hr))
        return hr;

    hr = _pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &_pRenderTargetView);
    pBackBuffer->Release();

    if (FAILED(hr))
        return hr;

    // create a rasterizer state to do wireframe rendering
    D3D11_RASTERIZER_DESC wfdesc;
    ZeroMemory(&wfdesc, sizeof(D3D11_RASTERIZER_DESC)); //Clears the size of memory need
    wfdesc.FillMode = D3D11_FILL_WIREFRAME; //Determines fill mode to use when rendering
    wfdesc.CullMode = D3D11_CULL_NONE;  //indicates that triangles facing the specified direction are not drawn (used in back face culling)
    hr = _pd3dDevice->CreateRasterizerState(&wfdesc, &_wireFrame);  //binds to the RS (render state) part of the pipeline, so created with CreateRasterizerState() method
    //First param (&wfdesc) is ther description of the render state, second is a pointer to a ID3D11RasterizerState object which holds the new render state

    // create a rasterizer state to do solid rendering
    D3D11_RASTERIZER_DESC sfdesc;
    ZeroMemory(&sfdesc, sizeof(D3D11_RASTERIZER_DESC)); //Clears the size of memory need
    sfdesc.FillMode = D3D11_FILL_SOLID; //Determines fill mode to use when rendering
    sfdesc.CullMode = D3D11_CULL_NONE;  //indicates that triangles facing the specified direction are not drawn (used in back face culling)
    hr = _pd3dDevice->CreateRasterizerState(&sfdesc, &_solidFill);  //binds to the RS (render state) part of the pipeline, so created with CreateRasterizerState() method
    //First param (&sfdesc) is ther description of the render state, second is a pointer to a ID3D11RasterizerState object which holds the new render state

    //Store the solid rasterizer state in _currentRasterizerState
    _currentRasterizerState = _solidFill;

    // Setup the viewport
    D3D11_VIEWPORT vp;
    vp.Width = (float)_WindowWidth;
    vp.Height = (float)_WindowHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    _pImmediateContext->RSSetViewports(1, &vp);

	hr = InitShadersAndInputLayout();
    if (FAILED(hr)) 
    { 
        return hr;
    }

    // Set primitive topology
    _pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    
    //Create the depth/stencil buffer
    _pd3dDevice->CreateTexture2D(&depthStencilDesc, nullptr, &_depthStencilBuffer);
    //Create the depth/stencil view that will be bound to the OM stage of the pipeline
    _pd3dDevice->CreateDepthStencilView(_depthStencilBuffer, nullptr, &_depthStencilView);

    /* Binds one or more render targets and the depth stencil buffer to the outputMerger stage.OMSetRenderTargets(   Number of render targets to bind,
    *                                                                                                               Pointer to an array of render targets to bind to the device,
    *                                                                                                               Pointer to a depth-stencil view to bind to the device   )*/
    _pImmediateContext->OMSetRenderTargets(1, &_pRenderTargetView, _depthStencilView);

	// Create the constant buffer
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(ConstantBuffer);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
    hr = _pd3dDevice->CreateBuffer(&bd, nullptr, &_pConstantBuffer);

    

    if (FAILED(hr))
        return hr;

    return S_OK;
}

#pragma region Loading

void Application::InitObjects()
{
    _actors = new std::map<std::string, Actor*>();

    _actors->insert({ "cube", new Actor(_pd3dDevice, _meshes->find("cube")->second, _materials->find("crate")->second, _textures->find("crateDiffuse")->second, _textures->find("crateSpecular")->second) });
    _actors->insert({ "cylinder", new Actor(_pd3dDevice, _meshes->find("cylinder")->second, _materials->find("crate")->second, _textures->find("crateDiffuse")->second, _textures->find("crateSpecular")->second) });;
    _actors->find("cylinder")->second->SetPosition(XMFLOAT3(3.0f, 0.0f, 3.0f));
    
    //Initialise Lights
    LoadLights();
    
    //Initialise the camera
    _camera = new Camera(XMFLOAT4(0.0f, 0.0f, -3.0f, 0.0f), XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f), XMFLOAT4(0.0f, 1.0f, 0.0f, 0.0f));
}

void Application::LoadTextures()
{
    _textures = new std::map<std::string, Texture*>();

    _textures->insert({ "crateDiffuse", LoadTexture(_pd3dDevice, "Textures/Crate_COLOR.dds") });
    _textures->insert({ "crateSpecular", LoadTexture(_pd3dDevice, "Textures/Crate_SPEC.dds") });
}

void Application::LoadMeshes()
{
    _meshes = new std::map<std::string, Mesh*>();

    _meshes->insert({ "cube", LoadMesh(_pd3dDevice, "Models/3dsMax/cube.obj") });
    _meshes->insert({ "cylinder", LoadMesh(_pd3dDevice, "Models/3dsMax/cylinder.obj") });
}

void Application::LoadMaterials()
{
    _materials = new std::map<std::string, Material*>();
   
    _materials->insert({ "crate", new Material(XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f), XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f), XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), 10.0f) });
}

void Application::LoadLights()
{
    _directionalLights = new std::map<std::string, DirectionalLight*>();
    _pointLights = new std::map<std::string, PointLight*>();
    _spotLights = new std::map<std::string, SpotLight*>();

    _directionalLights->insert({ "sun", new DirectionalLight(XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f), XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f), XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f), XMFLOAT3(0.0f, 0.5f, -0.5f)) });

    _pointLights->insert({ "bulb", new PointLight(XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT4(0.01f, 0.01f, 0.01f, 1.0f), XMFLOAT4(0.7f, 0.7f, 0.7, 25.0f), XMFLOAT3(3.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, 0.4f), 10.0f) });

    _spotLights->insert({ "torch", new SpotLight(XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f), XMFLOAT4(0.01f, 0.01f, 0.01f, 1.0f), XMFLOAT4(0.9f, 0.9f, 0.9, 20.0f), XMFLOAT3(0.0f, 0.0f, -3.0f), XMFLOAT3(0.0f, 0.0f, 0.4f), 10.0f, XMFLOAT3(-1.0f, -1.0f, 1.0f), 8.0f) });
}

#pragma endregion



void Application::Cleanup()
{
    if (_pd3dDevice) _pd3dDevice->Release();
    if (_pImmediateContext) _pImmediateContext->ClearState();
    if (_pImmediateContext) _pImmediateContext->Release();
    if (_pSwapChain) _pSwapChain->Release();
    if (_pRenderTargetView) _pRenderTargetView->Release();
    if (_pSamplerLinear) _pSamplerLinear->Release();
    if (_pVertexShader) _pVertexShader->Release();
    if (_pPixelShader) _pPixelShader->Release();
    if (_wireFrame) _wireFrame->Release();
    if (_solidFill) _solidFill->Release();
    if (_currentRasterizerState) _currentRasterizerState->Release();
    if (_pVertexLayout) _pVertexLayout->Release();
    if (_pVertexBuffer) _pVertexBuffer->Release();
    if (_pIndexBuffer) _pIndexBuffer->Release();
    if (_pConstantBuffer) _pConstantBuffer->Release();
    if (_depthStencilView) _depthStencilView->Release();
    if (_depthStencilBuffer) _depthStencilBuffer->Release();

    _textures->clear();
    delete _textures;
    _meshes->clear();
    delete _meshes;
    _materials->clear();
    delete _materials;
    _actors->clear();
    delete _actors;
    _directionalLights->clear();
    delete _directionalLights;
    _pointLights->clear();
    delete _pointLights;
    _spotLights->clear();
    delete _spotLights;
    delete _camera;
}

void Application::UpdateActors()
{
    // For each actor
    // Create a map iterator and point to beginning of map
    std::map<std::string, Actor*>::iterator it = _actors->begin();
    // Iterate over the map using Iterator till end.
    while (it != _actors->end())
    {
        // Access the actor from element pointed by it and call Update()
        it->second->Update();
        // Increment the Iterator to point to next entry
        it++;
    }
}


void Application::Update()
{
    // Update our time
    static float t = 0.0f;

    if (_driverType == D3D_DRIVER_TYPE_REFERENCE)
    {
        t += (float)XM_PI * 0.0125f;
    }
    else
    {
        static DWORD dwTimeStart = 0;
        DWORD dwTimeCur = GetTickCount();

        if (dwTimeStart == 0)
            dwTimeStart = dwTimeCur;

        t = (dwTimeCur - dwTimeStart) / 1000.0f;
    }

    //Sets rasterizer state
    if (WM_KEYDOWN)
    {
        if ((GetAsyncKeyState(VK_LBUTTON) & 0x01))
        {
            if (_currentRasterizerState == _solidFill)
            {
                _currentRasterizerState = _wireFrame;
            }
            else
            {
                _currentRasterizerState = _solidFill;
            }
            _pImmediateContext->RSSetState(_currentRasterizerState); //Set the render state in out immediate context before any objects we want to render in that state (filled)

        }
    }

    // Animate actors
    _actors->find("cube")->second->SetRotation(XMFLOAT3(t / 2, t, 0.0f));
    _actors->find("cylinder")->second->SetRotation(XMMatrixRotationRollPitchYaw(-t, -t/2, 0.0f));

    // Update actors
    UpdateActors();
}

void Application::StoreDirectionalLights(ConstantBuffer* cb)
{
    // For each directional Light
    // Initialise index to constant buffer
    int i = 0;
    // Create a map iterator and point to beginning of map
    std::map<std::string, DirectionalLight*>::iterator it = _directionalLights->begin();
    // Iterate over the map using Iterator till end.
    while (it != _directionalLights->end())
    {
        // Access the light from element pointed by it and store in the constant buffer
        cb->directionalLights[i] = *it->second;
        // Increment the Iterator to point to next entry
        it++;
        // Increment the index for the constant buffer
        i++;
    }
    // Store count into constant buffer
    cb->directionalLightsCount = i;
}

void Application::StorePointLights(ConstantBuffer* cb)
{
    // For each point Light
    // Initialise index to constant buffer
    int i = 0;
    // Create a map iterator and point to beginning of map
    std::map<std::string, PointLight*>::iterator it = _pointLights->begin();
    // Iterate over the map using Iterator till end.
    while (it != _pointLights->end())
    {
        // Access the light from element pointed by it and store in the constant buffer
        cb->pointLights[i] = *it->second;
        // Increment the Iterator to point to next entry
        it++;
        // Increment the index for the constant buffer
        i++;
    }
    cb->pointLightsCount = i;
}

void Application::StoreSpotLights(ConstantBuffer* cb)
{
    // For each spot Light
    // Initialise index to constant buffer
    int i = 0;
    // Create a map iterator and point to beginning of map
    std::map<std::string, SpotLight*>::iterator it = _spotLights->begin();
    // Iterate over the map using Iterator till end.
    while (it != _spotLights->end())
    {
        // Access the light from element pointed by it and store in the constant buffer
        cb->spotLights[i] = *it->second;
        // Increment the Iterator to point to next entry
        it++;
        // Increment the index for the constant buffer
        i++;
    }
    cb->spotLightsCount = i;
}

void Application::DrawActors(ID3D11DeviceContext* immediateContext, ID3D11Buffer* constantBuffer, ConstantBuffer cb)
{
    // For each actor
    // Create a map iterator and point to beginning of map
    std::map<std::string, Actor*>::iterator it = _actors->begin();
    // Iterate over the map using Iterator till end.
    while (it != _actors->end())
    {
        // Access the actor from element pointed by it and call Update()
        it->second->Draw(immediateContext, constantBuffer, cb);
        // Increment the Iterator to point to next entry
        it++;
    }
}

void Application::Draw()
{
    //
    // Clear the back buffer
    //
    float ClearColor[4] = { 0.0f, 0.125f, 0.3f, 1.0f };
    _pImmediateContext->ClearRenderTargetView(_pRenderTargetView, ClearColor);  // Clear the rendering target to blue
    _pImmediateContext->ClearDepthStencilView(_depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    XMMATRIX world = XMLoadFloat4x4(&_world);
    XMMATRIX view = XMLoadFloat4x4(&_view);
    XMMATRIX projection = XMLoadFloat4x4(&_projection); //Load in infromation about our object
    //
    // Update variables
    //
    ConstantBuffer cb;
    cb.mWorld = XMMatrixTranspose(world);
    cb.mView = XMMatrixTranspose(view);
    cb.mProjection = XMMatrixTranspose(projection);

    // Store lights in the constant buffer
    StoreDirectionalLights(&cb);
    StorePointLights(&cb);
    StoreSpotLights(&cb);

    cb.EyeWorldPos = _camera->GetEye();

    _pImmediateContext->VSSetShader(_pVertexShader, nullptr, 0);
    _pImmediateContext->VSSetConstantBuffers(0, 1, &_pConstantBuffer);
    _pImmediateContext->PSSetConstantBuffers(0, 1, &_pConstantBuffer);
    _pImmediateContext->PSSetShader(_pPixelShader, nullptr, 0);
    
    // Draw actors
    DrawActors(_pImmediateContext, _pConstantBuffer, cb);

    _pSwapChain->Present(0, 0);
}