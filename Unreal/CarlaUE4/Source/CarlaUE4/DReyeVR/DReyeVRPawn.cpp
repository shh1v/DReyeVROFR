#include "DReyeVRPawn.h"
#include "DReyeVRUtils.h"                      // ProjectGazeToScreen
#include "HeadMountedDisplayFunctionLibrary.h" // SetTrackingOrigin, GetWorldToMetersScale
#include "HeadMountedDisplayTypes.h"           // ESpectatorScreenMode

ADReyeVRPawn::ADReyeVRPawn(const FObjectInitializer &ObjectInitializer) : Super(ObjectInitializer)
{
    // this actor (pawn) ticks BEFORE the physics simulation, hence before EgoVehicle tick
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;

    auto *RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("DReyeVR_RootComponent"));
    SetRootComponent(RootComponent);

    // auto possess player 0 (this automatically calls Possess, which calls SetupInputComponent)
    AutoPossessPlayer = EAutoReceiveInput::Player0;

    // read params
    ReadConfigVariables();

    // spawn and construct the first person camera
    ConstructCamera();
}

void ADReyeVRPawn::ReadConfigVariables()
{
    // camera
    ReadConfigValue("Camera", "FieldOfView", FieldOfView);
    ReadConfigValue("Camera", "ScreenPercentage", ScreenPercentage);
    ReadConfigValue("Camera", "VignetteIntensity", VignetteIntensity);
    ReadConfigValue("Camera", "BloomIntensity", BloomIntensity);
    ReadConfigValue("Camera", "SceneFringeIntensity", SceneFringeIntensity);
    ReadConfigValue("Camera", "LensFlareIntensity", LensFlareIntensity);
    ReadConfigValue("Camera", "GrainIntensity", GrainIntensity);
    ReadConfigValue("Camera", "MotionBlurIntensity", MotionBlurIntensity);

    // input scaling
    ReadConfigValue("VehicleInputs", "InvertMouseY", InvertMouseY);
    ReadConfigValue("VehicleInputs", "ScaleMouseY", ScaleMouseY);
    ReadConfigValue("VehicleInputs", "ScaleMouseX", ScaleMouseX);

    // HUD
    ReadConfigValue("EgoVehicleHUD", "HUDScaleVR", HUDScaleVR);
    ReadConfigValue("EgoVehicleHUD", "DrawFPSCounter", bDrawFPSCounter);
    ReadConfigValue("EgoVehicleHUD", "DrawFlatReticle", bDrawFlatReticle);
    ReadConfigValue("EgoVehicleHUD", "ReticleSize", ReticleSize);
    ReadConfigValue("EgoVehicleHUD", "DrawGaze", bDrawGaze);
    ReadConfigValue("EgoVehicleHUD", "DrawSpectatorReticle", bDrawSpectatorReticle);
    ReadConfigValue("EgoVehicleHUD", "EnableSpectatorScreen", bEnableSpectatorScreen);

    // wheel hardware
    ReadConfigValue("Hardware", "DeviceIdx", WheelDeviceIdx);
    ReadConfigValue("Hardware", "LogUpdates", bLogLogitechWheel);
}

void ADReyeVRPawn::ConstructCamera()
{
    // Create a camera and attach to root component
    FirstPersonCam = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCam"));
    FirstPersonCam->PostProcessSettings = CreatePostProcessingParams();
    FirstPersonCam->bUsePawnControlRotation = false; // free for VR movement
    FirstPersonCam->bLockToHmd = true;               // lock orientation and position to HMD
    FirstPersonCam->FieldOfView = FieldOfView;       // editable
    FirstPersonCam->SetupAttachment(RootComponent);
}

FPostProcessSettings ADReyeVRPawn::CreatePostProcessingParams() const
{
    // modifying from here: https://docs.unrealengine.com/4.27/en-US/API/Runtime/Engine/Engine/FPostProcessSettings/
    FPostProcessSettings PP;
    PP.bOverride_VignetteIntensity = true;
    PP.VignetteIntensity = VignetteIntensity;

    PP.bOverride_ScreenPercentage = true;
    PP.ScreenPercentage = ScreenPercentage;

    PP.bOverride_BloomIntensity = true;
    PP.BloomIntensity = BloomIntensity;

    PP.bOverride_SceneFringeIntensity = true;
    PP.SceneFringeIntensity = SceneFringeIntensity;

    PP.bOverride_LensFlareIntensity = true;
    PP.LensFlareIntensity = LensFlareIntensity;

    PP.bOverride_GrainIntensity = true;
    PP.GrainIntensity = GrainIntensity;

    PP.bOverride_MotionBlurAmount = true;
    PP.MotionBlurAmount = MotionBlurIntensity;
    return PP;
}

