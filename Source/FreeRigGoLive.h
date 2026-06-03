#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FreeRigGoLive.generated.h"

class AFreeRigCharacter;

UCLASS()
class FREERIG_VT_API UFreeRigGoLive : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    static void GoLive(AFreeRigCharacter* Character, const FString& ModelPath = TEXT(""));

    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    static bool PackageModel(const FString& ModelPath, const FString& OutputPath);
};