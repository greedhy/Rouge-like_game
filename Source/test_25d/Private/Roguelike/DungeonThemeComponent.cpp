// 主要调用链：流式子关卡 → 本地房间材质/特效/灯色；玩家位置 → 当前房间 → 插值并更新全局环境。
#include "Roguelike/DungeonThemeComponent.h"
#include "Roguelike/DungeonRoomLightingRig.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataAsset.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/Level.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameters.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/Crc.h"
#include "Misc/PackageName.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "UObject/UnrealType.h"
#include <type_traits>
#if WITH_EDITOR
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Roguelike/DungeonGenerationRuntime.h"
#include "ShaderCompiler.h"
#include "Slate/SceneViewport.h"
#include "UnrealClient.h"
#endif

namespace
{
    const FName ThemeRoot(TEXT("Theme"));
    const FName FloorTag(TEXT("RG_Floor"));
    const FName ProbeTag(TEXT("RG_Probe"));
    const FName TileName(TEXT("SM_Tile_6x6"));
    const FName ColorTint(TEXT("ThemePrimaryTint"));
    const FName EmissiveStrength(TEXT("ThemeEmissiveIntensity"));
    const FName EmissiveTint(TEXT("ThemeEmissiveTint"));
    const FName ThemeAlpha(TEXT("ThemeBlend"));
    const FName ThemeIntensity(TEXT("ThemeIntensity"));

    // 主题数据仍由现有的蓝图 PrimaryDataAsset 定义，集中处理字段缺失与类型变更。
    template <typename T>
    T ReadValue(const UObject& Asset, const FName Name, T Fallback)
    {
        const FProperty* Field = Asset.GetClass()->FindPropertyByName(Name);
        if constexpr (std::is_same_v<T, FLinearColor>)
        {
            const auto* StructField = CastField<FStructProperty>(Field);
            if (StructField && StructField->Struct == TBaseStructure<FLinearColor>::Get())
            {
                return *StructField->ContainerPtrToValuePtr<FLinearColor>(&Asset);
            }
        }
        else
        {
            using FField = std::conditional_t<std::is_same_v<T, float>, FFloatProperty,
                                              std::conditional_t<std::is_same_v<T, FName>, FNameProperty, FStrProperty>>;
            if (const auto* TypedField = CastField<FField>(Field))
            {
                return TypedField->GetPropertyValue_InContainer(&Asset);
            }
        }
        return Fallback;
    }

    // 兼容已制作资产使用的包名形式，以及以后填写的完整对象路径。
    template <typename T>
    T* LoadThemeObject(FString Path)
    {
        if (!Path.StartsWith(TEXT("/Game/")))
        {
            return nullptr;
        }
        if (!Path.Contains(TEXT(".")))
        {
            Path += TEXT(".") + FPackageName::GetShortName(Path);
        }
        return Cast<T>(FSoftObjectPath(Path).TryLoad());
    }

    // 全局环境由宿主关卡持有，避免不小心改到某个子关卡的局部光照。
    template <typename T>
    T* FindPersistentActor(UWorld& World)
    {
        for (TActorIterator<T> It(&World); It; ++It)
        {
            if (It->GetLevel() == World.PersistentLevel)
            {
                return *It;
            }
        }
        return nullptr;
    }

    float BlendAlpha(float Elapsed, float Duration)
    {
        return FMath::SmoothStep(0.f, Duration, Elapsed);
    }

