#include "GameApp.h"
#include "d3dUtil.h"
#include "DXTrace.h"
using namespace DirectX;

GameApp::GameApp(HINSTANCE hInstance)
	: D3DApp(hInstance),
	m_CameraMode(CameraMode::Free),
	m_SkyBoxMode(SkyBoxMode::Daylight)
{
}

GameApp::~GameApp()
{
}

bool GameApp::Init()
{
	if (!D3DApp::Init())
		return false;

	// 务必先初始化所有渲染状态，以供下面的特效使用
	RenderStates::InitAll(m_pd3dDevice.Get());

	if (!m_BasicEffect.InitAll(m_pd3dDevice.Get()))
		return false;

	if (!m_SkyEffect.InitAll(m_pd3dDevice.Get()))
		return false;

	if (!InitResource())
		return false;

	// 初始化鼠标，键盘不需要
	m_pMouse->SetWindow(m_hMainWnd);
	m_pMouse->SetMode(DirectX::Mouse::MODE_RELATIVE);

	return true;
}

void GameApp::OnResize()
{
	assert(m_pd2dFactory);
	assert(m_pdwriteFactory);
	// 释放D2D的相关资源
	m_pColorBrush.Reset();
	m_pd2dRenderTarget.Reset();

	D3DApp::OnResize();

	// 为D2D创建DXGI表面渲染目标
	ComPtr<IDXGISurface> surface;
	HR(m_pSwapChain->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surface.GetAddressOf())));
	D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));
	HRESULT hr = m_pd2dFactory->CreateDxgiSurfaceRenderTarget(surface.Get(), &props, m_pd2dRenderTarget.GetAddressOf());
	surface.Reset();

	if (hr == E_NOINTERFACE)
	{
		OutputDebugStringW(L"\n警告：Direct2D与Direct3D互操作性功能受限，你将无法看到文本信息。现提供下述可选方法：\n"
			L"1. 对于Win7系统，需要更新至Win7 SP1，并安装KB2670838补丁以支持Direct2D显示。\n"
			L"2. 自行完成Direct3D 10.1与Direct2D的交互。详情参阅："
			L"https://docs.microsoft.com/zh-cn/windows/desktop/Direct2D/direct2d-and-direct3d-interoperation-overview""\n"
			L"3. 使用别的字体库，比如FreeType。\n\n");
	}
	else if (hr == S_OK)
	{
		// 创建固定颜色刷和文本格式
		HR(m_pd2dRenderTarget->CreateSolidColorBrush(
			D2D1::ColorF(D2D1::ColorF::White),
			m_pColorBrush.GetAddressOf()));
		HR(m_pdwriteFactory->CreateTextFormat(L"宋体", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15, L"zh-cn",
			m_pTextFormat.GetAddressOf()));
	}
	else
	{
		// 报告异常问题
		assert(m_pd2dRenderTarget);
	}

	// 摄像机变更显示
	if (m_pCamera != nullptr)
	{
		m_pCamera->SetFrustum(XM_PI / 3, AspectRatio(), 1.0f, 1000.0f);
		m_pCamera->SetViewPort(0.0f, 0.0f, (float)m_ClientWidth, (float)m_ClientHeight);
		m_BasicEffect.SetProjMatrix(m_pCamera->GetProjXM());
	}
}

