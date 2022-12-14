#pragma once
#include <Amethyst_API_Devices.h>
#include <Amethyst_API_Paths.h>

#include <fstream>
#include <shellapi.h>

#include <cereal/types/unordered_map.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/archives/xml.hpp>

#define FACILITY_CV1 0x301
#define E_NOT_STARTED MAKE_HRESULT(SEVERITY_ERROR, FACILITY_CV1, 2)
#define E_INIT_FAILURE MAKE_HRESULT(SEVERITY_ERROR, FACILITY_CV1, 3)

/* Not exported */

class DeviceHandler : public ktvr::K2TrackingDeviceBase_JointsBasis
{
public:
	/* K2API's things, which KTVR will make use of */

	DeviceHandler()
	{
		deviceName = L"Oculus Rift";

		Flags_SettingsSupported = false; // Only if S_OK
		Flags_OverridesJointPhysics = true;
		Flags_BlocksPositionFiltering = true;

		load_settings(); // Load settings
	}

	std::wstring getDeviceGUID() override
	{
		// This ID must be unique to this very plugin!
		// (Note: please keep the GUID string format)
		return L"DELTANRU-VEND-API1-DVCE-DVCEOTCHLINK";
	}

	~DeviceHandler() override
	{
	}

	void onLoad() override
	{
		auto enableODTKRA_label = CreateTextBlock(L"Enable keep rift alive ");
		auto enableODTKRA = CreateToggleSwitch();

		auto res_label = CreateTextBlock(L"Reduce resolution of rift ");
		auto res = CreateToggleSwitch();

		res->IsChecked(resEnabled);
		enableODTKRA->IsChecked(ODTKRAenabled);

		auto prediction_label = CreateTextBlock(L"Extra Predictions in milliseconds ");
		extra_prediction_ms = CreateNumberBox(extra_prediction);

		layoutRoot->AppendElementPairStack(
			prediction_label,
			extra_prediction_ms);

		layoutRoot->AppendElementPairStack(
			enableODTKRA_label,
			enableODTKRA);

		layoutRoot->AppendElementPairStack(
			res_label,
			res);

		// Why are these two seperate things?
		enableODTKRA->OnChecked =
			[&, this](ktvr::Interface::ToggleSwitch* sender)
			{
				ODTKRAenabled = true;
				save_settings(); // Save everything
			};
		enableODTKRA->OnUnchecked =
			[&, this](ktvr::Interface::ToggleSwitch* sender)
			{
				ODTKRAenabled = false;
				save_settings(); // Save everything
			};

		res->OnChecked =
			[&, this](ktvr::Interface::ToggleSwitch* sender)
			{
				resEnabled = true;
				save_settings(); // Save everything
			};
		res->OnUnchecked =
			[&, this](ktvr::Interface::ToggleSwitch* sender)
			{
				resEnabled = false;
				save_settings(); // Save everything
			};

		extra_prediction_ms->OnValueChanged = // also taken from the owotrack plugin
			[&, this](ktvr::Interface::NumberBox* sender, const int& new_value)
			{
				// Backup to writable
				int _value = new_value;

				// Handle resets
				if (_value < 0)_value = 11;

				const int fixed_new_value =
					std::clamp(_value, 0, 100);

				sender->Value(fixed_new_value); // Overwrite
				extra_prediction = fixed_new_value;
				//m_tracker_offset.y() = fixed_new_value;

				save_settings(); // Save everything
			};
	}

	HRESULT getStatusResult() override;
	std::wstring statusResultWString(HRESULT stat) override;

	void initialize() override;
	void update() override;
	void shutdown() override;
	void keepRiftAlive();

	void save_settings() // Thanks https://github.com/KimihikoAkayasaki/device_owoTrackVR
	{
		if (std::ofstream output(
				ktvr::GetK2AppDataFileDir(L"Device_Rift_settings.xml"));
			output.fail())
		{
			if (logErrorMessage)
				logErrorMessage(L"CV1 Device Error: Couldn't save settings!\n");
		}
		else
		{
			cereal::XMLOutputArchive archive(output);
			if (logInfoMessage)
				logInfoMessage(L"CV1 Device: Attempted to save settings\n");

			try
			{
				archive(
					CEREAL_NVP(extra_prediction),
					CEREAL_NVP(ODTKRAenabled),
					CEREAL_NVP(resEnabled)
				);
			}
			catch (...)
			{
				if (logErrorMessage)
					logErrorMessage(L"CV1 Device Error: Couldn't save settings, an exception occurred!\n");
			}
		}
	}

