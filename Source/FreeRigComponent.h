#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PaperFlipbookComponent.h"
#include "FreeRigComponent.generated.h"

// Forward declaration
class UFreeRigBoneDriver;

USTRUCT(BlueprintType)
struct FFreeRigParameter
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Name;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Min = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Max = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DefaultValue = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    float CurrentValue = 0.0f;
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class FREERIG_VT_API UFreeRigComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UFreeRigComponent();

    // Load a rigged model from JSON exported by the editor
    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    bool LoadModelFromJSON(const FString& JsonPath);

    // Set parameter value (drives blendshapes, bone transforms, etc.)
    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    void SetParameter(const FString& Name, float Value);

    // Get current parameter value
    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    float GetParameter(const FString& Name) const;

    // Convenience: Drive common face parameters from tracking data
    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    void UpdateFromFaceTracking(float LeftEyeOpen, float RightEyeOpen, float MouthOpen, float MouthSmile, float BrowUp, float Blink);

    // Get all parameters for UI display
    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    TArray<FFreeRigParameter> GetAllParameters() const;

    // Reset all parameters to default
    UFUNCTION(BlueprintCallable, Category = "FreeRig")
    void ResetAllParameters();

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    // Map of parameter name to parameter data
    UPROPERTY()
    TMap<FString, FFreeRigParameter> Parameters;

    // The visual representation
    UPROPERTY()
    UPaperFlipbookComponent* FlipbookComponent;

    // Loaded texture atlas
    UPROPERTY()
    UTexture2D* TextureAtlas;

    // Internal: apply current parameters to animation
    void ApplyParametersToAnimation();

    // Internal: load and parse the rig definition
    bool ParseRigJSON(const FString& JsonString);

    // Smooth interpolation
    TMap<FString, float> TargetValues;
    TMap<FString, float> CurrentInterpValues;

    // Interpolation speed (units per second)
    float InterpolationSpeed = 15.0f;

    UPROPERTY()
    UFreeRigBoneDriver* BoneDriver;

    void ApplyParametersToBones();
    void AddDefaultParameters();
};