    // 复用每个主题地表实例已经配置过的 PBR 纹理，运行时只为固定的着色器写参数。
    void SetTerrainTextures(UMaterialInstanceDynamic& Instance, const UMaterialInterface& Source, const TCHAR* Side)
    {
        for (const TCHAR* Parameter : {TEXT("GroundBaseColor"), TEXT("GroundNormal"), TEXT("GroundORM")})
        {
            UTexture* Texture = nullptr;
            if (Source.GetTextureParameterValue(FMaterialParameterInfo(FName(Parameter)), Texture) && Texture)
            {
                Instance.SetTextureParameterValue(FName(FString(Side) + Parameter), Texture);
            }
        }
    }

#if WITH_EDITOR
    // 直接采集 PIE 世界的游戏视口，避开编辑器预览相机。
    bool CaptureGameViewport(UWorld& World, const FString& Name)
    {
        const UGameViewportClient* Client = World.GetGameViewport();
        FViewport* Viewport = Client ? Client->Viewport : nullptr;
        if (!Viewport || Name.IsEmpty() || FPaths::MakeValidFileName(Name) != Name)
        {
            return false;
        }

        Viewport->Draw();
        const FIntPoint Size = Viewport->GetSizeXY();
        TArray<FColor> Pixels;
        if (Size.X <= 0 || Size.Y <= 0 || !Viewport->ReadPixels(Pixels) || Pixels.Num() != Size.X * Size.Y)
        {
            return false;
        }
        for (FColor& Pixel : Pixels)
        {
            Pixel.A = 255;
        }
        TArray64<uint8> Png;
        FImageUtils::PNGCompressImageArray(Size.X, Size.Y, MakeArrayView64(Pixels), Png);
        const FString Directory = FPaths::ProjectSavedDir() / TEXT("DungeonThemeQA");
        IFileManager::Get().MakeDirectory(*Directory, true);
        return FFileHelper::SaveArrayToFile(Png, *(Directory / (Name + TEXT(".png"))));
    }

    // 使用常规胶囊占位校验切换验收视角，操作仅存在于开发版 PIE 世界。
    bool VisitRoom(UWorld& World, const FDungeonGenerationManifest& Manifest, FName RoomId)
    {
        const FDungeonRoomPlacement* Room = Manifest.Layout.Rooms.FindByPredicate([RoomId](const auto& Placement)
        {
            return Placement.RoomId == RoomId;
        });
        const APlayerController* Controller = GEngine->GetFirstLocalPlayerController(&World);
        APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
        if (!Pawn || !Room)
        {
            return false;
        }
        const FVector Target(Room->Cell.X * Manifest.RoomSpacing, Room->Cell.Y * Manifest.RoomSpacing,
                             Pawn->GetActorLocation().Z);
        return Pawn->TeleportTo(Target, Pawn->GetActorRotation());
    }

    // 允许在真实 PIE 地图复用运行时入口执行切换并采样 Shader 工作量。
    void ProbeTheme(const TArray<FString>& Args, UWorld* World)
    {
        if (!World || !World->IsGameWorld())
        {
            return;
        }
        for (TActorIterator<ADungeonGenerationRuntime> It(World); It; ++It)
        {
            UDungeonThemeComponent* Theme = It->FindComponentByClass<UDungeonThemeComponent>();
            if (Args.Num() == 2 && Args[0] == TEXT("Capture"))
            {
                const bool bCaptured = CaptureGameViewport(*World, Args[1]);
                UE_LOG(LogTemp, Display, TEXT("DungeonTheme test screenshot name=%s saved=%d"),
                       *Args[1], bCaptured);
            }
            else if (Args.Num() == 2 && Args[0] == TEXT("Visit"))
            {
                const bool bVisited = VisitRoom(*World, It->Manifest, FName(Args[1]));
                UE_LOG(LogTemp, Display, TEXT("DungeonTheme test visit room=%s accepted=%d"), *Args[1], bVisited);
            }
            else if (Args.Num() == 2 && Theme)
            {
                const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(Args[1]), false);
                const bool bApplied = Theme->ApplyRoomTheme(FName(Args[0]), Tag);
                UE_LOG(LogTemp, Display, TEXT("DungeonTheme test request room=%s theme=%s accepted=%d"),
                       *Args[0], *Args[1], bApplied);
            }
            UE_LOG(LogTemp, Display, TEXT("DungeonTheme test shader_jobs=%d %s"),
                   GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : -1,
                   *It->GetGenerationReport());
        }
    }

    FAutoConsoleCommandWithWorldAndArgs ThemeProbeCommand(
        TEXT("Dungeon.Theme.Probe"),
        TEXT("Print generation/theme state. Optional: RoomId ThemeId, Capture Name, or Visit RoomId."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ProbeTheme));
#endif
}