	void load_settings()
	{
		if (std::ifstream input(
				ktvr::GetK2AppDataFileDir(L"Device_Rift_settings.xml"));
			input.fail())
		{
			if (logWarningMessage)
				logWarningMessage(L"CV1 Device Error: Couldn't read settings, re-generating!\n");

			save_settings(); // Re-generate the file
		}
		else
		{
			if (logInfoMessage)
				logInfoMessage(L"CV1 Device: Attempting to read settings\n");

			try
			{
				cereal::XMLInputArchive archive(input);
				archive(
					CEREAL_NVP(extra_prediction),
					CEREAL_NVP(ODTKRAenabled),
					CEREAL_NVP(resEnabled)
				);
			}
			catch (...)
			{
				if (logErrorMessage)
					logErrorMessage(L"CV1 Device Error: Couldn't read settings, an exception occurred!\n");
			}
		}
	}

	void killODT(int param) const
	{
		// Reverse ODT cli commands
		std::wstring temp = std::format(
			L"echo service set-pixels-per-display-pixel-override 1 | \"{}OculusDebugToolCLI.exe\"", ODTPath);
		ShellExecute(NULL, L"cmd.exe", temp.c_str(), NULL, NULL, SW_HIDE);

		if (HWND hWindowHandle =
				FindWindow(NULL, L"Oculus Debug Tool");
			hWindowHandle != NULL)
		{
			SendMessage(hWindowHandle, WM_CLOSE, 0, 0);
			SwitchToThisWindow(hWindowHandle, true);
		}
		Sleep(500);
	}


	// Check if ODT is running
	// Returns false if not, and true if it is
	bool check_ODT()
	{
		auto Target_window_Name = L"Oculus Debug Tool";
		HWND hWindowHandle = FindWindow(NULL, Target_window_Name);

		if (hWindowHandle == NULL)
			return false;
		return true;
	}

	void start_ODT(HWND& hWindowHandle, LPCWSTR& Target_window_Name)
	{
		// Starts ODT
		std::wstring tempstr = ODTPath + L"OculusDebugTool.exe";
		ShellExecute(NULL, L"open", tempstr.c_str(), NULL, NULL, SW_SHOWDEFAULT);
		Sleep(1000); // not sure if needed

		hWindowHandle = FindWindow(NULL, Target_window_Name);
		SwitchToThisWindow(hWindowHandle, true);

		Sleep(100); // not sure if needed

		// Goes to the "Bypass Proximity Sensor Check" toggle
		for (int i = 0; i < 7; i++)
		{
			keybd_event(VK_DOWN, 0xE0, KEYEVENTF_EXTENDEDKEY | 0, 0);
			keybd_event(VK_DOWN, 0xE0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
		}

		ShowWindow(hWindowHandle, SW_MINIMIZE);
	}

	void ODT_CLI()
	{
		//Sets "set-pixels-per-display-pixel-override" to 0.01 to decrease performance overhead
		std::wstring temp = std::format(
			L"echo service set-pixels-per-display-pixel-override 0.01 | \"{}OculusDebugToolCLI.exe\"", ODTPath);
		ShellExecute(NULL, L"cmd.exe", temp.c_str(), NULL, NULL, SW_HIDE);

		//Turn off ASW, we do not need it
		temp = std::format(L"echo server: asw.Off | \"{}OculusDebugToolCLI.exe\"", ODTPath);
		ShellExecute(NULL, L"cmd.exe", temp.c_str(), NULL, NULL, SW_HIDE);
	}

	ktvr::Interface::TextBlock *test, *TestOutput;
	ktvr::Interface::NumberBox* extra_prediction_ms;

	int extra_prediction = 11;
	bool ODTKRAenabled = false;
	bool resEnabled = true;

	//RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Oculus VR, LLC\\Oculus", L"Base", RRF_RT_ANY, NULL, (PVOID)&value, &BufferSize);
	std::wstring ODTPath = L"Test";

	HRESULT m_result = E_NOT_STARTED;
};

/* Exported for dynamic linking */
extern "C" __declspec(dllexport) void* TrackingDeviceBaseFactory(
	const char* pVersionName, int* pReturnCode)
{
	// Return the device handler for tracking
	// but only if interfaces are the same / up-to-date
	if (0 == strcmp(ktvr::IAME_API_Devices_Version, pVersionName))
	{
		static DeviceHandler TrackingHandler; // Create a new device handler -> KinectV2

		*pReturnCode = ktvr::K2InitError_None;
		return &TrackingHandler;
	}

	// Return code for initialization
	*pReturnCode = ktvr::K2InitError_BadInterface;
}