void ADReyeVRPawn::BeginPlay()
{
    Super::BeginPlay();

    World = GetWorld();
    ensure(World != nullptr);
}

void ADReyeVRPawn::BeginEgoVehicle(AEgoVehicle *Vehicle, UWorld *World, APlayerController *PlayerIn)
{
    /// NOTE: this should be run very early!
    // before anything that needs the EgoVehicle pointer (since this initializes it!)

    EgoVehicle = Vehicle;
    ensure(EgoVehicle != nullptr);
    EgoVehicle->SetPawn(this);

    Player = PlayerIn;
    ensure(Player != nullptr);

    // register inputs that require EgoVehicle
    ensure(InputComponent != nullptr);
    SetupEgoVehicleInputComponent(InputComponent, EgoVehicle);

    FirstPersonCam->RegisterComponentWithWorld(World);

    // Setup the HUD
    InitFlatHUD(Player);
}

void ADReyeVRPawn::BeginDestroy()
{
    Super::BeginDestroy();

    if (bIsLogiConnected)
        DestroyLogiWheel(false);
}

void ADReyeVRPawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Tick SteamVR
    TickSteamVR();

    // Tick the logitech wheel
    TickLogiWheel();
}

/// ========================================== ///
/// ----------------:STEAMVR:----------------- ///
/// ========================================== ///

void ADReyeVRPawn::InitSteamVR()
{
    bIsHMDConnected = UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled();
    if (bIsHMDConnected)
    {
        FString HMD_Name = UHeadMountedDisplayFunctionLibrary::GetHMDDeviceName().ToString();
        FString HMD_Version = UHeadMountedDisplayFunctionLibrary::GetVersionString();
        UE_LOG(LogTemp, Log, TEXT("HMD enabled: %s, version %s"), *HMD_Name, *HMD_Version);
        // Now we'll begin with setting up the VR Origin logic
        // this tracking origin is what moves the HMD camera to the right position
        UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(EHMDTrackingOrigin::Eye); // Also have Floor & Stage Level
        InitSpectator();
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("No head mounted device enabled!"));
    }
}

void ADReyeVRPawn::TickSteamVR()
{
    /// NOTE: this exists because UE4's package mode has a slight warm-up time for the SteamVR
    // plugin so it is not available to run UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled
    // on BeginPlay. UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayConnected() is a weaker
    // function that determines if a VR headset is connected at all (not connected + enabled) so we use
    // that here to try to enable the SteamVR plugin on every tick UNTIL it is enabled (bIsHMDConnected == true)
    if (!bIsHMDConnected && UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayConnected())
    {
        // try reinitializing steamvr if the headset is connected but not active
        InitSteamVR();
    }
}

void ADReyeVRPawn::InitReticleTexture()
{
    if (bIsHMDConnected)
        ReticleSize *= HUDScaleVR;

    /// NOTE: need to create transient like this bc of a UE4 bug in release mode
    // https://forums.unrealengine.com/development-discussion/rendering/1767838-fimageutils-createtexture2d-crashes-in-packaged-build
    TArray<FColor> ReticleSrc; // pixel values array for eye reticle texture
    if (bRectangularReticle)
    {
        GenerateSquareImage(ReticleSrc, ReticleSize, FColor(255, 0, 0, 128));
    }
    else
    {
        GenerateCrosshairImage(ReticleSrc, ReticleSize, FColor(255, 0, 0, 128));
    }
    ReticleTexture = UTexture2D::CreateTransient(ReticleSize, ReticleSize, PF_B8G8R8A8);
    void *TextureData = ReticleTexture->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(TextureData, ReticleSrc.GetData(), 4 * ReticleSize * ReticleSize);
    ReticleTexture->PlatformData->Mips[0].BulkData.Unlock();
    ReticleTexture->UpdateResource();
    // ReticleTexture = FImageUtils::CreateTexture2D(ReticleSize, ReticleSize, ReticleSrc, GetWorld(),
    //                                               "EyeReticleTexture", EObjectFlags::RF_Transient, params);

    check(ReticleTexture);
    check(ReticleTexture->Resource);
}