UDungeonThemeComponent::UDungeonThemeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UDungeonThemeComponent::BeginPlay()
{
    Super::BeginPlay();
    if (GetNetMode() == NM_DedicatedServer)
    {
        return;
    }

    const FGameplayTag Parent = FGameplayTag::RequestGameplayTag(ThemeRoot, false);
    for (const FName TagName : GetOwner()->Tags)
    {
        const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TagName, false);
        if (Tag.IsValid() && Tag != Parent && Tag.MatchesTag(Parent) && LoadPalettes())
        {
            bStandaloneRoom = true;
            FRoomVisual& Room = Rooms.AddDefaulted_GetRef();
            Room.Id = GetOwner()->GetFName();
            Room.Marker = GetOwner();
            CollectSurfaces(*GetOwner()->GetLevel(), Room);
            CaptureEnvironment();
            ApplyRoomTheme(Room.Id, Tag);
            SetComponentTickEnabled(true);
            return;
        }
    }
}

bool UDungeonThemeComponent::LoadPalettes()
{
    if (!Palettes.IsEmpty())
    {
        return true;
    }

    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TArray<FAssetData> Assets;
    const FName Path(TEXT("/Game/ThemeSystem/Themes"));
    Registry.ScanPathsSynchronous({Path.ToString()});
    Registry.GetAssetsByPath(Path, Assets, true);
    const FGameplayTag Parent = FGameplayTag::RequestGameplayTag(ThemeRoot, false);
    for (const FAssetData& AssetData : Assets)
    {
        const auto* Asset = Cast<UPrimaryDataAsset>(AssetData.GetAsset());
        if (!Asset)
        {
            continue;
        }
        const FName Id = ReadValue(*Asset, TEXT("ThemeId"), FName());
        const FString TagName = Id.ToString();
        const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
            TagName.StartsWith(TEXT("Theme.")) ? Id : FName(TEXT("Theme.") + TagName), false);
        if (!Tag.IsValid() || Tag == Parent || !Tag.MatchesTag(Parent) || Palettes.Contains(Tag))
        {
            UE_LOG(LogTemp, Warning, TEXT("DungeonTheme skipped invalid or repeated ThemeId in %s"),
                   *AssetData.GetObjectPathString());
            continue;
        }

        FPalette& Palette = Palettes.Add(Tag);
        FEnvironment& Value = Palette.Values;
        Value.Primary = ReadValue(*Asset, TEXT("PrimaryTint"), Value.Primary);
        Value.Secondary = ReadValue(*Asset, TEXT("SecondaryTint"), Value.Secondary);
        Value.Emissive = ReadValue(*Asset, TEXT("EmissiveTint"), Value.Emissive);
        Value.Emission = FMath::Clamp(ReadValue(*Asset, TEXT("EmissiveIntensity"), 0.f), 0.f, 8.f);
        Value.Fog = ReadValue(*Asset, TEXT("FogInscatterColor"), Value.Fog);
        Value.FogDensity = FMath::Max(ReadValue(*Asset, TEXT("FogDensity"), 0.f), 0.f);
        Value.Sky = ReadValue(*Asset, TEXT("SkyLightColor"), Value.Sky);
        Value.Sun = ReadValue(*Asset, TEXT("DirLightColor"), Value.Sun);
        Value.SunIntensity = FMath::Max(ReadValue(*Asset, TEXT("DirLightIntensity"), 1.f), 0.f);
        Value.Post = ReadValue(*Asset, TEXT("PostProcessTint"), Value.Post);
        Value.Bloom = FMath::Max(ReadValue(*Asset, TEXT("BloomIntensity"), 0.f), 0.f);
        Palette.Duration = FMath::Clamp(ReadValue(*Asset, TEXT("BlendDuration"), 1.f), .5f, 1.5f);
        Palette.GroundPath = ReadValue(*Asset, TEXT("GroundMaterialPath"), FString());
        Palette.NiagaraPath = ReadValue(*Asset, TEXT("NiagaraSetPath"), FString());
        for (int32 Index = 0; Index < 4; ++Index)
        {
            const FName Key(FString::Printf(TEXT("Tier%dIntensity"), Index));
            Palette.Strength[Index] = FMath::Clamp(ReadValue(*Asset, Key, Palette.Strength[Index]), 0.f, 1.f);
        }
        if (Tag.GetTagName() != FName(TEXT("Theme.Boss")))
        {
            ThemeOrder.Add(Tag);
        }
    }
    ThemeOrder.Sort([](const FGameplayTag& A, const FGameplayTag& B)
    {
        return A.GetTagName().LexicalLess(B.GetTagName());
    });
    return !Palettes.IsEmpty();
}

