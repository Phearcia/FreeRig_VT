#include "FreeRigPSDImporter.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Json.h"
#include "JsonUtilities.h"
#include "Misc/FileHelper.h"

UFreeRigPSDImporter::UFreeRigPSDImporter()
{
    SupportedClass = UTexture2D::StaticClass();
    Formats.Add(TEXT("psd;Photoshop Document"));
    bCreateNew = false;
    bEditAfterNew = false;
    bEditorImport = true;
}

bool UFreeRigPSDImporter::FactoryCanImport(const FString& Filename)
{
    return Filename.EndsWith(TEXT(".psd"), ESearchCase::IgnoreCase);
}

UObject* UFreeRigPSDImporter::FactoryCreateFile(
    UClass* InClass,
    UObject* InParent,
    FName InName,
    EObjectFlags Flags,
    const FString& Filename,
    const TCHAR* Parms,
    FFeedbackContext* Warn,
    bool& bOutOperationCanceled)
{
    FFreeRigPSDImportResult Result = ParsePSDFile(Filename);

    if (!Result.bSuccess)
    {
        Warn->Logf(ELogVerbosity::Error, TEXT("FreeRig: Failed to import PSD - %s"), *Result.ErrorMessage);
        return nullptr;
    }

    // For now, just import the first visible layer as a texture
    for (const FFreeRigPSDLayer& Layer : Result.Layers)
    {
        if (Layer.bVisible && Layer.PixelData.Num() > 0)
        {
            return CreateTextureFromRGBA(Layer.PixelData, Layer.Width, Layer.Height, InName.ToString(), InParent);
        }
    }

    return nullptr;
}

FFreeRigPSDImportResult UFreeRigPSDImporter::ParsePSDFile(const FString& FilePath)
{
    FFreeRigPSDImportResult Result;

    // Load file
    TArray<uint8> FileData;
    if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
    {
        Result.ErrorMessage = TEXT("Could not load file");
        return Result;
    }

    int32 Offset = 0;
    FPSDHeader Header;

    if (!ParseHeader(FileData, Offset, Header))
    {
        Result.ErrorMessage = TEXT("Invalid PSD header");
        return Result;
    }

    Result.Width = Header.Width;
    Result.Height = Header.Height;

    // Skip color mode data
    if (Offset + 4 > FileData.Num())
    {
        Result.ErrorMessage = TEXT("Unexpected end of file");
        return Result;
    }
    int32 ColorModeLen = (FileData[Offset] << 24) | (FileData[Offset + 1] << 16) | (FileData[Offset + 2] << 8) | FileData[Offset + 3];
    Offset += 4 + ColorModeLen;

    // Skip image resources
    if (Offset + 4 > FileData.Num())
    {
        Result.ErrorMessage = TEXT("Unexpected end of file");
        return Result;
    }
    int32 ResourceLen = (FileData[Offset] << 24) | (FileData[Offset + 1] << 16) | (FileData[Offset + 2] << 8) | FileData[Offset + 3];
    Offset += 4 + ResourceLen;

    // Parse layer and mask info
    TArray<FPSDLayerRecord> LayerRecords;
    if (!ParseLayerAndMaskInfo(FileData, Offset, LayerRecords))
    {
        Result.ErrorMessage = TEXT("Failed to parse layers");
        return Result;
    }

    // Convert to result layers
    for (const FPSDLayerRecord& Record : LayerRecords)
    {
        FFreeRigPSDLayer Layer;
        Layer.Name = Record.Name;
        Layer.X = Record.Left;
        Layer.Y = Record.Top;
        Layer.Width = Record.Right - Record.Left;
        Layer.Height = Record.Bottom - Record.Top;
        Layer.bVisible = Record.bVisible;
        Layer.Opacity = Record.Opacity / 255.0f;
        Layer.PixelData = ExtractLayerRGBA(Record, Header.Width, Header.Height);

        Result.Layers.Add(Layer);
    }

    Result.bSuccess = true;
    return Result;

}

bool UFreeRigPSDImporter::ParseHeader(const TArray<uint8>& Data, int32& Offset, FPSDHeader& OutHeader)
{
    // Check signature "8BPS"
    if (Offset + 4 > Data.Num()) return false;
    if (Data[Offset] != '8' || Data[Offset + 1] != 'B' || Data[Offset + 2] != 'P' || Data[Offset + 3] != 'S')
        return false;
    Offset += 4;

    // Version
    if (Offset + 2 > Data.Num()) return false;
    OutHeader.Version = (Data[Offset] << 8) | Data[Offset + 1];
    Offset += 2;

    if (OutHeader.Version != 1) return false;

    // Skip 6 reserved bytes
    Offset += 6;

    // Channels, Height, Width, Depth, ColorMode
    if (Offset + 14 > Data.Num()) return false;
    OutHeader.Channels = (Data[Offset] << 8) | Data[Offset + 1];
    Offset += 2;
    OutHeader.Height = (Data[Offset] << 24) | (Data[Offset + 1] << 16) | (Data[Offset + 2] << 8) | Data[Offset + 3];
    Offset += 4;
    OutHeader.Width = (Data[Offset] << 24) | (Data[Offset + 1] << 16) | (Data[Offset + 2] << 8) | Data[Offset + 3];
    Offset += 4;
    OutHeader.Depth = (Data[Offset] << 8) | Data[Offset + 1];
    Offset += 2;
    OutHeader.ColorMode = (Data[Offset] << 8) | Data[Offset + 1];
    Offset += 2;

    return true;
}

bool UFreeRigPSDImporter::ParseLayerAndMaskInfo(const TArray<uint8>& Data, int32& Offset, TArray<FPSDLayerRecord>& OutLayers)
{
    if (Offset + 4 > Data.Num()) return false;
    int32 LayerInfoLen = (Data[Offset] << 24) | (Data[Offset + 1] << 16) | (Data[Offset + 2] << 8) | Data[Offset + 3];
    Offset += 4;

    int32 LayerInfoEnd = Offset + LayerInfoLen;
    if (LayerInfoEnd > Data.Num()) return false;

    // Layer count (negative means first layer is blend clipping base)
    if (Offset + 2 > Data.Num()) return false;
    int16 LayerCount = (Data[Offset] << 8) | Data[Offset + 1];
    Offset += 2;

    bool bNegativeCount = LayerCount < 0;
    if (bNegativeCount) LayerCount = -LayerCount;

    for (int32 i = 0; i < LayerCount; i++)
    {
        if (Offset + 44 > Data.Num()) return false;

        FPSDLayerRecord Layer;

        // Layer bounds
        Layer.Top = (Data[Offset] << 24) | (Data[Offset + 1] << 16) | (Data[Offset + 2] << 8) | Data[Offset + 3];
        Offset += 4;
        Layer.Left = (Data[Offset] << 24) | (Data[Offset + 1] << 16) | (Data[Offset + 2] << 8) | Data[Offset + 3];
        Offset += 4;
        Layer.Bottom = (Data[Offset] << 24) | (Data[Offset + 1] << 16) | (Data[Offset + 2] << 8) | Data[Offset + 3];
        Offset += 4;
        Layer.Right = (Data[Offset] << 24) | (Data[Offset + 1] << 16) | (Data[Offset + 2] << 8) | Data[Offset + 3];
        Offset += 4;

        // Channel count
        Layer.ChannelCount = (Data[Offset] << 8) | Data[Offset + 1];
        Offset += 2;

        // Channel info
        for (int32 c = 0; c < Layer.ChannelCount; c++)
        {
            if (Offset + 6 > Data.Num()) return false;
            FPSDChannel Channel;
            Channel.ID = (Data[Offset] << 8) | Data[Offset + 1];
            Offset += 2;
            Channel.DataLength = (Data[Offset] << 24) | (Data[Offset + 1] << 16) | (Data[Offset + 2] << 8) | Data[Offset + 3];
            Offset += 4;

            if (Channel.DataLength > 0 && (int32)(Offset + Channel.DataLength) <= (int32)Data.Num())
            {
                Channel.Data.Append(&Data[Offset], Channel.DataLength);
                Offset += Channel.DataLength;
            }

            Layer.Channels.Add(Channel);
        }

        // Blend mode signature (8BIM)
        if (Offset + 8 > Data.Num()) return false;
        Offset += 4; // Skip signature
        char BlendKey[5] = { 0 };
        BlendKey[0] = Data[Offset]; BlendKey[1] = Data[Offset + 1]; BlendKey[2] = Data[Offset + 2]; BlendKey[3] = Data[Offset + 3];
        Layer.BlendMode = FString(BlendKey);
        Offset += 4;

        // Opacity, clipping, flags
        Layer.Opacity = Data[Offset]; Offset++;
        Layer.Clipping = Data[Offset]; Offset++;
        Layer.Flags = Data[Offset]; Offset++;
        Layer.bVisible = (Layer.Flags & 0x02) == 0;
        Offset++; // Skip filler

        // Extra data length
        int32 ExtraLen = (Data[Offset] << 24) | (Data[Offset + 1] << 16) | (Data[Offset + 2] << 8) | Data[Offset + 3];
        Offset += 4;
        int32 ExtraEnd = Offset + ExtraLen;

        // Layer name (Pascal string, padded to 4 bytes)
        if (Offset + 1 > Data.Num()) return false;
        uint8 NameLen = Data[Offset]; Offset++;
        if (NameLen > 0 && Offset + NameLen <= Data.Num())
        {
            Layer.Name = FString(NameLen, (char*)&Data[Offset]);
            Offset += NameLen;
        }

        // Skip to end of extra data
        if (Offset < ExtraEnd)
        {
            Offset = ExtraEnd;
        }

        OutLayers.Add(Layer);
    }

    // Skip to end of layer info
    if (Offset < LayerInfoEnd)
    {
        Offset = LayerInfoEnd;
    }

    return true;
}