void ADReyeVRPawn::InitSpectator()
{
    if (bIsHMDConnected)
    {
        if (bEnableSpectatorScreen)
        {
            InitReticleTexture(); // generate array of pixel values
            check(ReticleTexture);
            UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenMode(ESpectatorScreenMode::TexturePlusEye);
            UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenTexture(ReticleTexture);
        }
        else
        {
            UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenMode(ESpectatorScreenMode::Disabled);
        }
    }
}

void ADReyeVRPawn::DrawSpectatorScreen(const FVector &GazeOrigin, const FVector &GazeDir)
{
    if (!bEnableSpectatorScreen || Player == nullptr || !bIsHMDConnected)
        return;

    FVector2D ReticlePos = ProjectGazeToScreen(Player, GetCamera(), GazeOrigin, GazeDir);

    /// NOTE: we like using this constant offset for visualization
    ReticlePos += FVector2D(0.f, -0.5f * ReticleSize); // move reticle up by size/2 (texture in quadrant 4)

    // calculate View size (of open window). Note this is not the same as resolution
    FIntPoint ViewSize;
    Player->GetViewportSize(ViewSize.X, ViewSize.Y);

    /// TODO: draw other things on the spectator screen?
    if (bDrawSpectatorReticle)
    {
        /// NOTE: the SetSpectatorScreenModeTexturePlusEyeLayout expects normalized positions on the screen
        // define min and max bounds (where the texture is actually drawn on screen)
        const FVector2D TextureRectMin = ReticlePos / ViewSize;                 // top left
        const FVector2D TextureRectMax = (ReticlePos + ReticleSize) / ViewSize; // bottom right
        UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenModeTexturePlusEyeLayout(
            FVector2D{0.f, 0.f}, // whole window (top left)
            FVector2D{1.f, 1.f}, // whole window (top -> bottom right)
            TextureRectMin,      // top left of texture
            TextureRectMax,      // bottom right of texture
            true,                // draw eye data as background
            false,               // clear w/ black
            true                 // use alpha
        );
    }
}

/// ========================================== ///
/// ----------------:FLATHUD:----------------- ///
/// ========================================== ///

void ADReyeVRPawn::InitFlatHUD(APlayerController *P)
{
    check(P);
    AHUD *Raw_HUD = P->GetHUD();
    FlatHUD = Cast<ADReyeVRHUD>(Raw_HUD);
    if (FlatHUD)
        FlatHUD->SetPlayer(P);
    else
        UE_LOG(LogTemp, Warning, TEXT("Unable to initialize DReyeVR HUD!"));
    // make sure to disable the flat hud when in VR (not supported, only displays on half of one eye screen)
    if (bIsHMDConnected)
    {
        bDrawFlatHud = false;
    }
}

void ADReyeVRPawn::DrawFlatHUD(float DeltaSeconds, const FVector &GazeOrigin, const FVector &GazeDir)
{
    if (FlatHUD == nullptr || Player == nullptr || bDrawFlatHud == false || bIsHMDConnected == true)
        return;

    const FVector &WorldPos = GetCamera()->GetComponentLocation();
    const FRotator &WorldRot = GetCamera()->GetComponentRotation();
    const float RayLengthScale = 10.f * 100.f; // 10m ray length
    const FVector TargetStart = WorldPos + WorldRot.RotateVector(GazeOrigin);
    const FVector TargetEnd = TargetStart + RayLengthScale * WorldRot.RotateVector(GazeDir);

    // calculate View size (of open window). Note this is not the same as resolution
    FIntPoint ViewSize;
    Player->GetViewportSize(ViewSize.X, ViewSize.Y);
    // Get eye tracker variables

    // Draw elements of the HUD
    if (bDrawFlatReticle) // Draw reticle on flat-screen HUD
    {
        const float Diameter = ReticleSize;
        const float Thickness = (ReticleSize / 2.f) / 10.f; // 10 % of radius
        // FlatHUD->DrawDynamicSquare(GazeEnd, Diameter, FColor(255, 0, 0, 255), Thickness);
        FlatHUD->DrawDynamicCrosshair(TargetEnd, Diameter, FColor(255, 0, 0, 255), true, Thickness);
    }
    if (bDrawFPSCounter)
    {
        FlatHUD->DrawDynamicText(FString::FromInt(int(1.f / DeltaSeconds)), FVector2D(ViewSize.X - 100, 50),
                                 FColor(0, 255, 0, 213), 2);
    }
    if (bDrawGaze)
    {
        // Draw line components in FlatHUD
        FlatHUD->DrawDynamicLine(TargetStart, TargetEnd, FColor::Red, 3.0f);
    }
}

