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

CComPtr<IMFAttributes> transformAttrs;
CComQIPtr<IMFMediaEventGenerator> eventGen;

//CComPtr<IMFVideoSampleAllocatorEx> allocator;

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

HRESULT InitDXGI(CComPtr<ID3D11Device>& outDevice, CComPtr<ID3D11DeviceContext>& inContext)
{
	HRESULT hr = S_OK;

	if (FAILED(hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT |
		D3D11_CREATE_DEVICE_DEBUG, nullptr, 0, D3D11_SDK_VERSION, &outDevice, nullptr, &inContext)))
		return hr;

	// Probably not necessary in this application, but maybe the MFT requires it?
	CComQIPtr<ID3D10Multithread> mt(outDevice);
	mt->SetMultithreadProtected(TRUE);

	std::cout << "- Initialized DXGI" << std::endl;

	return hr;
}

HRESULT GetEncoder(const CComPtr<ID3D11Device>& inDevice, CComPtr<IMFTransform>& outTransform, CComPtr<IMFActivate>& outActivate)
{
	HRESULT hr = S_OK;
	// Find the encoder
	CComHeapPtr<IMFActivate*> activateRaw;
	UINT32 activateCount = 0;

	// Input & output types
	MFT_REGISTER_TYPE_INFO inInfo = { MFMediaType_Video, MFVideoFormat_NV12 };
	MFT_REGISTER_TYPE_INFO outInfo = { MFMediaType_Video, MFVideoFormat_H264 };

	// Query for the adapter LUID to get a matching encoder for the device.
	CComQIPtr<IDXGIDevice> dxgiDevice(inDevice);

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
	outActivate = activateRaw[0];

	// Release the other activates
	for (int i = 1; i < activateCount; i++)
		activateRaw[i]->Release();

	// Activate
	if (FAILED(hr = outActivate->ActivateObject(IID_PPV_ARGS(&outTransform))))
		return hr;

	// Get attributes
	if (FAILED(hr = outTransform->GetAttributes(&transformAttrs)))
		return hr;

	std::cout << "- GetEncoder() Found " << activateCount << " encoders" << std::endl;

	return hr;
}

HRESULT ConfigureEncoder(CComPtr<IMFTransform>& inTransform, CComPtr<IMFDXGIDeviceManager>& inDeviceManager, DWORD inInputStreamID,
	DWORD outputStreamID
)
{
	HRESULT hr = S_OK;

	// Sets or clears the Direct3D Device Manager for DirectX Video Accereration (DXVA).
	if (FAILED(hr = inTransform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(inDeviceManager.p))))
		return hr;

	// Create output type
	CComPtr<IMFMediaType> outputType;
	if (FAILED(hr = MFCreateMediaType(&outputType)))
		return hr;

	// Set output type
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

	// Set output type
	if (FAILED(hr = inTransform->SetOutputType(outputStreamID, outputType, 0)))
		return hr;

	// Input type, I have no idea how to do this
	CComPtr<IMFMediaType> inputType;
	/*if (FAILED(hr = MFCreateMediaType(&inputType)))
		return hr;*/

	if (FAILED(hr = inTransform->GetInputAvailableType(inInputStreamID, 0, &inputType)))
		return hr;

	// Input type settings
	if (FAILED(hr = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)))
		return hr;
	if (FAILED(hr = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12)))
		return hr;
	if (FAILED(hr = MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, ENCODE_WIDTH, ENCODE_HEIGHT)))
		return hr;
	if (FAILED(hr = MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, 60, 1)))
		return hr;

	// Set input type
	if (FAILED(hr = inTransform->SetInputType(inInputStreamID, inputType, 0)))
		return hr;

	std::cout << "- Set encoder configuration" << std::endl;

	// AMD encoder crashes on this line
	/*DWORD flags;
	if (FAILED(hr = inTransform->GetInputStatus(0, &flags)))
		return hr;*/

	return hr;
}

