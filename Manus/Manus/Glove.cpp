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
#include "Glove.h"
#include "ManusMath.h"

#include <limits>

// Sensorfusion constants
#define ACCEL_DIVISOR 16384.0f
#define QUAT_DIVISOR 16384.0f
#define COMPASS_DIVISOR 32.0f
#define FINGER_DIVISOR 255.0f
// magnetometer conversion values
#define FUTPERCOUNT 0.3f; 
#define FCOUNTSPERUT 3.333f;
// accelerometer converion values
#define FGPERCOUNT 0.00006103515f; // 1 / ACCEL_DIVISOR


Glove::Glove(const wchar_t* device_path)
	: m_connected(false)
	, m_service_handle(INVALID_HANDLE_VALUE)
	, m_num_characteristics(0)
	, m_characteristics(nullptr)
	, m_event_handle(INVALID_HANDLE_VALUE)
	, m_value_changed_event(nullptr)
{
	//memset(&m_report, 0, sizeof(m_report));

	size_t len = wcslen(device_path) + 1;
	m_device_path = new wchar_t[len];
	memcpy(m_device_path, device_path, len * sizeof(wchar_t));

	Connect();
}

Glove::~Glove()
{
	Disconnect();
	delete m_device_path;
}

bool Glove::GetData(GLOVE_DATA* data, unsigned int timeout)
{
	// Wait until the thread is done writing a packet
	std::unique_lock<std::mutex> lk(m_report_mutex);

	// Optionally wait until the next package is sent
	if (timeout > 0)
	{
		m_report_block.wait_for(lk, std::chrono::milliseconds(timeout));
		if (!IsConnected())
		{
			lk.unlock();
			return false;
		}
	}
	
	*data = m_data;

	lk.unlock();

	return m_data.PacketNumber > 0;
}

void Glove::Connect()
{
	if (IsConnected())
		Disconnect();

	// Open the device using CreateFile().
	m_service_handle = CreateFile(m_device_path,
		FILE_GENERIC_READ | FILE_GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);

	if (m_service_handle == INVALID_HANDLE_VALUE)
		return;

	// Query the required size for the structures.
	USHORT required_size = 0;
	BluetoothGATTGetCharacteristics(m_service_handle, nullptr, 0, nullptr,
		&required_size, BLUETOOTH_GATT_FLAG_NONE);

	// HRESULT will never be S_OK here, so just check the size.
	if (required_size == 0)
		return;

	// Allocate the characteristic structures.
	m_characteristics = (PBTH_LE_GATT_CHARACTERISTIC)
		malloc(required_size * sizeof(BTH_LE_GATT_CHARACTERISTIC));

	// Get the characteristics offered by this service.
	HRESULT hr = BluetoothGATTGetCharacteristics(m_service_handle, nullptr, required_size, m_characteristics,
		&m_num_characteristics, BLUETOOTH_GATT_FLAG_NONE);

	if (SUCCEEDED(hr))
	{
		// Configure the characteristics.
		for (int i = 0; i < m_num_characteristics; i++)
		{
			if (m_characteristics[i].CharacteristicUuid.Value.ShortUuid == BLE_UUID_MANUS_GLOVE_REPORT)
			{
				ConfigureCharacteristic(&m_characteristics[i], true, false);
			}
			else if (m_characteristics[i].CharacteristicUuid.Value.ShortUuid == BLE_UUID_MANUS_GLOVE_FLAGS)
			{
				ReadCharacteristic(&m_characteristics[i], &m_flags, sizeof(m_flags));
			}
			else if (m_characteristics[i].CharacteristicUuid.Value.ShortUuid == BLE_UUID_MANUS_GLOVE_CALIB)
			{
				ReadCharacteristic(&m_characteristics[i], &m_calib, sizeof(CALIB_REPORT));
			}
		}

		// Allocate the value changed structures.
		m_value_changed_event = (PBLUETOOTH_GATT_VALUE_CHANGED_EVENT_REGISTRATION)
			malloc(sizeof(PBLUETOOTH_GATT_VALUE_CHANGED_EVENT_REGISTRATION) + sizeof(BTH_LE_GATT_CHARACTERISTIC));

		// Register for event callbacks on the characteristics.
		m_value_changed_event->NumCharacteristics = 1;
		memcpy(&m_value_changed_event->Characteristics, m_characteristics, sizeof(BTH_LE_GATT_CHARACTERISTIC));
		HRESULT hr = BluetoothGATTRegisterEvent(m_service_handle, CharacteristicValueChangedEvent, m_value_changed_event,
			Glove::OnCharacteristicChanged, this, &m_event_handle, BLUETOOTH_GATT_FLAG_NONE);

		m_connected = SUCCEEDED(hr);
	}
}

