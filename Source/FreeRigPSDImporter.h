#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "FreeRigPSDImporter.generated.h"

// Forward declarations
struct FFreeRigPSDLayer;
struct FFreeRigPSDImportResult;
struct FFreeRigParameterDef;
struct FFreeRigMeshData;
static FFreeRigMeshData GenerateMeshForLayer(const FFreeRigPSDLayer& Layer, float EdgeDensity);

// Enums
UENUM(BlueprintType)
enum class EBoneType : uint8
{
    Root,
    Head,
    Neck,
    Body,
    EyeLeft,
    EyeRight,
    EyebrowLeft,
    EyebrowRight,
    Mouth,
    HairFront,
    HairBack,
    Accessory
};

UENUM(BlueprintType)
enum class ELayerRole : uint8
{
    Unknown,
    Head,
    Body,
    LeftEye,
    RightEye,
    Mouth,
    HairFront,
    HairBack,
    EyebrowLeft,
    EyebrowRight,
    Nose,
    Ear,
    Accessory
};

// Structs
USTRUCT(BlueprintType)
struct FFreeRigParameterFrame
{
    GENERATED_BODY()

    UPROPERTY()
    float Value = 0.0f;

    UPROPERTY()
    int32 LayerIndex = -1;
};

USTRUCT(BlueprintType)
struct FFreeRigParameterDef
{
    GENERATED_BODY()

    UPROPERTY()
    FString Name;

    UPROPERTY()
    float Min = 0.0f;

    UPROPERTY()
    float Max = 1.0f;

    UPROPERTY()
    float Default = 0.0f;

    UPROPERTY()
    FString Mode = TEXT("keyframe");

    UPROPERTY()
    TArray<FFreeRigParameterFrame> Frames;

    TMap<FString, TArray<int32>> DiscreteOptions;
};

USTRUCT(BlueprintType)
struct FFreeRigPSDLayer
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FString Name;

    UPROPERTY(BlueprintReadOnly)
    int32 X = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 Y = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 Width = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 Height = 0;

    UPROPERTY(BlueprintReadOnly)
    bool bVisible = true;

    UPROPERTY(BlueprintReadOnly)
    float Opacity = 1.0f;

    UPROPERTY(BlueprintReadOnly)
    ELayerRole DetectedRole = ELayerRole::Unknown;

    TArray<uint8> PixelData;
};

USTRUCT(BlueprintType)
struct FFreeRigSkinWeight
{
    GENERATED_BODY()

    UPROPERTY()
    int32 BoneIndex = -1;

    UPROPERTY()
    float Weight = 0.0f;
};

USTRUCT(BlueprintType)
struct FFreeRigSkinnedVertex
{
    GENERATED_BODY()

    UPROPERTY()
    FVector2D Position;

    UPROPERTY()
    FVector2D UV;

    UPROPERTY()
    TArray<FFreeRigSkinWeight> Weights;
};

USTRUCT(BlueprintType)
struct FFreeRigSkinnedMesh
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FFreeRigSkinnedVertex> Vertices;

    UPROPERTY()
    TArray<int32> Triangles;

    UPROPERTY()
    int32 LayerIndex = -1;

    UPROPERTY()
    FString LayerName;
};

USTRUCT(BlueprintType)
struct FFreeRigPSDImportResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    int32 Width = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 Height = 0;

    UPROPERTY(BlueprintReadOnly)
    TArray<FFreeRigPSDLayer> Layers;

    UPROPERTY(BlueprintReadOnly)
    bool bSuccess = false;

    UPROPERTY(BlueprintReadOnly)
    FString ErrorMessage;
};

USTRUCT(BlueprintType)
struct FFreeRigMeshVertex
{
    GENERATED_BODY()

    UPROPERTY()
    FVector2D Position;

    UPROPERTY()
    FVector2D UV;

    UPROPERTY()
    TArray<float> BoneWeights;

    UPROPERTY()
    TArray<int32> BoneIndices;
};

USTRUCT(BlueprintType)
struct FFreeRigMeshData
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FFreeRigMeshVertex> Vertices;

    UPROPERTY()
    TArray<int32> Triangles;

    UPROPERTY()
    int32 Width = 0;

    UPROPERTY()
    int32 Height = 0;

    UPROPERTY()
    FString LayerName;
};

USTRUCT(BlueprintType)
struct FFreeRigBoneData
{
    GENERATED_BODY()

    UPROPERTY()
    FString BoneName;

    UPROPERTY()
    EBoneType BoneType = EBoneType::Root;

    UPROPERTY()
    FString ParentBone;

    UPROPERTY()
    FVector2D Position;

    UPROPERTY()
    float Rotation = 0.0f;

    UPROPERTY()
    FVector2D Scale = FVector2D(1.0f, 1.0f);

    UPROPERTY()
    TArray<int32> BoundLayerIndices;
};

