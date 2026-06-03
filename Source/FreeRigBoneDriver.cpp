#include "FreeRigBoneDriver.h"
#include "FreeRigPSDImporter.h"
#include "FreeRigComponent.h"

UFreeRigBoneDriver::UFreeRigBoneDriver()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UFreeRigBoneDriver::LoadBoneData(const TArray<FFreeRigBoneData>& Bones)
{
    BoneMap.Empty();
    for (const FFreeRigBoneData& Bone : Bones)
    {
        BoneMap.Add(Bone.BoneName, Bone);

        FFreeRigBoneTransform DefaultTransform;
        DefaultTransform.BoneName = Bone.BoneName;
        DefaultTransform.Position = Bone.Position;
        DefaultTransform.Rotation = Bone.Rotation;
        DefaultTransform.Scale = Bone.Scale;
        CurrentTransforms.Add(Bone.BoneName, DefaultTransform);
    }
}

void UFreeRigBoneDriver::UpdateBonesFromParameters(UFreeRigComponent* RigComponent)
{
    if (!RigComponent) return;

    float EyeOpen = RigComponent->GetParameter(TEXT("EyeOpen"));
    float MouthOpen = RigComponent->GetParameter(TEXT("MouthOpen"));
    float MouthSmile = RigComponent->GetParameter(TEXT("MouthSmile"));
    float BrowUp = RigComponent->GetParameter(TEXT("BrowUp"));
    float Blink = RigComponent->GetParameter(TEXT("Blink"));

    // Apply blink override
    float FinalEyeOpen = Blink > 0.5f ? 0.0f : EyeOpen;

    for (auto& Pair : CurrentTransforms)
    {
        FFreeRigBoneTransform& Transform = Pair.Value;
        const FFreeRigBoneData* Bone = BoneMap.Find(Pair.Key);

        if (!Bone) continue;

        // Reset to default
        Transform.Scale = Bone->Scale;
        Transform.Rotation = Bone->Rotation;

        // Apply parameter-driven transforms
        switch (Bone->BoneType)
        {
        case EBoneType::EyeLeft:
        case EBoneType::EyeRight:
            UpdateEyeBone(Transform, *Bone, FinalEyeOpen);
            break;
        case EBoneType::Mouth:
            UpdateMouthBone(Transform, *Bone, MouthOpen);
            break;
        case EBoneType::EyebrowLeft:
        case EBoneType::EyebrowRight:
            UpdateBrowBone(Transform, *Bone, BrowUp);
            break;
        default:
            break;
        }
    }
}

void UFreeRigBoneDriver::UpdateEyeBone(FFreeRigBoneTransform& Transform, const FFreeRigBoneData& Bone, float EyeOpen)
{
    // Scale Y based on eye open (1.0 = fully open, 0.0 = closed)
    float EyeScale = FMath::Lerp(0.1f, 1.0f, EyeOpen);
    Transform.Scale.Y = EyeScale;
}

void UFreeRigBoneDriver::UpdateMouthBone(FFreeRigBoneTransform& Transform, const FFreeRigBoneData& Bone, float MouthOpen)
{
    // Scale Y based on mouth open
    float MouthScale = FMath::Lerp(0.2f, 1.0f, MouthOpen);
    Transform.Scale.Y = MouthScale;
}

void UFreeRigBoneDriver::UpdateBrowBone(FFreeRigBoneTransform& Transform, const FFreeRigBoneData& Bone, float BrowUp)
{
    // Rotate brows based on brow up (positive rotation = raised)
    Transform.Rotation = FMath::Lerp(0.0f, 15.0f, BrowUp);
}

FFreeRigBoneTransform UFreeRigBoneDriver::GetBoneTransform(const FString& BoneName) const
{
    if (const FFreeRigBoneTransform* Transform = CurrentTransforms.Find(BoneName))
    {
        return *Transform;
    }
    return FFreeRigBoneTransform();
}