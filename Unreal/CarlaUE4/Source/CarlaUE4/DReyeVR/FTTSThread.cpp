// Copyright notice
#include "TTSThread.h" // Change this to reference the header file above


#pragma region Main Thread Code
// This code will be run on the thread that invoked this thread (i.e. game thread)

std::string* TextStdStringPtr;

FTTSThread::FTTSThread(std::string& TextStdString)
{
	// Constructs the actual thread object. It will begin execution immediately
	// If you've passed in any inputs, set them up before calling this.
  TextStdStringPtr = &TextStdString;
	Thread = FRunnableThread::Create(this, TEXT("TTS Thread"));
    UE_LOG(LogTemp, Warning, TEXT("TTS Thread: Constructor"));
}


FTTSThread::~FTTSThread()
{
	if (Thread)
	{
		// Kill() is a blocking call, it waits for the thread to finish.
		// Hopefully that doesn't take too long
		Thread->Kill();
		delete Thread;
    UE_LOG(LogTemp, Warning, TEXT("TTS Thread: Killed"));

	}
}


#pragma endregion
// The code below will run on the new thread.


bool FTTSThread::Init()
{
	UE_LOG(LogTemp, Warning, TEXT("TTS Thread: Initialized"))

	// Return false if you want to abort the thread
	return true;
}


uint32 FTTSThread::Run()
{
  UE_LOG(LogTemp, Warning, TEXT("TTS Thread: Running"));
  ISpVoice* pVoice = NULL;
  if (FAILED(::CoInitialize(NULL)))
  {
    return false;
  }
  HRESULT hr = CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void**)&pVoice);
  if (SUCCEEDED(hr))
  {
    /*This only works if all the characters are single byte, i.e. ASCII or ISO-8859-1.
    Anything multi-byte will fail miserably, including UTF-8. However, we only use ASCII in our case.*/
    std::wstring stemp = std::wstring((*TextStdStringPtr).begin(), (*TextStdStringPtr).end());
    LPCWSTR TextLString = stemp.c_str();
    hr = pVoice->Speak(TextLString, SPF_IS_XML, NULL);
    pVoice->Release();
    pVoice = NULL;
  }
  ::CoUninitialize();
  return TRUE;
}