// Factory class
UCLASS()
class FREERIG_VT_API UFreeRigPSDImporter : public UFactory
{
    GENERATED_BODY()

public:
    UFreeRigPSDImporter();


    virtual UObject* FactoryCreateFile(
        UClass* InClass,
        UObject* InParent,
        FName InName,
        EObjectFlags Flags,
        const FString& Filename,
        const TCHAR* Parms,
        FFeedbackContext* Warn,
        bool& bOutOperationCanceled
    ) override;

    virtual bool FactoryCanImport(const FString& Filename) override;

    // PSD parsing
    static FFreeRigPSDImportResult ParsePSDFile(const FString& FilePath);
    static FFreeRigPSDImportResult ImportFromFolder(const FString& FolderPath);

    // Auto detection
    static void PositionLayersByHeuristic(TArray<FFreeRigPSDLayer>& Layers, int32 CanvasWidth, int32 CanvasHeight);
    static void AutoDetectLayerRoles(FFreeRigPSDImportResult& ImportResult);
    static bool AutoDetectParameters(const FFreeRigPSDImportResult& PSDData, TArray<FFreeRigParameterDef>& OutParameters, TArray<int32>& OutAlwaysVisible);
    static void AddBonesToJSON(TSharedPtr<FJsonObject> Root, const FFreeRigPSDImportResult& PSDData);

    // JSON export
    static bool ExportToJSON(const FFreeRigPSDImportResult& PSDData, const TArray<FFreeRigParameterDef>& Parameters, const TArray<int32>& AlwaysVisible, const FString& OutputPath);

    // Mesh generation
    static FFreeRigMeshData GenerateMeshForLayer(const FFreeRigPSDLayer& Layer, float EdgeDensity = 0.05f);
    static TArray<FVector2D> DetectEdges(const TArray<uint8>& PixelData, int32 Width, int32 Height, float AlphaThreshold = 10);
    static TArray<FVector2D> SimplifyPolygon(const TArray<FVector2D>& Points, float Tolerance = 2.0f);
    static TArray<FVector2D> GenerateAdaptiveGrid(const TArray<FVector2D>& EdgePoints, const FBox2D& Bounds, float Density);
    static TArray<int32> TriangulateMesh(const TArray<FVector2D>& Points);



    // Bone placement
    static TArray<FFreeRigBoneData> GenerateAutoBones(const FFreeRigPSDImportResult& PSDData);
    static void AssignLayersToBones(TArray<FFreeRigBoneData>& Bones, const FFreeRigPSDImportResult& PSDData);
    static FFreeRigBoneData CreateBone(EBoneType Type, const FString& Name, const FString& Parent, const FVector2D& Position);

    // Skinning
    static TArray<FFreeRigSkinnedMesh> GenerateSkinnedMeshes(
        const FFreeRigPSDImportResult& PSDData,
        const TArray<FFreeRigBoneData>& Bones,
        float MaxInfluenceDistance = 50.0f
    );
    static TArray<FFreeRigSkinWeight> CalculateVertexWeights(
        const FVector2D& VertexPos,
        const TArray<FFreeRigBoneData>& Bones,
        float MaxDistance
    );

    static void BindParametersToBones(TArray<FFreeRigBoneData>& Bones);

private:
    // PSD parsing helpers
    struct FPSDHeader
    {
        uint16 Version;
        int32 Height;
        int32 Width;
        uint16 Channels;
        uint16 Depth;
        uint16 ColorMode;
    };

    struct FPSDChannel
    {
        int16 ID;
        uint32 DataLength;
        TArray<uint8> Data;
    };

    struct FPSDLayerRecord
    {
        FString Name;
        int32 Top, Left, Bottom, Right;
        uint16 ChannelCount;
        TArray<FPSDChannel> Channels;
        uint8 Opacity;
        uint8 Clipping;
        uint8 Flags;
        bool bVisible;
        FString BlendMode;
    };

    static bool ParseHeader(const TArray<uint8>& Data, int32& Offset, FPSDHeader& OutHeader);
    static bool ParseLayerAndMaskInfo(const TArray<uint8>& Data, int32& Offset, TArray<FPSDLayerRecord>& OutLayers);
    static TArray<uint8> DecompressRLE(const TArray<uint8>& CompressedData, int32 ExpectedUncompressedSize);
    static TArray<uint8> ExtractLayerRGBA(const FPSDLayerRecord& Layer, int32 DocumentWidth, int32 DocumentHeight);
    static UTexture2D* CreateTextureFromRGBA(const TArray<uint8>& RGBA, int32 Width, int32 Height, const FString& Name, UObject* Outer);

    static FVector2D GetLayerCenter(const FFreeRigPSDLayer& Layer);
    static FBox2D GetModelBounds(const FFreeRigPSDImportResult& PSDData);
};