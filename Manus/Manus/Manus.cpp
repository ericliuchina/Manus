/*
   Copyright 2015 Manus VR

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 */

#include "stdafx.h"
#include "Manus.h"
#include "Glove.h"
#include "Devices.h"
#include "SkeletalModel.h"

#ifdef _WIN32
#include "WinDevices.h"
#endif

#include <vector>
#include <mutex>

bool g_initialized = false;

std::vector<Glove*> g_gloves;
std::mutex g_gloves_mutex;

Devices* g_devices;
SkeletalModel g_skeletal;

int GetGlove(GLOVE_HAND hand, Glove** elem)
{
	std::lock_guard<std::mutex> lock(g_gloves_mutex);

	for (int i = 0; i < g_gloves.size(); i++)
	{
		if (g_gloves[i]->GetHand() == hand && g_gloves[i]->IsConnected())
		{
			*elem = g_gloves[i];
			return MANUS_SUCCESS;
		}
	}

	return MANUS_DISCONNECTED;
}

void DeviceConnected(const wchar_t* device_path)
{
	std::lock_guard<std::mutex> lock(g_gloves_mutex);

	// Check if the glove already exists
	for (Glove* glove : g_gloves)
	{
		if (wcscmp(device_path, glove->GetDevicePath()) == 0)
		{
			// The glove was previously connected, reconnect it
			glove->Connect();
			return;
		}
	}

	// The glove hasn't been connected before, add it to the list of gloves
	g_gloves.push_back(new Glove(device_path));
}

int ManusInit()
{
	if (g_initialized)
		return MANUS_ERROR;

	if (!g_skeletal.InitializeScene())
		return MANUS_ERROR;

	std::lock_guard<std::mutex> lock(g_gloves_mutex);

	// Get a list of Manus Glove Services.
	HDEVINFO device_info_set = SetupDiGetClassDevs(&GUID_MANUS_GLOVE_SERVICE, nullptr, nullptr,
		DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

	if (device_info_set)
	{
		SP_DEVICE_INTERFACE_DATA device_interface_data = { 0 };
		device_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
		int device_index = 0;
		while (SetupDiEnumDeviceInterfaces(device_info_set, nullptr, &GUID_MANUS_GLOVE_SERVICE,
			device_index, &device_interface_data))
		{
			DWORD required_size = 0;

			// Query the required size for the structure.
			SetupDiGetDeviceInterfaceDetail(device_info_set, &device_interface_data, nullptr,
				0, &required_size, nullptr);

			// HRESULT will never be S_OK here, so just check the size.
			if (required_size > 0)
			{
				// Allocate the interface detail structure.
				SP_DEVICE_INTERFACE_DETAIL_DATA* device_interface_detail_data = (SP_DEVICE_INTERFACE_DETAIL_DATA*)malloc(required_size);
				device_interface_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

				// Get the detailed device data which includes the device path.
				if (SetupDiGetDeviceInterfaceDetail(device_info_set, &device_interface_data, device_interface_detail_data,
					required_size, nullptr, nullptr))
				{
					g_gloves.push_back(new Glove(device_interface_detail_data->DevicePath));
				}

				free(device_interface_detail_data);
			}

			device_index++;
		}

		SetupDiDestroyDeviceInfoList(device_info_set);
	}

#ifdef _WIN32
	g_devices = new WinDevices();
	g_devices->SetDeviceConnected(DeviceConnected);
#endif

	g_initialized = true;

	return MANUS_SUCCESS;
}

int ManusExit()
{
	if (!g_initialized)
		return MANUS_ERROR;

	std::lock_guard<std::mutex> lock(g_gloves_mutex);

	for (Glove* glove : g_gloves)
		delete glove;
	g_gloves.clear();

#ifdef _WIN32
	delete g_devices;
#endif

	g_initialized = false;

	return MANUS_SUCCESS;
}

int ManusGetData(GLOVE_HAND hand, GLOVE_DATA* data, unsigned int timeout)
{
	// Get the glove from the list
	Glove* elem;
	int ret = GetGlove(hand, &elem);
	if (ret != MANUS_SUCCESS)
		return ret;

	if (!data)
		return MANUS_INVALID_ARGUMENT;

	return elem->GetData(data, timeout) ? MANUS_SUCCESS : MANUS_ERROR;
}

int ManusGetSkeletal(GLOVE_HAND hand, GLOVE_SKELETAL* model, unsigned int timeout)
{
	GLOVE_DATA data;

	int ret = ManusGetData(hand, &data, timeout);
	if (ret != MANUS_SUCCESS)
		return ret;

	if (g_skeletal.Simulate(data, model, hand))
		return MANUS_SUCCESS;
	else
		return MANUS_ERROR;
}

int ManusGetSkeletalOSVR(GLOVE_HAND hand, GLOVE_SKELETAL* model, unsigned int timeout)
{
	GLOVE_DATA data;

	int ret = ManusGetData(hand, &data, timeout);
	if (ret != MANUS_SUCCESS)
		return ret;

	if (g_skeletal.Simulate(data, model, hand, true))
		return MANUS_SUCCESS;
	else
		return MANUS_ERROR;
}

int ManusSetHandedness(GLOVE_HAND hand, bool right_hand)
{
	// Get the glove from the list
	Glove* elem;
	int ret = GetGlove(hand, &elem);
	if (ret != MANUS_SUCCESS)
		return ret;

	// Set the flags
	uint8_t flags = elem->GetFlags();
	if (right_hand)
		flags |= GLOVE_FLAGS_HANDEDNESS;
	else
		flags &= ~GLOVE_FLAGS_HANDEDNESS;
	elem->SetFlags(flags);

	return MANUS_SUCCESS;
}

int ManusCalibrate(GLOVE_HAND hand, bool gyro, bool accel, bool fingers)
{
	// Get the glove from the list
	Glove* elem;
	int ret = GetGlove(hand, &elem);
	if (ret != MANUS_SUCCESS)
		return ret;

	// Set the flags
	uint8_t flags = elem->GetFlags();
	if (gyro)
		flags |= GLOVE_FLAGS_CAL_GYRO;
	else
		flags &= ~GLOVE_FLAGS_CAL_GYRO;
	if (accel)
		flags |= GLOVE_FLAGS_CAL_ACCEL;
	else
		flags &= ~GLOVE_FLAGS_CAL_ACCEL;
	if (fingers)
		flags |= GLOVE_FLAGS_CAL_FINGERS;
	else
		flags &= ~GLOVE_FLAGS_CAL_FINGERS;
	elem->SetFlags(flags);

	return MANUS_SUCCESS;
}

int ManusSetVibration(GLOVE_HAND hand, float power){
	Glove* elem;
	int ret = GetGlove(hand, &elem);
	
	if (ret != MANUS_SUCCESS)
		return ret;

	elem->SetVibration(power);

	return MANUS_SUCCESS;
}
