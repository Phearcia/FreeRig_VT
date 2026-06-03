#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FreeRigQRCode.generated.h"

USTRUCT(BlueprintType)
struct FQRCodeResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    bool bSuccess = false;

    UPROPERTY(BlueprintReadOnly)
    UTexture2D* QRTexture = nullptr;

    UPROPERTY(BlueprintReadOnly)
    FString URL;
};

UCLASS()
class FREERIG_VT_API UFreeRigQRCode : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "FreeRig|QRCode")
    static FQRCodeResult GenerateQRCode(const FString& URL, int32 Size = 512);

    UFUNCTION(BlueprintCallable, Category = "FreeRig|QRCode")
    static void ShowQRCodeWindow(const FString& URL, const FString& Title = TEXT("Scan with Phone"));

    UFUNCTION(BlueprintCallable, Category = "FreeRig|QRCode")
    static void ShowPhoneConnectionQR(UFreeRigPhoneServer* PhoneServer);

    // Add these declarations
    static void ExportToPhone(const FString& ModelPath, int32 Port = 8080);
    static FString GenerateModelQR(const FString& ModelJSONPath);
};