TArray<int32> UDungeonThemeComponent::CalculateRoomDepths(const FDungeonLayout& Layout)
{
    TArray<int32> Depths;
    Depths.Init(INDEX_NONE, Layout.Rooms.Num());
    if (Depths.IsEmpty())
    {
        return Depths;
    }

    TArray<TArray<int32>> Adjacent;
    Adjacent.SetNum(Depths.Num());
    for (const FDungeonRoomConnection& Edge : Layout.Connections)
    {
        if (Depths.IsValidIndex(Edge.From) && Depths.IsValidIndex(Edge.To))
        {
            Adjacent[Edge.From].Add(Edge.To);
            Adjacent[Edge.To].Add(Edge.From);
        }
    }

    Depths[0] = 0;
    TArray<int32> Queue{0};
    for (int32 Index = 0; Index < Queue.Num(); ++Index)
    {
        const int32 Room = Queue[Index];
        for (const int32 Neighbor : Adjacent[Room])
        {
            if (Depths[Neighbor] == INDEX_NONE)
            {
                Depths[Neighbor] = Depths[Room] + 1;
                Queue.Add(Neighbor);
            }
        }
    }
    return Depths;
}

FGameplayTag UDungeonThemeComponent::ChooseTheme(const FDungeonRoomPlacement& Placement, int32 Seed) const
{
    FGameplayTag Preferred;
    if (Placement.bPersistent)
    {
        Preferred = FGameplayTag::RequestGameplayTag(TEXT("Theme.Frost"), false);
    }
    else if (Placement.Role == EDungeonRoomRole::Boss)
    {
        Preferred = FGameplayTag::RequestGameplayTag(TEXT("Theme.Boss"), false);
    }
    if (Palettes.Contains(Preferred) || ThemeOrder.IsEmpty())
    {
        return Preferred;
    }
    const uint32 Key = HashCombineFast(static_cast<uint32>(Seed), FCrc::StrCrc32(*Placement.RoomId.ToString()));
    return ThemeOrder[Key % static_cast<uint32>(ThemeOrder.Num())];
}

void UDungeonThemeComponent::InitializeFloor(const FDungeonLayout& Layout, float RoomSpacing,
                                             const TArray<ULevel*>& Levels)
{
    if (GetNetMode() == NM_DedicatedServer || !Rooms.IsEmpty() || Levels.Num() != Layout.Rooms.Num() ||
        !LoadPalettes())
    {
        return;
    }
    Spacing = RoomSpacing;
    CaptureEnvironment();
    const TArray<int32> Depths = CalculateRoomDepths(Layout);
    for (int32 Index = 0; Index < Levels.Num(); ++Index)
    {
        if (!Levels[Index] || Depths[Index] < 0)
        {
            continue;
        }
        const FDungeonRoomPlacement& Placement = Layout.Rooms[Index];
        FRoomVisual& Room = Rooms.AddDefaulted_GetRef();
        Room.Id = Placement.RoomId;
        Room.Cell = Placement.Cell;
        Room.Depth = Depths[Index];
        Room.Role = Placement.Role;
        CollectSurfaces(*Levels[Index], Room);
        ApplyRoomTheme(Room.Id, ChooseTheme(Placement, Layout.Seed));
    }
    SetComponentTickEnabled(!Rooms.IsEmpty());
    UE_LOG(LogTemp, Display, TEXT("DungeonTheme initialized %d room(s) for seed=%d"), Rooms.Num(), Layout.Seed);
}

