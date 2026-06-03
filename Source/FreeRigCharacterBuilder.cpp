#include "FreeRigCharacterBuilder.h"
#include "Engine/StaticMesh.h"
#include "FreeRigBoneDriver.h"
#include "FreeRigPhoneServer.h"
#include "EngineUtils.h"
#include "FreeRigQRCode.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"

AFreeRigCharacter::AFreeRigCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    RootComponentRef = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(RootComponentRef);

    RigComponent = CreateDefaultSubobject<UFreeRigComponent>(TEXT("RigComponent"));
    FaceTracker = CreateDefaultSubobject<UFreeRigFaceTracker>(TEXT("FaceTracker"));
    FaceTracker->TargetRigComponent = RigComponent;

    // Add BoneDriver
    BoneDriver = CreateDefaultSubobject<UFreeRigBoneDriver>(TEXT("BoneDriver"));
}

void AFreeRigCharacter::BeginPlay()
{
    Super::BeginPlay();
}

bool AFreeRigCharacter::BuildFromPSD(const FString& PSDPath)
{
    // Parse the PSD file
    FFreeRigPSDImportResult PSDData = UFreeRigPSDImporter::ParsePSDFile(PSDPath);

    if (!PSDData.bSuccess)
    {
        UE_LOG(LogTemp, Error, TEXT("FreeRig: Failed to parse PSD - %s"), *PSDData.ErrorMessage);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("FreeRig: Building character from %d layers"), PSDData.Layers.Num());

    // Build bones from layer data
    BuildBonesFromLayers(PSDData);

    // Register default parameter bindings
    RegisterDefaultParameterBindings();

    return true;
}