TArray<uint8> UFreeRigPSDImporter::DecompressRLE(const TArray<uint8>& CompressedData, int32 ExpectedUncompressedSize)
{
    TArray<uint8> Result;
    Result.Reserve(ExpectedUncompressedSize);

    int32 Pos = 0;
    while (Pos < CompressedData.Num() && Result.Num() < ExpectedUncompressedSize)
    {
        int8 Byte = static_cast<int8>(CompressedData[Pos]);
        Pos++;

        if (Byte >= 0)
        {
            // Copy next Byte+1 bytes
            int32 Length = Byte + 1;
            for (int32 i = 0; i < Length && Pos + i < CompressedData.Num(); i++)
            {
                Result.Add(CompressedData[Pos + i]);
            }
            Pos += Length;
        }
        else if (Byte != -128)
        {
            // Repeat next byte (1 - Byte) times
            int32 Length = 1 - Byte;
            if (Pos < CompressedData.Num())
            {
                uint8 RepeatByte = CompressedData[Pos];
                Pos++;
                for (int32 i = 0; i < Length && Result.Num() < ExpectedUncompressedSize; i++)
                {
                    Result.Add(RepeatByte);
                }
            }
        }
    }

    return Result;
}

TArray<uint8> UFreeRigPSDImporter::ExtractLayerRGBA(const FPSDLayerRecord& Layer, int32 DocumentWidth, int32 DocumentHeight)
{
    int32 LayerWidth = Layer.Right - Layer.Left;
    int32 LayerHeight = Layer.Bottom - Layer.Top;

    if (LayerWidth <= 0 || LayerHeight <= 0)
    {
        return TArray<uint8>();
    }

    TArray<uint8> RGBA;
    RGBA.SetNumZeroed(LayerWidth * LayerHeight * 4);

    // Fill with transparent by default
    for (int32 i = 0; i < RGBA.Num(); i += 4)
    {
        RGBA[i + 3] = 0; // Alpha
    }

    // Find channel data
    TArray<uint8> RedData, GreenData, BlueData, AlphaData;

    for (const FPSDChannel& Channel : Layer.Channels)
    {
        // Channel ID: -1 = alpha, 0 = red, 1 = green, 2 = blue
        if (Channel.DataLength > 0)
        {
            // Read compression type
            int32 DataOffset = 0;
            uint16 Compression = 0;
            if (Channel.Data.Num() >= 2)
            {
                Compression = (Channel.Data[0] << 8) | Channel.Data[1];
                DataOffset = 2;
            }

            TArray<uint8> Decompressed;
            if (Compression == 1) // RLE
            {
                Decompressed = DecompressRLE(Channel.Data, LayerWidth * LayerHeight);
            }
            else // Raw or unsupported
            {
                Decompressed.Append(&Channel.Data[DataOffset], Channel.Data.Num() - DataOffset);
            }

            if (Channel.ID == 0) RedData = Decompressed;
            else if (Channel.ID == 1) GreenData = Decompressed;
            else if (Channel.ID == 2) BlueData = Decompressed;
            else if (Channel.ID == -1) AlphaData = Decompressed;
        }
    }

    // Combine into RGBA
    for (int32 y = 0; y < LayerHeight; y++)
    {
        for (int32 x = 0; x < LayerWidth; x++)
        {
            int32 PixelIndex = y * LayerWidth + x;
            int32 RGBAIndex = PixelIndex * 4;

            uint8 R = (RedData.IsValidIndex(PixelIndex)) ? RedData[PixelIndex] : 0;
            uint8 G = (GreenData.IsValidIndex(PixelIndex)) ? GreenData[PixelIndex] : 0;
            uint8 B = (BlueData.IsValidIndex(PixelIndex)) ? BlueData[PixelIndex] : 0;
            uint8 A = (AlphaData.IsValidIndex(PixelIndex)) ? AlphaData[PixelIndex] : (uint8)255;

            RGBA[RGBAIndex] = R;
            RGBA[RGBAIndex + 1] = G;
            RGBA[RGBAIndex + 2] = B;
            RGBA[RGBAIndex + 3] = A;
        }
    }

    return RGBA;
}

UTexture2D* UFreeRigPSDImporter::CreateTextureFromRGBA(const TArray<uint8>& RGBA, int32 Width, int32 Height, const FString& Name, UObject* Outer)
{
    if (RGBA.Num() != Width * Height * 4)
    {
        return nullptr;
    }

    UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_R8G8B8A8);
    if (!Texture)
    {
        return nullptr;
    }

    Texture->Rename(*Name, Outer);

    // Lock texture and copy data
    FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
    void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(TextureData, RGBA.GetData(), RGBA.Num());
    Mip.BulkData.Unlock();

    Texture->UpdateResource();

    return Texture;
}