/// ========================================== ///
/// ---------------:LOGITECH:----------------- ///
/// ========================================== ///

void ADReyeVRPawn::InitLogiWheel()
{
#if USE_LOGITECH_PLUGIN
    LogiSteeringInitialize(false);
    bIsLogiConnected = LogiIsConnected(WheelDeviceIdx); // get status of connected device
    if (bIsLogiConnected)
    {
        const size_t n = 1000; // name shouldn't be more than 1000 chars right?
        wchar_t *NameBuffer = (wchar_t *)malloc(n * sizeof(wchar_t));
        if (LogiGetFriendlyProductName(WheelDeviceIdx, NameBuffer, n) == false)
        {
            UE_LOG(LogTemp, Warning, TEXT("Unable to get Logi friendly name!"));
            NameBuffer = L"Unknown";
        }
        std::wstring wNameStr(NameBuffer, n);
        std::string NameStr(wNameStr.begin(), wNameStr.end());
        FString LogiName(NameStr.c_str());
        UE_LOG(LogTemp, Log, TEXT("Found a Logitech device (%s) connected on input %d"), *LogiName, WheelDeviceIdx);
        free(NameBuffer); // no longer needed
    }
    else
    {
        const FString LogiError = "Could not find Logitech device connected on input 0";
        const bool PrintToLog = false; // kinda annoying when flooding the logs with warning messages
        const bool PrintToScreen = true;
        const float ScreenDurationSec = 20.f;
        const FLinearColor MsgColour = FLinearColor(1, 0, 0, 1); // RED
        UKismetSystemLibrary::PrintString(World, LogiError, PrintToScreen, PrintToLog, MsgColour, ScreenDurationSec);
        if (PrintToLog)
            UE_LOG(LogTemp, Warning, TEXT("%s"), *LogiError); // Error is RED
    }
#endif
}

void ADReyeVRPawn::DestroyLogiWheel(bool DestroyModule)
{
#if USE_LOGITECH_PLUGIN
    if (bIsLogiConnected)
    {
        // stop any forces on the wheel (we only use spring force feedback)
        LogiStopSpringForce(WheelDeviceIdx);

        if (DestroyModule) // only destroy the module at the end of the game (not ego life)
        {
            // shutdown the entire module (dangerous bc lingering pointers)
            LogiSteeringShutdown();
        }
    }
#endif
}

void ADReyeVRPawn::TickLogiWheel()
{
    if (EgoVehicle == nullptr)
        return;
    // first try to initialize the Logi hardware if not currently active
    if (!bIsLogiConnected)
    {
        InitLogiWheel();
    }
#if USE_LOGITECH_PLUGIN
    bIsLogiConnected = LogiIsConnected(WheelDeviceIdx); // get status of connected device
    if (bIsLogiConnected && bOverrideInputsWithKbd == false)
    {
        // Taking logitech inputs for steering
        LogitechWheelUpdate();

        // Add Force Feedback to the hardware steering wheel when a LogitechWheel is used
        ApplyForceFeedback();
    }
    bOverrideInputsWithKbd = false; // disable for the next tick (unless held, which will set to true)
#endif
}

#if USE_LOGITECH_PLUGIN

// const std::vector<FString> VarNames = {"rgdwPOV[0]", "rgdwPOV[1]", "rgdwPOV[2]", "rgdwPOV[3]"};
const std::vector<FString> VarNames = {                                                    // 34 values
    "lX",           "lY",           "lZ",         "lRz",           "lRy",           "lRz", // variable names
    "rglSlider[0]", "rglSlider[1]", "rgdwPOV[0]", "rgbButtons[0]", "lVX",           "lVY",           "lVZ",
    "lVRx",         "lVRy",         "lVRz",       "rglVSlider[0]", "rglVSlider[1]", "lAX",           "lAY",
    "lAZ",          "lARx",         "lARy",       "lARz",          "rglASlider[0]", "rglASlider[1]", "lFX",
    "lFY",          "lFZ",          "lFRx",       "lFRy",          "lFRz",          "rglFSlider[0]", "rglFSlider[1]"};
