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
#include "Defs.h"

// Constants
constexpr UINT ENCODE_WIDTH = 1920;
constexpr UINT ENCODE_HEIGHT = 1080;

CComPtr<IMFVideoSampleAllocatorEx> allocator;

CComPtr<IMFAttributes> transformAttrs;
CComQIPtr<IMFMediaEventGenerator> eventGen;

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
	MFT_REGISTER_TYPE_INFO inInfo = { MFMediaType_Video, MFVideoFormat_NV12 };
	MFT_REGISTER_TYPE_INFO outInfo = { MFMediaType_Video, MFVideoFormat_H264 };

	// Query for the adapter LUID to get a matching encoder for the device.
	CComQIPtr<IDXGIDevice> dxgiDevice(device);

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

	if (FAILED(hr = MFTEnum2(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER, &inInfo, &outInfo,
		enumAttrs, &activateRaw, &activateCount)))
		return hr;

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

HRESULT ConfigureEncoder(IN CComPtr<IMFTransform>& transform, CComPtr<IMFDXGIDeviceManager>& deviceManager, DWORD inputStreamID,
	DWORD outputStreamID
)
{
	HRESULT hr = S_OK;
	// Sets or clears the Direct3D Device Manager for DirectX Video Accereration (DXVA).
	if (FAILED(hr = transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(deviceManager.p))))
		return hr;

	// Set output type
	CComPtr<IMFMediaType> outputType;
	if (FAILED(hr = MFCreateMediaType(&outputType)))
		return hr;

	if (FAILED(hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)))
		return hr;
	if (FAILED(hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264)))
		return hr;
	if (FAILED(hr = outputType->SetUINT32(MF_MT_AVG_BITRATE, 30000000)))
		return hr;
	if (FAILED(hr = MFSetAttributeSize(outputType, MF_MT_FRAME_SIZE, ENCODE_WIDTH, ENCODE_HEIGHT)))
		return hr;
	if (FAILED(hr = MFSetAttributeRatio(outputType, MF_MT_FRAME_RATE, 60, 1)))
		return hr;
	if (FAILED(hr = outputType->SetUINT32(MF_MT_INTERLACE_MODE, 2)))
		return hr;
	if (FAILED(hr = outputType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, true)))
		return hr;

	if (FAILED(hr = transform->SetOutputType(outputStreamID, outputType, 0)))
		return hr;

	// Set input type
	CComPtr<IMFMediaType> inputType;
	if (FAILED(hr = transform->GetInputAvailableType(inputStreamID, 0, &inputType)))
		return hr;

	if (FAILED(hr = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)))
		return hr;
	if (FAILED(hr = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12)))
		return hr;
	if (FAILED(hr = MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, ENCODE_WIDTH, ENCODE_HEIGHT)))
		return hr;
	if (FAILED(hr = MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, 60, 1)))
		return hr;

	if (FAILED(hr = transform->SetInputType(inputStreamID, inputType, 0)))
		return hr;

	std::cout << "- Set encoder configuration" << std::endl;
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
	if (FAILED(hr = GetEncoder(device, transform, activate)))
		return hr;

	// Get the name of the encoder
	CComHeapPtr<WCHAR> friendlyName;
	UINT32 friendlyNameLength;
	if (FAILED(hr = activate->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &friendlyName, &friendlyNameLength)))
		return hr;
	std::wcout << "- Selected encoder: " << static_cast<WCHAR const*>(friendlyName) << std::endl;

	// Unlock the transform for async use and get event generator
	if (FAILED(hr = transformAttrs->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, true)))
		return hr;

	// Get Stream IDs
	DWORD inputStreamID, outputStreamID;
	hr = transform->GetStreamIDs(1, &inputStreamID, 1, &outputStreamID);
	if (hr == E_NOTIMPL) // Doesn't mean failed, see remarks
	{				     // https://docs.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-getstreamids
		inputStreamID = 0;
		outputStreamID = 0;
		hr = S_OK;
	}
	if (FAILED(hr))
		return hr;

	if (FAILED(hr = ConfigureEncoder(transform, deviceManager, inputStreamID, outputStreamID)))
		return hr;

	// Create DDAImpl Class
	DDAImpl d(device, context);
	if (FAILED(hr = d.Init()))
		return hr;

	// Capture loop
	const int nFrames = 60;
	const int WAIT_BASE = 17;
	int capturedFrames = 0;
	LARGE_INTEGER start = { 0 };
	LARGE_INTEGER end = { 0 };
	LARGE_INTEGER interval = { 0 };
	LARGE_INTEGER freq = { 0 };
	int wait = WAIT_BASE;

	// Failure count from Capture API
	UINT failCount = 0;

	ID3D11Texture2D* pDupTex2D;

	QueryPerformanceFrequency(&freq);

	// Reset waiting time for the next screen capture attempt
#define RESET_WAIT_TIME(start, end, interval, freq)         \
    QueryPerformanceCounter(&end);                          \
    interval.QuadPart = end.QuadPart - start.QuadPart;      \
    MICROSEC_TIME(interval, freq);                          \
    wait = (int)(WAIT_BASE - (interval.QuadPart * 1000));

	// Run capture loop
	do
	{
		// get start timestamp. 
		// use this to adjust the waiting period in each capture attempt to approximately attempt 60 captures in a second
		QueryPerformanceCounter(&start);
		// Get a frame from DDA
		if(FAILED(hr = d.GetCapturedFrame(&pDupTex2D, wait))) // Release after preproc
			failCount++;

		if (hr == DXGI_ERROR_WAIT_TIMEOUT)
		{
			// retry if there was no new update to the screen during our specific timeout interval
			// reset our waiting time
			RESET_WAIT_TIME(start, end, interval, freq);
			continue;
		}
		else
		{
			if (FAILED(hr))
			{
				// Re-try with a new DDA object
				printf("Capture failed with error 0x%08x. Re-create DDA and try again.\n", hr);
				__debugbreak();
				/*Demo.Cleanup();
				hr = Demo.Init();*/
				if (FAILED(hr))
				{
					// Could not initialize DDA, bail out/
					printf("Failed to Init DDDemo. return error 0x%08x\n", hr);
					return -1;
				}
				RESET_WAIT_TIME(start, end, interval, freq);
				QueryPerformanceCounter(&start);
				// Get a frame from DDA
				//Demo.Capture(wait);
			}
			RESET_WAIT_TIME(start, end, interval, freq);

			// Preprocess for encoding
			// [Insert preprocessing code here]

			if (FAILED(hr))
			{
				printf("Pre-processing failed with error 0x%08x\n", hr);
				return -1;
			}

			// Encode
			// [Insert encoding code here]

			if (FAILED(hr))
			{
				printf("Encode failed with error 0x%08x\n", hr);
				return -1;
			}
			capturedFrames++;
		}
	} while (capturedFrames <= nFrames);

	// Shutdown
	if (FAILED(hr = MFShutdown()))
		return hr;

	return 0;
}