bool UFreeRigPSDImporter::AutoDetectParameters(const FFreeRigPSDImportResult& PSDData, TArray<FFreeRigParameterDef>& OutParameters, TArray<int32>& OutAlwaysVisible)
{
    TMap<FString, TArray<int32>> LayerGroups;
    TSet<int32> UsedLayers;

    // Group layers by detected parameter
    for (int32 i = 0; i < PSDData.Layers.Num(); i++)
    {
        const FFreeRigPSDLayer& Layer = PSDData.Layers[i];
        FString LowerName = Layer.Name.ToLower();

        // Eye detection
        if (LowerName.Contains(TEXT("eye")) || LowerName.Contains(TEXT("blink")))
        {
            if (LowerName.Contains(TEXT("open")))
                LayerGroups.FindOrAdd(TEXT("EyeOpen")).Add(i);
            else if (LowerName.Contains(TEXT("half")))
                LayerGroups.FindOrAdd(TEXT("EyeOpen")).Add(i);
            else if (LowerName.Contains(TEXT("closed")) || LowerName.Contains(TEXT("blink")))
                LayerGroups.FindOrAdd(TEXT("EyeOpen")).Add(i);
            else
                LayerGroups.FindOrAdd(TEXT("EyeOpen")).Add(i); // assume open

            UsedLayers.Add(i);
        }
        // Mouth detection
        else if (LowerName.Contains(TEXT("mouth")) || LowerName.Contains(TEXT("smile")))
        {
            if (LowerName.Contains(TEXT("open")) || LowerName.Contains(TEXT("ah")))
                LayerGroups.FindOrAdd(TEXT("MouthOpen")).Add(i);
            else if (LowerName.Contains(TEXT("smile")))
                LayerGroups.FindOrAdd(TEXT("MouthSmile")).Add(i);
            else
                LayerGroups.FindOrAdd(TEXT("MouthOpen")).Add(i);

            UsedLayers.Add(i);
        }
        // Brow detection
        else if (LowerName.Contains(TEXT("brow")) || LowerName.Contains(TEXT("eyebrow")))
        {
            if (LowerName.Contains(TEXT("up")))
                LayerGroups.FindOrAdd(TEXT("BrowUp")).Add(i);
            else if (LowerName.Contains(TEXT("down")))
                LayerGroups.FindOrAdd(TEXT("BrowDown")).Add(i);
            else
                LayerGroups.FindOrAdd(TEXT("BrowUp")).Add(i);

            UsedLayers.Add(i);
        }
        // Expression detection
        else if (LowerName.Contains(TEXT("happy")) || LowerName.Contains(TEXT("sad")))
        {
            FString Expr = LowerName.Contains(TEXT("happy")) ? TEXT("happy") :
                (LowerName.Contains(TEXT("sad")) ? TEXT("sad") :
                    (LowerName.Contains(TEXT("angry")) ? TEXT("angry") : TEXT("neutral")));
            LayerGroups.FindOrAdd(TEXT("Expression")).Add(i);
            UsedLayers.Add(i);
        }
    }

    // Build parameter definitions from groups
    for (auto& Group : LayerGroups)
    {
        FFreeRigParameterDef Param;
        Param.Name = Group.Key;

        if (Group.Key == TEXT("Expression"))
        {
            Param.Mode = TEXT("discrete");
            Param.Default = 0.0f;
            // TODO: Map layer indices to discrete options
        }
        else
        {
            Param.Mode = TEXT("keyframe");
            Param.Min = 0.0f;
            Param.Max = 1.0f;
            Param.Default = 0.5f;

            // Sort layers by name to determine order
            Group.Value.Sort([&](int32 A, int32 B) {
                return PSDData.Layers[A].Name < PSDData.Layers[B].Name;
                });

            // Create keyframes
            for (int32 j = 0; j < Group.Value.Num(); j++)
            {
                FFreeRigParameterFrame Frame;
                Frame.Value = (float)j / (float)(Group.Value.Num() - 1);
                Frame.LayerIndex = Group.Value[j];
                Param.Frames.Add(Frame);
            }
        }

        OutParameters.Add(Param);
    }

    // Always visible = layers not used in any parameter
    for (int32 i = 0; i < PSDData.Layers.Num(); i++)
    {
        if (!UsedLayers.Contains(i))
        {
            OutAlwaysVisible.Add(i);
        }
    }

    return OutParameters.Num() > 0;
}