void GameApp::UpdateScene(float dt)
{

	// 更新鼠标事件，获取相对偏移量
	Mouse::State mouseState = m_pMouse->GetState();
	Mouse::State lastMouseState = m_MouseTracker.GetLastState();
	m_MouseTracker.Update(mouseState);
	// 更新键盘事件
	Keyboard::State keyState = m_pKeyboard->GetState();
	m_KeyboardTracker.Update(keyState);

	auto cam1st = std::dynamic_pointer_cast<FirstPersonCamera>(m_pCamera);

	// ******************
	// 自由摄像机的操作
	//

	// 方向移动
	if (keyState.IsKeyDown(Keyboard::W))
		cam1st->MoveForward(dt * 6.0f);
	if (keyState.IsKeyDown(Keyboard::S))
		cam1st->MoveForward(dt * -6.0f);
	if (keyState.IsKeyDown(Keyboard::A))
		cam1st->Strafe(dt * -6.0f);
	if (keyState.IsKeyDown(Keyboard::D))
		cam1st->Strafe(dt * 6.0f);

	// 在鼠标没进入窗口前仍为ABSOLUTE模式
	if (mouseState.positionMode == Mouse::MODE_RELATIVE)
	{
		cam1st->Pitch(mouseState.y * dt * 1.25f);
		cam1st->RotateY(mouseState.x * dt * 1.25f);
	}

	// 限制移动范围
	XMFLOAT3 adjustedPos;
	const XMVECTORF32 g_Min = { -100.0f,3.0f,-100.f,0.f };
	const XMVECTORF32 g_Max = { 100.0f,100.0f,100.0f,0.f };
	XMStoreFloat3(&adjustedPos, XMVectorClamp(cam1st->GetPositionXM(), g_Min, g_Max));
	cam1st->SetPosition(adjustedPos);
	

	// 更新观察矩阵
	m_BasicEffect.SetViewMatrix(m_pCamera->GetViewXM());
	m_BasicEffect.SetEyePos(m_pCamera->GetPosition());

	// 重置滚轮值
	m_pMouse->ResetScrollWheelValue();

	// 选择天空盒
	if (m_KeyboardTracker.IsKeyPressed(Keyboard::D1))
	{
		m_SkyBoxMode = SkyBoxMode::Daylight;
		m_BasicEffect.SetTextureCube(m_pDaylight->GetTextureCube());
	}
	if (m_KeyboardTracker.IsKeyPressed(Keyboard::D2))
	{
		m_SkyBoxMode = SkyBoxMode::Sunset;
		m_BasicEffect.SetTextureCube(m_pSunset->GetTextureCube());
	}
	
	// 退出程序，这里应向窗口发送销毁信息
	if (m_KeyboardTracker.IsKeyPressed(Keyboard::Escape))
		SendMessage(MainWnd(), WM_DESTROY, 0, 0);
}

