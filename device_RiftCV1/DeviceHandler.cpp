#include "pch.h"
#include "DeviceHandler.h"

#include <iostream>
#include <chrono>
#include <ppl.h>
#include <thread>
#include <winreg.h>

#include "Win32_DirectXAppUtil.h"
#include <OVR_CAPI_D3D.h>


// Stuff taken from https://github.com/mm0zct/Oculus_Touch_Steam_Link

DirectX11 DIRECTX;

class GuardianSystem
{
public:
	GuardianSystem(DeviceHandler* _this) :
		logInfoMessage(_this->logInfoMessage),
		logWarningMessage(_this->logWarningMessage),
		logErrorMessage(_this->logErrorMessage),
		m_result(_this->m_result)
	{
	}

	void start_ovr();
	void InitRenderTargets(const ovrHmdDesc& hmdDesc);

	void Render();

	uint32_t vrObjects;
	ovrSession mSession = nullptr;

private:
	uint32_t mFrameIndex = 0; // Global frame counter
	ovrPosef mHmdToEyePose[ovrEye_Count] = {}; // Offset from the center of the HMD to each eye
	ovrRecti mEyeRenderViewport[ovrEye_Count] = {}; // Eye render target viewport

	ovrLayerEyeFov mEyeRenderLayer = {}; // OVR  - Eye render layers description
	ovrTextureSwapChain mTextureChain[ovrEye_Count] = {}; // OVR  - Eye render target swap chain
	ID3D11DepthStencilView* mEyeDepthTarget[ovrEye_Count] = {}; // DX11 - Eye depth view
	std::vector<ID3D11RenderTargetView*> mEyeRenderTargets[ovrEye_Count]; // DX11 - Eye render view

	bool mShouldQuit = false;

	// From parent for logging
	std::function<void(std::wstring)>& logInfoMessage;
	std::function<void(std::wstring)>& logWarningMessage;
	std::function<void(std::wstring)>& logErrorMessage;

	HRESULT& m_result;
};

void GuardianSystem::InitRenderTargets(const ovrHmdDesc& hmdDesc)
{
	// For each eye
	for (int i = 0; i < ovrEye_Count; ++i)
	{
		// Viewport
		const ovrSizei idealSize = ovr_GetFovTextureSize(
			mSession, static_cast<ovrEyeType>(i),
			hmdDesc.DefaultEyeFov[i], 1.0f);

		mEyeRenderViewport[i] = {
			0, 0, idealSize.w, idealSize.h
		};

		// Create Swap Chain
		ovrTextureSwapChainDesc desc = {
			ovrTexture_2D, OVR_FORMAT_R8G8B8A8_UNORM_SRGB, 1,
			idealSize.w, idealSize.h, 1, 1,
			ovrFalse, ovrTextureMisc_DX_Typeless, ovrTextureBind_DX_RenderTarget
		};

		// Configure Eye render layers
		mEyeRenderLayer.Header.Type = ovrLayerType_EyeFov;
		mEyeRenderLayer.Viewport[i] = mEyeRenderViewport[i];
		mEyeRenderLayer.Fov[i] = hmdDesc.DefaultEyeFov[i];
		mHmdToEyePose[i] = ovr_GetRenderDesc(
			mSession, static_cast<ovrEyeType>(i),
			hmdDesc.DefaultEyeFov[i]).HmdToEyePose;

		// DirectX 11 - Generate RenderTargetView from textures in swap chain
		// ----------------------------------------------------------------------
		ovrResult result = ovr_CreateTextureSwapChainDX(
			mSession, DIRECTX.Device, &desc, &mTextureChain[i]);

		if (!OVR_SUCCESS(result))
			logErrorMessage(L"ovr_CreateTextureSwapChainDX failed");

		// Render Target, normally triple-buffered
		int textureCount = 0;
		ovr_GetTextureSwapChainLength(mSession, mTextureChain[i], &textureCount);
		for (int j = 0; j < textureCount; ++j)
		{
			ID3D11Texture2D* renderTexture = nullptr;
			ovr_GetTextureSwapChainBufferDX(mSession, mTextureChain[i], j, IID_PPV_ARGS(&renderTexture));

			D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc = {
				DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_RTV_DIMENSION_TEXTURE2D
			};

			ID3D11RenderTargetView* renderTargetView = nullptr;
			DIRECTX.Device->CreateRenderTargetView(renderTexture,
			                                       &renderTargetViewDesc, &renderTargetView);
			mEyeRenderTargets[i].push_back(renderTargetView);
			renderTexture->Release();
		}

		// DirectX 11 - Generate Depth
		// ----------------------------------------------------------------------
		D3D11_TEXTURE2D_DESC depthTextureDesc = {
			(UINT)idealSize.w, (UINT)idealSize.h, 1, 1, DXGI_FORMAT_D32_FLOAT, {1, 0},
			D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL, 0, 0
		};

		ID3D11Texture2D* depthTexture = nullptr;
		DIRECTX.Device->CreateTexture2D(&depthTextureDesc, NULL, &depthTexture);
		DIRECTX.Device->CreateDepthStencilView(depthTexture, NULL, &mEyeDepthTarget[i]);
		depthTexture->Release();
	}
}

