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

class GuardianSystemDemo
{
public:
	void start_ovr(HINSTANCE hinst);
	void InitRenderTargets(const ovrHmdDesc& hmdDesc);

	void  Render();

	uint32_t vrObjects;
	ovrSession mSession = nullptr;

private:
	//    XMVECTOR mObjPosition[Scene::MAX_MODELS];                               // Objects cached position 
	//    XMVECTOR mObjVelocity[Scene::MAX_MODELS];                               // Objects velocity
	//    Scene mDynamicScene;                                                    // Scene graph

	//   high_resolution_clock mLastUpdateClock;                                 // Stores last update time
	//   float mGlobalTimeSec = 0;                                               // Game global time

	uint32_t mFrameIndex = 0;                                               // Global frame counter
	ovrPosef mHmdToEyePose[ovrEye_Count] = {};                              // Offset from the center of the HMD to each eye
	ovrRecti mEyeRenderViewport[ovrEye_Count] = {};                         // Eye render target viewport

	ovrLayerEyeFov mEyeRenderLayer = {};                                    // OVR  - Eye render layers description
	ovrTextureSwapChain mTextureChain[ovrEye_Count] = {};                   // OVR  - Eye render target swap chain
	ID3D11DepthStencilView* mEyeDepthTarget[ovrEye_Count] = {};             // DX11 - Eye depth view
	std::vector<ID3D11RenderTargetView*> mEyeRenderTargets[ovrEye_Count];   // DX11 - Eye render view

	bool mShouldQuit = false;
	//    bool mSlowMotionMode = false;                                           // Slow motion gets enabled when too close to the boundary
};

