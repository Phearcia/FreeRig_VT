#include "FreeRigProgressUI.h"

void UFreeRigProgressUI::ShowProgress(const FString& Title)
{
    bIsVisible = true;
    UE_LOG(LogTemp, Log, TEXT("FreeRig Progress: %s"), *Title);
}

void UFreeRigProgressUI::UpdateProgress(float Progress, const FString& Status)
{
    if (OnProgressUpdate.IsBound())
    {
        OnProgressUpdate.Broadcast(Progress);
    }
    if (OnStatusUpdate.IsBound())
    {
        OnStatusUpdate.Broadcast(Status);
    }
    UE_LOG(LogTemp, Log, TEXT("FreeRig Progress: %.1f%% - %s"), Progress * 100.0f, *Status);
}

void UFreeRigProgressUI::HideProgress()
{
    bIsVisible = false;
    UE_LOG(LogTemp, Log, TEXT("FreeRig Progress: Hidden"));
}