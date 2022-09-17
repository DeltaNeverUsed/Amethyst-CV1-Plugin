#pragma once
#include "Amethyst_API_Devices.h"
#include <Amethyst_API_Paths.h>

#include <fstream>
#include <shellapi.h>

#include <cereal/types/unordered_map.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/archives/xml.hpp>



/* Not exported */

class DeviceHandler : public ktvr::K2TrackingDeviceBase_JointsBasis
{
public:
	/* K2API's things, which KTVR will make use of */

	DeviceHandler()
	{
		deviceType = ktvr::K2_Joints;
		deviceName = "Oculus Rift";

		settingsSupported = true;

		load_settings(); // Load settings
	}

	virtual ~DeviceHandler()
	{
	}

	void onLoad() override
	{
		/*
		test = CreateTextBlock(L"Test Output: ");
		TestOutput = CreateTextBlock(L"127.0.0.1");
		*/

		auto enableODTKRA_label = CreateTextBlock(L"Enable keep rift alive ");
		auto enableODTKRA = CreateToggleSwitch();

		auto res_label = CreateTextBlock(L"Reduce resolution of rift ");
		auto res = CreateToggleSwitch();
		
		res->IsChecked(resEnabled);
		enableODTKRA->IsChecked(ODTKRAenabled);

		auto oculus_diag_dir_label = CreateTextBlock(L"Oculus Diagnostics Directory");
		oculus_diag_dir = CreateTextBox();

		oculus_diag_dir->Text(std::wstring(ODTPath.begin(), ODTPath.end())); // why Mr. wide :(

		auto prediction_label = CreateTextBlock(L"Extra Predictions in milliseconds ");
		extra_prediction_ms = CreateNumberBox(
			static_cast<int>(extra_prediction));


		layoutRoot->AppendElementPairStack(
			prediction_label,
			extra_prediction_ms);
		/*
		layoutRoot->AppendElementPairStack(
			test,
			TestOutput);
		*/

		layoutRoot->AppendElementPairStack(
			enableODTKRA_label,
			enableODTKRA);

		layoutRoot->AppendElementPairStack(
			res_label,
			res);

		layoutRoot->AppendElementPairStack(
			oculus_diag_dir_label,
			oculus_diag_dir);


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

		oculus_diag_dir->OnEnterKeyDown =
			[&, this](ktvr::Interface::TextBox* sender)
		{
			ODTPath = std::string(sender->Text().begin(), sender->Text().end());
			ODTPath = std::string(ODTPath) + (ODTPath[ODTPath.length()] == '\\' ? "" : "\\"); // adds \ if user forgot to add it
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
			// too lazy to implement logging :(
		}
		else
		{
			cereal::XMLOutputArchive archive(output);
			//LOG(INFO) << "OWO Device: Attempted to save settings";

			try
			{
				archive(
					//CEREAL_NVP(m_net_port),
					CEREAL_NVP(extra_prediction),
					CEREAL_NVP(ODTPath),
					CEREAL_NVP(ODTKRAenabled),
					CEREAL_NVP(resEnabled)
				);
			}
			catch (...)
			{
				//LOG(ERROR) << "OWO Device Error: Couldn't save settings, an exception occurred!\n";
			}
		}
	}

	void load_settings()
	{
		if (std::ifstream input(
			ktvr::GetK2AppDataFileDir(L"Device_Rift_settings.xml"));
			input.fail())
		{
			//LOG(WARNING) << "OWO Device Error: Couldn't read settings, re-generating!\n";
			save_settings(); // Re-generate the file
		}
		else
		{
			//LOG(INFO) << "OWO Device: Attempting to read settings";

			try
			{
				cereal::XMLInputArchive archive(input);
				archive(
					//CEREAL_NVP(m_net_port),
					CEREAL_NVP(extra_prediction),
					CEREAL_NVP(ODTPath),
					CEREAL_NVP(ODTKRAenabled),
					CEREAL_NVP(resEnabled)
				);
			}
			catch (...)
			{
				//LOG(ERROR) << "OWO Device Error: Couldn't read settings, an exception occurred!\n";
			}
		}
	}

	void killODT(int param)
	{

		//Reverse ODT cli commands
		std::string temp = "echo service set-pixels-per-display-pixel-override 1 | \"" + ODTPath + "OculusDebugToolCLI.exe\"";
		system(temp.c_str());

		LPCWSTR Target_window_Name = L"Oculus Debug Tool";
		HWND hWindowHandle = FindWindow(NULL, Target_window_Name);
		if (hWindowHandle != NULL) {
			SendMessage(hWindowHandle, WM_CLOSE, 0, 0);
			SwitchToThisWindow(hWindowHandle, true);
		}
		Sleep(500);
	}


	// Check if ODT is running
	// Returns false if not, and true if it is
	bool check_ODT() {
		LPCWSTR Target_window_Name = L"Oculus Debug Tool";
		HWND hWindowHandle = FindWindow(NULL, Target_window_Name);

		if (hWindowHandle == NULL)
			return false;
		return true;
	}

	void start_ODT(HWND& hWindowHandle, LPCWSTR& Target_window_Name)
	{
		// Starts ODT
		std::string tempstr = ODTPath + "OculusDebugTool.exe";
		ShellExecute(NULL, L"open", (LPCWSTR)std::wstring(tempstr.begin(), tempstr.end()).c_str(), NULL, NULL, SW_SHOWDEFAULT);
		Sleep(1000); // not sure if needed

		hWindowHandle = FindWindow(NULL, Target_window_Name);
		SwitchToThisWindow(hWindowHandle, true);

		Sleep(100); // not sure if needed

		// Goes to the "Bypass Proximity Sensor Check" toggle
		for (int i = 0; i < 7; i++) {
			keybd_event(VK_DOWN, 0xE0, KEYEVENTF_EXTENDEDKEY | 0, 0);
			keybd_event(VK_DOWN, 0xE0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
		}

		ShowWindow(hWindowHandle, SW_MINIMIZE);
	}

	void ODT_CLI()
	{
		//Sets "set-pixels-per-display-pixel-override" to 0.01 to decrease performance overhead
		std::string temp = "echo service set-pixels-per-display-pixel-override 0.01 | \"" + ODTPath + "OculusDebugToolCLI.exe\"";
		system(temp.c_str());

		//Turn off ASW, we do not need it
		temp = "echo server: asw.Off | \"" + ODTPath + "OculusDebugToolCLI.exe\"";
		system(temp.c_str());

		//Clear screen
		system("cls");
	}

	ktvr::Interface::TextBlock* test, * TestOutput;
	ktvr::Interface::NumberBox* extra_prediction_ms;
	ktvr::Interface::TextBox* oculus_diag_dir;

	int extra_prediction = 11;
	std::string ODTPath = "C:\\Program Files\\Oculus\\Support\\oculus-diagnostics\\";
	bool ODTKRAenabled = false;
	bool resEnabled = true;
};

/* Exported for dynamic linking */
extern "C" __declspec(dllexport) void* TrackingDeviceBaseFactory(
	const char* pVersionName, int* pReturnCode)
{
	// Return the device handler for tracking
	// but only if interfaces are the same / up-to-date
	if (0 == strcmp(ktvr::IK2API_Devices_Version, pVersionName))
	{
		static DeviceHandler TrackingHandler; // Create a new device handler -> KinectV2

		*pReturnCode = ktvr::K2InitError_None;
		return &TrackingHandler;
	}

	// Return code for initialization
	*pReturnCode = ktvr::K2InitError_BadInterface;
}
