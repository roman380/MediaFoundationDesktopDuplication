// libs
#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "evr.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "Winmm.lib")
// std
#include <iostream>
#include <string>
// Windows
#include <windows.h>
#include <atlbase.h>
// DirectX
#include <d3d11.h>
// Media Foundation
#include <mfapi.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <Codecapi.h>
// Custom
#include "DDAImpl.h"

// Constants
constexpr UINT ENCODE_WIDTH = 1920;
constexpr UINT ENCODE_HEIGHT = 1080;
constexpr UINT ENCODE_FRAMES = 120;

CComPtr<IMFVideoSampleAllocatorEx> allocator;

CComPtr<IMFAttributes> transformAttrs;
CComQIPtr<IMFMediaEventGenerator> eventGen;
DWORD inputStreamID;
DWORD outputStreamID;

HRESULT InitMF()
{
	HRESULT hr = S_OK;
	if (FAILED(hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
		return hr;
	if (FAILED(hr = MFStartup(MF_VERSION)))
		return hr;

	std::cout << "- Initialized Media Foundation" << std::endl;

	return hr;
}

HRESULT InitDXGI(OUT CComPtr<ID3D11Device>& device, IN CComPtr<ID3D11DeviceContext>& context)
{
	HRESULT hr = S_OK;

	if (FAILED(hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT |
		D3D11_CREATE_DEVICE_DEBUG, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)))
		return hr;

	// Probably not necessary in this application, but maybe the MFT requires it?
	CComQIPtr<ID3D10Multithread> mt(device);
	mt->SetMultithreadProtected(TRUE);

	std::cout << "- Initialized DXGI" << std::endl;

	return hr;
}

HRESULT GetEncoder(IN const CComPtr<ID3D11Device>& device, OUT CComPtr<IMFTransform>& transform, OUT CComPtr<IMFActivate>& activate)
{
	HRESULT hr = S_OK;
	// Find the encoder
	CComHeapPtr<IMFActivate*> activateRaw;
	UINT32 activateCount = 0;

	// Input & output types
	//MFT_REGISTER_TYPE_INFO inInfo = { MFMediaType_Video, MFVideoFormat_NV12 };
	MFT_REGISTER_TYPE_INFO outInfo = { MFMediaType_Video, MFVideoFormat_H264 };

	// Query for the adapter LUID to get a matching encoder for the device.
	CComQIPtr<IDXGIDevice> dxgiDevice(device);
	//CHECK(dxgiDevice);
	CComPtr<IDXGIAdapter> adapter;
	if (FAILED(hr = dxgiDevice->GetAdapter(&adapter)))
		return hr;

	DXGI_ADAPTER_DESC adapterDesc;
	if (FAILED(hr = adapter->GetDesc(&adapterDesc)))
		return hr;

	CComPtr<IMFAttributes> enumAttrs;
	if (FAILED(hr = MFCreateAttributes(&enumAttrs, 1)))
		return hr;

	if (FAILED(hr = enumAttrs->SetBlob(MFT_ENUM_ADAPTER_LUID, (BYTE*)&adapterDesc.AdapterLuid, sizeof(LUID))))
		return hr;

	if (FAILED(hr = MFTEnum2(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER, nullptr, &outInfo,
		enumAttrs, &activateRaw, &activateCount)))
		return hr;

	//CHECK(activateCount != 0);

	// Choose the first returned encoder
	activate = activateRaw[0];

	// Memory management
	for (UINT32 i = 0; i < activateCount; i++)
		activateRaw[i]->Release();

	// Activate
	if (FAILED(hr = activate->ActivateObject(IID_PPV_ARGS(&transform))))
		return hr;

	// Get attributes
	if (FAILED(hr = transform->GetAttributes(&transformAttrs)))
		return hr;

	std::cout << "- GetEncoder() Found " << activateCount << " encoders" << std::endl;

	return hr;
}

int main()
{
	HRESULT hr;

	if (FAILED(hr = InitMF()))
		return hr;

	// Initialize DXGI
	CComPtr<ID3D11Device> device;
	CComPtr<ID3D11DeviceContext> context;
	if (FAILED(hr = InitDXGI(device, context)))
		return hr;

	// Create device manager
	CComPtr<IMFDXGIDeviceManager> deviceManager;
	UINT resetToken;
	if (FAILED(hr = MFCreateDXGIDeviceManager(&resetToken, &deviceManager)))
		return hr;

	// https://docs.microsoft.com/en-us/windows/win32/api/dxva2api/nf-dxva2api-idirect3ddevicemanager9-resetdevice
	// When you first create the Direct3D device manager, call this method with a pointer to the Direct3D device.
	if (FAILED(hr = deviceManager->ResetDevice(device, resetToken)))
		return hr;

	// Get encoder
	CComPtr<IMFTransform> transform;
	CComPtr<IMFActivate> activate;
	if(FAILED(hr = GetEncoder(device, transform, activate)))
		return hr;

	// Get the name of the encoder
	CComHeapPtr<WCHAR> friendlyName;
	UINT32 friendlyNameLength;
	if (FAILED(hr = activate->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &friendlyName, &friendlyNameLength)))
		return hr;
	std::wcout << "- Selected encoder: " <<  static_cast<WCHAR const*>(friendlyName) << std::endl;
	
	// Unlock the transform for async use and get event generator
	if (FAILED(hr = transformAttrs->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, true)))
		return hr;

	// Shutdown
	if (FAILED(hr = MFShutdown()))
		return hr;

	return 0;
}

//void runEncode()
//{
//
//
//
//	// ------------------------------------------------------------------------
//	// Initialize D3D11
//	// ------------------------------------------------------------------------
//
//
//
//
//	// ------------------------------------------------------------------------
//	// Initialize hardware encoder MFT
//	// ------------------------------------------------------------------------
//
//