bool Glove::ReadCharacteristic(PBTH_LE_GATT_CHARACTERISTIC characteristic, void* dest, size_t length)
{
	// Query the required size for the structure.
	USHORT required_size = 0;
	BluetoothGATTGetCharacteristicValue(m_service_handle, characteristic, 0, nullptr,
		&required_size, BLUETOOTH_GATT_FLAG_NONE);

	// HRESULT will never be S_OK here, so just check the size.
	if (required_size == 0)
		return false;

	// Allocate the characteristic value structure.
	PBTH_LE_GATT_CHARACTERISTIC_VALUE value = (PBTH_LE_GATT_CHARACTERISTIC_VALUE)malloc(required_size);

	// Read the characteristic value.
	USHORT actual_size = 0;
	HRESULT hr = BluetoothGATTGetCharacteristicValue(m_service_handle, characteristic, required_size, value,
		&actual_size, BLUETOOTH_GATT_FLAG_NONE);

	// Ensure there is enough room in the buffer.
	if (SUCCEEDED(hr) && length >= value->DataSize)
		memcpy(dest, &value->Data, value->DataSize);

	free(value);
	return SUCCEEDED(hr);
}

bool Glove::WriteCharacteristic(PBTH_LE_GATT_CHARACTERISTIC characteristic, void* src, size_t length)
{
	// Make sure the characteristic is not a nullptr
	if (characteristic == nullptr)
		return false;

	// Allocate the characteristic value structure.
	PBTH_LE_GATT_CHARACTERISTIC_VALUE value = (PBTH_LE_GATT_CHARACTERISTIC_VALUE)
		malloc(length + sizeof(PBTH_LE_GATT_CHARACTERISTIC_VALUE));

	// Initialize the value structure.
	value->DataSize = (ULONG)length;
	memcpy(value->Data, src, length);

	// Write the characteristic value.
	USHORT actual_size = 0;
	HRESULT hr = BluetoothGATTSetCharacteristicValue(m_service_handle, characteristic, value,
		0, BLUETOOTH_GATT_FLAG_NONE);

	free(value);
	return SUCCEEDED(hr);
}

PBTH_LE_GATT_CHARACTERISTIC Glove::GetCharacteristic(USHORT identifier){
	for (int i = 0; i < m_num_characteristics; i++)
	{
		if (m_characteristics[i].CharacteristicUuid.Value.ShortUuid == identifier)
			return &m_characteristics[i];
	}

	return nullptr;
}

bool Glove::ConfigureCharacteristic(PBTH_LE_GATT_CHARACTERISTIC characteristic, bool notify, bool indicate)
{
	// Query the required size for the structure.
	USHORT required_size = 0;
	BluetoothGATTGetDescriptors(m_service_handle, characteristic, 0, nullptr,
		&required_size, BLUETOOTH_GATT_FLAG_NONE);

	// HRESULT will never be S_OK here, so just check the size.
	if (required_size == 0)
		return false;

	// Allocate the descriptor structures.
	PBTH_LE_GATT_DESCRIPTOR descriptors = (PBTH_LE_GATT_DESCRIPTOR)
		malloc(required_size * sizeof(BTH_LE_GATT_DESCRIPTOR));

	// Get the descriptors offered by this characteristic.
	USHORT actual_size = 0;
	HRESULT hr = BluetoothGATTGetDescriptors(m_service_handle, characteristic, required_size, descriptors,
		&actual_size, BLUETOOTH_GATT_FLAG_NONE);
	if (SUCCEEDED(hr))
	{
		for (int i = 0; i < actual_size; i++)
		{
			// Look for the client configuration.
			if (descriptors[i].DescriptorType == ClientCharacteristicConfiguration)
			{
				// Configure this characteristic.
				BTH_LE_GATT_DESCRIPTOR_VALUE value;
				memset(&value, 0, sizeof(value));
				value.DescriptorType = ClientCharacteristicConfiguration;
				value.ClientCharacteristicConfiguration.IsSubscribeToNotification = notify;
				value.ClientCharacteristicConfiguration.IsSubscribeToIndication = indicate;

				HRESULT hr = BluetoothGATTSetDescriptorValue(m_service_handle, &descriptors[i], &value, BLUETOOTH_GATT_FLAG_NONE);

				return SUCCEEDED(hr);
			}
		}
	}

	return false;
}