void UDungeonThemeComponent::CollectSurfaces(ULevel& Level, FRoomVisual& Room)
{
    for (AActor* Actor : Level.Actors)
    {
        if (!IsValid(Actor) || Actor->IsHidden())
        {
            continue;
        }
        if (auto* Lighting = Cast<ADungeonRoomLightingRig>(Actor))
        {
            Room.Lighting = Lighting;
        }
        const bool bIsRoomGeometry = bStandaloneRoom
                                         ? (Actor == GetOwner() || Actor->IsAttachedTo(GetOwner()))
                                         : (Actor->ActorHasTag(FloorTag) || Actor->ActorHasTag(ProbeTag));
        if (!bIsRoomGeometry)
        {
            continue;
        }
        TInlineComponentArray<UStaticMeshComponent*> Components(Actor);
        for (UStaticMeshComponent* Mesh : Components)
        {
            const UStaticMesh* Asset = Mesh->GetStaticMesh();
            if (!Asset || !Asset->GetPathName().StartsWith(TEXT("/Game/Assets/Dungeon/")))
            {
                continue;
            }
            const bool bGround = Asset->GetFName() == TileName;
            for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
            {
                if (UMaterialInterface* Material = Mesh->GetMaterial(Slot))
                {
                    Room.Surfaces.Add({Mesh, Material, Slot, bGround});
                }
            }
            if (bGround && !Room.Marker.IsValid())
            {
                Room.Marker = Actor;
            }
        }
    }
}

bool UDungeonThemeComponent::ApplyRoomTheme(FName RoomId, FGameplayTag ThemeId)
{
    const FPalette* Palette = Palettes.Find(ThemeId);
    const int32 RoomIndex = Rooms.IndexOfByPredicate([RoomId](const FRoomVisual& Room)
    {
        return Room.Id == RoomId;
    });
    if (!Palette || !Rooms.IsValidIndex(RoomIndex))
    {
        return false;
    }
    FRoomVisual& Room = Rooms[RoomIndex];
    if (Room.Theme == ThemeId)
    {
        Room.PendingTheme = FGameplayTag();
        return true;
    }
    if (Room.Theme.IsValid() && Room.Elapsed < Room.Duration)
    {
        // 纹理只有两组输入。当前过渡结束后再采用最后一次请求，保留画面的连续性。
        Room.PendingTheme = ThemeId;
        return true;
    }
    if (!MasterMaterial)
    {
        MasterMaterial = LoadThemeObject<UMaterialInterface>(TEXT("/Game/ThemeSystem/Materials/M_ThemeMaster"));
    }
    TObjectPtr<UMaterialInterface>& Ground = LoadedGrounds.FindOrAdd(ThemeId);
    if (!Ground)
    {
        Ground = LoadThemeObject<UMaterialInterface>(Palette->GroundPath);
    }
    if ((!Ground || !MasterMaterial) && !Room.Surfaces.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("DungeonTheme %s has invalid ground material %s"), *ThemeId.ToString(),
               *Palette->GroundPath);
        return false;
    }

    Room.Start = Room.Theme.IsValid() ? Blend(Room.Start, Room.Target, BlendAlpha(Room.Elapsed, Room.Duration))
                                     : FEnvironment();
    Room.Target = Palette->Values;
    // 高度雾是全局环境，按本地房型过渡而不是在每个子关卡叠加全局雾。
    const float FogScale = Room.Role == EDungeonRoomRole::Boss ? 2.2f :
                           (Room.Role == EDungeonRoomRole::Elite ? 1.6f : 1.f);
    Room.Target.FogDensity *= FogScale;
    const float Tier = bStandaloneRoom ? 1.f : FMath::Clamp(static_cast<float>(Room.Depth) / 3.f, 0.f, 3.f);
    const int32 Lower = FMath::FloorToInt(Tier);
    const float Strength = FMath::Lerp(Palette->Strength[Lower], Palette->Strength[FMath::Min(Lower + 1, 3)],
                                      Tier - Lower);
    Room.Target.Primary = FMath::Lerp(FLinearColor::White, Room.Target.Primary, Strength);
    Room.Target.Secondary = FMath::Lerp(FLinearColor::White, Room.Target.Secondary, Strength);
    Room.Target.Emission *= Strength;
    Room.Target.Intensity = Strength;
    Room.Elapsed = 0.f;
    Room.Duration = Palette->Duration;

    if (AActor* Marker = Room.Marker.Get())
    {
        Marker->Tags.Remove(Room.Theme.GetTagName());
        Marker->Tags.AddUnique(ThemeId.GetTagName());
    }
    Room.Theme = ThemeId;
    if (Ground && MasterMaterial && !Room.Surfaces.IsEmpty())
    {
        UMaterialInterface* Previous = Room.GroundSource.IsValid() ? Room.GroundSource.Get() : Ground.Get();
        Room.Ground = UMaterialInstanceDynamic::Create(MasterMaterial, this);
        Room.Detail = UMaterialInstanceDynamic::Create(MasterMaterial, this);
        Room.Ground->SetScalarParameterValue(TEXT("ThemeUVScale"), 3.f);
        for (UMaterialInstanceDynamic* Instance : {Room.Ground.Get(), Room.Detail.Get()})
        {
            SetTerrainTextures(*Instance, *Previous, TEXT("From"));
            SetTerrainTextures(*Instance, *Ground, TEXT("To"));
        }
        Room.GroundSource = Ground.Get();
        for (const FSurface& Surface : Room.Surfaces)
        {
            if (UStaticMeshComponent* Mesh = Surface.Mesh.Get())
            {
                Mesh->SetMaterial(Surface.Slot, Surface.bGround ? Room.Ground.Get() : Room.Detail.Get());
            }
        }
        UpdateRoomMaterials(Room, Room.Start, 0.f);
    }
    if (UNiagaraComponent* Old = Room.Ambience.Get())
    {
        Old->DestroyComponent();
    }
    if (UNiagaraSystem* Effect = LoadThemeObject<UNiagaraSystem>(Palette->NiagaraPath))
    {
        const FVector Location(
            Room.Cell.X * Spacing + Spacing * .32f, Room.Cell.Y * Spacing + Spacing * .32f, 20.f);
        const FVector Position = bStandaloneRoom ? GetOwner()->GetActorLocation() : Location;
        Room.Ambience = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
            this, Effect, Position, FRotator::ZeroRotator, FVector(.35f), false, false,
            ENCPoolMethod::None, false);
    }
    if (ActiveRoom == RoomIndex)
    {
        ActiveRoom = INDEX_NONE;
    }
    return true;
}