//	// Get stream IDs (expect 1 input and 1 output stream)
//	{
//		HRESULT hr = transform->GetStreamIDs(1, &inputStreamID, 1, &outputStreamID);
//		if (hr == E_NOTIMPL)
//		{
//			inputStreamID = 0;
//			outputStreamID = 0;
//			hr = S_OK;
//		}
//		CHECK_HR(hr);
//	}
//
//
//	// ------------------------------------------------------------------------
//	// Configure hardware encoder MFT
//	// ------------------------------------------------------------------------
//
//	// Set D3D manager
//	CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(deviceManager.p)));
//
//	// Set output type
//	CComPtr<IMFMediaType> outputType;
//	CHECK_HR(MFCreateMediaType(&outputType));
//
//	CHECK_HR(outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
//	CHECK_HR(outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
//	CHECK_HR(outputType->SetUINT32(MF_MT_AVG_BITRATE, 30000000));
//	CHECK_HR(MFSetAttributeSize(outputType, MF_MT_FRAME_SIZE, ENCODE_WIDTH, ENCODE_HEIGHT));
//	CHECK_HR(MFSetAttributeRatio(outputType, MF_MT_FRAME_RATE, 60, 1));
//	CHECK_HR(outputType->SetUINT32(MF_MT_INTERLACE_MODE, 2));
//	CHECK_HR(outputType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));
//
//	CHECK_HR(transform->SetOutputType(outputStreamID, outputType, 0));
//
//	// Set input type
//	CComPtr<IMFMediaType> inputType;
//	CHECK_HR(transform->GetInputAvailableType(inputStreamID, 0, &inputType));
//
//	CHECK_HR(inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
//	CHECK_HR(inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
//	CHECK_HR(MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, ENCODE_WIDTH, ENCODE_HEIGHT));
//	CHECK_HR(MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, 60, 1));
//
//	CHECK_HR(transform->SetInputType(inputStreamID, inputType, 0));
//
//
//	// ------------------------------------------------------------------------
//	// Create sample allocator
//	// ------------------------------------------------------------------------
//
//	{
//		MFCreateVideoSampleAllocatorEx(IID_PPV_ARGS(&allocator));
//		CHECK(allocator);
//
//		CComPtr<IMFAttributes> allocAttrs;
//		MFCreateAttributes(&allocAttrs, 2);
//
//		CHECK_HR(allocAttrs->SetUINT32(MF_SA_D3D11_BINDFLAGS, D3D11_BIND_RENDER_TARGET));
//		CHECK_HR(allocAttrs->SetUINT32(MF_SA_D3D11_USAGE, D3D11_USAGE_DEFAULT));
//
//		CHECK_HR(allocator->SetDirectXManager(deviceManager));
//		CHECK_HR(allocator->InitializeSampleAllocatorEx(1, 2, allocAttrs, inputType));
//	}
//
//
//	// ------------------------------------------------------------------------
//	// Start encoding
//	// ------------------------------------------------------------------------
//
//	CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL));
//	CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL));
//	CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL));
//
//	// Encode loop
//	for (int i = 0; i < ENCODE_FRAMES; i++)
//	{
//		// Get next event
//		CComPtr<IMFMediaEvent> event;
//		CHECK_HR(eventGen->GetEvent(0, &event));
//
//		MediaEventType eventType;
//		CHECK_HR(event->GetType(&eventType));
//
//		switch (eventType)
//		{
//		case METransformNeedInput:
//		{
//			CComPtr<IMFSample> sample;
//			CHECK_HR(allocator->AllocateSample(&sample));
//			CHECK_HR(transform->ProcessInput(inputStreamID, sample, 0));
//
//			// Dereferencing the device once after feeding each frame "fixes" the leak.
//			//device.p->Release();
//
//			break;
//		}
//
//		case METransformHaveOutput:
//		{
//			DWORD status;
//			MFT_OUTPUT_DATA_BUFFER outputBuffer = {};
//			outputBuffer.dwStreamID = outputStreamID;
//
//			CHECK_HR(transform->ProcessOutput(0, 1, &outputBuffer, &status));
//
//			DWORD bufCount;
//			DWORD bufLength;
//			CHECK_HR(outputBuffer.pSample->GetBufferCount(&bufCount));
//
//			CComPtr<IMFMediaBuffer> outBuffer;
//			CHECK_HR(outputBuffer.pSample->GetBufferByIndex(0, &outBuffer));
//			CHECK_HR(outBuffer->GetCurrentLength(&bufLength));
//
//			printf("METransformHaveOutput buffers=%d, bytes=%d\n", bufCount, bufLength);
//
//			// Release the sample as it is not processed further.
//			if (outputBuffer.pSample)
//				outputBuffer.pSample->Release();
//			if (outputBuffer.pEvents)
//				outputBuffer.pEvents->Release();
//			break;
//		}
//		}
//	}
//
//	// ------------------------------------------------------------------------
//	// Finish encoding
//	// ------------------------------------------------------------------------
//
//	CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL));
//	CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, NULL));
//	CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL));
//
//	// Shutdown
//	printf("Finished encoding\n");
//
//	// I've tried all kinds of things...
//	//CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(nullptr)));
//
//	//transform->SetInputType(inputStreamID, NULL, 0);
//	//transform->SetOutputType(outputStreamID, NULL, 0);
//
//	//transform->DeleteInputStream(inputStreamID);
//
//	//deviceManager->ResetDevice(NULL, resetToken);
//
//	CHECK_HR(MFShutdownObject(transform));
//}