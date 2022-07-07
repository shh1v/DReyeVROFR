#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "sapi.h" /* Text to Speech using Microsoft SAPI 5.3 */
#include <string>
/**
 *
 */
class /*GAMENAME_API*/ FTTSThread : public FRunnable
{
public:

	// Constructor, create the thread by calling this
	FTTSThread(std::string& TextStdString);

	// Destructor
	virtual ~FTTSThread() override;


	// Overriden from FRunnable
	// Do not call these functions youself, that will happen automatically
	bool Init() override; // Do your setup here, allocate memory, ect.
	uint32 Run() override; // Main data processing happens here


private:

	// Thread handle. Control the thread using this, with operators like Kill and Suspend
	FRunnableThread* Thread;
};