void GuardianSystem::Render()
{
	// Get current eye pose for rendering
	double eyePoseTime = 0;
	ovrPosef eyePose[ovrEye_Count] = {};
	ovr_GetEyePoses(mSession, mFrameIndex, ovrTrue, mHmdToEyePose, eyePose, &eyePoseTime);

	// Render each eye
	for (int i = 0; i < ovrEye_Count; ++i)
	{
		int renderTargetIndex = 0;
		ovr_GetTextureSwapChainCurrentIndex(mSession, mTextureChain[i], &renderTargetIndex);
		ID3D11RenderTargetView* renderTargetView = mEyeRenderTargets[i][renderTargetIndex];
		ID3D11DepthStencilView* depthTargetView = mEyeDepthTarget[i];

		// Clear and set render/depth target and viewport
		DIRECTX.SetAndClearRenderTarget(renderTargetView, depthTargetView, 0.0f, 0.0f, 0.0f, 1.0f);
		// THE SCREEN RENDER COLOUR
		DIRECTX.SetViewport(static_cast<float>(mEyeRenderViewport[i].Pos.x),
		                    static_cast<float>(mEyeRenderViewport[i].Pos.y),
		                    static_cast<float>(mEyeRenderViewport[i].Size.w),
		                    static_cast<float>(mEyeRenderViewport[i].Size.h));

		// Render and commit to swap chain
		ovr_CommitTextureSwapChain(mSession, mTextureChain[i]);

		// Update eye layer
		mEyeRenderLayer.ColorTexture[i] = mTextureChain[i];
		mEyeRenderLayer.RenderPose[i] = eyePose[i];
		mEyeRenderLayer.SensorSampleTime = eyePoseTime;
	}

	// Submit frames
	ovrLayerHeader* layers = &mEyeRenderLayer.Header;
	ovrResult result = ovr_SubmitFrame(mSession, mFrameIndex++, nullptr, &layers, 1);

	if (!OVR_SUCCESS(result))
		logErrorMessage(L"ovr_SubmitFrame failed");
}

void GuardianSystem::start_ovr()
{
	__try
	{
		[&, this]
		{
			ovrResult result = ovr_Initialize(nullptr);
			if (!OVR_SUCCESS(result))
				logErrorMessage(L"ovr_Initialize failed");

			ovrGraphicsLuid luid;
			result = ovr_Create(&mSession, &luid);
			if (!OVR_SUCCESS(result))
				logErrorMessage(L"ovr_Create failed");

			if (!DIRECTX.InitWindow(nullptr, L"GuardianSystemDemo"))
				logErrorMessage(L"DIRECTX.InitWindow failed");

			// Use HMD desc to initialize device
			ovrHmdDesc hmdDesc = ovr_GetHmdDesc(mSession);
			if (!DIRECTX.InitDevice(hmdDesc.Resolution.w / 2,
			                        hmdDesc.Resolution.h / 2,
			                        reinterpret_cast<LUID*>(&luid)))
				logErrorMessage(L"DIRECTX.InitDevice failed");

			// Use FloorLevel tracking origin
			ovr_SetTrackingOriginType(mSession, ovrTrackingOrigin_FloorLevel);

			InitRenderTargets(hmdDesc);
			vrObjects = (ovr_GetConnectedControllerTypes(mSession) >> 8) & 0xf;

			// Main Loop
			Render();
		}();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		[&, this]
		{
			logErrorMessage(L"OVR initialization failure!");
			m_result = E_INIT_FAILURE;
		}();
	}
}

// Funny Variable
GuardianSystem* instance;

// ODTKRA
bool is_ODTKRA_started = false;
bool ODTKRAstop = false;