HRESULT ConfigureColorConversion(IMFTransform* m_pXVP)
{
	HRESULT hr = S_OK;

	CComPtr<IMFMediaType> inputType;
	if (FAILED(hr = MFCreateMediaType(&inputType)))
		return hr;
	if (FAILED(hr = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)))
		return hr;
	if (FAILED(hr = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32)))
		return hr;
	if (FAILED(hr = inputType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, 1)))
		return hr;
	if (FAILED(hr = MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, 3840, 2160)))
		return hr;
	/*if (FAILED(hr = MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, 60, 1)))
		return hr;*/

	if (FAILED(hr = m_pXVP->SetInputType(0, inputType, 0)))
		return hr;

	CComPtr<IMFMediaType> outputType;
	if (FAILED(hr = MFCreateMediaType(&outputType)))
		return hr;
	if (FAILED(hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)))
		return hr;
	if (FAILED(hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12)))
		return hr;
	if (FAILED(hr = outputType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, 1)))
		return hr;
	if (FAILED(hr = MFSetAttributeSize(outputType, MF_MT_FRAME_SIZE, ENCODE_WIDTH, ENCODE_HEIGHT)))
		return hr;

	if (FAILED(hr = m_pXVP->SetOutputType(0, outputType, 0)))
		return hr;

	return hr;
}

HRESULT ColorConvert(IMFTransform* inTransform, ID3D11Texture2D* inTexture, IMFSample** pSampleOut)
{
	HRESULT hr = S_OK;

	CD3D11_TEXTURE2D_DESC Desc;
	inTexture->GetDesc(&Desc);
	CComPtr<ID3D11Device> Device;
	inTexture->GetDevice(&Device);
	CD3D11_TEXTURE2D_DESC CopyDesc(Desc.Format, Desc.Width, Desc.Height, 1, 1, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
	CComPtr<ID3D11Texture2D> CopyTexture;
	Device->CreateTexture2D(&CopyDesc, nullptr, &CopyTexture);
	CComPtr<ID3D11DeviceContext> DeviceContext;
	Device->GetImmediateContext(&DeviceContext);
	DeviceContext->CopyResource(CopyTexture, inTexture);
	inTexture = CopyTexture;

	// Create buffer
	CComPtr<IMFMediaBuffer> inputBuffer;
	if (FAILED(hr = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), inTexture, 0, false, &inputBuffer)))
		return hr;

	// Create sample
	CComPtr<IMFSample> inputSample;
	if (FAILED(hr = MFCreateSample(&inputSample)))
		return hr;
	if (FAILED(hr = inputSample->AddBuffer(inputBuffer)))
		return hr;

	// Set input sample times
	if (FAILED(hr = inputSample->SetSampleTime(100)))
		return hr;
	if (FAILED(hr = inputSample->SetSampleDuration(1000)))
		return hr;

	// Process input
	if (FAILED(hr = inTransform->ProcessInput(0, inputSample, 0)))
		return hr;

	// Process output
	DWORD status;
	MFT_OUTPUT_DATA_BUFFER outputBuffer;
	outputBuffer.pSample = nullptr;
	outputBuffer.pEvents = nullptr;
	outputBuffer.dwStreamID = 0;
	outputBuffer.dwStatus = 0;

	MFT_OUTPUT_STREAM_INFO mftStreamInfo;
	ZeroMemory(&mftStreamInfo, sizeof(MFT_OUTPUT_STREAM_INFO));

	if (FAILED(hr = inTransform->GetOutputStreamInfo(0, &mftStreamInfo)))
		return hr;

	ATLASSERT(mftStreamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES);

	if (FAILED(hr = inTransform->ProcessOutput(0, 1, &outputBuffer, &status)))
		return hr;

	*pSampleOut = outputBuffer.pSample;

	// Test output to file
	/*IMFMediaBuffer* buffer;
	if (FAILED(hr = outputBuffer.pSample->ConvertToContiguousBuffer(&buffer)))
		return hr;

	unsigned char* data;
	DWORD length;
	if (FAILED(hr = buffer->GetCurrentLength(&length)))
		return hr;

	if (FAILED(hr = buffer->Lock(&data, nullptr, &length)))
		return hr;

	std::ofstream fout;
	fout.open("raw.nv12", std::ios::binary | std::ios::out);
	fout.write((char*)data, length);
	fout.close();

	if (FAILED(hr = buffer->Unlock()))
		return hr;*/

	return hr;
}

