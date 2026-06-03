#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LiveLinkTypes.h"
#include "FreeRigFaceTracker.generated.h"

// Forward declarations
class UFreeRigComponent;
class ILiveLinkClient;
struct FLiveLinkSubjectFrameData;

UENUM(BlueprintType)
enum class EFreeRigTrackingSource : uint8
{
    LiveLinkFace        UMETA(DisplayName = "Live Link Face (iOS)"),
    MediaPipe           UMETA(DisplayName = "MediaPipe (Webcam)"),
    OpenSeeFace         UMETA(DisplayName = "OpenSeeFace"),
    Manual              UMETA(DisplayName = "Manual Control")
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class FREERIG_VT_API UFreeRigFaceTracker : public UActorComponent
{
    GENERATED_BODY()

public:
    UFreeRigFaceTracker();

    // Which tracking source to use
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FreeRig|Tracking")
    EFreeRigTrackingSource TrackingSource = EFreeRigTrackingSource::LiveLinkFace;

    // Live Link subject name (for Live Link Face)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FreeRig|Tracking")
    FString LiveLinkSubjectName = TEXT("LiveLinkFace");

    // The rig component to drive
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FreeRig|Tracking")
    UFreeRigComponent* TargetRigComponent;

    // Smoothing amount (0 = raw, 1 = very smooth)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FreeRig|Tracking", meta = (ClampMin = "0", ClampMax = "1"))
    float SmoothingAmount = 0.5f;

    // Enable/disable tracking
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FreeRig|Tracking")
    bool bTrackingEnabled = true;

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    // Live Link handling (5.7: pointer to interface)
    ILiveLinkClient* LiveLinkClient;
    FLiveLinkSubjectKey LiveLinkSubjectKey;
    bool bIsLiveLinkConnected = false;

    // Current face values (smoothed)
    struct FFaceValues
    {
        float LeftEyeOpen = 0.0f;
        float RightEyeOpen = 0.0f;
        float MouthOpen = 0.0f;
        float MouthSmile = 0.0f;
        float BrowUp = 0.0f;
        float Blink = 0.0f;
        float EyeLookX = 0.0f;
        float EyeLookY = 0.0f;
    };

    FFaceValues RawValues;
    FFaceValues SmoothedValues;

    // Connection attempt timer
    float TimeSinceLastConnectionAttempt = 0.0f;
    static constexpr float ConnectionAttemptInterval = 2.0f;

    // Internal methods
    void TryConnectLiveLink();
    void UpdateFromLiveLinkFace(float DeltaTime);
    void UpdateFromManualControl();
    void ApplySmoothing(FFaceValues& Output, const FFaceValues& Input, float DeltaTime);
    void SendToRigComponent();

    // Manual control values (for testing)
    UPROPERTY(VisibleAnywhere, Category = "FreeRig|Manual")
    float ManualLeftEyeOpen = 0.5f;

    UPROPERTY(VisibleAnywhere, Category = "FreeRig|Manual")
    float ManualRightEyeOpen = 0.5f;

    UPROPERTY(VisibleAnywhere, Category = "FreeRig|Manual")
    float ManualMouthOpen = 0.0f;

    UPROPERTY(VisibleAnywhere, Category = "FreeRig|Manual")
    float ManualMouthSmile = 0.0f;

    UPROPERTY(VisibleAnywhere, Category = "FreeRig|Manual")
    float ManualBrowUp = 0.0f;

    UPROPERTY(VisibleAnywhere, Category = "FreeRig|Manual")
    float ManualBlink = 0.0f;

public:
    // Manual control interface (callable from Blueprints)
    UFUNCTION(BlueprintCallable, Category = "FreeRig|Tracking")
    void SetManualEyeOpen(float Left, float Right);

    UFUNCTION(BlueprintCallable, Category = "FreeRig|Tracking")
    void SetManualMouth(float Open, float Smile);

    UFUNCTION(BlueprintCallable, Category = "FreeRig|Tracking")
    void SetManualBrow(float Up);

    UFUNCTION(BlueprintCallable, Category = "FreeRig|Tracking")
    void SetManualBlink(float Blink);

    // Force a one-time calibration
    UFUNCTION(BlueprintCallable, Category = "FreeRig|Tracking")
    void Calibrate();
};