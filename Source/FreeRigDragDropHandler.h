#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "FreeRigDragDropHandler.generated.h"

UCLASS()
class FREERIG_VT_API UFreeRigDragDropHandler : public UObject
{
    GENERATED_BODY()

public:
    static void RegisterDragDropHandler();
    static void OnFilesDropped(const TArray<FString>& FilePaths);
};