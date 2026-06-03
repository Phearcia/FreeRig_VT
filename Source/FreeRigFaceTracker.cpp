#include "FreeRigFaceTracker.h"
#include "FreeRigComponent.h"
#include "ILiveLinkClient.h"
#include "LiveLinkClient.h"
#include "Engine/Engine.h"
#include "FreeRigComponent.h" 
#include "Modules/ModuleManager.h"
#include "LiveLinkRole.h"

UFreeRigFaceTracker::UFreeRigFaceTracker()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
    LiveLinkClient = nullptr;
}

void UFreeRigFaceTracker::BeginPlay()
{
    Super::BeginPlay();

    // Find the rig component if not set
    if (!TargetRigComponent)
    {
        TargetRigComponent = GetOwner()->FindComponentByClass<UFreeRigComponent>();
        if (!TargetRigComponent)
        {
            UE_LOG(LogTemp, Warning, TEXT("FreeRigFaceTracker: No FreeRigComponent found on %s. Tracking will do nothing."), *GetOwner()->GetName());
        }
    }

    // Get Live Link client
    if (TrackingSource == EFreeRigTrackingSource::LiveLinkFace)
    {
        TryConnectLiveLink();
    }
}

void UFreeRigFaceTracker::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bTrackingEnabled || !TargetRigComponent)
    {
        return;
    }

    switch (TrackingSource)
    {
    case EFreeRigTrackingSource::LiveLinkFace:
        UpdateFromLiveLinkFace(DeltaTime);
        break;

    case EFreeRigTrackingSource::Manual:
        UpdateFromManualControl();
        break;

    case EFreeRigTrackingSource::MediaPipe:
    case EFreeRigTrackingSource::OpenSeeFace:
        // TODO: Implement MediaPipe and OpenSeeFace support
        UpdateFromManualControl(); // Fallback for now
        break;
    }

    ApplySmoothing(SmoothedValues, RawValues, DeltaTime);
    SendToRigComponent();
}