void UDungeonThemeComponent::CaptureEnvironment()
{
    UWorld& World = *GetWorld();
    Fog = FindPersistentActor<AExponentialHeightFog>(World);
    Sky = FindPersistentActor<ASkyLight>(World);
    Sun = FindPersistentActor<ADirectionalLight>(World);
    Post = FindPersistentActor<APostProcessVolume>(World);
    Collection = LoadThemeObject<UMaterialParameterCollection>(TEXT("/Game/ThemeSystem/MPC_ThemeGlobals"));
    if (Fog.IsValid())
    {
        const UExponentialHeightFogComponent* Component = Fog->GetComponent();
        OriginalEnvironment.Fog = Component->FogInscatteringLuminance;
        OriginalEnvironment.FogDensity = Component->FogDensity;
    }
    if (Sky.IsValid())
    {
        OriginalEnvironment.Sky = Sky->GetLightComponent()->GetLightColor();
    }
    if (Sun.IsValid())
    {
        OriginalEnvironment.Sun = Sun->GetLightComponent()->GetLightColor();
        OriginalEnvironment.SunIntensity = Sun->GetLightComponent()->Intensity;
    }
    if (Post.IsValid())
    {
        OriginalEnvironment.Post = Post->Settings.SceneColorTint;
        OriginalEnvironment.Bloom = Post->Settings.BloomIntensity;
        bOriginalPostTintOverride = Post->Settings.bOverride_SceneColorTint;
        bOriginalBloomOverride = Post->Settings.bOverride_BloomIntensity;
    }
    EnvironmentCurrent = OriginalEnvironment;
    bHasEnvironment = true;
}