bool UFreeRigPSDImporter::ExportToJSON(const FFreeRigPSDImportResult& PSDData, const TArray<FFreeRigParameterDef>& Parameters, const TArray<int32>& AlwaysVisible, const FString& OutputPath)
{
    TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);

    // Basic info
    Root->SetStringField(TEXT("model_name"), FPaths::GetBaseFilename(OutputPath));
    Root->SetNumberField(TEXT("version"), 2);  // Bump version for new features

    // Texture atlas info
    TSharedPtr<FJsonObject> Atlas = MakeShareable(new FJsonObject);
    Atlas->SetNumberField(TEXT("width"), PSDData.Width);
    Atlas->SetNumberField(TEXT("height"), PSDData.Height);
    Atlas->SetStringField(TEXT("path"), TEXT("atlas.png"));
    Root->SetObjectField(TEXT("texture_atlas"), Atlas);

    // ──────────────────────────────────────────────────────────
    // LAYERS ARRAY
    // ──────────────────────────────────────────────────────────
    TArray<TSharedPtr<FJsonValue>> LayersArray;
    for (int32 i = 0; i < PSDData.Layers.Num(); i++)
    {
        const FFreeRigPSDLayer& Layer = PSDData.Layers[i];
        TSharedPtr<FJsonObject> LayerObj = MakeShareable(new FJsonObject);
        LayerObj->SetNumberField(TEXT("index"), i);
        LayerObj->SetStringField(TEXT("name"), Layer.Name);
        LayerObj->SetStringField(TEXT("role"), UEnum::GetValueAsString(Layer.DetectedRole));
        LayerObj->SetBoolField(TEXT("visible"), Layer.bVisible);
        LayerObj->SetNumberField(TEXT("width"), Layer.Width);
        LayerObj->SetNumberField(TEXT("height"), Layer.Height);
        LayerObj->SetNumberField(TEXT("x"), Layer.X);
        LayerObj->SetNumberField(TEXT("y"), Layer.Y);
        LayersArray.Add(MakeShareable(new FJsonValueObject(LayerObj)));
    }
    Root->SetArrayField(TEXT("layers"), LayersArray);

    // ──────────────────────────────────────────────────────────
    // PARAMETERS ARRAY
    // ──────────────────────────────────────────────────────────
    TArray<TSharedPtr<FJsonValue>> ParamsArray;
    for (const FFreeRigParameterDef& Param : Parameters)
    {
        TSharedPtr<FJsonObject> ParamObj = MakeShareable(new FJsonObject);
        ParamObj->SetStringField(TEXT("name"), Param.Name);
        ParamObj->SetNumberField(TEXT("min"), Param.Min);
        ParamObj->SetNumberField(TEXT("max"), Param.Max);
        ParamObj->SetNumberField(TEXT("default"), Param.Default);
        ParamObj->SetStringField(TEXT("mode"), Param.Mode);

        if (Param.Mode == TEXT("keyframe"))
        {
            TArray<TSharedPtr<FJsonValue>> FramesArray;
            for (const FFreeRigParameterFrame& Frame : Param.Frames)
            {
                TSharedPtr<FJsonObject> FrameObj = MakeShareable(new FJsonObject);
                FrameObj->SetNumberField(TEXT("value"), Frame.Value);
                FrameObj->SetNumberField(TEXT("layer_index"), Frame.LayerIndex);
                FramesArray.Add(MakeShareable(new FJsonValueObject(FrameObj)));
            }
            ParamObj->SetArrayField(TEXT("frames"), FramesArray);
        }

        ParamsArray.Add(MakeShareable(new FJsonValueObject(ParamObj)));
    }
    Root->SetArrayField(TEXT("parameters"), ParamsArray);

    // ──────────────────────────────────────────────────────────
    // ALWAYS VISIBLE
    // ──────────────────────────────────────────────────────────
    TArray<TSharedPtr<FJsonValue>> AlwaysVisibleArray;
    for (int32 Index : AlwaysVisible)
    {
        AlwaysVisibleArray.Add(MakeShareable(new FJsonValueNumber(Index)));
    }
    Root->SetArrayField(TEXT("always_visible"), AlwaysVisibleArray);

    // ──────────────────────────────────────────────────────────
    // BONES ARRAY (NEW)
    // ──────────────────────────────────────────────────────────
    TArray<FFreeRigBoneData> Bones = GenerateAutoBones(PSDData);
    AssignLayersToBones(Bones, PSDData);
    BindParametersToBones(Bones);

    TArray<TSharedPtr<FJsonValue>> BonesArray;
    for (const FFreeRigBoneData& Bone : Bones)
    {
        TSharedPtr<FJsonObject> BoneObj = MakeShareable(new FJsonObject);
        BoneObj->SetStringField(TEXT("name"), Bone.BoneName);
        BoneObj->SetStringField(TEXT("type"), UEnum::GetValueAsString(Bone.BoneType));
        BoneObj->SetStringField(TEXT("parent"), Bone.ParentBone);
        BoneObj->SetNumberField(TEXT("x"), Bone.Position.X);
        BoneObj->SetNumberField(TEXT("y"), Bone.Position.Y);
        BoneObj->SetNumberField(TEXT("rotation"), Bone.Rotation);

        TSharedPtr<FJsonObject> ScaleObj = MakeShareable(new FJsonObject);
        ScaleObj->SetNumberField(TEXT("x"), Bone.Scale.X);
        ScaleObj->SetNumberField(TEXT("y"), Bone.Scale.Y);
        BoneObj->SetObjectField(TEXT("scale"), ScaleObj);

        // Bound layers
        TArray<TSharedPtr<FJsonValue>> BoundLayersArray;
        for (int32 LayerIdx : Bone.BoundLayerIndices)
        {
            BoundLayersArray.Add(MakeShareable(new FJsonValueNumber(LayerIdx)));
        }
        BoneObj->SetArrayField(TEXT("bound_layers"), BoundLayersArray);

        BonesArray.Add(MakeShareable(new FJsonValueObject(BoneObj)));
    }
    Root->SetArrayField(TEXT("bones"), BonesArray);

    // ──────────────────────────────────────────────────────────
    // SKINNED MESHES ARRAY (NEW)
    // ──────────────────────────────────────────────────────────
    TArray<FFreeRigSkinnedMesh> SkinnedMeshes = GenerateSkinnedMeshes(PSDData, Bones, 50.0f);

    TArray<TSharedPtr<FJsonValue>> SkinnedMeshesArray;
    for (const FFreeRigSkinnedMesh& Mesh : SkinnedMeshes)
    {
        TSharedPtr<FJsonObject> MeshObj = MakeShareable(new FJsonObject);
        MeshObj->SetNumberField(TEXT("layer_index"), Mesh.LayerIndex);
        MeshObj->SetStringField(TEXT("layer_name"), Mesh.LayerName);

        // Vertices
        TArray<TSharedPtr<FJsonValue>> VerticesArray;
        for (const FFreeRigSkinnedVertex& Vertex : Mesh.Vertices)
        {
            TSharedPtr<FJsonObject> VertexObj = MakeShareable(new FJsonObject);
            VertexObj->SetNumberField(TEXT("x"), Vertex.Position.X);
            VertexObj->SetNumberField(TEXT("y"), Vertex.Position.Y);
            VertexObj->SetNumberField(TEXT("u"), Vertex.UV.X);
            VertexObj->SetNumberField(TEXT("v"), Vertex.UV.Y);

            // Weights
            TArray<TSharedPtr<FJsonValue>> WeightsArray;
            for (const FFreeRigSkinWeight& Weight : Vertex.Weights)
            {
                TSharedPtr<FJsonObject> WeightObj = MakeShareable(new FJsonObject);
                WeightObj->SetNumberField(TEXT("bone_index"), Weight.BoneIndex);
                WeightObj->SetNumberField(TEXT("weight"), Weight.Weight);
                WeightsArray.Add(MakeShareable(new FJsonValueObject(WeightObj)));
            }
            VertexObj->SetArrayField(TEXT("weights"), WeightsArray);

            VerticesArray.Add(MakeShareable(new FJsonValueObject(VertexObj)));
        }
        MeshObj->SetArrayField(TEXT("vertices"), VerticesArray);

        // Triangles
        TArray<TSharedPtr<FJsonValue>> TrianglesArray;
        for (int32 Tri : Mesh.Triangles)
        {
            TrianglesArray.Add(MakeShareable(new FJsonValueNumber(Tri)));
        }
        MeshObj->SetArrayField(TEXT("triangles"), TrianglesArray);

        SkinnedMeshesArray.Add(MakeShareable(new FJsonValueObject(MeshObj)));
    }
    Root->SetArrayField(TEXT("skinned_meshes"), SkinnedMeshesArray);

    // ──────────────────────────────────────────────────────────
    // BINDINGS (Parameter to bone mappings)
    // ──────────────────────────────────────────────────────────
    TArray<TSharedPtr<FJsonValue>> BindingsArray;

    // Eye binding
    TSharedPtr<FJsonObject> EyeBinding = MakeShareable(new FJsonObject);
    EyeBinding->SetStringField(TEXT("parameter"), TEXT("EyeOpen"));
    EyeBinding->SetStringField(TEXT("target_type"), TEXT("bone_scale"));
    EyeBinding->SetStringField(TEXT("bone_name"), TEXT("EyeLeft"));
    EyeBinding->SetStringField(TEXT("axis"), TEXT("Y"));
    EyeBinding->SetNumberField(TEXT("min_scale"), 0.1f);
    EyeBinding->SetNumberField(TEXT("max_scale"), 1.0f);
    BindingsArray.Add(MakeShareable(new FJsonValueObject(EyeBinding)));

    TSharedPtr<FJsonObject> RightEyeBinding = MakeShareable(new FJsonObject);
    RightEyeBinding->SetStringField(TEXT("parameter"), TEXT("EyeOpen"));
    RightEyeBinding->SetStringField(TEXT("target_type"), TEXT("bone_scale"));
    RightEyeBinding->SetStringField(TEXT("bone_name"), TEXT("EyeRight"));
    RightEyeBinding->SetStringField(TEXT("axis"), TEXT("Y"));
    RightEyeBinding->SetNumberField(TEXT("min_scale"), 0.1f);
    RightEyeBinding->SetNumberField(TEXT("max_scale"), 1.0f);
    BindingsArray.Add(MakeShareable(new FJsonValueObject(RightEyeBinding)));

    // Mouth binding
    TSharedPtr<FJsonObject> MouthBinding = MakeShareable(new FJsonObject);
    MouthBinding->SetStringField(TEXT("parameter"), TEXT("MouthOpen"));
    MouthBinding->SetStringField(TEXT("target_type"), TEXT("bone_scale"));
    MouthBinding->SetStringField(TEXT("bone_name"), TEXT("Mouth"));
    MouthBinding->SetStringField(TEXT("axis"), TEXT("Y"));
    MouthBinding->SetNumberField(TEXT("min_scale"), 0.2f);
    MouthBinding->SetNumberField(TEXT("max_scale"), 1.0f);
    BindingsArray.Add(MakeShareable(new FJsonValueObject(MouthBinding)));

    // Brow binding
    TSharedPtr<FJsonObject> BrowLeftBinding = MakeShareable(new FJsonObject);
    BrowLeftBinding->SetStringField(TEXT("parameter"), TEXT("BrowUp"));
    BrowLeftBinding->SetStringField(TEXT("target_type"), TEXT("bone_rotation"));
    BrowLeftBinding->SetStringField(TEXT("bone_name"), TEXT("EyebrowLeft"));
    BrowLeftBinding->SetNumberField(TEXT("min_rotation"), 0.0f);
    BrowLeftBinding->SetNumberField(TEXT("max_rotation"), 15.0f);
    BindingsArray.Add(MakeShareable(new FJsonValueObject(BrowLeftBinding)));

    TSharedPtr<FJsonObject> BrowRightBinding = MakeShareable(new FJsonObject);
    BrowRightBinding->SetStringField(TEXT("parameter"), TEXT("BrowUp"));
    BrowRightBinding->SetStringField(TEXT("target_type"), TEXT("bone_rotation"));
    BrowRightBinding->SetStringField(TEXT("bone_name"), TEXT("EyebrowRight"));
    BrowRightBinding->SetNumberField(TEXT("min_rotation"), 0.0f);
    BrowRightBinding->SetNumberField(TEXT("max_rotation"), 15.0f);
    BindingsArray.Add(MakeShareable(new FJsonValueObject(BrowRightBinding)));

    Root->SetArrayField(TEXT("parameter_bindings"), BindingsArray);

    // ──────────────────────────────────────────────────────────
    // WRITE TO FILE
    // ──────────────────────────────────────────────────────────
    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

    UE_LOG(LogTemp, Log, TEXT("Exported to JSON: %d layers, %d parameters, %d bones, %d skinned meshes"),
        PSDData.Layers.Num(), Parameters.Num(), Bones.Num(), SkinnedMeshes.Num());

    return FFileHelper::SaveStringToFile(OutputString, *OutputPath);
}