void DeviceHandler::keepRiftAlive()
{
	int seconds = 600000;

	auto Target_window_Name = L"Oculus Debug Tool";
	HWND hWindowHandle = FindWindow(nullptr, Target_window_Name);

	if (hWindowHandle != nullptr)
	{
		SendMessage(hWindowHandle, WM_CLOSE, 0, 0);
		SwitchToThisWindow(hWindowHandle, true);
	}

	Sleep(500); // not sure if needed

	//Sends commands to the Oculus Debug Tool CLI to decrease performance overhead
	//Unlikely to do much, but no reason not to.
	if (resEnabled)
		ODT_CLI();

	//Starts Oculus Debug Tool
	//std::string temp = ODTPath + "OculusDebugTool.exe";
	//TestOutput->Text(std::wstring(temp.begin(), temp.end()));

	start_ODT(hWindowHandle, Target_window_Name);

	Sleep(1000);

	if (check_ODT() == false)
	{
		is_ODTKRA_started = false;
		return;
	}


	hWindowHandle = FindWindow(nullptr, Target_window_Name);

	HWND PropertGrid = FindWindowEx(hWindowHandle, nullptr, L"wxWindowNR", nullptr);
	HWND wxWindow = FindWindowEx(PropertGrid, nullptr, L"wxWindow", nullptr);

	while (!ODTKRAstop)
	{
		if (seconds == 600000)
		{
			SendMessage(wxWindow, WM_KEYDOWN, VK_UP, 0);
			SendMessage(wxWindow, WM_KEYUP, VK_UP, 0);
			Sleep(50);
			SendMessage(wxWindow, WM_KEYDOWN, VK_DOWN, 0);
			SendMessage(wxWindow, WM_KEYUP, VK_DOWN, 0);
			seconds = 0;
		}

		seconds++;
		Sleep(1000);
	}
	ODTKRAstop = false;
}

// Regular Amethyst stuff
std::thread ODTKRAThread;

HRESULT DeviceHandler::getStatusResult()
{
	// Enable/disable settings
	Flags_SettingsSupported = m_result == S_OK;

	return m_result;
}

std::wstring DeviceHandler::statusResultWString(HRESULT stat)
{
	// Parse your device's status into some nice text here,
	// it has to be formatted like [HEADER]\n[TYPE]\n[MESSAGE]

	switch (stat)
	{
	case S_OK: return L"Success!\nS_OK\nEverything's good!";
	case E_NOT_STARTED: return
			L"Not started yet!\nE_NOT_STARTED\nClick 'Refresh' to initialize this device!";
	case E_INIT_FAILURE: return
			L"Init failure!\nE_INIT_FAILURE\nCheck if your Oculus HMD is connected and dash is started properly!";
	default: return L"Undefined: " + std::to_wstring(stat) +
			L"\nE_UNDEFINED\nSomething weird has happened, though we can't tell what.";
	}
}

void DeviceHandler::initialize()
{
	// Find out the size of the buffer required to store the value
	DWORD dwBufSize = 0;
	LONG lRetVal = RegGetValue(
		HKEY_LOCAL_MACHINE,
		L"SOFTWARE\\WOW6432Node\\Oculus VR, LLC\\Oculus",
		L"Base",
		RRF_RT_ANY,
		nullptr,
		nullptr,
		&dwBufSize);

	if (ERROR_SUCCESS != lRetVal ||
		dwBufSize <= 0)
		logErrorMessage(L"ODT could not be found! Some things may refuse to work!");

	// If we're ok
	else
	{
		std::wstring data;
		data.resize(dwBufSize / sizeof(wchar_t));

		RegGetValue(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Oculus VR, LLC\\Oculus",
		            L"Base", RRF_RT_ANY, nullptr, &data[0], &dwBufSize);

		ODTPath = data + L"Support\\oculus-diagnostics\\";
	}

	instance = new(_aligned_malloc(sizeof(GuardianSystem), 16)) GuardianSystem(this);

	// Assume success
	m_result = S_OK;

	// Setup Oculus Stuff
	instance->start_ovr();

	// Always should keep >0 joints at init so replace the 1st one
	trackedJoints[0] = ktvr::K2TrackedJoint(L"Left Touch Controller");
	trackedJoints.push_back(ktvr::K2TrackedJoint(L"Right Touch Controller"));

	for (int i = 0; i < instance->vrObjects; i++)
		trackedJoints.push_back(ktvr::K2TrackedJoint(L"VR Object " + std::to_wstring(i + 1)));

	// Mark the device as initialized
	initialized = true;
}

unsigned int frame = 0;

