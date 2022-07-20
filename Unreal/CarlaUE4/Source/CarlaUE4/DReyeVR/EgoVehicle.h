#pragma once

#include "Camera/CameraComponent.h"                   // UCameraComponent
#include "Carla/Actor/DReyeVRCustomActor.h"           // ADReyeVRCustomActor
#include "Carla/Game/CarlaEpisode.h"                  // CarlaEpisode
#include "Carla/Sensor/DReyeVRData.h"                 // DReyeVR namespace
#include "Carla/Vehicle/CarlaWheeledVehicle.h"        // ACarlaWheeledVehicle
#include "Carla/Vehicle/WheeledVehicleAIController.h" // AWheeledVehicleAIController
#include "Components/AudioComponent.h"                // UAudioComponent
#include "Components/InputComponent.h"                // InputComponent
#include "Components/PlanarReflectionComponent.h"     // Planar Reflection
#include "Components/SceneComponent.h"                // USceneComponent
#include "CoreMinimal.h"                              // Unreal functions
#include "DReyeVRUtils.h"                             // ReadConfigValue
#include "EgoSensor.h"                                // AEgoSensor
#include "FlatHUD.h"                                  // ADReyeVRHUD
#include "ImageUtils.h"                               // CreateTexture2D
#include "LevelScript.h"                              // ADReyeVRLevel
#include "WheeledVehicle.h"                           // VehicleMovementComponent
#include <stdio.h>
#include <vector>
#include "EgoVehicle.generated.h"

class ADReyeVRLevel;
class ADReyeVRPawn;

UCLASS()
class CARLAUE4_API AEgoVehicle : public ACarlaWheeledVehicle
{
    GENERATED_BODY()

    friend class ADReyeVRPawn;

  public:
    // Sets default values for this pawn's properties
    AEgoVehicle(const FObjectInitializer &ObjectInitializer);

    void ReadConfigVariables();

    virtual void Tick(float DeltaTime) override; // called automatically

    // Setters from external classes
    void SetLevel(ADReyeVRLevel *Level);
    void SetPawn(ADReyeVRPawn *Pawn);
    void SetVolume(const float VolumeIn);

    // Getters
    FVector GetCameraOffset() const;
    FVector GetCameraPosn() const;
    FVector GetNextCameraPosn(const float DeltaSeconds) const;
    FRotator GetCameraRot() const;
    const UCameraComponent *GetCamera() const;
    UCameraComponent *GetCamera();
    const DReyeVR::UserInputs &GetVehicleInputs() const;

    // autopilot API
    void SetAutopilot(const bool AutopilotOn);
    bool GetAutopilotStatus() const;
    /// TODO: add custom routes for autopilot

    // Play sounds
    void PlayGearShiftSound(const float DelayBeforePlay = 0.f) const;
    void PlayTurnSignalSound(const float DelayBeforePlay = 0.f) const;
    void PlayTORAlertSound(const float DelayBeforePlay = 0.f) const;

  protected:
    // Called when the game starts (spawned) or ends (destroyed)
    virtual void BeginPlay() override;
    virtual void BeginDestroy() override;

    // World variables
    class UWorld *World;

  private:
    void Register(); // function to register the AEgoVehicle with Carla's ActorRegistry