void GuardianSystemDemo::InitRenderTargets(const ovrHmdDesc& hmdDesc)
{
	// For each eye
	for (int i = 0; i < ovrEye_Count; ++i) {
		// Viewport
		const float kPixelsPerDisplayPixel = 1.0f;
		ovrSizei idealSize = ovr_GetFovTextureSize(mSession, (ovrEyeType)i, hmdDesc.DefaultEyeFov[i], kPixelsPerDisplayPixel);
		mEyeRenderViewport[i] = { 0, 0, idealSize.w, idealSize.h };

		// Create Swap Chain
		ovrTextureSwapChainDesc desc = {
			ovrTexture_2D, OVR_FORMAT_R8G8B8A8_UNORM_SRGB, 1, idealSize.w, idealSize.h, 1, 1,
			ovrFalse, ovrTextureMisc_DX_Typeless, ovrTextureBind_DX_RenderTarget
		};

		// Configure Eye render layers
		mEyeRenderLayer.Header.Type = ovrLayerType_EyeFov;
		mEyeRenderLayer.Viewport[i] = mEyeRenderViewport[i];
		mEyeRenderLayer.Fov[i] = hmdDesc.DefaultEyeFov[i];
		mHmdToEyePose[i] = ovr_GetRenderDesc(mSession, (ovrEyeType)i, hmdDesc.DefaultEyeFov[i]).HmdToEyePose;

		// DirectX 11 - Generate RenderTargetView from textures in swap chain
		// ----------------------------------------------------------------------
		ovrResult result = ovr_CreateTextureSwapChainDX(mSession, DIRECTX.Device, &desc, &mTextureChain[i]);
		if (!OVR_SUCCESS(result)) {
			printf("ovr_CreateTextureSwapChainDX failed");
		}

		// Render Target, normally triple-buffered
		int textureCount = 0;
		ovr_GetTextureSwapChainLength(mSession, mTextureChain[i], &textureCount);
		for (int j = 0; j < textureCount; ++j) {
			ID3D11Texture2D* renderTexture = nullptr;
			ovr_GetTextureSwapChainBufferDX(mSession, mTextureChain[i], j, IID_PPV_ARGS(&renderTexture));

			D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc = {
				DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_RTV_DIMENSION_TEXTURE2D
			};

			ID3D11RenderTargetView* renderTargetView = nullptr;
			DIRECTX.Device->CreateRenderTargetView(renderTexture, &renderTargetViewDesc, &renderTargetView);
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

void GuardianSystemDemo::Render()
{
	// Get current eye pose for rendering
	double eyePoseTime = 0;
	ovrPosef eyePose[ovrEye_Count] = {};
	ovr_GetEyePoses(mSession, mFrameIndex, ovrTrue, mHmdToEyePose, eyePose, &eyePoseTime);

	// Render each eye
	for (int i = 0; i < ovrEye_Count; ++i) {
		int renderTargetIndex = 0;
		ovr_GetTextureSwapChainCurrentIndex(mSession, mTextureChain[i], &renderTargetIndex);
		ID3D11RenderTargetView* renderTargetView = mEyeRenderTargets[i][renderTargetIndex];
		ID3D11DepthStencilView* depthTargetView = mEyeDepthTarget[i];

		// Clear and set render/depth target and viewport
		DIRECTX.SetAndClearRenderTarget(renderTargetView, depthTargetView, 0.0f, 0.0f, 0.0f, 1.0f);  // THE SCREEN RENDER COLOUR
		DIRECTX.SetViewport((float)mEyeRenderViewport[i].Pos.x, (float)mEyeRenderViewport[i].Pos.y,
			(float)mEyeRenderViewport[i].Size.w, (float)mEyeRenderViewport[i].Size.h);

		// Eye
		XMVECTOR eyeRot = XMVectorSet(eyePose[i].Orientation.x, eyePose[i].Orientation.y,
			eyePose[i].Orientation.z, eyePose[i].Orientation.w);
		XMVECTOR eyePos = XMVectorSet(eyePose[i].Position.x, eyePose[i].Position.y, eyePose[i].Position.z, 0);
		XMVECTOR eyeForward = XMVector3Rotate(XMVectorSet(0, 0, -1, 0), eyeRot);

		// Matrices
		XMMATRIX viewMat = XMMatrixLookAtRH(eyePos, XMVectorAdd(eyePos, eyeForward),
			XMVector3Rotate(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), eyeRot));
		ovrMatrix4f proj = ovrMatrix4f_Projection(mEyeRenderLayer.Fov[i], 0.001f, 1000.0f, ovrProjection_None);
		XMMATRIX projMat = XMMatrixTranspose(XMMATRIX(&proj.M[0][0]));
		XMMATRIX viewProjMat = XMMatrixMultiply(viewMat, projMat);

		// Render and commit to swap chain
		//mDynamicScene.Render(&viewProjMat, 1.0f, 1.0f, 1.0f, 1.0f, true);
		ovr_CommitTextureSwapChain(mSession, mTextureChain[i]);

		// Update eye layer
		mEyeRenderLayer.ColorTexture[i] = mTextureChain[i];
		mEyeRenderLayer.RenderPose[i] = eyePose[i];
		mEyeRenderLayer.SensorSampleTime = eyePoseTime;
	}

	// Submit frames
	ovrLayerHeader* layers = &mEyeRenderLayer.Header;
	ovrResult result = ovr_SubmitFrame(mSession, mFrameIndex++, nullptr, &layers, 1);
	if (!OVR_SUCCESS(result)) {
		printf("ovr_SubmitFrame failed");
	}
}

void GuardianSystemDemo::start_ovr(HINSTANCE hinst) {
	hinst = hinst;
	ovrResult result;
	result = ovr_Initialize(nullptr);
	if (!OVR_SUCCESS(result)) {
		printf("ovr_Initialize failed");
	}

	ovrGraphicsLuid luid;
	result = ovr_Create(&mSession, &luid);
	if (!OVR_SUCCESS(result)) {
		printf("ovr_Create failed");
	}

	if (!DIRECTX.InitWindow(0/*hinst*/, L"GuardianSystemDemo")) {
		printf("DIRECTX.InitWindow failed");
	}

	// Use HMD desc to initialize device
	ovrHmdDesc hmdDesc = ovr_GetHmdDesc(mSession);
	if (!DIRECTX.InitDevice(hmdDesc.Resolution.w / 2, hmdDesc.Resolution.h / 2, reinterpret_cast<LUID*>(&luid))) {
		printf("DIRECTX.InitDevice failed");
	}

	// Use FloorLevel tracking origin
	ovr_SetTrackingOriginType(mSession, ovrTrackingOrigin_FloorLevel);

	InitRenderTargets(hmdDesc);
	//InitSceneGraph();
	//mLastUpdateClock = std::chrono::high_resolution_clock::now();

	vrObjects = (ovr_GetConnectedControllerTypes(mSession) >> 8) & 0xf;

	// Main Loop
	uint64_t counter = 0;
	Render();
	uint64_t frame_count = 0;
	uint8_t buf[128] = { 0 };// , 255, 0, 0, 255, 255, 0, 0, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0     };
	unsigned int sizeof_buf = sizeof(buf);
	ovrHapticsBuffer vibuffer;
	vibuffer.Samples = buf;
	vibuffer.SamplesCount = sizeof_buf;
	vibuffer.SubmitMode = ovrHapticsBufferSubmit_Enqueue;

	for (int i = 0; i < (sizeof_buf/* / 2*/); i++) {
		buf[i/* *2*/] = 255;
	}
}

// Funny Variable

GuardianSystemDemo* instance;

// ODTKRA
bool is_ODTKRA_started = false;

bool ODTKRAstop = false;
void DeviceHandler::keepRiftAlive()
{
	int seconds = 600000;

	LPCWSTR Target_window_Name = L"Oculus Debug Tool";
	HWND hWindowHandle = FindWindow(NULL, Target_window_Name);

	if (hWindowHandle != NULL) {
		SendMessage(hWindowHandle, WM_CLOSE, 0, 0);
		SwitchToThisWindow(hWindowHandle, true);
	}

	Sleep(500); // not sure if needed

	//Sends commands to the Oculus Debug Tool CLI to decrease performance overhead
	//Unlikely to do much, but no reason not to.
	if(resEnabled)
		ODT_CLI();

	//Starts Oculus Debug Tool
	//std::string temp = ODTPath + "OculusDebugTool.exe";
	//TestOutput->Text(std::wstring(temp.begin(), temp.end()));

	start_ODT(hWindowHandle, Target_window_Name);

	Sleep(1000);

	if (check_ODT() == false) {
		is_ODTKRA_started = false;
		return;
	}
		

	hWindowHandle = FindWindow(NULL, Target_window_Name);

	HWND PropertGrid = FindWindowEx(hWindowHandle, NULL, L"wxWindowNR", NULL);
	HWND wxWindow = FindWindowEx(PropertGrid, NULL, L"wxWindow", NULL);

	while (!ODTKRAstop) {
		if (seconds==600000) {
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
	return S_OK;
}

std::wstring DeviceHandler::statusResultWString(HRESULT stat)
{
	// Parse your device's status into some nice text here,
	// it has to be formatted like [HEADER]\n[TYPE]\n[MESSAGE]

	switch (stat)
	{
	case S_OK: return L"Success!\nS_OK\nEverything's good!";
	default: return L"Undefined: " + std::to_wstring(stat) +
			L"\nE_UNDEFINED\nSomething weird has happened, though we can't tell what.";
	}
}

void rm_nonprinting(std::string& str) //https://stackoverflow.com/questions/60934716/how-to-strip-all-non-visible-characters-from-a-string-and-keep-special-characte
{
	str.erase(std::remove_if(str.begin(), str.end(),
		[](unsigned char c) {
			return !std::isprint(c);
		}),
		str.end());
}

void DeviceHandler::initialize()
{
	// Initialize your device here

	trackedJoints.clear();

	char value[512];
	DWORD BufferSize = 512;
	RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Oculus VR, LLC\\Oculus", L"Base", RRF_RT_ANY, NULL, (PVOID)&value, &BufferSize);
	std::string valuetemp(value, BufferSize);
	rm_nonprinting(valuetemp); // Thanks windows..

	ODTPath = valuetemp + "Support\\oculus-diagnostics\\";

	instance = new (_aligned_malloc(sizeof(GuardianSystemDemo), 16)) GuardianSystemDemo();

	// Setup Oculus Stuff
	instance->start_ovr(0);
	
	trackedJoints.push_back(ktvr::K2TrackedJoint("Left Touch Controller"));
	trackedJoints.push_back(ktvr::K2TrackedJoint("Right Touch Controller"));

	for (int i = 0; i < instance->vrObjects; i++)
	{
		trackedJoints.push_back(ktvr::K2TrackedJoint("VR Object " + std::to_string(i+1)));
	}


	// Mark the device as initialized
	initialized = true;
}

unsigned int frame = 0;
void DeviceHandler::update()
{
	// Update joints' positions here
	// Note: this is fired up every loop

	if (isInitialized())
	{
		//TestOutput->Text(std::wstring(ODTPath.begin(), ODTPath.end()));

		// For the plugin's sake, we'll update the joint with
		// the user's head position + 1m in z (front) axis
		// and the user's head orientation
		
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

		if (true) // really scuffed, but i am lazy
			instance->Render();

		//TestOutput->Text(std::to_wstring(testint));

		ovrTrackingState ss = ovr_GetTrackingState(instance->mSession, 0, false);

		ovrTrackingState tracking_state = ovr_GetTrackingState(instance->mSession, (ovr_GetTimeInSeconds() + ((float)extra_prediction * 0.001)), ovrTrue);

		for (int i = 0; i < 2; i++)
		{
			trackedJoints.at(i).update(
				Eigen::Vector3f(
					tracking_state.HandPoses[i].ThePose.Position.x,
					tracking_state.HandPoses[i].ThePose.Position.y,
					tracking_state.HandPoses[i].ThePose.Position.z),
				Eigen::Quaternion(
					tracking_state.HandPoses[i].ThePose.Orientation.w,
					tracking_state.HandPoses[i].ThePose.Orientation.x,
					tracking_state.HandPoses[i].ThePose.Orientation.y,
					tracking_state.HandPoses[i].ThePose.Orientation.z),
				ktvr::State_Tracked);

		}

		for (int i = 0; i < instance->vrObjects; i++) {
			ovrTrackedDeviceType deviceType = (ovrTrackedDeviceType)(ovrTrackedDevice_Object0 + i);
			ovrPoseStatef ovr_pose;

			ovr_GetDevicePoses(instance->mSession, &deviceType, 1, (ovr_GetTimeInSeconds() + ((float)extra_prediction * 0.001)), &ovr_pose);
			if ((ovr_pose.ThePose.Orientation.x != 0) && (ovr_pose.ThePose.Orientation.y != 0) && (ovr_pose.ThePose.Orientation.z != 0)) {
				trackedJoints.at(2+i).update(
					Eigen::Vector3f(
						ovr_pose.ThePose.Position.x,
						ovr_pose.ThePose.Position.y,
						ovr_pose.ThePose.Position.z),
					Eigen::Quaternion(
						ovr_pose.ThePose.Orientation.w,
						ovr_pose.ThePose.Orientation.x,
						ovr_pose.ThePose.Orientation.y,
						ovr_pose.ThePose.Orientation.z),
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

	if (ODTKRAenabled)
	{
		ODTKRAstop = true;
		ODTKRAThread.join();
	}

	DIRECTX.ReleaseDevice();
	ovr_Destroy(instance->mSession);
	ovr_Shutdown();
	//delete instance; // crashes for some reason??
}