//HRESULT Encode(IMFTransform* inTransform, IMFSample* inpSample)
//{
//	HRESULT hr = S_OK;
//
//	// Process output
//
//
//	return hr;
//}

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
	CComPtr<IMFTransform> encoderTransform;
	CComPtr<IMFActivate> activate;
	if (FAILED(hr = GetEncoder(device, encoderTransform, activate)))
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

	// Get encoder stream IDs
	DWORD inputStreamID, outputStreamID;
	hr = encoderTransform->GetStreamIDs(1, &inputStreamID, 1, &outputStreamID);
	if (hr == E_NOTIMPL) // Doesn't mean failed, see remarks
	{				     // https://docs.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-getstreamids
		inputStreamID = 0;
		outputStreamID = 0;
		hr = S_OK;
	}

	if (FAILED(hr))
		return hr;

	// Init encoder-related objects/variables
	if (FAILED(hr = ConfigureEncoder(encoderTransform, deviceManager, inputStreamID, outputStreamID)))
		return hr;

	eventGen = encoderTransform;

	// Init color conversion-related objects/variables
	IMFTransform* colorTransform;
	if (FAILED(hr = CoCreateInstance(CLSID_VideoProcessorMFT, nullptr, CLSCTX_INPROC_SERVER,
		IID_IMFTransform, (void**)&colorTransform)))
		return hr;

	if (FAILED(hr = colorTransform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(deviceManager.p))))
		return hr;

	if (FAILED(hr = ConfigureColorConversion(colorTransform)))
		return hr;

	if (FAILED(hr = encoderTransform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL)))
		return hr;
	if (FAILED(hr = encoderTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL)))
		return hr;
	if (FAILED(hr = encoderTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL)))
		return hr;

	// Create DDAImpl Class
	DDAImpl d(device, context);
	if (FAILED(hr = d.Init()))
		return hr;

	// Capture loop
	const int nFrames = 360;
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

	std::ofstream fout;
	fout.open("raw.h264", std::ios::binary | std::ios::out);
	// Run capture loop
	do
	{
		// get start timestamp. 
		// use this to adjust the waiting period in each capture attempt to approximately attempt 60 captures in a second
		QueryPerformanceCounter(&start);
		// Get a frame from DDA
		if (FAILED(hr = d.GetCapturedFrame(&pDupTex2D, wait))) // Release after preproc
			failCount++;

		if (hr == DXGI_ERROR_WAIT_TIMEOUT)
		{
			// retry if there was no new update to the screen during our specific timeout interval
			// reset our waiting time
			printf("DXGI_ERROR_WAIT_TIMEOUT\n");
			RESET_WAIT_TIME(start, end, interval, freq);
			continue;
		}

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


		// Encode the NV12 frame to H264
		CComPtr<IMFMediaEvent> event;
		if (FAILED(hr = eventGen->GetEvent(0, &event)))
			return hr;

		MediaEventType eventType;
		if (FAILED(hr = event->GetType(&eventType)))
			return hr;

		switch (eventType)
		{
			case METransformNeedInput:
			{
				// Color conversion from ARGB32 to NV12 for encoding
				IMFSample* nv12sample = nullptr;
				if (FAILED(hr = ColorConvert(colorTransform, pDupTex2D, &nv12sample)))
					return hr;

				pDupTex2D->Release();

				// Process input
				if (FAILED(hr = encoderTransform->ProcessInput(0, nv12sample, 0)))
					return hr;

				nv12sample->Release();
				device.Release();

				break;
			}
			case METransformHaveOutput:
			{
				DWORD status;
				MFT_OUTPUT_DATA_BUFFER outputBuffer;
				outputBuffer.pSample = nullptr;
				outputBuffer.pEvents = nullptr;
				outputBuffer.dwStreamID = 0;
				outputBuffer.dwStatus = 0;

				MFT_OUTPUT_STREAM_INFO mftStreamInfo;
				ZeroMemory(&mftStreamInfo, sizeof(MFT_OUTPUT_STREAM_INFO));

				if (FAILED(hr = encoderTransform->GetOutputStreamInfo(0, &mftStreamInfo)))
					return hr;

				ATLASSERT(mftStreamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES);

				if (FAILED(hr = encoderTransform->ProcessOutput(0, 1, &outputBuffer, &status)))
					return hr;

				// Test output to file
				IMFMediaBuffer* buffer;
				if (FAILED(hr = outputBuffer.pSample->ConvertToContiguousBuffer(&buffer)))
					return hr;

				unsigned char* data;
				DWORD length;
				if (FAILED(hr = buffer->GetCurrentLength(&length)))
					return hr;

				if (FAILED(hr = buffer->Lock(&data, nullptr, &length)))
					return hr;

				
				fout.write((char*)data, length);
				

				if (FAILED(hr = buffer->Unlock()))
					return hr;
				// End test output to file

				if (outputBuffer.pSample)
					outputBuffer.pSample->Release();
				if (outputBuffer.pEvents)
					outputBuffer.pEvents->Release();

				break;
			}
		}

		capturedFrames++;
	} while (capturedFrames <= nFrames);
	fout.close();
	// Shutdown
	if (FAILED(hr = MFShutdown()))
		return hr;

	return 0;
}