/// NOTE: this is a debug function used to dump all the information we can regarding
// the Logitech wheel hardware we used since the exact buttons were not documented in
// the repo: https://github.com/HARPLab/LogitechWheelPlugin
void ADReyeVRPawn::LogLogitechPluginStruct(const struct DIJOYSTATE2 *Now)
{
    if (Old == nullptr)
    {
        Old = new struct DIJOYSTATE2;
        (*Old) = (*Now); // assign to the new (current) dijoystate struct
        return;          // initializing the Old struct ptr
    }
    const std::vector<int> NowVals = {
        Now->lX, Now->lY, Now->lZ, Now->lRx, Now->lRy, Now->lRz, Now->rglSlider[0], Now->rglSlider[1],
        // Converting unsigned int & unsigned char to int
        int(Now->rgdwPOV[0]), int(Now->rgbButtons[0]), Now->lVX, Now->lVY, Now->lVZ, Now->lVRx, Now->lVRy, Now->lVRz,
        Now->rglVSlider[0], Now->rglVSlider[1], Now->lAX, Now->lAY, Now->lAZ, Now->lARx, Now->lARy, Now->lARz,
        Now->rglASlider[0], Now->rglASlider[1], Now->lFX, Now->lFY, Now->lFZ, Now->lFRx, Now->lFRy, Now->lFRz,
        Now->rglFSlider[0], Now->rglFSlider[1]}; // 32 elements
    // Getting the (34) values from the old struct
    const std::vector<int> OldVals = {
        Old->lX, Old->lY, Old->lZ, Old->lRx, Old->lRy, Old->lRz, Old->rglSlider[0], Old->rglSlider[1],
        // Converting unsigned int & unsigned char to int
        int(Old->rgdwPOV[0]), int(Old->rgbButtons[0]), Old->lVX, Old->lVY, Old->lVZ, Old->lVRx, Old->lVRy, Old->lVRz,
        Old->rglVSlider[0], Old->rglVSlider[1], Old->lAX, Old->lAY, Old->lAZ, Old->lARx, Old->lARy, Old->lARz,
        Old->rglASlider[0], Old->rglASlider[1], Old->lFX, Old->lFY, Old->lFZ, Old->lFRx, Old->lFRy, Old->lFRz,
        Old->rglFSlider[0], Old->rglFSlider[1]};

    check(NowVals.size() == OldVals.size() && NowVals.size() == VarNames.size());

    // print any differences
    bool isDiff = false;
    for (size_t i = 0; i < NowVals.size(); i++)
    {
        if (NowVals[i] != OldVals[i])
        {
            if (!isDiff) // only gets triggered at MOST once
            {
                UE_LOG(LogTemp, Log, TEXT("Logging joystick at t=%.3f"), UGameplayStatics::GetRealTimeSeconds(World));
                isDiff = true;
            }
            UE_LOG(LogTemp, Log, TEXT("Triggered \"%s\" from %d to %d"), *(VarNames[i]), OldVals[i], NowVals[i]);
        }
    }

    // also check the 128 rgbButtons array
    for (size_t i = 0; i < 127; i++)
    {
        if (Old->rgbButtons[i] != Now->rgbButtons[i])
        {
            if (!isDiff) // only gets triggered at MOST once
            {
                UE_LOG(LogTemp, Log, TEXT("Logging joystick at t=%.3f"), UGameplayStatics::GetRealTimeSeconds(World));
                isDiff = true;
            }
            UE_LOG(LogTemp, Log, TEXT("Triggered \"rgbButtons[%d]\" from %d to %d"), int(i), int(OldVals[i]),
                   int(NowVals[i]));
        }
    }

    // assign the current joystate into the old one
    (*Old) = (*Now);
}

