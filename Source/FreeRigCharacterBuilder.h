#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FreeRigComponent.h"
#include "FreeRigFaceTracker.h"
#include "FreeRigPSDImporter.h"
#include "FreeRigBoneDriver.h"
#include "FreeRigPhoneServer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FreeRigCharacterBuilder.generated.h"

UCLASS(BlueprintType)
class FREERIG_VT_API AFreeRigCharacter : public AActor
{
    GENERATED_BODY()

public:
    AFreeRigCharacter();

    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    bool BuildFromPSD(const FString& PSDPath);

    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    UFreeRigComponent* GetRigComponent() const { return RigComponent; }

    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    UFreeRigFaceTracker* GetFaceTracker() const { return FaceTracker; }

    // Start phone control - shows QR code
    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    static void StartPhoneControl(AFreeRigCharacter* Character, int32 Port = 8080);

    // Auto-rig from PSD
    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    static bool AutoRigFromPSD(const FString& PSDPath, const FString& JSONOutputPath);

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    USceneComponent* RootComponentRef;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    UFreeRigComponent* RigComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
    UFreeRigFaceTracker* FaceTracker;

    TMap<FString, USceneComponent*> BoneMap;
    UFreeRigBoneDriver* BoneDriver;

    void BuildBonesFromLayers(const FFreeRigPSDImportResult& PSDData);
    UStaticMeshComponent* CreateLayerPlane(const FFreeRigPSDLayer& Layer, USceneComponent* Parent, const FString& BoneName);
    void RegisterDefaultParameterBindings();
};

// Separate Blueprint Function Library for static utility functions
UCLASS()
class FREERIG_VT_API UFreeRigCharacterBuilder : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    static void StartPhoneServer(UFreeRigComponent* TargetRig, int32 Port = 8080);

    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    static void ShowQRCode(const FString& URL);
};