void UFreeRigFaceTracker::TryConnectLiveLink()
{
    // Get the LiveLink client module
    IModularFeatures& ModularFeatures = IModularFeatures::Get();
    if (!ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
    {
        UE_LOG(LogTemp, Warning, TEXT("FreeRigFaceTracker: LiveLink client module not available"));
        return;
    }

    LiveLinkClient = &ModularFeatures.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
    if (!LiveLinkClient)
    {
        UE_LOG(LogTemp, Warning, TEXT("FreeRigFaceTracker: Could not get LiveLink client"));
        return;
    }

    // Try to find the subject (5.7 API: GetSubjects takes bIncludeDisabled, bIncludeVirtual)
    TArray<FLiveLinkSubjectKey> Subjects = LiveLinkClient->GetSubjects(true, true);
    for (const FLiveLinkSubjectKey& Subject : Subjects)
    {
        if (Subject.SubjectName.ToString().Equals(LiveLinkSubjectName))
        {
            LiveLinkSubjectKey = Subject;
            bIsLiveLinkConnected = true;
            UE_LOG(LogTemp, Log, TEXT("FreeRigFaceTracker: Connected to LiveLink subject '%s'"), *LiveLinkSubjectName);
            return;
        }
    }

    bIsLiveLinkConnected = false;
    UE_LOG(LogTemp, Warning, TEXT("FreeRigFaceTracker: LiveLink subject '%s' not found. Make sure Live Link Face is running."), *LiveLinkSubjectName);
}

void UFreeRigFaceTracker::UpdateFromLiveLinkFace(float DeltaTime)
{
    // Reconnect attempts if disconnected
    if (!bIsLiveLinkConnected)
    {
        TimeSinceLastConnectionAttempt += DeltaTime;
        if (TimeSinceLastConnectionAttempt >= ConnectionAttemptInterval)
        {
            TimeSinceLastConnectionAttempt = 0.0f;
            TryConnectLiveLink();
        }
        return;
    }

    if (!LiveLinkClient)
    {
        return;
    }

    // Get Live Link data using EvaluateFrame_AnyThread
    FLiveLinkSubjectFrameData FrameData;
    if (!LiveLinkClient->EvaluateFrame_AnyThread(FLiveLinkSubjectName(*LiveLinkSubjectName), ULiveLinkRole::StaticClass(), FrameData))
    {
        // Failed to get frame, maybe disconnected
        bIsLiveLinkConnected = false;
        return;
    }

    // Extract blendshape curves from the frame data
    const FLiveLinkBaseStaticData* StaticData = FrameData.StaticData.Cast<FLiveLinkBaseStaticData>();
    const FLiveLinkBaseFrameData* Frame = FrameData.FrameData.Cast<FLiveLinkBaseFrameData>();

    if (StaticData && Frame)
    {
        // In 5.7, curve data is stored in the frame's property values
        // The exact structure depends on your LiveLink Face source
        // For now, we'll just keep raw values at zero and let manual control work

        // TODO: Parse the actual blendshape curves from Frame->PropertyValues
        // This requires understanding the specific property names from your Live Link Face source

        UE_LOG(LogTemp, Warning, TEXT("FreeRigFaceTracker: LiveLink Face curve parsing needs customization for your setup"));
    }
}

void UFreeRigFaceTracker::UpdateFromManualControl()
{
    RawValues.LeftEyeOpen = ManualLeftEyeOpen;
    RawValues.RightEyeOpen = ManualRightEyeOpen;
    RawValues.MouthOpen = ManualMouthOpen;
    RawValues.MouthSmile = ManualMouthSmile;
    RawValues.BrowUp = ManualBrowUp;
    RawValues.Blink = ManualBlink;
    RawValues.EyeLookX = 0.0f;
    RawValues.EyeLookY = 0.0f;
}

void UFreeRigFaceTracker::ApplySmoothing(FFaceValues& Output, const FFaceValues& Input, float DeltaTime)
{
    if (SmoothingAmount <= 0.0f)
    {
        Output = Input;
        return;
    }

    float Alpha = 1.0f - FMath::Pow(1.0f - SmoothingAmount, DeltaTime * 60.0f);

    Output.LeftEyeOpen = FMath::Lerp(Output.LeftEyeOpen, Input.LeftEyeOpen, Alpha);
    Output.RightEyeOpen = FMath::Lerp(Output.RightEyeOpen, Input.RightEyeOpen, Alpha);
    Output.MouthOpen = FMath::Lerp(Output.MouthOpen, Input.MouthOpen, Alpha);
    Output.MouthSmile = FMath::Lerp(Output.MouthSmile, Input.MouthSmile, Alpha);
    Output.BrowUp = FMath::Lerp(Output.BrowUp, Input.BrowUp, Alpha);
    Output.Blink = FMath::Lerp(Output.Blink, Input.Blink, Alpha);
    Output.EyeLookX = FMath::Lerp(Output.EyeLookX, Input.EyeLookX, Alpha);
    Output.EyeLookY = FMath::Lerp(Output.EyeLookY, Input.EyeLookY, Alpha);
}

void UFreeRigFaceTracker::SendToRigComponent()
{
    if (!TargetRigComponent)
    {
        return;
    }

    TargetRigComponent->UpdateFromFaceTracking(
        SmoothedValues.LeftEyeOpen,
        SmoothedValues.RightEyeOpen,
        SmoothedValues.MouthOpen,
        SmoothedValues.MouthSmile,
        SmoothedValues.BrowUp,
        SmoothedValues.Blink
    );
}

void UFreeRigFaceTracker::SetManualEyeOpen(float Left, float Right)
{
    ManualLeftEyeOpen = FMath::Clamp(Left, 0.0f, 1.0f);
    ManualRightEyeOpen = FMath::Clamp(Right, 0.0f, 1.0f);
}

void UFreeRigFaceTracker::SetManualMouth(float Open, float Smile)
{
    ManualMouthOpen = FMath::Clamp(Open, 0.0f, 1.0f);
    ManualMouthSmile = FMath::Clamp(Smile, 0.0f, 1.0f);
}

void UFreeRigFaceTracker::SetManualBrow(float Up)
{
    ManualBrowUp = FMath::Clamp(Up, 0.0f, 1.0f);
}

void UFreeRigFaceTracker::SetManualBlink(float Blink)
{
    ManualBlink = FMath::Clamp(Blink, 0.0f, 1.0f);
}

void UFreeRigFaceTracker::Calibrate()
{
    // Reset smoothing to current raw values
    RawValues = FFaceValues();
    SmoothedValues = FFaceValues();
    UE_LOG(LogTemp, Log, TEXT("FreeRigFaceTracker: Calibrated"));
}