void Glove::Disconnect()
{
	m_connected = false;
	m_report_block.notify_all();

	std::unique_lock<std::mutex> lk(m_report_mutex);

	if (m_event_handle != INVALID_HANDLE_VALUE)
		BluetoothGATTUnregisterEvent(m_event_handle, BLUETOOTH_GATT_FLAG_NONE);
	m_event_handle = INVALID_HANDLE_VALUE;

	if (m_value_changed_event != nullptr)
		free(m_value_changed_event);
	m_value_changed_event = nullptr;

	if (m_service_handle != INVALID_HANDLE_VALUE)
		CloseHandle(m_service_handle);
	m_service_handle = INVALID_HANDLE_VALUE;

	if (m_characteristics != nullptr)
		free(m_characteristics);
	m_characteristics = nullptr;
}

void Glove::OnCharacteristicChanged(BTH_LE_GATT_EVENT_TYPE event_type, void* event_out, void* context)
{
	Glove* glove = (Glove*)context;

	// Normally we would get this parameter from event_out, but it looks like it is an invalid pointer.
	// However it seems the event struct we allocated is being kept up-to-date, so we'll just use that.
	PBLUETOOTH_GATT_VALUE_CHANGED_EVENT_REGISTRATION changed_event =
		(PBLUETOOTH_GATT_VALUE_CHANGED_EVENT_REGISTRATION)glove->m_value_changed_event;

	std::lock_guard<std::mutex> lk(glove->m_report_mutex);

	// Read all characteristics we're monitoring.
	for (int i = 0; i < changed_event->NumCharacteristics; i++)
	{
		PBTH_LE_GATT_CHARACTERISTIC characteristic = &changed_event->Characteristics[i];

		if (characteristic->CharacteristicUuid.Value.ShortUuid == BLE_UUID_MANUS_GLOVE_REPORT)
			glove->ReadCharacteristic(characteristic, &glove->m_report, sizeof(GLOVE_REPORT));
	}

	glove->UpdateState();
	glove->m_report_block.notify_all();
}

void Glove::UpdateState()
{
	m_data.PacketNumber++;

	m_data.Acceleration.x = m_report.accel[0] / ACCEL_DIVISOR;
	m_data.Acceleration.y = m_report.accel[1] / ACCEL_DIVISOR;
	m_data.Acceleration.z = m_report.accel[2] / ACCEL_DIVISOR;

	// normalize quaternion data
	m_data.Quaternion.w = m_report.quat[0] / QUAT_DIVISOR;
	m_data.Quaternion.x = m_report.quat[1] / QUAT_DIVISOR;
	m_data.Quaternion.y = m_report.quat[2] / QUAT_DIVISOR;
	m_data.Quaternion.z = m_report.quat[3] / QUAT_DIVISOR;

	// normalize finger data
	for (int i = 0; i < GLOVE_FINGERS; i++)
	{
		// account for finger order
		if (GetHand() == GLOVE_RIGHT)
			m_data.Fingers[i] = m_report.fingers[i] / FINGER_DIVISOR;
		else
			m_data.Fingers[i] = m_report.fingers[GLOVE_FINGERS - (i + 1)] / FINGER_DIVISOR;
	}

	// calculate the euler angles
	ManusMath::GetEuler(&m_data.Euler, &m_data.Quaternion);
}

uint8_t Glove::GetFlags()
{
	return m_flags;
}

GLOVE_HAND Glove::GetHand(){
	return (m_flags & GLOVE_FLAGS_HANDEDNESS) ? GLOVE_RIGHT : GLOVE_LEFT;
}

void Glove::SetFlags(uint8_t flags)
{
	m_flags = flags;

	WriteCharacteristic(GetCharacteristic(BLE_UUID_MANUS_GLOVE_FLAGS), &m_flags, sizeof(m_flags));
}

void Glove::SetVibration(float power)
{
	RUMBLE_REPORT report;

	// clipping
	if (power < 0.0) power = 0.0;
	if (power > 1.0) power = 1.0;

	report.value = uint16_t(power * std::numeric_limits<uint16_t>::max());

	WriteCharacteristic(GetCharacteristic(BLE_UUID_MANUS_GLOVE_RUMBLE), &report, sizeof(report));
}
