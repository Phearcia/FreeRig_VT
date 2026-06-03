#include "FreeRigQRCode.h"
#include "FreeRigPhoneServer.h"
#include "FreeRigQrEncoder.h"
#include "Engine/Texture2D.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/SWindow.h"
#include "Widgets/Images/SImage.h"
#include "Brushes/SlateColorBrush.h"
#include "HAL/PlatformFileManager.h"

static TArray<uint8> IPPortToBytes(const FString& URL)
{
    TArray<uint8> Bytes;

    // Parse "http://192.168.50.94:8080"
    FString CleanURL = URL;
    CleanURL.RemoveFromStart(TEXT("http://"));

    FString IP, PortStr;
    if (CleanURL.Split(TEXT(":"), &IP, &PortStr))
    {
        // Parse IP: "192.168.50.94"
        TArray<FString> Octets;
        IP.ParseIntoArray(Octets, TEXT("."));
        for (const FString& Octet : Octets)
        {
            Bytes.Add(FCString::Atoi(*Octet));
        }

        // Parse port
        int32 Port = FCString::Atoi(*PortStr);
        Bytes.Add((Port >> 8) & 0xFF);  // High byte
        Bytes.Add(Port & 0xFF);          // Low byte
    }

    return Bytes;
}

FQRCodeResult UFreeRigQRCode::GenerateQRCode(const FString& URL, int32 Size)
{
    FQRCodeResult Result;
    Result.URL = URL;
    Result.bSuccess = false;

    // Convert URL to 6-byte binary (IP + port)
    TArray<uint8> BinaryData = IPPortToBytes(URL);

    if (BinaryData.Num() != 6)
    {
        UE_LOG(LogTemp, Error, TEXT("FreeRig: Failed to parse IP/port from URL: %s"), *URL);
        return Result;
    }

    // Encode binary data as QR (byte mode, not text)
    int32 QRSize = 0;
    TArray<bool> QRMatrix;

    // You need a new encoder function that takes raw bytes
    if (!FreeRigQrEncoder::EncodeBytes(BinaryData, QRSize, QRMatrix))
    {
        UE_LOG(LogTemp, Error, TEXT("FreeRig: Failed to encode QR code for %d bytes"), BinaryData.Num());
        return Result;
    }

    // Scale QR to desired texture size
    int32 BlockSize = Size / QRSize;
    if (BlockSize < 1) BlockSize = 1;

    int32 ScaledSize = QRSize * BlockSize;

    // Create texture
    UTexture2D* Texture = UTexture2D::CreateTransient(ScaledSize, ScaledSize, PF_R8G8B8A8);
    if (!Texture)
    {
        return Result;
    }

    // Generate pixels from QR matrix
    TArray<FColor> Pixels;
    Pixels.SetNum(ScaledSize * ScaledSize);

    for (int32 y = 0; y < ScaledSize; y++)
    {
        for (int32 x = 0; x < ScaledSize; x++)
        {
            int32 QRX = x / BlockSize;
            int32 QRY = y / BlockSize;
            bool bBlack = QRMatrix[QRY * QRSize + QRX];
            Pixels[y * ScaledSize + x] = bBlack ? FColor::Black : FColor::White;
        }
    }

    // Update texture
    void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(TextureData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
    Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
    Texture->UpdateResource();

    Result.bSuccess = true;
    Result.QRTexture = Texture;

    // Log URL for backup
    UE_LOG(LogTemp, Warning, TEXT("========================================"));
    UE_LOG(LogTemp, Warning, TEXT("FREE RIG - SCAN QR CODE"));
    UE_LOG(LogTemp, Warning, TEXT("========================================"));
    UE_LOG(LogTemp, Warning, TEXT("IP:Port encoded as 6 bytes in QR"));
    UE_LOG(LogTemp, Warning, TEXT("URL: %s"), *URL);
    UE_LOG(LogTemp, Warning, TEXT("========================================"));

    return Result;
}


void UFreeRigQRCode::ShowQRCodeWindow(const FString& URL, const FString& Title)
{
    FQRCodeResult QR = GenerateQRCode(URL, 512);
    if (!QR.bSuccess || !QR.QRTexture)
    {
        FNotificationInfo Info(FText::FromString(TEXT("Failed to generate QR code")));
        Info.ExpireDuration = 3.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
        return;
    }

    TSharedPtr<FSlateBrush> Brush = MakeShareable(new FSlateBrush());
    Brush->SetResourceObject(QR.QRTexture);
    Brush->ImageSize = FVector2D(512, 512);
    Brush->DrawAs = ESlateBrushDrawType::Image;

    static TSharedPtr<SWindow> QRWindow;

    if (QRWindow.IsValid())
    {
        QRWindow->RequestDestroyWindow();
    }

    QRWindow = SNew(SWindow)
        .Title(FText::FromString(Title))
        .ClientSize(FVector2D(600, 700))
        .SizingRule(ESizingRule::FixedSize)
        .SupportsMinimize(true)
        .SupportsMaximize(false);

    TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

    Content->AddSlot()
        .AutoHeight()
        .Padding(10)
        [
            SNew(SImage)
                .Image(Brush.Get())
        ];

    Content->AddSlot()
        .AutoHeight()
        .Padding(10)
        [
            SNew(STextBlock)
                .Text(FText::FromString(URL))
                .Justification(ETextJustify::Center)
        ];

    Content->AddSlot()
        .AutoHeight()
        .Padding(10)
        [
            SNew(STextBlock)
                .Text(FText::FromString(TEXT("1. Open camera app on your phone\n2. Scan QR code\n3. Grant camera permission\n4. Make faces!")))
                .Justification(ETextJustify::Center)
        ];

    QRWindow->SetContent(Content);

    // Store window reference in the phone server (static)
    UFreeRigPhoneServer::QRWindow = QRWindow;

    FSlateApplication::Get().AddWindow(QRWindow.ToSharedRef());

    FNotificationInfo Info(FText::FromString(TEXT("QR Code ready! Scan with your phone.")));
    Info.ExpireDuration = 5.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
}


void UFreeRigQRCode::ShowPhoneConnectionQR(UFreeRigPhoneServer* PhoneServer)
{
    if (!PhoneServer)
    {
        UE_LOG(LogTemp, Error, TEXT("FreeRig: PhoneServer is null"));
        return;
    }

    FString URL = PhoneServer->GetConnectionURL();
    ShowQRCodeWindow(URL, TEXT("FreeRig - Scan to Connect Phone"));
}

void UFreeRigQRCode::ExportToPhone(const FString& ModelPath, int32 Port)
{
    UFreeRigPhoneServer* PhoneServer = NewObject<UFreeRigPhoneServer>();
    PhoneServer->AddToRoot();
    PhoneServer->StartServer(Port);

    FString URL = PhoneServer->GetConnectionURL();
    ShowQRCodeWindow(URL, TEXT("FreeRig - Connect Phone"));

    FString WebDir = FPaths::ProjectSavedDir() / TEXT("WebServer");
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    PlatformFile.CreateDirectory(*WebDir);

    FString DestPath = WebDir / TEXT("model.json");
    PlatformFile.CopyFile(*DestPath, *ModelPath);

    UE_LOG(LogTemp, Log, TEXT("FreeRig: Model exported to %s for phone access"), *DestPath);
}

FString UFreeRigQRCode::GenerateModelQR(const FString& ModelJSONPath)
{
    FString JSONContent;
    if (!FFileHelper::LoadFileToString(JSONContent, *ModelJSONPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load model JSON: %s"), *ModelJSONPath);
        return TEXT("");
    }

    FString EncodedData = FBase64::Encode(JSONContent);

    if (EncodedData.Len() > 2950)
    {
        EncodedData = EncodedData.Left(2950);
        UE_LOG(LogTemp, Warning, TEXT("Model JSON truncated for QR code"));
    }

    return FString::Printf(TEXT("freerig://model?data=%s"), *EncodedData);
}