void ADReyeVRPawn::LogitechWheelUpdate()
{
    check(EgoVehicle);
    ensure(bOverrideInputsWithKbd == false); // kbd inputs should be false

    // only execute this in Windows, the Logitech plugin is incompatible with Linux
    if (LogiUpdate() == false) // update the logitech wheel
        UE_LOG(LogTemp, Warning, TEXT("Logitech wheel %d failed to update!"), WheelDeviceIdx);
    DIJOYSTATE2 *WheelState = LogiGetState(WheelDeviceIdx);
    if (bLogLogitechWheel)
        LogLogitechPluginStruct(WheelState);
    /// NOTE: obtained these from LogitechWheelInputDevice.cpp:~111
    // -32768 to 32767. -32768 = all the way to the left. 32767 = all the way to the right.
    const float WheelRotation = FMath::Clamp(float(WheelState->lX), -32767.0f, 32767.0f) / 32767.0f; // (-1, 1)
    // -32768 to 32767. 32767 = pedal not pressed. -32768 = pedal fully pressed.
    const float AccelerationPedal = fabs(((WheelState->lY - 32767.0f) / (65535.0f))); // (0, 1)
    // -32768 to 32767. Higher value = less pressure on brake pedal
    const float BrakePedal = fabs(((WheelState->lRz - 32767.0f) / (65535.0f))); // (0, 1)
    // -1 = not pressed. 0 = Top. 0.25 = Right. 0.5 = Bottom. 0.75 = Left.
    const float Dpad = fabs(((WheelState->rgdwPOV[0] - 32767.0f) / (65535.0f)));

    const float LogiThresh = 0.01f; // threshold for analog input "equality"

    // weird behaviour: "Pedals will output a value of 0.5 until the wheel/pedals receive any kind of input"
    // as per https://github.com/HARPLab/LogitechWheelPlugin
    if (bPedalsDefaulting)
    {
        // this bPedalsDefaulting flag is initially set to not send inputs when the pedals are "defaulting", once the
        // pedals/wheel is used (pressed/turned) once then this flag is ignored (false) for the remainder of the game
        if (!FMath::IsNearlyEqual(WheelRotation, 0.f, LogiThresh) ||      // wheel is not at 0 (rest)
            !FMath::IsNearlyEqual(AccelerationPedal, 0.5f, LogiThresh) || // accel pedal is pressed
            !FMath::IsNearlyEqual(BrakePedal, 0.5f, LogiThresh))          // brake pedal is pressed
        {
            bPedalsDefaulting = false;
        }
    }
    else
    {
        /// NOTE: directly calling the EgoVehicle functions
        if (EgoVehicle->GetAutopilotStatus() &&
            (FMath::IsNearlyEqual(WheelRotation, WheelRotationLast, LogiThresh) &&
             FMath::IsNearlyEqual(AccelerationPedal, AccelerationPedalLast, LogiThresh) &&
             FMath::IsNearlyEqual(BrakePedal, BrakePedalLast, LogiThresh)))
        {
            // let the autopilot drive if the user is not putting significant inputs
        }
        else
        {
            // take over the vehicle control completely
            EgoVehicle->SetSteering(WheelRotation);
            EgoVehicle->SetThrottle(AccelerationPedal);
            EgoVehicle->SetBrake(BrakePedal);
        }
    }
    // save the last values for the wheel & pedals
    WheelRotationLast = WheelRotation;
    AccelerationPedalLast = AccelerationPedal;
    BrakePedalLast = BrakePedal;

    // Button presses (turn signals, reverse)
    if (WheelState->rgbButtons[0] || WheelState->rgbButtons[1] || // Any of the 4 face pads
        WheelState->rgbButtons[2] || WheelState->rgbButtons[3])
        EgoVehicle->PressReverse();
    else
        EgoVehicle->ReleaseReverse();

    if (WheelState->rgbButtons[4])
        EgoVehicle->PressTurnSignalR();
    else
        EgoVehicle->ReleaseTurnSignalR();

    if (WheelState->rgbButtons[5])
        EgoVehicle->PressTurnSignalL();
    else
        EgoVehicle->ReleaseTurnSignalL();

    // if (WheelState->rgbButtons[23]) // big red button on right side of g923

    // EgoVehicle VRCamerRoot base position adjustment
    if (WheelState->rgdwPOV[0] == 0) // positive in X
        EgoVehicle->CameraFwd();
    else if (WheelState->rgdwPOV[0] == 18000) // negative in X
        EgoVehicle->CameraBack();
    else if (WheelState->rgdwPOV[0] == 9000) // positive in Y
        EgoVehicle->CameraRight();
    else if (WheelState->rgdwPOV[0] == 27000) // negative in Y
        EgoVehicle->CameraLeft();
    else if (WheelState->rgbButtons[19]) // positive in Z
        EgoVehicle->CameraUp();
    else if (WheelState->rgbButtons[20]) // negative in Z
        EgoVehicle->CameraDown();
}