void UDungeonThemeComponent::UpdateEnvironment(const FEnvironment& Value, float Alpha, bool bRestore)
{
    if (Fog.IsValid())
    {
        Fog->GetComponent()->SetFogInscatteringColor(Value.Fog);
        Fog->GetComponent()->SetFogDensity(Value.FogDensity);
    }
    if (Sky.IsValid())
    {
        Sky->GetLightComponent()->SetLightColor(Value.Sky);
    }
    if (Sun.IsValid())
    {
        Sun->GetLightComponent()->SetLightColor(Value.Sun);
        Sun->GetLightComponent()->SetIntensity(Value.SunIntensity);
    }
    if (Post.IsValid())
    {
        Post->Settings.SceneColorTint = Value.Post;
        Post->Settings.BloomIntensity = Value.Bloom;
        Post->Settings.bOverride_SceneColorTint = !bRestore || bOriginalPostTintOverride;
        Post->Settings.bOverride_BloomIntensity = !bRestore || bOriginalBloomOverride;
    }
    if (Collection.IsValid())
    {
        UMaterialParameterCollectionInstance* MPC = GetWorld()->GetParameterCollectionInstance(Collection.Get());
        if (MPC)
        {
            MPC->SetVectorParameterValue(TEXT("ThemeFogColor"), Value.Fog);
            MPC->SetScalarParameterValue(TEXT("ThemeFogDensity"), Value.FogDensity);
            MPC->SetVectorParameterValue(TEXT("ThemeSkyColor"), Value.Sky);
            MPC->SetScalarParameterValue(ThemeAlpha, Alpha);
            MPC->SetScalarParameterValue(ThemeIntensity, Value.Intensity);
        }
    }
}

int32 UDungeonThemeComponent::FindLocalRoom() const
{
    if (bStandaloneRoom)
    {
        return 0;
    }
    const APlayerController* PC = GEngine->GetFirstLocalPlayerController(GetWorld());
    if (!PC || !PC->GetPawn() || Spacing <= 0.f)
    {
        return INDEX_NONE;
    }
    const FVector Location = PC->GetPawn()->GetActorLocation();
    const FIntPoint Cell(FMath::FloorToInt(Location.X / Spacing + .5),
                        FMath::FloorToInt(Location.Y / Spacing + .5));
    return Rooms.IndexOfByPredicate([Cell](const FRoomVisual& Room)
    {
        return Room.Cell == Cell && Room.Theme.IsValid();
    });
}

void UDungeonThemeComponent::UpdateRoomMaterials(const FRoomVisual& Room, const FEnvironment& Value, float Alpha) const
{
    if (Room.Lighting.IsValid())
    {
        Room.Lighting->SetThemeColor(Value.Emissive);
    }
    if (Room.Ground.IsValid())
    {
        Room.Ground->SetVectorParameterValue(ColorTint, Value.Primary);
        Room.Ground->SetVectorParameterValue(EmissiveTint, Value.Emissive);
        Room.Ground->SetScalarParameterValue(EmissiveStrength, Value.Emission);
        Room.Ground->SetScalarParameterValue(ThemeAlpha, Alpha);
    }
    if (Room.Detail.IsValid())
    {
        Room.Detail->SetVectorParameterValue(ColorTint, Value.Secondary);
        Room.Detail->SetVectorParameterValue(EmissiveTint, Value.Emissive);
        Room.Detail->SetScalarParameterValue(EmissiveStrength, Value.Emission * .35f);
        Room.Detail->SetScalarParameterValue(ThemeAlpha, Alpha);
    }
}

UDungeonThemeComponent::FEnvironment UDungeonThemeComponent::Blend(
    const FEnvironment& From, const FEnvironment& To, float Alpha)
{
    FEnvironment Value;
    Value.Fog = FMath::Lerp(From.Fog, To.Fog, Alpha);
    Value.Sky = FMath::Lerp(From.Sky, To.Sky, Alpha);
    Value.Sun = FMath::Lerp(From.Sun, To.Sun, Alpha);
    Value.Post = FMath::Lerp(From.Post, To.Post, Alpha);
    Value.Primary = FMath::Lerp(From.Primary, To.Primary, Alpha);
    Value.Secondary = FMath::Lerp(From.Secondary, To.Secondary, Alpha);
    Value.Emissive = FMath::Lerp(From.Emissive, To.Emissive, Alpha);
    Value.FogDensity = FMath::Lerp(From.FogDensity, To.FogDensity, Alpha);
    Value.SunIntensity = FMath::Lerp(From.SunIntensity, To.SunIntensity, Alpha);
    Value.Bloom = FMath::Lerp(From.Bloom, To.Bloom, Alpha);
    Value.Emission = FMath::Lerp(From.Emission, To.Emission, Alpha);
    Value.Intensity = FMath::Lerp(From.Intensity, To.Intensity, Alpha);
    return Value;
}

void UDungeonThemeComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                           FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime, TickType, TickFunction);
    const int32 LocalRoom = FindLocalRoom();
    if (Rooms.IsValidIndex(LocalRoom) && LocalRoom != ActiveRoom)
    {
        if (Rooms.IsValidIndex(ActiveRoom) && Rooms[ActiveRoom].Ambience.IsValid())
        {
            Rooms[ActiveRoom].Ambience->Deactivate();
        }
        ActiveRoom = LocalRoom;
        const FRoomVisual& Room = Rooms[ActiveRoom];
        if (Room.Ambience.IsValid())
        {
            Room.Ambience->Activate();
        }
        EnvironmentFrom = EnvironmentCurrent;
        EnvironmentTarget = Room.Target;
        EnvironmentElapsed = 0.f;
        EnvironmentDuration = Room.Duration;
    }
    for (FRoomVisual& Room : Rooms)
    {
        if (Room.Theme.IsValid() && Room.Elapsed < Room.Duration)
        {
            Room.Elapsed = FMath::Min(Room.Elapsed + DeltaTime, Room.Duration);
            const float Alpha = BlendAlpha(Room.Elapsed, Room.Duration);
            UpdateRoomMaterials(Room, Blend(Room.Start, Room.Target, Alpha), Alpha);
        }
        if (Room.PendingTheme.IsValid() && Room.Elapsed >= Room.Duration)
        {
            const FGameplayTag Next = Room.PendingTheme;
            Room.PendingTheme = FGameplayTag();
            ApplyRoomTheme(Room.Id, Next);
        }
    }
    if (Rooms.IsValidIndex(ActiveRoom) && EnvironmentElapsed < EnvironmentDuration)
    {
        EnvironmentElapsed = FMath::Min(EnvironmentElapsed + DeltaTime, EnvironmentDuration);
        const float Alpha = BlendAlpha(EnvironmentElapsed, EnvironmentDuration);
        EnvironmentCurrent = Blend(EnvironmentFrom, EnvironmentTarget, Alpha);
        UpdateEnvironment(EnvironmentCurrent, Alpha);
    }
}

FString UDungeonThemeComponent::GetThemeReport() const
{
    if (!Rooms.IsValidIndex(ActiveRoom))
    {
        return FString::Printf(TEXT("rooms=%d active=none"), Rooms.Num());
    }
    const FRoomVisual& Room = Rooms[ActiveRoom];
    return FString::Printf(TEXT("rooms=%d active=%s theme=%s depth=%d surfaces=%d blend=%.3f strength=%.3f fx=%d"),
                            Rooms.Num(), *Room.Id.ToString(), *Room.Theme.ToString(), Room.Depth, Room.Surfaces.Num(),
                            BlendAlpha(EnvironmentElapsed, EnvironmentDuration), EnvironmentCurrent.Intensity,
                            Room.Ambience.IsValid() ? 1 : 0);
}

void UDungeonThemeComponent::EndPlay(EEndPlayReason::Type Reason)
{
    for (const FRoomVisual& Room : Rooms)
    {
        if (AActor* Marker = Room.Marker.Get(); Marker && !bStandaloneRoom)
        {
            Marker->Tags.Remove(Room.Theme.GetTagName());
        }
        for (const FSurface& Surface : Room.Surfaces)
        {
            if (UStaticMeshComponent* Mesh = Surface.Mesh.Get())
            {
                Mesh->SetMaterial(Surface.Slot, Surface.Original.Get());
            }
        }
        if (UNiagaraComponent* Effect = Room.Ambience.Get())
        {
            Effect->DestroyComponent();
        }
    }
    if (bHasEnvironment && IsValid(GetWorld()))
    {
        UpdateEnvironment(OriginalEnvironment, 0.f, true);
    }
    Rooms.Reset();
    Super::EndPlay(Reason);
}
