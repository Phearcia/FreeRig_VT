#pragma once
#include "CoreMinimal.h"

namespace FreeRigQrEncoder
{
    // Encodes text into a QR matrix (true = black, false = white)
    bool EncodeText(const FString& Text, int32& OutSize, TArray<bool>& OutMatrix);

    // Encodes raw bytes into a QR matrix (more compact for binary data)
    bool EncodeBytes(const TArray<uint8>& Bytes, int32& OutSize, TArray<bool>& OutMatrix);
}