void DeviceHandler::update()
{
	// Update joints' positions here
	// Note: this is fired up every loop

	// Enable/disable settings
	Flags_SettingsSupported = m_result == S_OK;

	// Run the update loop
	if (isInitialized() && m_result == S_OK)
	{
		if (ODTKRAenabled && !ODTKRAstop && !is_ODTKRA_started)
		{
			ODTKRAThread = std::thread([this] { this->keepRiftAlive(); });
			is_ODTKRA_started = true;
		}
		else if (!ODTKRAenabled && is_ODTKRA_started)
		{
			ODTKRAstop = true;
			is_ODTKRA_started = false;
			killODT(0);
			ODTKRAThread.join();
			ODTKRAstop = false;
		}

		instance->Render();

		auto tracking_state = ovr_GetTrackingState(
			instance->mSession,
			ovr_GetTimeInSeconds() +
			static_cast<float>(extra_prediction) * 0.001,
			ovrTrue);

		for (int i = 0; i < 2; i++)
		{
			trackedJoints.at(i).update(
				{
					tracking_state.HandPoses[i].ThePose.Position.x,
					tracking_state.HandPoses[i].ThePose.Position.y,
					tracking_state.HandPoses[i].ThePose.Position.z
				},
				{
					tracking_state.HandPoses[i].ThePose.Orientation.w,
					tracking_state.HandPoses[i].ThePose.Orientation.x,
					tracking_state.HandPoses[i].ThePose.Orientation.y,
					tracking_state.HandPoses[i].ThePose.Orientation.z
				},
				{
					tracking_state.HandPoses[i].LinearVelocity.x,
					tracking_state.HandPoses[i].LinearVelocity.y,
					tracking_state.HandPoses[i].LinearVelocity.z
				},
				{
					tracking_state.HandPoses[i].LinearAcceleration.x,
					tracking_state.HandPoses[i].LinearAcceleration.y,
					tracking_state.HandPoses[i].LinearAcceleration.z
				},
				{
					tracking_state.HandPoses[i].AngularVelocity.x,
					tracking_state.HandPoses[i].AngularVelocity.y,
					tracking_state.HandPoses[i].AngularVelocity.z
				},
				{
					tracking_state.HandPoses[i].AngularAcceleration.x,
					tracking_state.HandPoses[i].AngularAcceleration.y,
					tracking_state.HandPoses[i].AngularAcceleration.z
				},
				ktvr::State_Tracked);
		}

		for (int i = 0; i < instance->vrObjects; i++)
		{
			auto deviceType = static_cast<ovrTrackedDeviceType>(ovrTrackedDevice_Object0 + i);
			ovrPoseStatef ovr_pose;

			ovr_GetDevicePoses(instance->mSession, &deviceType, 1,
			                   (ovr_GetTimeInSeconds() + (static_cast<float>(extra_prediction) * 0.001)), &ovr_pose);
			if ((ovr_pose.ThePose.Orientation.x != 0) && (ovr_pose.ThePose.Orientation.y != 0) && (ovr_pose.ThePose.
				Orientation.z != 0))
			{
				trackedJoints.at(i + 2).update(
					{
						ovr_pose.ThePose.Position.x,
						ovr_pose.ThePose.Position.y,
						ovr_pose.ThePose.Position.z
					},
					{
						ovr_pose.ThePose.Orientation.w,
						ovr_pose.ThePose.Orientation.x,
						ovr_pose.ThePose.Orientation.y,
						ovr_pose.ThePose.Orientation.z
					},
					{
						ovr_pose.LinearVelocity.x,
						ovr_pose.LinearVelocity.y,
						ovr_pose.LinearVelocity.z
					},
					{
						ovr_pose.LinearAcceleration.x,
						ovr_pose.LinearAcceleration.y,
						ovr_pose.LinearAcceleration.z
					},
					{
						ovr_pose.AngularVelocity.x,
						ovr_pose.AngularVelocity.y,
						ovr_pose.AngularVelocity.z
					},
					{
						ovr_pose.AngularAcceleration.x,
						ovr_pose.AngularAcceleration.y,
						ovr_pose.AngularAcceleration.z
					},
					ktvr::State_Tracked);
			}
		}


		// Mark that we see the user
		skeletonTracked = true;
		frame++;
	}
}

void DeviceHandler::shutdown()
{
	initialized = false;

	__try
	{
		if (ODTKRAenabled)
		{
			ODTKRAstop = true;
			ODTKRAThread.join();
		}

		DIRECTX.ReleaseDevice();
		ovr_Destroy(instance->mSession);
		ovr_Shutdown();
		delete[] instance;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		[&, this]
		{
			logErrorMessage(L"OVR shutdown failure!");
			m_result = E_INIT_FAILURE;
		}();
	}
}
