#include "FreeRigQrEncoder.h"

// ============================================================
//  GF(256) arithmetic for Reed–Solomon
// ============================================================

static uint8 gf_mul(uint8 a, uint8 b)
{
    uint8 r = 0;
    while (b)
    {
        if (b & 1) r ^= a;
        bool hi = a & 0x80;
        a <<= 1;
        if (hi) a ^= 0x1D; // primitive polynomial
        b >>= 1;
    }
    return r;
}

static void ComputeRS(const TArray<uint8>& Data, TArray<uint8>& OutEC)
{
    const int ECCount = 7;
    OutEC.Init(0, ECCount);

    // Generator polynomial for EC=7
    const uint8 Gen[7] = { 87, 229, 146, 149, 238, 102, 21 };

    for (uint8 byte : Data)
    {
        uint8 factor = byte ^ OutEC[0];

        // Shift left
        for (int i = 0; i < ECCount - 1; i++)
            OutEC[i] = OutEC[i + 1];

        OutEC[ECCount - 1] = 0;

        // Apply generator
        for (int i = 0; i < ECCount; i++)
            OutEC[i] ^= gf_mul(Gen[i], factor);
    }
}

// ============================================================
//  Bit buffer helper
// ============================================================

struct BitBuffer
{
    TArray<uint8> Bits;

    void AddBits(uint32 Value, int Count)
    {
        for (int i = Count - 1; i >= 0; i--)
            Bits.Add((Value >> i) & 1);
    }

    void AddByte(uint8 B)
    {
        for (int i = 7; i >= 0; i--)
            Bits.Add((B >> i) & 1);
    }
};

// ============================================================
//  Matrix helpers
// ============================================================

static void SetModule(TArray<bool>& M, int Size, int X, int Y, bool V)
{
    if (X >= 0 && X < Size && Y >= 0 && Y < Size)
        M[Y * Size + X] = V;
}

static void DrawFinder(TArray<bool>& M, int Size, int X, int Y)
{
    for (int dy = -1; dy <= 7; dy++)
    {
        for (int dx = -1; dx <= 7; dx++)
        {
            int xx = X + dx;
            int yy = Y + dy;

            bool in = (dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6);
            bool ring = (dx == 0 || dx == 6 || dy == 0 || dy == 6);
            bool center = (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4);

            bool val = in && (ring || center);
            SetModule(M, Size, xx, yy, val);
        }
    }
}

static void DrawTiming(TArray<bool>& M, int Size)
{
    for (int i = 8; i < Size - 8; i++)
    {
        bool v = (i % 2 == 0);
        SetModule(M, Size, i, 6, v);
        SetModule(M, Size, 6, i, v);
    }
}

// ============================================================
//  Main encoder
// ============================================================