void AFreeRigCharacter::BuildBonesFromLayers(const FFreeRigPSDImportResult& PSDData)
{
    // Auto-detect bone hierarchy based on layer names
    TMap<FString, FString> ParentMap;
    TMap<FString, FVector2D> BonePositions;
    TMap<FString, TArray<FString>> LayerAttachments;

    for (const FFreeRigPSDLayer& Layer : PSDData.Layers)
    {
        FString LowerName = Layer.Name.ToLower();
        FString BoneName;
        FString ParentBone;

        // Heuristic: determine bone from layer name
        if (LowerName.Contains(TEXT("head")))
        {
            BoneName = TEXT("Head");
            ParentBone = TEXT("");
            BonePositions.FindOrAdd(BoneName) = FVector2D(Layer.X + Layer.Width / 2, Layer.Y + Layer.Height / 2);
        }
        else if (LowerName.Contains(TEXT("eye_left")) || LowerName.Contains(TEXT("lefteye")))
        {
            BoneName = TEXT("EyeLeft");
            ParentBone = TEXT("Head");
            BonePositions.FindOrAdd(BoneName) = FVector2D(Layer.X + Layer.Width / 2, Layer.Y + Layer.Height / 2);
        }
        else if (LowerName.Contains(TEXT("eye_right")) || LowerName.Contains(TEXT("righteye")))
        {
            BoneName = TEXT("EyeRight");
            ParentBone = TEXT("Head");
            BonePositions.FindOrAdd(BoneName) = FVector2D(Layer.X + Layer.Width / 2, Layer.Y + Layer.Height / 2);
        }
        else if (LowerName.Contains(TEXT("mouth")))
        {
            BoneName = TEXT("Mouth");
            ParentBone = TEXT("Head");
            BonePositions.FindOrAdd(BoneName) = FVector2D(Layer.X + Layer.Width / 2, Layer.Y + Layer.Height / 2);
        }
        else if (LowerName.Contains(TEXT("brow_left")) || LowerName.Contains(TEXT("leftbrow")))
        {
            BoneName = TEXT("BrowLeft");
            ParentBone = TEXT("Head");
            BonePositions.FindOrAdd(BoneName) = FVector2D(Layer.X + Layer.Width / 2, Layer.Y + Layer.Height / 2);
        }
        else if (LowerName.Contains(TEXT("brow_right")) || LowerName.Contains(TEXT("rightbrow")))
        {
            BoneName = TEXT("BrowRight");
            ParentBone = TEXT("Head");
            BonePositions.FindOrAdd(BoneName) = FVector2D(Layer.X + Layer.Width / 2, Layer.Y + Layer.Height / 2);
        }
        else if (LowerName.Contains(TEXT("hair")))
        {
            BoneName = TEXT("Hair");
            ParentBone = TEXT("Head");
            BonePositions.FindOrAdd(BoneName) = FVector2D(Layer.X + Layer.Width / 2, Layer.Y + Layer.Height / 2);
        }
        else if (LowerName.Contains(TEXT("body")) || LowerName.Contains(TEXT("torso")))
        {
            BoneName = TEXT("Body");
            ParentBone = TEXT("");
            BonePositions.FindOrAdd(BoneName) = FVector2D(Layer.X + Layer.Width / 2, Layer.Y + Layer.Height / 2);
        }
        else
        {
            // Attach unknown layers to head
            BoneName = TEXT("Head");
        }

        // Store parent relationship
        if (!ParentMap.Contains(BoneName))
        {
            ParentMap.Add(BoneName, ParentBone);
        }

        // Attach this layer to the bone
        LayerAttachments.FindOrAdd(BoneName).Add(Layer.Name);
    }

    // Create bone hierarchy
    for (const auto& BonePair : BonePositions)
    {
        const FString& BoneName = BonePair.Key;
        FVector2D Position = BonePair.Value;
        FString ParentName = ParentMap.FindRef(BoneName);

        // Create bone component
        USceneComponent* BoneComp = NewObject<USceneComponent>(this, *FString::Printf(TEXT("Bone_%s"), *BoneName));
        BoneComp->RegisterComponent();

        // Convert pixels to world units (100 pixels = 1 meter)
        BoneComp->SetRelativeLocation(FVector(Position.X * 0.01f, Position.Y * 0.01f, 0.0f));

        // Attach to parent or root
        if (ParentName.IsEmpty())
        {
            BoneComp->AttachToComponent(RootComponentRef, FAttachmentTransformRules::KeepRelativeTransform);
        }
        else if (USceneComponent** ParentComp = BoneMap.Find(ParentName))
        {
            BoneComp->AttachToComponent(*ParentComp, FAttachmentTransformRules::KeepRelativeTransform);
        }

        BoneMap.Add(BoneName, BoneComp);

        // Create plane meshes for attached layers
        for (const FString& LayerName : LayerAttachments.FindRef(BoneName))
        {
            // Find the layer data
            for (const FFreeRigPSDLayer& Layer : PSDData.Layers)
            {
                if (Layer.Name == LayerName)
                {
                    CreateLayerPlane(Layer, BoneComp, BoneName);
                    break;
                }
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("FreeRig: Created %d bones"), BoneMap.Num());
}

UStaticMeshComponent* AFreeRigCharacter::CreateLayerPlane(const FFreeRigPSDLayer& Layer, USceneComponent* Parent, const FString& BoneName)
{
    // Calculate plane size in world units
    float WidthInWorld = Layer.Width * 0.01f;
    float HeightInWorld = Layer.Height * 0.01f;

    // Create a simple plane mesh
    UStaticMesh* PlaneMesh = CreateDefaultSubobject<UStaticMesh>(*FString::Printf(TEXT("Mesh_%s"), *Layer.Name));

    // For now, use a simple cube or we can create a procedural plane
    // In production, you'd want to create a proper quad mesh

    // Create the mesh component
    UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("%s_%s"), *BoneName, *Layer.Name));
    MeshComp->RegisterComponent();
    MeshComp->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);

    // Position the plane (center it)
    MeshComp->SetRelativeLocation(FVector(0, 0, 0));

    // Set scale to match layer dimensions
    MeshComp->SetWorldScale3D(FVector(WidthInWorld, HeightInWorld, 1.0f));

    // Create material from layer texture if we have pixel data
    if (Layer.PixelData.Num() > 0)
    {
        // Create texture from pixel data
        UTexture2D* LayerTexture = UTexture2D::CreateTransient(Layer.Width, Layer.Height, PF_R8G8B8A8);
        if (LayerTexture)
        {
            void* TextureData = LayerTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
            FMemory::Memcpy(TextureData, Layer.PixelData.GetData(), Layer.PixelData.Num());
            LayerTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
            LayerTexture->UpdateResource();

            // Apply texture to material (you'll need a proper 2D unlit material)
            // MeshComp->SetMaterial(0, Material);
        }
    }

    return MeshComp;
}

void AFreeRigCharacter::RegisterDefaultParameterBindings()
{
    if (!RigComponent) return;

    // Register default parameter bindings
    // These will drive bone transforms

    // TODO: Bind parameters to specific bones
    // For now, we'll just log that bindings are registered
    UE_LOG(LogTemp, Log, TEXT("FreeRig: Registered default parameter bindings"));
}