FFreeRigPSDImportResult UFreeRigPSDImporter::ImportFromFolder(const FString& FolderPath)
{
    FFreeRigPSDImportResult Result;
    Result.bSuccess = false;

    // Find all PNG files in folder
    IFileManager& FileManager = IFileManager::Get();
    TArray<FString> PngFiles;

    FString SearchPath = FolderPath / TEXT("*.png");
    FileManager.FindFiles(PngFiles, *SearchPath, true, false);

    if (PngFiles.Num() == 0)
    {
        Result.ErrorMessage = TEXT("No PNG files found in folder");
        return Result;
    }

    // Sort by name (so layers import in consistent order)
    PngFiles.Sort();

    // Track overall bounds
    int32 MaxWidth = 0;
    int32 MaxHeight = 0;

    // First pass: load all images to determine max size
    TArray<FFreeRigPSDLayer> LoadedLayers;

    for (const FString& PngFile : PngFiles)
    {
        FString FullPath = FolderPath / PngFile;

        // Load PNG as texture
        UTexture2D* TempTexture = FImageUtils::ImportFileAsTexture2D(FullPath);
        if (!TempTexture) continue;

        FFreeRigPSDLayer Layer;
        Layer.Name = FPaths::GetBaseFilename(PngFile);
        Layer.Width = TempTexture->GetSizeX();
        Layer.Height = TempTexture->GetSizeY();
        Layer.X = 0;
        Layer.Y = 0;
        Layer.bVisible = true;
        Layer.Opacity = 1.0f;

        // Extract pixel data
        TArray<FColor> PixelData;
        FTexture2DMipMap& Mip = TempTexture->GetPlatformData()->Mips[0];
        void* TextureData = Mip.BulkData.Lock(LOCK_READ_ONLY);
        int32 PixelCount = Layer.Width * Layer.Height;
        PixelData.SetNum(PixelCount);
        FMemory::Memcpy(PixelData.GetData(), TextureData, PixelCount * sizeof(FColor));
        Mip.BulkData.Unlock();

        // Convert FColor to RGBA bytes
        Layer.PixelData.SetNum(PixelCount * 4);
        for (int32 i = 0; i < PixelCount; i++)
        {
            Layer.PixelData[i * 4] = PixelData[i].R;
            Layer.PixelData[i * 4 + 1] = PixelData[i].G;
            Layer.PixelData[i * 4 + 2] = PixelData[i].B;
            Layer.PixelData[i * 4 + 3] = PixelData[i].A;
        }

        LoadedLayers.Add(Layer);
        MaxWidth = FMath::Max(MaxWidth, Layer.Width);
        MaxHeight = FMath::Max(MaxHeight, Layer.Height);

        TempTexture->ConditionalBeginDestroy();
    }

    // Position layers based on name heuristics
    PositionLayersByHeuristic(LoadedLayers, MaxWidth, MaxHeight);

    // Copy to result
    Result.Layers = LoadedLayers;
    Result.Width = MaxWidth;
    Result.Height = MaxHeight;
    Result.bSuccess = LoadedLayers.Num() > 0;

    if (Result.bSuccess)
    {
        // Auto-detect layer roles
        AutoDetectLayerRoles(Result);

        // ✓ Generate meshes here where Result exists
        for (FFreeRigPSDLayer& Layer : Result.Layers)
        {
            FFreeRigMeshData MeshData = GenerateMeshForLayer(Layer, 0.05f);
            // Store mesh data if needed...
        }
    }

    return Result;
}

void UFreeRigPSDImporter::PositionLayersByHeuristic(TArray<FFreeRigPSDLayer>& Layers, int32 CanvasWidth, int32 CanvasHeight)
{
    // Simple positioning based on layer name
    for (FFreeRigPSDLayer& Layer : Layers)
    {
        FString LowerName = Layer.Name.ToLower();

        // Center positioning for main features
        if (LowerName.Contains(TEXT("head")) || LowerName.Contains(TEXT("face")))
        {
            Layer.X = (CanvasWidth - Layer.Width) / 2;
            Layer.Y = (CanvasHeight - Layer.Height) / 3;  // Upper portion
        }
        else if (LowerName.Contains(TEXT("eye")))
        {
            Layer.X = (CanvasWidth - Layer.Width) / 2;
            Layer.Y = CanvasHeight / 3;

            // Adjust for left/right
            if (LowerName.Contains(TEXT("left")))
                Layer.X = (CanvasWidth / 2) - Layer.Width - 5;
            else if (LowerName.Contains(TEXT("right")))
                Layer.X = (CanvasWidth / 2) + 5;
        }
        else if (LowerName.Contains(TEXT("mouth")))
        {
            Layer.X = (CanvasWidth - Layer.Width) / 2;
            Layer.Y = CanvasHeight / 2;
        }
        else if (LowerName.Contains(TEXT("body")) || LowerName.Contains(TEXT("torso")))
        {
            Layer.X = (CanvasWidth - Layer.Width) / 2;
            Layer.Y = CanvasHeight / 2;
        }
        else if (LowerName.Contains(TEXT("hair")))
        {
            Layer.X = (CanvasWidth - Layer.Width) / 2;
            Layer.Y = 0;
        }
        else
        {
            // Default to center
            Layer.X = (CanvasWidth - Layer.Width) / 2;
            Layer.Y = (CanvasHeight - Layer.Height) / 2;
        }
    }
}

void UFreeRigPSDImporter::AutoDetectLayerRoles(FFreeRigPSDImportResult& ImportResult)
{
    TMap<FString, TArray<FString>> RoleNames;

    // Define role keywords
    RoleNames.Add(TEXT("Head"), { TEXT("head"), TEXT("face"), TEXT("skull") });
    RoleNames.Add(TEXT("Body"), { TEXT("body"), TEXT("torso"), TEXT("chest") });
    RoleNames.Add(TEXT("LeftEye"), { TEXT("lefteye"), TEXT("eye_left"), TEXT("leye") });
    RoleNames.Add(TEXT("RightEye"), { TEXT("righteye"), TEXT("eye_right"), TEXT("reye") });
    RoleNames.Add(TEXT("Mouth"), { TEXT("mouth"), TEXT("lips") });
    RoleNames.Add(TEXT("HairFront"), { TEXT("hairfront"), TEXT("hair_front") });
    RoleNames.Add(TEXT("HairBack"), { TEXT("hairback"), TEXT("hair_back") });
    RoleNames.Add(TEXT("EyebrowLeft"), { TEXT("leftbrow"), TEXT("brow_left") });
    RoleNames.Add(TEXT("EyebrowRight"), { TEXT("rightbrow"), TEXT("brow_right") });
    RoleNames.Add(TEXT("Nose"), { TEXT("nose") });

    // Detect and tag each layer
    for (FFreeRigPSDLayer& Layer : ImportResult.Layers)
    {
        FString LowerName = Layer.Name.ToLower();
        FString DetectedRole = TEXT("Unknown");

        for (const auto& RolePair : RoleNames)
        {
            for (const FString& Keyword : RolePair.Value)
            {
                if (LowerName.Contains(Keyword))
                {
                    DetectedRole = RolePair.Key;
                    break;
                }
            }
            if (DetectedRole != TEXT("Unknown")) break;
        }

        // Store detection result (add to layer metadata)
        // For now, just log it
        UE_LOG(LogTemp, Log, TEXT("Layer '%s' detected as: %s"), *Layer.Name, *DetectedRole);
    }
}