bool FreeRigQrEncoder::EncodeText(const FString& Text, int32& OutSize, TArray<bool>& OutMatrix)
{
    OutSize = 21;
    OutMatrix.Init(false, OutSize * OutSize);

    // ------------------------------------------------------------
    // 1. Convert text to bytes
    // ------------------------------------------------------------
    TArray<uint8> Bytes;
    FTCHARToUTF8 Conv(*Text);
    Bytes.Append((uint8*)Conv.Get(), Conv.Length());

    if (Bytes.Num() > 17)
    {
        // Version 1-L max is 17 bytes
        return false;
    }

    // ------------------------------------------------------------
    // 2. Build bitstream
    // ------------------------------------------------------------
    BitBuffer BB;

    // Mode: Byte = 0100
    BB.AddBits(0b0100, 4);

    // Length (8 bits)
    BB.AddBits(Bytes.Num(), 8);

    // Data bytes
    for (uint8 B : Bytes)
        BB.AddByte(B);

    // Terminator (up to 4 bits)
    int TotalBits = 152; // Version 1-L = 19 data bytes = 152 bits
    int Remaining = TotalBits - BB.Bits.Num();
    for (int i = 0; i < FMath::Min(4, Remaining); i++)
        BB.Bits.Add(0);

    // Pad to byte boundary
    while (BB.Bits.Num() % 8 != 0)
        BB.Bits.Add(0);

    // Pad bytes 0xEC, 0x11
    uint8 PadBytes[2] = { 0xEC, 0x11 };
    int PadIndex = 0;

    while (BB.Bits.Num() < TotalBits)
    {
        uint8 P = PadBytes[PadIndex++ % 2];
        BB.AddByte(P);
    }

    // ------------------------------------------------------------
    // 3. Convert to data bytes
    // ------------------------------------------------------------
    TArray<uint8> Data;
    for (int i = 0; i < TotalBits; i += 8)
    {
        uint8 B = 0;
        for (int j = 0; j < 8; j++)
            B = (B << 1) | BB.Bits[i + j];
        Data.Add(B);
    }

    // ------------------------------------------------------------
    // 4. Reed–Solomon EC (7 bytes)
    // ------------------------------------------------------------
    TArray<uint8> EC;
    ComputeRS(Data, EC);

    // ------------------------------------------------------------
    // 5. Build final codeword stream
    // ------------------------------------------------------------
    TArray<uint8> Final = Data;
    Final.Append(EC);

    // ------------------------------------------------------------
    // 6. Place into matrix (zig-zag)
    // ------------------------------------------------------------
    TArray<bool> Used;
    Used.Init(false, OutSize * OutSize);

    // Finder patterns
    DrawFinder(OutMatrix, OutSize, 0, 0);
    DrawFinder(OutMatrix, OutSize, OutSize - 7, 0);
    DrawFinder(OutMatrix, OutSize, 0, OutSize - 7);

    // Timing
    DrawTiming(OutMatrix, OutSize);

    // Reserve finder/timing areas
    for (int y = 0; y < OutSize; y++)
    {
        for (int x = 0; x < OutSize; x++)
        {
            if (OutMatrix[y * OutSize + x])
                Used[y * OutSize + x] = true;
        }
    }

    // Data placement
    int BitIndex = 0;
    int Dir = -1; // up
    int X = OutSize - 1;
    int Y = OutSize - 1;

    while (X > 0)
    {
        if (X == 6) X--; // skip timing column

        for (int i = 0; i < 2; i++)
        {
            int xx = X - i;
            if (!Used[Y * OutSize + xx])
            {
                bool bit = false;
                if (BitIndex < Final.Num() * 8)
                {
                    int byteIndex = BitIndex / 8;
                    int bitPos = 7 - (BitIndex % 8);
                    bit = (Final[byteIndex] >> bitPos) & 1;
                }
                OutMatrix[Y * OutSize + xx] = bit;
                BitIndex++;
            }
        }

        Y += Dir;
        if (Y < 0 || Y >= OutSize)
        {
            Dir = -Dir;
            X -= 2;
            Y += Dir;
        }
    }

    // ------------------------------------------------------------
    // 7. Mask 0: (row + col) % 2 == 0
    // ------------------------------------------------------------
    for (int y = 0; y < OutSize; y++)
    {
        for (int x = 0; x < OutSize; x++)
        {
            if (!Used[y * OutSize + x])
            {
                if (((x + y) & 1) == 0)
                    OutMatrix[y * OutSize + x] = !OutMatrix[y * OutSize + x];
            }
        }
    }

    // ------------------------------------------------------------
    // 8. Format info (L + mask 0)
    // ------------------------------------------------------------
    const uint16 Format = 0b111011111000100; // precomputed for L + mask 0

    auto SetFormat = [&](int X, int Y, int Bit)
        {
            SetModule(OutMatrix, OutSize, X, Y, (Format >> Bit) & 1);
        };

    // Top-left
    for (int i = 0; i < 6; i++) SetFormat(i, 8, 14 - i);
    SetFormat(7, 8, 8);
    SetFormat(8, 8, 7);
    SetFormat(8, 7, 6);
    for (int i = 0; i < 6; i++) SetFormat(8, 5 - i, 5 - i);

    // Top-right
    for (int i = 0; i < 8; i++) SetFormat(OutSize - 1 - i, 8, 14 - (i + 7));

    // Bottom-left
    for (int i = 0; i < 7; i++) SetFormat(8, OutSize - 1 - i, 14 - (i + 14));

    return true;
}

