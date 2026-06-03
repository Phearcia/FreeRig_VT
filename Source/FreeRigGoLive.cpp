#include "FreeRigGoLive.h"
#include "FreeRigCharacterBuilder.h"
#include "FreeRigQRCode.h"
#include "FreeRigPhoneServer.h"
#include "FreeRigPSDImporter.h"
#include "EngineUtils.h"

void UFreeRigGoLive::GoLive(AFreeRigCharacter* Character, const FString& ModelPath)
{
    if (!Character)
    {
        UE_LOG(LogTemp, Error, TEXT("FreeRig: No character provided for Go Live"));
        return;
    }

    // Step 1: Package the model
    FString OutputPath = FPaths::ProjectSavedDir() / TEXT("GoLive") / FDateTime::Now().ToString();
    if (!PackageModel(ModelPath, OutputPath))
    {
        UE_LOG(LogTemp, Error, TEXT("FreeRig: Failed to package model"));
        return;
    }

    // Step 2: Start phone server
    UFreeRigPhoneServer* PhoneServer = NewObject<UFreeRigPhoneServer>();
    PhoneServer->AddToRoot();
    PhoneServer->SetTargetRigComponent(Character->GetRigComponent());
    PhoneServer->StartServer(8080);

    // Step 3: Generate and show QR code
    FString JSONPath = OutputPath / TEXT("model.json");
    FString ModelData = UFreeRigQRCode::GenerateModelQR(JSONPath);

    if (!ModelData.IsEmpty())
    {
        UFreeRigQRCode::ShowQRCodeWindow(PhoneServer->GetConnectionURL(),
            TEXT("FreeRig - Scan to Control"));
    }

    UE_LOG(LogTemp, Warning, TEXT("========================================"));
    UE_LOG(LogTemp, Warning, TEXT("FREE RIG - GO LIVE!"));
    UE_LOG(LogTemp, Warning, TEXT("========================================"));
    UE_LOG(LogTemp, Warning, TEXT("1. Scan QR code with your phone"));
    UE_LOG(LogTemp, Warning, TEXT("2. Grant camera permission"));
    UE_LOG(LogTemp, Warning, TEXT("3. Your character will mirror your face!"));
    UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

bool UFreeRigGoLive::PackageModel(const FString& ModelPath, const FString& OutputPath)
{
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    // Create output directory
    if (!PlatformFile.CreateDirectoryTree(*OutputPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create output directory: %s"), *OutputPath);
        return false;
    }

    // Copy model JSON
    FString DestJSON = OutputPath / TEXT("model.json");
    if (!PlatformFile.CopyFile(*DestJSON, *ModelPath))
    {
        // If copy fails, maybe model doesn't exist - create placeholder
        UE_LOG(LogTemp, Warning, TEXT("Model JSON not found, creating placeholder"));
        FFileHelper::SaveStringToFile(TEXT("{\"model\":\"placeholder\"}"), *DestJSON);
    }

    // Export textures
    FString TextureDir = OutputPath / TEXT("textures");
    PlatformFile.CreateDirectoryTree(*TextureDir);

    UE_LOG(LogTemp, Log, TEXT("Model packaged to: %s"), *OutputPath);
    return true;
}

static FAutoConsoleCommand CmdGoLive(
    TEXT("FreeRig.GoLive"),
    TEXT("Go live with current character. Usage: FreeRig.GoLive"),
    FConsoleCommandDelegate::CreateLambda([]()
        {
            // Find first FreeRig character in level
            UWorld* World = GEngine->GetWorldContexts()[0].World();
            for (TActorIterator<AFreeRigCharacter> It(World); It; ++It)
            {
                FString DefaultModel = FPaths::ProjectContentDir() / TEXT("Models/default/model.json");
                UFreeRigGoLive::GoLive(*It, DefaultModel);
                return;
            }
            UE_LOG(LogTemp, Error, TEXT("No FreeRigCharacter found in level"));
        })
);