    ////////////////:CAMERA:////////////////
    void ConstructCameraRoot(); // needs to be called in the constructor
    UPROPERTY(Category = Camera, EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class USceneComponent *VRCameraRoot;
    UPROPERTY(Category = Camera, EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UCameraComponent *FirstPersonCam;
    FVector CameraLocnInVehicle{21.0f, -40.0f, 120.0f}; // depends on vehicle mesh (units in cm)

    ////////////////:SENSOR:////////////////
    void ReplayTick();
    void InitSensor();
    class AEgoSensor *EgoSensor; // custom sensor helper that holds logic for extracting useful data
    void UpdateSensor(const float DeltaTime);
    FVector CombinedGaze, CombinedOrigin;
    FVector LeftGaze, LeftOrigin;
    FVector RightGaze, RightOrigin;

    ///////////////:DREYEVRPAWN://///////////
    class ADReyeVRPawn *Pawn = nullptr;

    ////////////////:MIRRORS:////////////////
    void ConstructMirrors();
    struct MirrorParams
    {
        bool Enabled;
        FVector MirrorPos, MirrorScale, ReflectionPos, ReflectionScale;
        FRotator MirrorRot, ReflectionRot;
        float ScreenPercentage;
        FString Name;
        void Initialize(class UStaticMeshComponent *SM, class UPlanarReflectionComponent *Reflection,
                        class USkeletalMeshComponent *VehicleMesh);
    };
    struct MirrorParams RearMirrorParams, LeftMirrorParams, RightMirrorParams;
    UPROPERTY(Category = Mirrors, EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UStaticMeshComponent *RightMirrorSM;
    UPROPERTY(Category = Mirrors, EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UPlanarReflectionComponent *RightReflection;
    UPROPERTY(Category = Mirrors, EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UStaticMeshComponent *LeftMirrorSM;
    UPROPERTY(Category = Mirrors, EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UPlanarReflectionComponent *LeftReflection;
    UPROPERTY(Category = Mirrors, EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UStaticMeshComponent *RearMirrorSM;
    UPROPERTY(Category = Mirrors, EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UPlanarReflectionComponent *RearReflection;
    // rear mirror chassis (dynamic)
    UPROPERTY(Category = Mirrors, EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UStaticMeshComponent *RearMirrorChassisSM;
    FVector RearMirrorChassisPos, RearMirrorChassisScale;
    FRotator RearMirrorChassisRot;

    ////////////////:AICONTROLLER:////////////////
    class AWheeledVehicleAIController *AI_Player = nullptr;
    void InitAIPlayer();
    bool bAutopilotEnabled = false;

    ////////////////:INPUTS:////////////////
    /// NOTE: since there are so many functions here, they are defined in EgoInputs.cpp
    struct DReyeVR::UserInputs VehicleInputs; // struct for user inputs
    // Vehicle control functions
    void SetSteering(const float SteeringInput);
    void SetThrottle(const float ThrottleInput);
    void SetBrake(const float BrakeInput);
    bool bReverse;

    // "button presses" should have both a "Press" and "Release" function
    // And, if using the logitech plugin, should also have an "is rising edge" bool so they can only
    // be pressed after being released (cant double press w/ no release)
    // Reverse toggle
    void PressReverse();
    void ReleaseReverse();
    bool bCanPressReverse = true;
    // turn signals
    bool bEnableTurnSignalAction = true; // tune with "EnableTurnSignalAction" in config
    // left turn signal
    void PressTurnSignalL();
    void ReleaseTurnSignalL();
    float LeftSignalTimeToDie; // how long until the blinkers go out
    bool bCanPressTurnSignalL = true;
    // right turn signal
    void PressTurnSignalR();
    void ReleaseTurnSignalR();
    float RightSignalTimeToDie; // how long until the blinkers go out
    bool bCanPressTurnSignalR = true;
    // handbrake
    void PressHandbrake();
    void ReleaseHandbrake();
    bool bCanPressHandbrake = true;

    // Camera control functions (offset by some amount)
    void CameraPositionAdjust(const FVector &displacement);
    void CameraFwd();
    void CameraBack();
    void CameraLeft();
    void CameraRight();
    void CameraUp();
    void CameraDown();

    // Vehicle parameters
    float ScaleSteeringInput;
    float ScaleThrottleInput;
    float ScaleBrakeInput;

    ////////////////:SOUNDS:////////////////
    void ConstructEgoSounds(); // needs to be called in the constructor
    UPROPERTY(Category = "Audio", EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UAudioComponent *GearShiftSound; // nice for toggling reverse
    UPROPERTY(Category = "Audio", EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UAudioComponent *TurnSignalSound; // good for turn signals

    ////////////////:LEVEL:////////////////
    void TickLevel(float DeltaSeconds);
    class ADReyeVRLevel *DReyeVRLevel;

    ////////////////:DASH:////////////////
    // Text Render components (Like the HUD but part of the mesh and works in VR)
    void ConstructDashText(); // needs to be called in the constructor
    UPROPERTY(Category = "Dash", EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UTextRenderComponent *Speedometer;
    UPROPERTY(Category = "Dash", EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UTextRenderComponent *TurnSignals;
    float TurnSignalDuration; // time in seconds
    UPROPERTY(Category = "Dash", EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UTextRenderComponent *GearShifter;
    void UpdateDash(); // This method will either call RSVP or STD method
    FVector DashboardLocnInVehicle{110, 0, 105}; // can change via params
    bool bUseMPH;
    float SpeedometerScale; // scale from CM/s to MPH or KPH depending on bUseMPH

    // Get text from local text and settings file
    void RetriveText();
    void ReadSettingsFile();
    TArray<FString> TextWordsArray;
    void SetWPM(int32 WPM);

    // Common variables/methods for STP and RSVP techniques
    const float ExtraPause = 1.f;
    FString PathToTextFile = FPaths::ProjectContentDir() / TEXT("ConfigFiles/Text1.txt");
    class UTextRenderComponent *TextDisplay;
    class UStaticMeshComponent *HUD;
    void ConstructInterface();
    void EnableTextToSpeech();
    bool bThreadInit = false;
    bool bRSVP = false; // WARNING: This should ONLY be modified before starting the program (for now)
    bool bIsFirst = true;
    int32 EndIndex = 0; // Index of the first word to start a new sentence.
    float FutureTimeStamp;

    // Text to Speech
    bool bTTS = false; // WARNING: This should ONLY be modified before starting the program (for now)
    int32 WPM; // Retrived from the settings file. Default value may be set in case of reading failure
    std::string TextStdString;


    // Scrolling Text Display (STP)
    /*******************************************
     * Total Words per minute = 200
     * Each line has 4 words.
     * Total lines = (200/4) = UPPER(50) = 50
     * Initial lines = 6
     * Left out lines to display = 50 - 6 = 44
     * so, in the 60 seconds, one has to display 44 more lines
     * the display will shift one line in (44/60) seconds = 0.7333 seconds.
     * *******************************************/
    const int32 CharacterLimit = 23;
    float LineShiftInterval = 0.7333f;
    TArray<FString> CurrentLines; // This will store the current lines that are displayed on HUD.
    FString GenerateSentence();
    void STP(); // Will be called in UpdateDash()
    void SetTextSTP(); // Will generate a paragraph from CurrentLines array and set text on HUD.
    int32 EmptyGeneratedTexts = 0; // This will start incerementing once reading task is over
    // Rapid Serial Visual Presentation Technique
    float NextWordInterval = 0.3f;
    void RSVP();

    // Take-Over Requests
    void SendTORSignal(const FString& signal);      // This method will send signal to PythonAPI to execute TOR
    void DisplayTORAlert();                         // This will destroy HUD and display TOR alert
    void ResumeAIcontrol();                         // Handing back control to AI when situation in ODD
    bool bIsNDRTComplete = false;                   // To stop calling STP() and RSVP()
    void StopTORAlert();                            // Destory TOR message and stop alert sound
    class UAudioComponent* TORAlertSound;           // For TOR alert sound


    ////////////////:STEERINGWHEEL:////////////////
    void ConstructSteeringWheel(); // needs to be called in the constructor
    UPROPERTY(Category = Steering, EditDefaultsOnly, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    class UStaticMeshComponent *SteeringWheel;
    void TickSteeringWheel(const float DeltaTime);
    FVector InitWheelLocation;
    FRotator InitWheelRotation;
    float MaxSteerAngleDeg;
    float MaxSteerVelocity;
    float SteeringAnimScale;

    ////////////////:OTHER:////////////////

    // Actor registry
    int EgoVehicleID;
    UCarlaEpisode *Episode = nullptr;

    // Other
    void DebugLines() const;
    bool bDrawDebugEditor = false;
};
