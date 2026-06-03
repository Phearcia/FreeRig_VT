#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FreeRigWebCamCapture.generated.h"

class UMediaPlayer;
class UMediaTexture;

UCLASS()
class FREERIG_VT_API UFreeRigWebCamCapture : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "FreeRig|Webcam")
    void EnumerateDevices();

    UFUNCTION(BlueprintCallable, Category = "FreeRig|Webcam")
    void StartWebcam(int32 DeviceIndex = 0);

    UFUNCTION(BlueprintCallable, Category = "FreeRig|Webcam")
    void StopWebcam();

    UFUNCTION(BlueprintCallable, Category = "FreeRig|Webcam")
    void TakePicture();

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "FreeRig|Webcam")
    UMediaTexture* GetMediaTexture() const { return MediaTexture; }

    // Events
    UFUNCTION(BlueprintImplementableEvent, Category = "FreeRig|Webcam")
    void OnDevicesEnumerated(const TArray<FString>& DeviceNames);

    UFUNCTION(BlueprintImplementableEvent, Category = "FreeRig|Webcam")
    void OnWebcamStarted();

    UFUNCTION(BlueprintImplementableEvent, Category = "FreeRig|Webcam")
    void OnWebcamError(const FString& ErrorMessage);

    UFUNCTION(BlueprintImplementableEvent, Category = "FreeRig|Webcam")
    void OnPictureTaken(UTexture2D* CapturedImage);

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    UPROPERTY()
    UMediaPlayer* MediaPlayer = nullptr;

    UPROPERTY()
    UMediaTexture* MediaTexture = nullptr;

    void CreateMediaAssets();
    void CaptureFrame();
};