void GameApp::DrawScene()
{
	assert(m_pd3dImmediateContext);
	assert(m_pSwapChain);

	// ******************
	// 绘制Direct3D部分
	//
	m_pd3dImmediateContext->ClearRenderTargetView(m_pRenderTargetView.Get(), reinterpret_cast<const float*>(&Colors::Black));
	m_pd3dImmediateContext->ClearDepthStencilView(m_pDepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	// 绘制模型
	m_BasicEffect.SetRenderDefault(m_pd3dImmediateContext.Get(), BasicEffect::RenderObject);
	m_BasicEffect.SetTextureUsed(true);
	m_BasicEffect.SetReflectionEnabled(false);

	m_Ground.Draw(m_pd3dImmediateContext.Get(), m_BasicEffect);
	std::vector<Transform> acceptedData;
	acceptedData = Collision::FrustumCulling(m_InstanceTrees, m_Trees.GetLocalBoundingBox(),
		m_pCamera->GetViewXM(), m_pCamera->GetProjXM());
	m_BasicEffect.SetRenderDefault(m_pd3dImmediateContext.Get(), BasicEffect::RenderInstance);
	m_Trees.DrawInstanced(m_pd3dImmediateContext.Get(), m_BasicEffect, acceptedData);
	m_House.Draw(m_pd3dImmediateContext.Get(), m_BasicEffect);


	// 绘制天空盒
	m_SkyEffect.SetRenderDefault(m_pd3dImmediateContext.Get());
	switch (m_SkyBoxMode)
	{
	case SkyBoxMode::Daylight: m_pDaylight->Draw(m_pd3dImmediateContext.Get(), m_SkyEffect, *m_pCamera); break;
	case SkyBoxMode::Sunset: m_pSunset->Draw(m_pd3dImmediateContext.Get(), m_SkyEffect, *m_pCamera); break;
	}
	

	// ******************
	// 绘制Direct2D部分
	//
	if (m_pd2dRenderTarget != nullptr)
	{
		m_pd2dRenderTarget->BeginDraw();
		std::wstring text = L"当前摄像机模式: 自由视角  Esc退出\n"
			L"鼠标移动控制视野 W/S/A/D移动\n"
			L"切换天空盒: 1-白天 2-日落\n"
			L"当前天空盒: ";

		switch (m_SkyBoxMode)
		{
		case SkyBoxMode::Daylight: text += L"白天"; break;
		case SkyBoxMode::Sunset: text += L"日落"; break;
		}

		m_pd2dRenderTarget->DrawTextW(text.c_str(), (UINT32)text.length(), m_pTextFormat.Get(),
			D2D1_RECT_F{ 0.0f, 0.0f, 600.0f, 200.0f }, m_pColorBrush.Get());
		HR(m_pd2dRenderTarget->EndDraw());
	}

	HR(m_pSwapChain->Present(0, 0));
}



bool GameApp::InitResource()
{

	// ******************
	// 初始化天空盒相关

	m_pDaylight = std::make_unique<SkyRender>();
	HR(m_pDaylight->InitResource(m_pd3dDevice.Get(), m_pd3dImmediateContext.Get(),
		L"..\\Texture\\daylight.jpg", 
		5000.0f));

	m_pSunset = std::make_unique<SkyRender>();
	HR(m_pSunset->InitResource(m_pd3dDevice.Get(), m_pd3dImmediateContext.Get(),
		std::vector<std::wstring>{
		L"..\\Texture\\sunset_posX.bmp", L"..\\Texture\\sunset_negX.bmp",
			L"..\\Texture\\sunset_posY.bmp", L"..\\Texture\\sunset_negY.bmp",
			L"..\\Texture\\sunset_posZ.bmp", L"..\\Texture\\sunset_negZ.bmp", },
		5000.0f));
	
	m_BasicEffect.SetTextureCube(m_pDaylight->GetTextureCube());
	// ******************
	// 初始化游戏对象
	//
	
	Model model;
	
	// 地面
	model.SetMesh(m_pd3dDevice.Get(), Geometry::CreatePlane(XMFLOAT2(200.0f, 200.0f), XMFLOAT2(50.0f, 50.0f)));
	model.modelParts[0].material.ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	model.modelParts[0].material.diffuse = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
	model.modelParts[0].material.specular = XMFLOAT4(0.2f, 0.2f, 0.2f, 16.0f); 
	model.modelParts[0].material.reflect = XMFLOAT4();
	HR(CreateDDSTextureFromFile(m_pd3dDevice.Get(),
		L"..\\Texture\\grass.dds",
		nullptr,
		model.modelParts[0].texDiffuse.GetAddressOf()));
	m_Ground.SetModel(std::move(model));
	m_Ground.GetTransform().SetPosition(0.0f, 0.0f, 0.0f);
	
	// 树
	m_ObjReader.Read( L"..\\Model\\tree.mbo",L"..\\Model\\tree.obj");
	//m_ObjReader.Read(NULL,L"..\\models\\trees\\tree.obj");
	m_Trees.SetModel(Model(m_pd3dDevice.Get(), m_ObjReader));
	// 获取树包围盒
	XMMATRIX S = XMMatrixScaling(0.015f, 0.015f, 0.015f);
	BoundingBox treeBox = m_Trees.GetLocalBoundingBox();
	treeBox.Transform(treeBox, S);
	srand((unsigned)time(nullptr));
	// 在dong北方向随机生成36颗随机朝向的树
	m_InstanceTrees.resize(36);
	m_Trees.ResizeBuffer(m_pd3dDevice.Get(), 36);
	float theta = 0.0f;
	int pos = 0;
	for (int i = 0; i < 4; ++i)
	{
		// 取5-95的半径放置随机的树
		for (int j = 0; j < 3; ++j)
		{
			// 距离越远，树木越多
			for (int k = 0; k < 2 * j + 1; ++k, ++pos)
			{
				float radius = (float)(rand() % 30 + 30 * j + 5);
				float randomRad = rand() % 256 / 256.0f * XM_2PI / 16;
				
				m_InstanceTrees[pos].SetScale(0.015f, 0.015f, 0.015f);
				m_InstanceTrees[pos].SetRotation(0.0f, rand() % 256 / 256.0f * XM_2PI, 0.0f);
				m_InstanceTrees[pos].SetPosition(radius * cosf(theta + randomRad), -(treeBox.Center.y - treeBox.Extents.y), radius * sinf(theta + randomRad));
			}
		}
		theta += XM_2PI / 16;
	}

	// 初始化房屋模型
	m_ObjReader.Read(L"..\\Model\\house.mbo", L"..\\Model\\house.obj");
	m_House.SetModel(Model(m_pd3dDevice.Get(), m_ObjReader));

	// 获取房屋包围盒
	//XMMATRIX S = XMMatrixScaling(0.015f, 0.015f, 0.015f);
	BoundingBox houseBox = m_House.GetLocalBoundingBox();
	houseBox.Transform(houseBox, S);
	// 让房屋底部紧贴地面
	Transform& houseTransform = m_House.GetTransform();
	houseTransform.SetScale(0.015f, 0.015f, 0.015f);
	houseTransform.SetPosition(-70.0f, -(houseBox.Center.y - houseBox.Extents.y ), -70.0f);
	
	//高校
	/*m_ObjReader.ReadObj(L"..\\models\\buildings\\highschool.obj");
	m_Highschool.SetModel(Model(m_pd3dDevice.Get(), m_ObjReader));
	BoundingBox hsBox = m_Highschool.GetLocalBoundingBox();
	hsBox.Transform(hsBox, S);
	// 让房屋底部紧贴地面
	Transform& houseTransform = m_Highschool.GetTransform();
	houseTransform.SetScale(0.015f, 0.015f, 0.015f);
	houseTransform.SetPosition(50.0f, -(hsBox.Center.y - hsBox.Extents.y), 0.0f);*/



	// ******************
	// 初始化摄像机
	//
	auto camera = std::shared_ptr<FirstPersonCamera>(new FirstPersonCamera);
	m_pCamera = camera;
	camera->SetViewPort(0.0f, 0.0f, (float)m_ClientWidth, (float)m_ClientHeight);
	camera->SetFrustum(XM_PI / 3, AspectRatio(), 1.0f, 1000.0f);
	camera->LookTo(XMFLOAT3(0.0f, 0.0f, -10.0f), XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f));

	m_BasicEffect.SetViewMatrix(camera->GetViewXM());
	m_BasicEffect.SetProjMatrix(camera->GetProjXM());


	// ******************
	// 初始化不会变化的值
	//

	// 方向光
	DirectionalLight dirLight[4];
	dirLight[0].ambient = XMFLOAT4(0.15f, 0.15f, 0.15f, 1.0f);
	dirLight[0].diffuse = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	dirLight[0].specular = XMFLOAT4(0.1f, 0.1f, 0.1f, 1.0f);
	dirLight[0].direction = XMFLOAT3(-0.577f, -0.577f, 0.577f);
	dirLight[1] = dirLight[0];
	dirLight[1].direction = XMFLOAT3(0.577f, -0.577f, 0.577f);
	dirLight[2] = dirLight[0];
	dirLight[2].direction = XMFLOAT3(0.577f, -0.577f, -0.577f);
	dirLight[3] = dirLight[0];
	dirLight[3].direction = XMFLOAT3(-0.577f, -0.577f, -0.577f);
	for (int i = 0; i < 4; ++i)
		m_BasicEffect.SetDirLight(i, dirLight[i]);


	// ******************
	// 设置调试对象名
	//
	m_Trees.SetDebugObjectName("Trees");
	m_Ground.SetDebugObjectName("Ground");
	m_pDaylight->SetDebugObjectName("DayLight");
	m_pSunset->SetDebugObjectName("Sunset");
	return true;
}
/*
void GameApp::CreateRandomTrees()
{
	srand((unsigned)time(nullptr));
	// 初始化树
	m_ObjReader.Read(L"..\\Model\\tree.mbo", L"..\\Model\\tree.obj");
	m_Trees.SetModel(Model(m_pd3dDevice.Get(), m_ObjReader));
	XMMATRIX S = XMMatrixScaling(0.015f, 0.015f, 0.015f);

	BoundingBox treeBox = m_Trees.GetLocalBoundingBox();

	// 让树木底部紧贴地面位于y = -2的平面
	treeBox.Transform(treeBox, S);
	float Ty = -(treeBox.Center.y - treeBox.Extents.y + 2.0f);
	// 随机生成144颗随机朝向的树
	m_InstancedData.resize(144);
	m_Trees.ResizeBuffer(m_pd3dDevice.Get(), 144);
	float theta = 0.0f;
	int pos = 0;
	for (int i = 0; i < 16; ++i)
	{
		// 取5-95的半径放置随机的树
		for (int j = 0; j < 3; ++j)
		{
			// 距离越远，树木越多
			for (int k = 0; k < 2 * j + 1; ++k, ++pos)
			{
				float radius = (float)(rand() % 30 + 30 * j + 5);
				float randomRad = rand() % 256 / 256.0f * XM_2PI / 16;

				m_InstancedData[pos].SetScale(0.015f, 0.015f, 0.015f);
				m_InstancedData[pos].SetRotation(0.0f, rand() % 256 / 256.0f * XM_2PI, 0.0f);
				m_InstancedData[pos].SetPosition(radius * cosf(theta + randomRad), Ty, radius * sinf(theta + randomRad));
			}
		}
		theta += XM_2PI / 16;
	}
}
*/