TArray<FVector2D> UFreeRigPSDImporter::DetectEdges(const TArray<uint8>& PixelData, int32 Width, int32 Height, float AlphaThreshold)
{
    TArray<FVector2D> EdgePoints;

    // Marching squares algorithm simplified
    // Scan for alpha edges
    for (int32 y = 0; y < Height - 1; y++)
    {
        for (int32 x = 0; x < Width - 1; x++)
        {
            // Get alpha values for 2x2 block
            int32 idx00 = (y * Width + x) * 4 + 3;
            int32 idx10 = (y * Width + (x + 1)) * 4 + 3;
            int32 idx01 = ((y + 1) * Width + x) * 4 + 3;
            int32 idx11 = ((y + 1) * Width + (x + 1)) * 4 + 3;

            uint8 a00 = PixelData[idx00];
            uint8 a10 = PixelData[idx10];
            uint8 a01 = PixelData[idx01];
            uint8 a11 = PixelData[idx11];

            // Check if edge exists (transitions between opaque and transparent)
            int32 Case = 0;
            if (a00 > AlphaThreshold) Case |= 1;
            if (a10 > AlphaThreshold) Case |= 2;
            if (a01 > AlphaThreshold) Case |= 4;
            if (a11 > AlphaThreshold) Case |= 8;

            if (Case == 0 || Case == 15) continue; // No edge

            // Add edge points based on marching squares case
            float fx = (float)x / Width;
            float fy = (float)y / Height;

            // Simplified: add center point of edge cells
            EdgePoints.Add(FVector2D(fx + 0.5f / Width, fy + 0.5f / Height));
        }
    }

    return EdgePoints;
}

TArray<FVector2D> UFreeRigPSDImporter::SimplifyPolygon(const TArray<FVector2D>& Points, float Tolerance)
{
    if (Points.Num() < 3) return Points;

    TArray<FVector2D> Result;
    Result.Add(Points[0]);

    // Ramer-Douglas-Peucker simplification
    int32 End = Points.Num() - 1;
    TArray<int32> Stack;
    Stack.Add(0);
    Stack.Add(End);

    TArray<bool> Keep;
    Keep.SetNum(Points.Num());
    Keep[0] = true;
    Keep[End] = true;

    while (Stack.Num() > 0)
    {
        int32 Start = Stack.Pop();
        int32 EndIdx = Stack.Pop();

        // Find point with max distance from line
        float MaxDist = 0;
        int32 MaxIndex = Start;

        for (int32 i = Start + 1; i < EndIdx; i++)
        {
            // Calculate perpendicular distance from point to line segment
            FVector2D A = Points[Start];
            FVector2D B = Points[EndIdx];
            FVector2D P = Points[i];

            FVector2D AP = P - A;
            FVector2D AB = B - A;
            float ABLenSq = AB.SizeSquared();

            if (ABLenSq < 1e-6f)
            {
                // Start and end are too close, use direct distance
                float Dist = FVector2D::Distance(P, A);
                if (Dist > MaxDist)
                {
                    MaxDist = Dist;
                    MaxIndex = i;
                }
                continue;
            }

            float T = FMath::Clamp((AP | AB) / ABLenSq, 0.0f, 1.0f);
            FVector2D Closest = A + AB * T;
            float Dist = FVector2D::Distance(P, Closest);

            if (Dist > MaxDist)
            {
                MaxDist = Dist;
                MaxIndex = i;
            }
        }

        if (MaxDist > Tolerance)
        {
            Keep[MaxIndex] = true;
            Stack.Add(Start);
            Stack.Add(MaxIndex);
            Stack.Add(MaxIndex);
            Stack.Add(EndIdx);
        }
    }

    for (int32 i = 0; i < Points.Num(); i++)
    {
        if (Keep[i])
        {
            Result.Add(Points[i]);
        }
    }

    return Result;
}

TArray<FVector2D> UFreeRigPSDImporter::GenerateAdaptiveGrid(const TArray<FVector2D>& EdgePoints, const FBox2D& Bounds, float Density)
{
    TArray<FVector2D> GridPoints;

    // Add edge points
    GridPoints.Append(EdgePoints);

    // Add interior points based on density
    int32 GridX = FMath::Max(3, FMath::CeilToInt(Bounds.GetSize().X * Density));
    int32 GridY = FMath::Max(3, FMath::CeilToInt(Bounds.GetSize().Y * Density));

    for (int32 y = 0; y <= GridY; y++)
    {
        for (int32 x = 0; x <= GridX; x++)
        {
            FVector2D Point;
            Point.X = Bounds.Min.X + (Bounds.GetSize().X * x / GridX);
            Point.Y = Bounds.Min.Y + (Bounds.GetSize().Y * y / GridY);
            GridPoints.AddUnique(Point);
        }
    }

    return GridPoints;
}

TArray<int32> UFreeRigPSDImporter::TriangulateMesh(const TArray<FVector2D>& Points)
{
    TArray<int32> Triangles;

    // Simple ear-clipping triangulation for polygons
    // For MVP, use a grid-based triangulation

    if (Points.Num() < 3) return Triangles;

    // Create a simple grid triangulation
    int32 GridSize = FMath::Sqrt((float)Points.Num());
    if (GridSize * GridSize != Points.Num())
    {
        // Fallback to fan triangulation
        for (int32 i = 1; i < Points.Num() - 1; i++)
        {
            Triangles.Add(0);
            Triangles.Add(i);
            Triangles.Add(i + 1);
        }
        return Triangles;
    }

    // Grid triangulation
    for (int32 y = 0; y < GridSize - 1; y++)
    {
        for (int32 x = 0; x < GridSize - 1; x++)
        {
            int32 idx = y * GridSize + x;
            int32 idxRight = idx + 1;
            int32 idxBottom = (y + 1) * GridSize + x;
            int32 idxBottomRight = idxBottom + 1;

            // Triangle 1
            Triangles.Add(idx);
            Triangles.Add(idxRight);
            Triangles.Add(idxBottom);

            // Triangle 2
            Triangles.Add(idxRight);
            Triangles.Add(idxBottomRight);
            Triangles.Add(idxBottom);
        }
    }

    return Triangles;
}

FFreeRigMeshData UFreeRigPSDImporter::GenerateMeshForLayer(const FFreeRigPSDLayer& Layer, float EdgeDensity)
{
    FFreeRigMeshData MeshData;
    MeshData.Width = Layer.Width;
    MeshData.Height = Layer.Height;
    MeshData.LayerName = Layer.Name;

    // Detect edges from alpha channel
    TArray<FVector2D> EdgePoints = DetectEdges(Layer.PixelData, Layer.Width, Layer.Height);

    if (EdgePoints.Num() < 3)
    {
        // No alpha edges, use full rectangle
        MeshData.Vertices.SetNum(4);
        MeshData.Vertices[0].Position = FVector2D(0, 0);
        MeshData.Vertices[1].Position = FVector2D(1, 0);
        MeshData.Vertices[2].Position = FVector2D(1, 1);
        MeshData.Vertices[3].Position = FVector2D(0, 1);

        MeshData.Triangles = { 0, 1, 2, 0, 2, 3 };

        for (int32 i = 0; i < 4; i++)
        {
            MeshData.Vertices[i].UV = MeshData.Vertices[i].Position;
            MeshData.Vertices[i].BoneWeights = { 1.0f };
            MeshData.Vertices[i].BoneIndices = { 0 };
        }

        return MeshData;
    }

    // Simplify edge polygon
    TArray<FVector2D> SimplifiedEdges = SimplifyPolygon(EdgePoints, 2.0f);

    // Get bounds
    FBox2D Bounds;
    for (const FVector2D& Point : SimplifiedEdges)
    {
        Bounds += Point;
    }

    // Generate adaptive grid
    TArray<FVector2D> AllPoints = GenerateAdaptiveGrid(SimplifiedEdges, Bounds, EdgeDensity);

    // Triangulate
    MeshData.Triangles = TriangulateMesh(AllPoints);

    // Build vertex list
    MeshData.Vertices.SetNum(AllPoints.Num());
    for (int32 i = 0; i < AllPoints.Num(); i++)
    {
        MeshData.Vertices[i].Position = AllPoints[i];
        MeshData.Vertices[i].UV = AllPoints[i];
        MeshData.Vertices[i].BoneWeights = { 1.0f };
        MeshData.Vertices[i].BoneIndices = { 0 };
    }

    UE_LOG(LogTemp, Log, TEXT("Generated mesh for layer '%s': %d vertices, %d triangles"),
        *Layer.Name, MeshData.Vertices.Num(), MeshData.Triangles.Num() / 3);

    return MeshData;
}

