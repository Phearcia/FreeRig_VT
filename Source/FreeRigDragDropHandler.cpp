#include "FreeRigDragDropHandler.h"
#include "FreeRigPSDImporter.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformFileManager.h"

void UFreeRigDragDropHandler::RegisterDragDropHandler()
{
    // This registers for drag-drop in the editor
    // For now, we'll just listen via console command
    UE_LOG(LogTemp, Log, TEXT("Drag-drop handler registered. Drop PSD files or PNG folders onto editor window."));
}

void UFreeRigDragDropHandler::OnFilesDropped(const TArray<FString>& FilePaths)
{
    if (FilePaths.Num() == 0) return;

    FString FirstPath = FilePaths[0];

    // Check if it's a folder (contains no extension)
    if (FPaths::GetExtension(FirstPath).IsEmpty())
    {
        // Import folder of PNGs
        FFreeRigPSDImportResult Result = UFreeRigPSDImporter::ImportFromFolder(FirstPath);
        if (Result.bSuccess)
        {
            UE_LOG(LogTemp, Log, TEXT("Imported %d PNGs from folder: %s"), Result.Layers.Num(), *FirstPath);
        }
    }
    else if (FirstPath.EndsWith(TEXT(".psd")))
    {
        // Import PSD file
        FFreeRigPSDImportResult Result = UFreeRigPSDImporter::ParsePSDFile(FirstPath);
        if (Result.bSuccess)
        {
            UFreeRigPSDImporter::AutoDetectLayerRoles(Result);
            UE_LOG(LogTemp, Log, TEXT("Imported %d layers from PSD: %s"), Result.Layers.Num(), *FirstPath);
        }
    }
}