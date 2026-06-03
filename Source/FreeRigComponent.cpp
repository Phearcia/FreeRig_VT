#include "FreeRigComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "FreeRigBoneDriver.h" 
#include "Engine/Engine.h"
#include "Materials/MaterialInstanceDynamic.h"

UFreeRigComponent::UFreeRigComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UFreeRigComponent::BeginPlay()
{
    Super::BeginPlay();

    // Add default parameters if none exist
    if (Parameters.Num() == 0)
    {
        AddDefaultParameters();
    }

    // Find or create flipbook component on owner
    FlipbookComponent = GetOwner()->FindComponentByClass<UPaperFlipbookComponent>();
    if (!FlipbookComponent)
    {
        FlipbookComponent = NewObject<UPaperFlipbookComponent>(GetOwner());
        FlipbookComponent->RegisterComponent();
        FlipbookComponent->AttachToComponent(GetOwner()->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
    }
}

void UFreeRigComponent::AddDefaultParameters()
{
    FFreeRigParameter Param;

    Param.Name = TEXT("EyeOpen");
    Param.Min = 0.0f;
    Param.Max = 1.0f;
    Param.DefaultValue = 0.5f;
    Param.CurrentValue = 0.5f;
    Parameters.Add(Param.Name, Param);
    TargetValues.Add(Param.Name, 0.5f);

    Param.Name = TEXT("MouthOpen");
    Param.Min = 0.0f;
    Param.Max = 1.0f;
    Param.DefaultValue = 0.0f;
    Param.CurrentValue = 0.0f;
    Parameters.Add(Param.Name, Param);
    TargetValues.Add(Param.Name, 0.0f);

    Param.Name = TEXT("MouthSmile");
    Param.Min = 0.0f;
    Param.Max = 1.0f;
    Param.DefaultValue = 0.0f;
    Param.CurrentValue = 0.0f;
    Parameters.Add(Param.Name, Param);
    TargetValues.Add(Param.Name, 0.0f);

    Param.Name = TEXT("BrowUp");
    Param.Min = 0.0f;
    Param.Max = 1.0f;
    Param.DefaultValue = 0.0f;
    Param.CurrentValue = 0.0f;
    Parameters.Add(Param.Name, Param);
    TargetValues.Add(Param.Name, 0.0f);

    Param.Name = TEXT("Blink");
    Param.Min = 0.0f;
    Param.Max = 1.0f;
    Param.DefaultValue = 0.0f;
    Param.CurrentValue = 0.0f;
    Parameters.Add(Param.Name, Param);
    TargetValues.Add(Param.Name, 0.0f);

    Param.Name = TEXT("LeftEyeOpen");
    Param.Min = 0.0f;
    Param.Max = 1.0f;
    Param.DefaultValue = 0.5f;
    Param.CurrentValue = 0.5f;
    Parameters.Add(Param.Name, Param);
    TargetValues.Add(Param.Name, 0.5f);

    Param.Name = TEXT("RightEyeOpen");
    Param.Min = 0.0f;
    Param.Max = 1.0f;
    Param.DefaultValue = 0.5f;
    Param.CurrentValue = 0.5f;
    Parameters.Add(Param.Name, Param);
    TargetValues.Add(Param.Name, 0.5f);
}

void UFreeRigComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Smooth interpolation for all parameters
    bool bNeedsUpdate = false;
    for (auto& Pair : TargetValues)
    {
        float* Current = CurrentInterpValues.Find(Pair.Key);
        if (Current)
        {
            float NewValue = FMath::FInterpTo(*Current, Pair.Value, DeltaTime, InterpolationSpeed);
            if (!FMath::IsNearlyEqual(NewValue, *Current, 0.005f))
            {
                *Current = NewValue;
                bNeedsUpdate = true;

                // Update parameter struct
                if (FFreeRigParameter* Param = Parameters.Find(Pair.Key))
                {
                    Param->CurrentValue = NewValue;
                }
            }
        }
        else
        {
            CurrentInterpValues.Add(Pair.Key, Pair.Value);
            bNeedsUpdate = true;
        }
    }

    if (bNeedsUpdate)
    {
        ApplyParametersToAnimation();
    }
}

bool UFreeRigComponent::LoadModelFromJSON(const FString& JsonPath)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonPath))
    {
        UE_LOG(LogTemp, Error, TEXT("FreeRig_VT: Failed to load JSON from %s"), *JsonPath);
        return false;
    }

    return ParseRigJSON(JsonString);
}