FBox2D UFreeRigPSDImporter::GetModelBounds(const FFreeRigPSDImportResult& PSDData)
{
    FBox2D Bounds;
    for (const FFreeRigPSDLayer& Layer : PSDData.Layers)
    {
        FVector2D Min(Layer.X, Layer.Y);
        FVector2D Max(Layer.X + Layer.Width, Layer.Y + Layer.Height);
        Bounds += Min;
        Bounds += Max;
    }
    return Bounds;
}

FVector2D UFreeRigPSDImporter::GetLayerCenter(const FFreeRigPSDLayer& Layer)
{
    return FVector2D(Layer.X + Layer.Width / 2.0f, Layer.Y + Layer.Height / 2.0f);
}

FFreeRigBoneData UFreeRigPSDImporter::CreateBone(EBoneType Type, const FString& Name, const FString& Parent, const FVector2D& Position)
{
    FFreeRigBoneData Bone;
    Bone.BoneType = Type;
    Bone.BoneName = Name;
    Bone.ParentBone = Parent;
    Bone.Position = Position;
    return Bone;
}

TArray<FFreeRigBoneData> UFreeRigPSDImporter::GenerateAutoBones(const FFreeRigPSDImportResult& PSDData)
{
    TArray<FFreeRigBoneData> Bones;

    // Get model bounds for normalization
    FBox2D ModelBounds = GetModelBounds(PSDData);
    FVector2D ModelSize = ModelBounds.GetSize();
    FVector2D ModelCenter = ModelBounds.GetCenter();

    // Find key layers
    const FFreeRigPSDLayer* HeadLayer = nullptr;
    const FFreeRigPSDLayer* BodyLayer = nullptr;
    const FFreeRigPSDLayer* LeftEyeLayer = nullptr;
    const FFreeRigPSDLayer* RightEyeLayer = nullptr;
    const FFreeRigPSDLayer* MouthLayer = nullptr;
    const FFreeRigPSDLayer* LeftBrowLayer = nullptr;
    const FFreeRigPSDLayer* RightBrowLayer = nullptr;
    const FFreeRigPSDLayer* HairFrontLayer = nullptr;
    const FFreeRigPSDLayer* HairBackLayer = nullptr;

    // Detect layers by name (Phase 1 detection)
    for (const FFreeRigPSDLayer& Layer : PSDData.Layers)
    {
        FString LowerName = Layer.Name.ToLower();

        if (LowerName.Contains(TEXT("head")) || LowerName.Contains(TEXT("face")))
            HeadLayer = &Layer;
        else if (LowerName.Contains(TEXT("body")) || LowerName.Contains(TEXT("torso")))
            BodyLayer = &Layer;
        else if (LowerName.Contains(TEXT("eye_left")) || LowerName.Contains(TEXT("lefteye")))
            LeftEyeLayer = &Layer;
        else if (LowerName.Contains(TEXT("eye_right")) || LowerName.Contains(TEXT("righteye")))
            RightEyeLayer = &Layer;
        else if (LowerName.Contains(TEXT("mouth")))
            MouthLayer = &Layer;
        else if (LowerName.Contains(TEXT("brow_left")) || LowerName.Contains(TEXT("leftbrow")))
            LeftBrowLayer = &Layer;
        else if (LowerName.Contains(TEXT("brow_right")) || LowerName.Contains(TEXT("rightbrow")))
            RightBrowLayer = &Layer;
        else if (LowerName.Contains(TEXT("hair_front")))
            HairFrontLayer = &Layer;
        else if (LowerName.Contains(TEXT("hair_back")))
            HairBackLayer = &Layer;
    }

    // 1. Root bone (at model center-bottom)
    FVector2D RootPos = FVector2D(ModelCenter.X, ModelBounds.Min.Y);
    Bones.Add(CreateBone(EBoneType::Root, TEXT("Root"), TEXT(""), RootPos));

    // 2. Body bone (if body layer exists)
    if (BodyLayer)
    {
        FVector2D BodyPos = GetLayerCenter(*BodyLayer);
        Bones.Add(CreateBone(EBoneType::Body, TEXT("Body"), TEXT("Root"), BodyPos));
    }
    else
    {
        // Default body position
        FVector2D BodyPos = FVector2D(ModelCenter.X, ModelCenter.Y + ModelSize.Y * 0.3f);
        Bones.Add(CreateBone(EBoneType::Body, TEXT("Body"), TEXT("Root"), BodyPos));
    }

    // 3. Head bone
    if (HeadLayer)
    {
        FVector2D HeadPos = GetLayerCenter(*HeadLayer);
        Bones.Add(CreateBone(EBoneType::Head, TEXT("Head"), TEXT("Body"), HeadPos));
    }
    else
    {
        // Default head position (upper half)
        FVector2D HeadPos = FVector2D(ModelCenter.X, ModelBounds.Min.Y + ModelSize.Y * 0.25f);
        Bones.Add(CreateBone(EBoneType::Head, TEXT("Head"), TEXT("Body"), HeadPos));
    }

    // 4. Left Eye
    if (LeftEyeLayer)
    {
        FVector2D EyePos = GetLayerCenter(*LeftEyeLayer);
        Bones.Add(CreateBone(EBoneType::EyeLeft, TEXT("EyeLeft"), TEXT("Head"), EyePos));
    }

    // 5. Right Eye
    if (RightEyeLayer)
    {
        FVector2D EyePos = GetLayerCenter(*RightEyeLayer);
        Bones.Add(CreateBone(EBoneType::EyeRight, TEXT("EyeRight"), TEXT("Head"), EyePos));
    }

    // 6. Mouth
    if (MouthLayer)
    {
        FVector2D MouthPos = GetLayerCenter(*MouthLayer);
        Bones.Add(CreateBone(EBoneType::Mouth, TEXT("Mouth"), TEXT("Head"), MouthPos));
    }

    // 7. Eyebrows
    if (LeftBrowLayer)
    {
        FVector2D BrowPos = GetLayerCenter(*LeftBrowLayer);
        Bones.Add(CreateBone(EBoneType::EyebrowLeft, TEXT("EyebrowLeft"), TEXT("Head"), BrowPos));
    }

    if (RightBrowLayer)
    {
        FVector2D BrowPos = GetLayerCenter(*RightBrowLayer);
        Bones.Add(CreateBone(EBoneType::EyebrowRight, TEXT("EyebrowRight"), TEXT("Head"), BrowPos));
    }

    // 8. Hair bones
    if (HairFrontLayer)
    {
        FVector2D HairPos = GetLayerCenter(*HairFrontLayer);
        Bones.Add(CreateBone(EBoneType::HairFront, TEXT("HairFront"), TEXT("Head"), HairPos));
    }

    if (HairBackLayer)
    {
        FVector2D HairPos = GetLayerCenter(*HairBackLayer);
        Bones.Add(CreateBone(EBoneType::HairBack, TEXT("HairBack"), TEXT("Head"), HairPos));
    }

    UE_LOG(LogTemp, Log, TEXT("Generated %d bones for auto-rig"), Bones.Num());

    return Bones;
}

