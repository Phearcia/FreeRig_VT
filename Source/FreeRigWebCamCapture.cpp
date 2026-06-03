#include "FreeRigWebCamCapture.h"
#include "IMediaModule.h"
#include "IMediaPlayerFactory.h"
#include "Modules/ModuleManager.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"

void UFreeRigWebCamCapture::NativeConstruct()
{
    Super::NativeConstruct();
    CreateMediaAssets();
}

void UFreeRigWebCamCapture::NativeDestruct()
{
    StopWebcam();
    Super::NativeDestruct();
}

void UFreeRigWebCamCapture::CreateMediaAssets()
{
    if (!MediaPlayer)
    {
        MediaPlayer = NewObject<UMediaPlayer>(this);
        MediaPlayer->PlayOnOpen = true;
    }

    if (!MediaTexture)
    {
        MediaTexture = NewObject<UMediaTexture>(this);
        MediaTexture->SetMediaPlayer(MediaPlayer);
        MediaTexture->AutoClear = true;
    }

    UE_LOG(LogTemp, Log, TEXT("FreeRig: Media assets created"));
}

void UFreeRigWebCamCapture::EnumerateDevices()
{
    // UE 5.7 doesn't expose camera enumeration easily via IMediaModule
    // For now, we'll just provide a default option and let users specify URLs
    TArray<FString> DeviceNames;
    DeviceNames.Add("Default Webcam (0)");
    DeviceNames.Add("Front Camera");
    DeviceNames.Add("Back Camera");

    OnDevicesEnumerated(DeviceNames);
    UE_LOG(LogTemp, Log, TEXT("FreeRig: Device enumeration complete"));
}

void UFreeRigWebCamCapture::StartWebcam(int32 DeviceIndex)
{
    if (!MediaPlayer)
    {
        OnWebcamError("MediaPlayer not initialized");
        return;
    }

    // Build webcam URL based on device index
    // Format: bmmf://device?name=USB Video Device
    FString WebcamUrl = FString::Printf(TEXT("bmmf://device?name=Webcam&index=%d"), DeviceIndex);

    // Alternative: Try direct media player factory
    // In UE 5.7, you can also use platform-specific URLs

    if (MediaPlayer->OpenUrl(WebcamUrl))
    {
        OnWebcamStarted();
        UE_LOG(LogTemp, Log, TEXT("FreeRig: Webcam started with URL: %s"), *WebcamUrl);
    }
    else
    {
        // Try fallback URL schemes
        FString FallbackUrl = TEXT("bmmf://device");
        if (MediaPlayer->OpenUrl(FallbackUrl))
        {
            OnWebcamStarted();
            UE_LOG(LogTemp, Log, TEXT("FreeRig: Webcam started (fallback)"));
        }
        else
        {
            OnWebcamError("Failed to open webcam - check Media Source plugin is enabled");
        }
    }
}

void UFreeRigWebCamCapture::StopWebcam()
{
    if (MediaPlayer)
    {
        MediaPlayer->Close();
        UE_LOG(LogTemp, Log, TEXT("FreeRig: Webcam stopped"));
    }
}

void UFreeRigWebCamCapture::TakePicture()
{
    if (!MediaPlayer || !MediaPlayer->IsPlaying())
    {
        OnWebcamError("Webcam is not running");
        return;
    }

    CaptureFrame();
}

void UFreeRigWebCamCapture::CaptureFrame()
{
    if (!MediaTexture || !MediaTexture->GetMediaPlayer() || !MediaTexture->GetMediaPlayer()->IsPlaying())
    {
        OnWebcamError("No active media texture or webcam not playing");
        return;
    }

    // Get the media texture dimensions
    int32 Width = MediaTexture->GetWidth();
    int32 Height = MediaTexture->GetHeight();

    if (Width <= 0 || Height <= 0)
    {
        OnWebcamError("Invalid media texture dimensions");
        return;
    }

    // Create render target to capture frame
    UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>();
    RenderTarget->InitCustomFormat(Width, Height, EPixelFormat::PF_B8G8R8A8, false);

    // Create a simple material to copy the media texture
    UMaterialInstanceDynamic* CopyMaterial = UMaterialInstanceDynamic::Create(nullptr, this);
    CopyMaterial->SetTextureParameterValue(FName("Texture"), MediaTexture);

    // Draw to render target
    UKismetRenderingLibrary::DrawMaterialToRenderTarget(GetWorld(), RenderTarget, CopyMaterial);

    // Create output texture
    UTexture2D* CapturedTexture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
    if (CapturedTexture)
    {
        void* TextureData = CapturedTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);

        // Copy render target data to texture
        FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
        if (RTResource)
        {
            TArray<FColor> Pixels;
            RTResource->ReadPixels(Pixels);

            if (Pixels.Num() > 0)
            {
                FMemory::Memcpy(TextureData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
            }
        }

        CapturedTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
        CapturedTexture->UpdateResource();

        OnPictureTaken(CapturedTexture);
        UE_LOG(LogTemp, Log, TEXT("FreeRig: Picture captured - %dx%d"), Width, Height);
    }
    else
    {
        OnWebcamError("Failed to create capture texture");
    }

    // Cleanup
    if (RenderTarget)
    {
        RenderTarget->RemoveFromRoot();
    }
}
