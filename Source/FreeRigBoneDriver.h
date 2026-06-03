#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FreeRigPSDImporter.h"
#include "FreeRigBoneDriver.generated.h"

// Forward declaration
class UFreeRigComponent;

USTRUCT(BlueprintType)
struct FFreeRigBoneTransform
{
    GENERATED_BODY()

    UPROPERTY()
    FString BoneName;

    UPROPERTY()
    FVector2D Position;

    UPROPERTY()
    float Rotation = 0.0f;

    UPROPERTY()
    FVector2D Scale = FVector2D(1.0f, 1.0f);
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class FREERIG_VT_API UFreeRigBoneDriver : public UActorComponent
{
    GENERATED_BODY()

public:
    UFreeRigBoneDriver();

    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    void LoadBoneData(const TArray<FFreeRigBoneData>& Bones);

    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    void UpdateBonesFromParameters(UFreeRigComponent* RigComponent);

    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    FFreeRigBoneTransform GetBoneTransform(const FString& BoneName) const;

private:
    UPROPERTY()
    TMap<FString, FFreeRigBoneData> BoneMap;

    UPROPERTY()
    TMap<FString, FFreeRigBoneTransform> CurrentTransforms;

    void UpdateEyeBone(FFreeRigBoneTransform& Transform, const FFreeRigBoneData& Bone, float EyeOpen);
    void UpdateMouthBone(FFreeRigBoneTransform& Transform, const FFreeRigBoneData& Bone, float MouthOpen);
    void UpdateBrowBone(FFreeRigBoneTransform& Transform, const FFreeRigBoneData& Bone, float BrowUp);
};