bool AFreeRigCharacter::AutoRigFromPSD(
    const FString& PSDPath,
    const FString& JSONOutputPath)
{
    // For now, just a stub
    UE_LOG(LogTemp, Warning, TEXT("AutoRig: Not yet implemented"));
    return false;
}

void AFreeRigCharacter::StartPhoneControl(AFreeRigCharacter* Character, int32 Port)
{
    if (!Character)
    {
        UE_LOG(LogTemp, Error, TEXT("FreeRig: No character provided"));
        return;
    }

    // Create phone server if needed
    UFreeRigPhoneServer* PhoneServer = NewObject<UFreeRigPhoneServer>();
    PhoneServer->AddToRoot(); // Prevent garbage collection

    // Set target rig
    PhoneServer->SetTargetRigComponent(Character->GetRigComponent());

    // Start server
    PhoneServer->StartServer(Port);

    UE_LOG(LogTemp, Log, TEXT("FreeRig: Phone control started on port %d!"), Port);
}

// Static function library implementations
void UFreeRigCharacterBuilder::StartPhoneServer(UFreeRigComponent* TargetRig, int32 Port)
{
    if (!TargetRig)
    {
        UE_LOG(LogTemp, Error, TEXT("FreeRig: No rig component provided"));
        return;
    }

    // Create phone server
    UFreeRigPhoneServer* PhoneServer = NewObject<UFreeRigPhoneServer>();
    PhoneServer->AddToRoot(); // Prevent garbage collection
    PhoneServer->SetTargetRigComponent(TargetRig);
    PhoneServer->StartServer(Port);

    UE_LOG(LogTemp, Warning, TEXT("FreeRig: Phone server started!"));
    UE_LOG(LogTemp, Warning, TEXT("URL: %s"), *PhoneServer->GetConnectionURL());
}

void UFreeRigCharacterBuilder::ShowQRCode(const FString& URL)
{
    UE_LOG(LogTemp, Log, TEXT("FreeRig: Connect to: %s"), *URL);
    // QR code generation would go here
    // For now, just log the URL
}

static FAutoConsoleCommand CmdImportFolder(
    TEXT("FreeRig.ImportFolder"),
    TEXT("Import PNG folder as layers. Usage: FreeRig.ImportFolder \"C:/path/to/folder\""),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
        {
            if (Args.Num() >= 1)
            {
                FFreeRigPSDImportResult Result = UFreeRigPSDImporter::ImportFromFolder(Args[0]);
                if (Result.bSuccess)
                {
                    UE_LOG(LogTemp, Log, TEXT("Imported %d layers. Use FreeRig.AutoRig to generate rig."), Result.Layers.Num());
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("Usage: FreeRig.ImportFolder \"path/to/folder\""));
            }
        })
);

static FAutoConsoleCommand CmdStartPhone(
    TEXT("FreeRig.Phone"),
    TEXT("Start phone server for face tracking"),
    FConsoleCommandDelegate::CreateLambda([]()
        {
            UWorld* World = nullptr;
            for (const FWorldContext& Context : GEngine->GetWorldContexts())
            {
                if (Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game)
                {
                    World = Context.World();
                    break;
                }
            }

            if (!World)
            {
                UE_LOG(LogTemp, Error, TEXT("FreeRig: No world found. Make sure you're in Play mode."));
                return;
            }

            for (TActorIterator<AFreeRigCharacter> It(World); It; ++It)
            {
                AFreeRigCharacter* Character = *It;
                if (Character)
                {
                    // Create phone server
                    UFreeRigPhoneServer* PhoneServer = NewObject<UFreeRigPhoneServer>(Character);
                    PhoneServer->RegisterComponent();
                    PhoneServer->SetTargetRigComponent(Character->GetRigComponent());
                    PhoneServer->StartServer(8080);

                    UE_LOG(LogTemp, Warning, TEXT("Phone server started!"));

                    // Show QR code
                    UFreeRigQRCode::ShowQRCodeWindow(PhoneServer->GetConnectionURL(), TEXT("FreeRig - Scan to Connect"));
                    return;
                }
            }
            UE_LOG(LogTemp, Error, TEXT("FreeRig: No AFreeRigCharacter found in level. Spawn one first."));
        })
);