bool UFreeRigComponent::ParseRigJSON(const FString& JsonString)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("FreeRig_VT: Failed to parse JSON"));
        return false;
    }

    // Clear existing
    Parameters.Empty();
    TargetValues.Empty();
    CurrentInterpValues.Empty();

    // Load parameters array
    const TArray<TSharedPtr<FJsonValue>>* ParamsArray;
    if (JsonObject->TryGetArrayField(TEXT("parameters"), ParamsArray))
    {
        for (const auto& ParamValue : *ParamsArray)
        {
            const TSharedPtr<FJsonObject>& ParamObj = ParamValue->AsObject();
            if (ParamObj.IsValid())
            {
                FFreeRigParameter Param;
                Param.Name = ParamObj->GetStringField(TEXT("name"));
                Param.Min = ParamObj->GetNumberField(TEXT("min"));
                Param.Max = ParamObj->GetNumberField(TEXT("max"));
                Param.DefaultValue = ParamObj->GetNumberField(TEXT("default"));
                Param.CurrentValue = Param.DefaultValue;

                Parameters.Add(Param.Name, Param);
                TargetValues.Add(Param.Name, Param.DefaultValue);
            }
        }
    }

    // Load model metadata
    FString ModelName = JsonObject->GetStringField(TEXT("model_name"));
    int32 Version = JsonObject->GetIntegerField(TEXT("version"));

    // Load texture atlas info
    const TSharedPtr<FJsonObject>* TextureObj;
    if (JsonObject->TryGetObjectField(TEXT("texture_atlas"), TextureObj))
    {
        FString TexturePath = (*TextureObj)->GetStringField(TEXT("path"));
        // TODO: Load texture asset
        UE_LOG(LogTemp, Log, TEXT("FreeRig_VT: Texture atlas at %s"), *TexturePath);
    }

    // Load flipbook info
    const TSharedPtr<FJsonObject>* FlipbookObj;
    if (JsonObject->TryGetObjectField(TEXT("flipbook"), FlipbookObj))
    {
        FString FlipbookPath = (*FlipbookObj)->GetStringField(TEXT("path"));
        // TODO: Load flipbook asset
        UE_LOG(LogTemp, Log, TEXT("FreeRig_VT: Flipbook at %s"), *FlipbookPath);
    }

    // Load parameter bindings (which parameters affect which flipbook slots)
    const TArray<TSharedPtr<FJsonValue>>* BindingsArray;
    if (JsonObject->TryGetArrayField(TEXT("parameter_bindings"), BindingsArray))
    {
        for (const auto& BindingValue : *BindingsArray)
        {
            const TSharedPtr<FJsonObject>& BindingObj = BindingValue->AsObject();
            if (BindingObj.IsValid())
            {
                FString ParamName = BindingObj->GetStringField(TEXT("parameter"));
                FString TargetNode = BindingObj->GetStringField(TEXT("target"));
                // TODO: Store binding for runtime evaluation
                UE_LOG(LogTemp, Log, TEXT("FreeRig_VT: Binding %s -> %s"), *ParamName, *TargetNode);
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("FreeRig_VT: Loaded '%s' v%d with %d parameters"), *ModelName, Version, Parameters.Num());
    return true;
}

void UFreeRigComponent::SetParameter(const FString& Name, float Value)
{
    if (FFreeRigParameter* Param = Parameters.Find(Name))
    {
        float ClampedValue = FMath::Clamp(Value, Param->Min, Param->Max);
        TargetValues.FindOrAdd(Name, Param->DefaultValue) = ClampedValue;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("FreeRig_VT: Unknown parameter '%s'"), *Name);
    }
}

float UFreeRigComponent::GetParameter(const FString& Name) const
{
    if (const FFreeRigParameter* Param = Parameters.Find(Name))
    {
        return Param->CurrentValue;
    }
    return 0.0f;
}

void UFreeRigComponent::UpdateFromFaceTracking(float LeftEyeOpen, float RightEyeOpen, float MouthOpen, float MouthSmile, float BrowUp, float Blink)
{
    // Average both eyes for general eye open
    float EyeOpen = (LeftEyeOpen + RightEyeOpen) * 0.5f;

    // Blink overrides eye open when blinking
    float FinalEyeOpen = Blink > 0.5f ? 0.0f : EyeOpen;

    SetParameter(TEXT("EyeOpen"), FinalEyeOpen);
    SetParameter(TEXT("MouthOpen"), MouthOpen);
    SetParameter(TEXT("MouthSmile"), MouthSmile);
    SetParameter(TEXT("BrowUp"), BrowUp);
    SetParameter(TEXT("Blink"), Blink);

    // Optional: Separate left/right eye parameters if your rig supports it
    SetParameter(TEXT("LeftEyeOpen"), LeftEyeOpen);
    SetParameter(TEXT("RightEyeOpen"), RightEyeOpen);
}

TArray<FFreeRigParameter> UFreeRigComponent::GetAllParameters() const
{
    TArray<FFreeRigParameter> Result;
    Parameters.GenerateValueArray(Result);
    return Result;
}

void UFreeRigComponent::ResetAllParameters()
{
    for (auto& Pair : Parameters)
    {
        SetParameter(Pair.Key, Pair.Value.DefaultValue);
    }
}

void UFreeRigComponent::ApplyParametersToAnimation()
{
    // Update bones from parameters
    ApplyParametersToBones();

    // Fallback debug visualization
    if (FlipbookComponent)
    {
        float EyeOpen = GetParameter(TEXT("EyeOpen"));
        float MouthOpen = GetParameter(TEXT("MouthOpen"));
        float Blink = GetParameter(TEXT("Blink"));

        FLinearColor Color;
        Color.R = EyeOpen;
        Color.G = MouthOpen;
        Color.B = Blink;
        Color.A = 1.0f;
        FlipbookComponent->SetSpriteColor(Color);

        float Scale = 0.8f + (EyeOpen * 0.4f);
        GetOwner()->SetActorScale3D(FVector(Scale, Scale, 1.0f));
    }
}

void UFreeRigComponent::ApplyParametersToBones()
{
    if (BoneDriver)
    {
        BoneDriver->UpdateBonesFromParameters(this);
    }
}