void ADReyeVRPawn::ApplyForceFeedback() const
{
    check(EgoVehicle);

    // only execute this in Windows, the Logitech plugin is incompatible with Linux
    const float Speed = EgoVehicle->GetVelocity().Size(); // get magnitude of self (AActor's) velocity
    /// TODO: move outside this function (in tick()) to avoid redundancy
    if (bIsLogiConnected && LogiHasForceFeedback(WheelDeviceIdx))
    {
        const int OffsetPercentage = 0;      // "Specifies the center of the spring force effect"
        const int SaturationPercentage = 30; // "Level of saturation... comparable to a magnitude"
        const int CoeffPercentage = 100; // "Slope of the effect strength increase relative to deflection from Offset"
        LogiPlaySpringForce(WheelDeviceIdx, OffsetPercentage, SaturationPercentage, CoeffPercentage);
    }
    else
    {
        LogiStopSpringForce(WheelDeviceIdx);
    }
    /// NOTE: there are other kinds of forces as described in the LogitechWheelPlugin API:
    // https://github.com/HARPLab/LogitechWheelPlugin/blob/master/LogitechWheelPlugin/Source/LogitechWheelPlugin/Private/LogitechBWheelInputDevice.cpp
    // For example:
    /*
        Force Types
        0 = Spring				5 = Dirt Road
        1 = Constant			6 = Bumpy Road
        2 = Damper				7 = Slippery Road
        3 = Side Collision		8 = Surface Effect
        4 = Frontal Collision	9 = Car Airborne
    */
}
#endif

/// ========================================== ///
/// --------------:INPUTRELAY:---------------- ///
/// ========================================== ///

// the whole reason to possess this Pawn instance is to relay these inputs
// back to the EgoVehicle so that the EgoVehicle can still be controlled by an AI controller
// and these inputs will not be blocked.
// Else, (since only one AController can possess an actor) either the PlayerController
// or AIController would be in control exclusively.

void ADReyeVRPawn::SetupPlayerInputComponent(UInputComponent *PlayerInputComponent)
{
    /// NOTE: this function gets called once the pawn is possessed
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    this->InputComponent = PlayerInputComponent; // keep track of this
    check(InputComponent);

    /// NOTE: an Action is a digital input, an Axis is an analog input
    // steering and throttle analog inputs (axes)
    PlayerInputComponent->BindAxis("Steer_DReyeVR", this, &ADReyeVRPawn::SetSteeringKbd);
    PlayerInputComponent->BindAxis("Throttle_DReyeVR", this, &ADReyeVRPawn::SetThrottleKbd);
    PlayerInputComponent->BindAxis("Brake_DReyeVR", this, &ADReyeVRPawn::SetBrakeKbd);
    /// Mouse X and Y input for looking up and turning
    PlayerInputComponent->BindAxis("MouseLookUp_DReyeVR", this, &ADReyeVRPawn::MouseLookUp);
    PlayerInputComponent->BindAxis("MouseTurn_DReyeVR", this, &ADReyeVRPawn::MouseTurn);
}

void ADReyeVRPawn::SetupEgoVehicleInputComponent(UInputComponent *PlayerInputComponent, AEgoVehicle *EV)
{
    UE_LOG(LogTemp, Log, TEXT("Initializing EgoVehicle relay mechanisms"));
    // this function sets up the direct relay mechanisms to call EgoVehicle input functions
    check(PlayerInputComponent != nullptr);
    check(EV != nullptr);
    // button actions (press & release)
    PlayerInputComponent->BindAction("ToggleReverse_DReyeVR", IE_Pressed, EV, &AEgoVehicle::PressReverse);
    PlayerInputComponent->BindAction("TurnSignalRight_DReyeVR", IE_Pressed, EV, &AEgoVehicle::PressTurnSignalR);
    PlayerInputComponent->BindAction("TurnSignalLeft_DReyeVR", IE_Pressed, EV, &AEgoVehicle::PressTurnSignalL);
    PlayerInputComponent->BindAction("ToggleReverse_DReyeVR", IE_Released, EV, &AEgoVehicle::ReleaseReverse);
    PlayerInputComponent->BindAction("TurnSignalRight_DReyeVR", IE_Released, EV, &AEgoVehicle::ReleaseTurnSignalR);
    PlayerInputComponent->BindAction("TurnSignalLeft_DReyeVR", IE_Released, EV, &AEgoVehicle::ReleaseTurnSignalL);
    PlayerInputComponent->BindAction("HoldHandbrake_DReyeVR", IE_Pressed, EV, &AEgoVehicle::PressHandbrake);
    PlayerInputComponent->BindAction("HoldHandbrake_DReyeVR", IE_Released, EV, &AEgoVehicle::ReleaseHandbrake);
    // Camera position adjustments
    PlayerInputComponent->BindAction("CameraFwd_DReyeVR", IE_Pressed, EV, &AEgoVehicle::CameraFwd);
    PlayerInputComponent->BindAction("CameraBack_DReyeVR", IE_Pressed, EV, &AEgoVehicle::CameraBack);
    PlayerInputComponent->BindAction("CameraLeft_DReyeVR", IE_Pressed, EV, &AEgoVehicle::CameraLeft);
    PlayerInputComponent->BindAction("CameraRight_DReyeVR", IE_Pressed, EV, &AEgoVehicle::CameraRight);
    PlayerInputComponent->BindAction("CameraUp_DReyeVR", IE_Pressed, EV, &AEgoVehicle::CameraUp);
    PlayerInputComponent->BindAction("CameraDown_DReyeVR", IE_Pressed, EV, &AEgoVehicle::CameraDown);
}

