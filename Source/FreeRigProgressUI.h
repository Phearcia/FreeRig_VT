#pragma once
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FreeRigProgressUI.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnProgressUpdate, float, Progress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStatusUpdate, const FString&, Status);

UCLASS()
class FREERIG_VT_API UFreeRigProgressUI : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    void ShowProgress(const FString& Title);

    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    void UpdateProgress(float Progress, const FString& Status);

    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    void HideProgress();

    UPROPERTY(BlueprintAssignable, Category = "FreeRig")
    FOnProgressUpdate OnProgressUpdate;

    UPROPERTY(BlueprintAssignable, Category = "FreeRig")
    FOnStatusUpdate OnStatusUpdate;

private:
    UPROPERTY()
    bool bIsVisible = false;
};