void UFreeRigPSDImporter::AssignLayersToBones(TArray<FFreeRigBoneData>& Bones, const FFreeRigPSDImportResult& PSDData)
{
    // Assign each layer to the closest bone
    for (int32 LayerIdx = 0; LayerIdx < PSDData.Layers.Num(); LayerIdx++)
    {
        const FFreeRigPSDLayer& Layer = PSDData.Layers[LayerIdx];
        FVector2D LayerCenter = GetLayerCenter(Layer);

        // Find closest bone
        int32 ClosestBoneIdx = -1;
        float ClosestDist = FLT_MAX;

        for (int32 BoneIdx = 0; BoneIdx < Bones.Num(); BoneIdx++)
        {
            float Dist = FVector2D::DistSquared(LayerCenter, Bones[BoneIdx].Position);
            if (Dist < ClosestDist)
            {
                ClosestDist = Dist;
                ClosestBoneIdx = BoneIdx;
            }
        }

        if (ClosestBoneIdx >= 0)
        {
            Bones[ClosestBoneIdx].BoundLayerIndices.Add(LayerIdx);
            UE_LOG(LogTemp, Verbose, TEXT("Assigned layer '%s' to bone '%s'"),
                *Layer.Name, *Bones[ClosestBoneIdx].BoneName);
        }
    }
}

void UFreeRigPSDImporter::AddBonesToJSON(TSharedPtr<FJsonObject> Root, const FFreeRigPSDImportResult& PSDData)
{
    TArray<FFreeRigBoneData> Bones = GenerateAutoBones(PSDData);
    AssignLayersToBones(Bones, PSDData);

    TArray<TSharedPtr<FJsonValue>> BonesArray;
    for (const FFreeRigBoneData& Bone : Bones)
    {
        TSharedPtr<FJsonObject> BoneObj = MakeShareable(new FJsonObject);
        BoneObj->SetStringField(TEXT("name"), Bone.BoneName);
        BoneObj->SetStringField(TEXT("type"), UEnum::GetValueAsString(Bone.BoneType));
        BoneObj->SetStringField(TEXT("parent"), Bone.ParentBone);
        BoneObj->SetNumberField(TEXT("x"), Bone.Position.X);
        BoneObj->SetNumberField(TEXT("y"), Bone.Position.Y);

        // Layer indices bound to this bone
        TArray<TSharedPtr<FJsonValue>> LayerIndicesArray;
        for (int32 LayerIdx : Bone.BoundLayerIndices)
        {
            LayerIndicesArray.Add(MakeShareable(new FJsonValueNumber(LayerIdx)));
        }
        BoneObj->SetArrayField(TEXT("layers"), LayerIndicesArray);
        BonesArray.Add(MakeShareable(new FJsonValueObject(BoneObj)));
    }

    Root->SetArrayField(TEXT("bones"), BonesArray);
}

TArray<FFreeRigSkinnedMesh> UFreeRigPSDImporter::GenerateSkinnedMeshes(
    const FFreeRigPSDImportResult& PSDData,
    const TArray<FFreeRigBoneData>& Bones,
    float MaxInfluenceDistance)
{
    TArray<FFreeRigSkinnedMesh> SkinnedMeshes;

    for (int32 LayerIdx = 0; LayerIdx < PSDData.Layers.Num(); LayerIdx++)
    {
        const FFreeRigPSDLayer& Layer = PSDData.Layers[LayerIdx];

        // Generate base mesh for this layer
        FFreeRigMeshData BaseMesh = GenerateMeshForLayer(Layer, 0.05f);

        FFreeRigSkinnedMesh SkinnedMesh;
        SkinnedMesh.LayerIndex = LayerIdx;
        SkinnedMesh.LayerName = Layer.Name;
        SkinnedMesh.Triangles = BaseMesh.Triangles;

        // Skin each vertex
        for (const FFreeRigMeshVertex& Vertex : BaseMesh.Vertices)
        {
            // Convert vertex position to world space (pixel coordinates)
            FVector2D WorldPos(
                Layer.X + Vertex.Position.X * Layer.Width,
                Layer.Y + Vertex.Position.Y * Layer.Height
            );

            // Calculate bone weights for this vertex
            TArray<FFreeRigSkinWeight> Weights = CalculateVertexWeights(WorldPos, Bones, MaxInfluenceDistance);

            FFreeRigSkinnedVertex SkinnedVertex;
            SkinnedVertex.Position = Vertex.Position;
            SkinnedVertex.UV = Vertex.UV;
            SkinnedVertex.Weights = Weights;

            SkinnedMesh.Vertices.Add(SkinnedVertex);
        }

        SkinnedMeshes.Add(SkinnedMesh);

        UE_LOG(LogTemp, Log, TEXT("Skinned mesh '%s': %d vertices, %d bones influences"),
            *Layer.Name, SkinnedMesh.Vertices.Num(), Bones.Num());
    }

    return SkinnedMeshes;
}

TArray<FFreeRigSkinWeight> UFreeRigPSDImporter::CalculateVertexWeights(
    const FVector2D& VertexPos,
    const TArray<FFreeRigBoneData>& Bones,
    float MaxDistance)
{
    TArray<FFreeRigSkinWeight> Weights;

    // Find all bones within influence distance
    struct FBoneDistance
    {
        int32 Index;
        float Distance;
    };
    TArray<FBoneDistance> NearbyBones;

    for (int32 i = 0; i < Bones.Num(); i++)
    {
        float Distance = FVector2D::Distance(VertexPos, Bones[i].Position);
        if (Distance <= MaxDistance)
        {
            NearbyBones.Add({ i, Distance });
        }
    }

    if (NearbyBones.Num() == 0)
    {
        // Fall back to closest bone
        int32 ClosestIdx = 0;
        float ClosestDist = FLT_MAX;
        for (int32 i = 0; i < Bones.Num(); i++)
        {
            float Distance = FVector2D::Distance(VertexPos, Bones[i].Position);
            if (Distance < ClosestDist)
            {
                ClosestDist = Distance;
                ClosestIdx = i;
            }
        }

        FFreeRigSkinWeight Weight;
        Weight.BoneIndex = ClosestIdx;
        Weight.Weight = 1.0f;
        Weights.Add(Weight);
        return Weights;
    }

    // Sort by distance
    NearbyBones.Sort([](const FBoneDistance& A, const FBoneDistance& B) {
        return A.Distance < B.Distance;
        });

    // Take up to 4 closest bones
    int32 NumWeights = FMath::Min(4, NearbyBones.Num());
    float TotalWeight = 0.0f;

    for (int32 i = 0; i < NumWeights; i++)
    {
        float Weight = 1.0f - (NearbyBones[i].Distance / MaxDistance);
        Weight = FMath::Pow(Weight, 2.0f); // Quadratic falloff

        FFreeRigSkinWeight SkinWeight;
        SkinWeight.BoneIndex = NearbyBones[i].Index;
        SkinWeight.Weight = Weight;
        Weights.Add(SkinWeight);
        TotalWeight += Weight;
    }

    // Normalize weights
    if (TotalWeight > 0.0f)
    {
        for (FFreeRigSkinWeight& Weight : Weights)
        {
            Weight.Weight /= TotalWeight;
        }
    }

    return Weights;
}

void UFreeRigPSDImporter::BindParametersToBones(TArray<FFreeRigBoneData>& Bones)
{
    // Map common parameters to bone transforms
    for (FFreeRigBoneData& Bone : Bones)
    {
        if (Bone.BoneType == EBoneType::EyeLeft || Bone.BoneType == EBoneType::EyeRight)
        {
            // Eye bones will scale Y based on EyeOpen parameter
            // This is stored as metadata for the runtime
            Bone.Scale = FVector2D(1.0f, 0.5f); // Default half-closed
        }
        else if (Bone.BoneType == EBoneType::Mouth)
        {
            // Mouth bone will scale Y based on MouthOpen
            Bone.Scale = FVector2D(1.0f, 0.2f);
        }
        else if (Bone.BoneType == EBoneType::EyebrowLeft || Bone.BoneType == EBoneType::EyebrowRight)
        {
            // Eyebrows will rotate based on BrowUp
            Bone.Rotation = 0.0f;
        }
    }
}

// Helper to convert enum to string (since UEnum::GetValueAsString needs the enum class)
template<typename T>
FString EnumToString(const T& EnumValue)
{
    return UEnum::GetValueAsString(EnumValue);
}