#define CHECK_EGO_VEHICLE(FUNCTION)                                                                                    \
    if (EgoVehicle)                                                                                                    \
        FUNCTION;                                                                                                      \
    else                                                                                                               \
        UE_LOG(LogTemp, Error, TEXT("EgoVehicle is NULL!"));

void ADReyeVRPawn::SetThrottleKbd(const float ThrottleInput)
{
    if (ThrottleInput != 0)
    {
        bOverrideInputsWithKbd = true;
        CHECK_EGO_VEHICLE(EgoVehicle->SetThrottle(ThrottleInput))
    }
}

void ADReyeVRPawn::SetBrakeKbd(const float BrakeInput)
{
    if (BrakeInput != 0)
    {
        bOverrideInputsWithKbd = true;
        CHECK_EGO_VEHICLE(EgoVehicle->SetBrake(BrakeInput))
    }
}

void ADReyeVRPawn::SetSteeringKbd(const float SteeringInput)
{
    if (SteeringInput != 0)
    {
        bOverrideInputsWithKbd = true;
        CHECK_EGO_VEHICLE(EgoVehicle->SetSteering(SteeringInput))
    }
    else
    {
        // so the steering wheel does go to 0 when letting go
        ensure(EgoVehicle != nullptr);
        EgoVehicle->VehicleInputs.Steering = 0;
    }
}

/// ========================================== ///
/// -----------------:MOUSE:------------------ ///
/// ========================================== ///

/// NOTE: in UE4 rotators are of the form: {Pitch, Yaw, Roll} (stored in degrees)
/// We are basing the limits off of "Cervical Spine Functional Anatomy ad the Biomechanics of Injury":
// "The cervical spine's range of motion is approximately 80 deg to 90 deg of flexion, 70 deg of extension,
// 20 deg to 45 deg of lateral flexion, and up to 90 deg of rotation to both sides."
// (www.ncbi.nlm.nih.gov/pmc/articles/PMC1250253/)
/// NOTE: flexion = looking down to chest, extension = looking up , lateral = roll
/// ALSO: These functions are only used in non-VR mode, in VR you can move freely

void ADReyeVRPawn::MouseLookUp(const float mY_Input)
{
    if (mY_Input != 0.f)
    {
        const float ScaleY = (this->InvertMouseY ? 1 : -1) * this->ScaleMouseY; // negative Y is "normal" controls
        FRotator UpDir = this->GetCamera()->GetRelativeRotation() + FRotator(ScaleY * mY_Input, 0.f, 0.f);
        // get the limits of a human neck (only clamping pitch)
        const float MinFlexion = -85.f;
        const float MaxExtension = 70.f;
        UpDir.Pitch = FMath::Clamp(UpDir.Pitch, MinFlexion, MaxExtension);
        this->GetCamera()->SetRelativeRotation(UpDir);
    }
}

void ADReyeVRPawn::MouseTurn(const float mX_Input)
{
    if (mX_Input != 0.f)
    {
        const float ScaleX = this->ScaleMouseX;
        FRotator CurrentDir = this->GetCamera()->GetRelativeRotation();
        FRotator TurnDir = CurrentDir + FRotator(0.f, ScaleX * mX_Input, 0.f);
        // get the limits of a human neck (only clamping pitch)
        const float MinLeft = -90.f;
        const float MaxRight = 90.f; // may consider increasing to allow users to look through the back window
        TurnDir.Yaw = FMath::Clamp(TurnDir.Yaw, MinLeft, MaxRight);
        this->GetCamera()->SetRelativeRotation(TurnDir);
    }
}