bool FreeRigQrEncoder::EncodeBytes(const TArray<uint8>& Bytes, int32& OutSize, TArray<bool>& OutMatrix)
{
    OutSize = 21;
    OutMatrix.Init(false, OutSize * OutSize);

    // Version 1-L max is 19 bytes (152 bits)
    if (Bytes.Num() > 19)
    {
        UE_LOG(LogTemp, Error, TEXT("QR: Too many bytes for Version 1: %d (max 19)"), Bytes.Num());
        return false;
    }

    // ------------------------------------------------------------
    // 1. Build bitstream directly from bytes (no UTF-8 conversion)
    // ------------------------------------------------------------
    BitBuffer BB;

    // Mode: Byte = 0100
    BB.AddBits(0b0100, 4);

    // Length (8 bits)
    BB.AddBits(Bytes.Num(), 8);

    // Raw data bytes
    for (uint8 B : Bytes)
        BB.AddByte(B);

    // Terminator (up to 4 bits)
    int TotalBits = 152; // Version 1-L = 19 data bytes = 152 bits
    int Remaining = TotalBits - BB.Bits.Num();
    for (int i = 0; i < FMath::Min(4, Remaining); i++)
        BB.Bits.Add(0);

    // Pad to byte boundary
    while (BB.Bits.Num() % 8 != 0)
        BB.Bits.Add(0);

    // Pad bytes 0xEC, 0x11
    uint8 PadBytes[2] = { 0xEC, 0x11 };
    int PadIndex = 0;

    while (BB.Bits.Num() < TotalBits)
    {
        uint8 P = PadBytes[PadIndex++ % 2];
        BB.AddByte(P);
    }

    // ------------------------------------------------------------
    // 2. Convert to data bytes
    // ------------------------------------------------------------
    TArray<uint8> Data;
    for (int i = 0; i < TotalBits; i += 8)
    {
        uint8 B = 0;
        for (int j = 0; j < 8; j++)
            B = (B << 1) | BB.Bits[i + j];
        Data.Add(B);
    }

    // ------------------------------------------------------------
    // 3. Reed–Solomon EC (7 bytes)
    // ------------------------------------------------------------
    TArray<uint8> EC;
    ComputeRS(Data, EC);

    // ------------------------------------------------------------
    // 4. Build final codeword stream
    // ------------------------------------------------------------
    TArray<uint8> Final = Data;
    Final.Append(EC);

    // ------------------------------------------------------------
    // 5. Place into matrix (same as EncodeText)
    // ------------------------------------------------------------
    TArray<bool> Used;
    Used.Init(false, OutSize * OutSize);

    DrawFinder(OutMatrix, OutSize, 0, 0);
    DrawFinder(OutMatrix, OutSize, OutSize - 7, 0);
    DrawFinder(OutMatrix, OutSize, 0, OutSize - 7);
    DrawTiming(OutMatrix, OutSize);

    for (int y = 0; y < OutSize; y++)
    {
        for (int x = 0; x < OutSize; x++)
        {
            if (OutMatrix[y * OutSize + x])
                Used[y * OutSize + x] = true;
        }
    }

    int BitIndex = 0;
    int Dir = -1;
    int X = OutSize - 1;
    int Y = OutSize - 1;

    while (X > 0)
    {
        if (X == 6) X--;

        for (int i = 0; i < 2; i++)
        {
            int xx = X - i;
            if (!Used[Y * OutSize + xx])
            {
                bool bit = false;
                if (BitIndex < Final.Num() * 8)
                {
                    int byteIndex = BitIndex / 8;
                    int bitPos = 7 - (BitIndex % 8);
                    bit = (Final[byteIndex] >> bitPos) & 1;
                }
                OutMatrix[Y * OutSize + xx] = bit;
                BitIndex++;
            }
        }

        Y += Dir;
        if (Y < 0 || Y >= OutSize)
        {
            Dir = -Dir;
            X -= 2;
            Y += Dir;
        }
    }

    // Mask 0
    for (int y = 0; y < OutSize; y++)
    {
        for (int x = 0; x < OutSize; x++)
        {
            if (!Used[y * OutSize + x])
            {
                if (((x + y) & 1) == 0)
                    OutMatrix[y * OutSize + x] = !OutMatrix[y * OutSize + x];
            }
        }
    }

    // Format info
    const uint16 Format = 0b111011111000100;
    auto SetFormat = [&](int X, int Y, int Bit)
        {
            SetModule(OutMatrix, OutSize, X, Y, (Format >> Bit) & 1);
        };

    for (int i = 0; i < 6; i++) SetFormat(i, 8, 14 - i);
    SetFormat(7, 8, 8);
    SetFormat(8, 8, 7);
    SetFormat(8, 7, 6);
    for (int i = 0; i < 6; i++) SetFormat(8, 5 - i, 5 - i);
    for (int i = 0; i < 8; i++) SetFormat(OutSize - 1 - i, 8, 14 - (i + 7));
    for (int i = 0; i < 7; i++) SetFormat(8, OutSize - 1 - i, 14 - (i + 14));

    return true;
}