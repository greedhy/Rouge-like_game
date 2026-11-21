// 主要调用链：房间流式加载完成 → InitializeFloor → 主题数据、局部材质和特效；本地玩家换房 → 环境插值。
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Roguelike/DungeonGenerationTypes.h"
#include "DungeonThemeComponent.generated.h"

class ADirectionalLight;
class ADungeonRoomLightingRig;
class AExponentialHeightFog;
class APostProcessVolume;
class ASkyLight;
class ULevel;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraComponent;
class UStaticMeshComponent;

/** 本地视觉组件。房间装配向它提供只读元数据与已加载关卡。 */
UCLASS(Blueprintable, ClassGroup = (Dungeon), meta = (BlueprintSpawnableComponent))
class UDungeonThemeComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDungeonThemeComponent();

    // 首次处理可见关卡。加载失败的主题保持原场景表现。
    void InitializeFloor(const FDungeonLayout& Layout, float RoomSpacing, const TArray<ULevel*>& Levels);

    // 为指定房间应用一个已注册的主题，可在运行时重新调用。
    UFUNCTION(BlueprintCallable, Category = "Dungeon|Theme")
    bool ApplyRoomTheme(FName RoomId, FGameplayTag ThemeId);

    // 返回当前本地视角的主题和插值进度，供 PIE 回读。
    UFUNCTION(BlueprintPure, Category = "Dungeon|Theme")
    FString GetThemeReport() const;

    // 使用清单中的通路计算从入口到各房间的最短步数。
    static TArray<int32> CalculateRoomDepths(const FDungeonLayout& Layout);

protected:
    // 支持独立房间 Actor 上挂载的蓝图组件，其 Owner 可以配置 Theme.* 标签。
    void BeginPlay() override;
    void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    void EndPlay(EEndPlayReason::Type Reason) override;

private:
    struct FEnvironment
    {
        FLinearColor Fog = FLinearColor::White;
        FLinearColor Sky = FLinearColor::White;
        FLinearColor Sun = FLinearColor::White;
        FLinearColor Post = FLinearColor::White;
        FLinearColor Primary = FLinearColor::White;
        FLinearColor Secondary = FLinearColor::White;
        FLinearColor Emissive = FLinearColor::Black;
        float FogDensity = 0.f;
        float SunIntensity = 0.f;
        float Bloom = 0.f;
        float Emission = 0.f;
        float Intensity = 0.f;
    };

    struct FPalette
    {
        FEnvironment Values;
        float Strength[4] = {0.15f, 0.4f, 0.7f, 1.f};
        FString GroundPath;
        FString NiagaraPath;
        float Duration = 1.f;
    };

    struct FSurface
    {
        TWeakObjectPtr<UStaticMeshComponent> Mesh;
        TWeakObjectPtr<UMaterialInterface> Original;
        int32 Slot = 0;
        bool bGround = true;
    };

    struct FRoomVisual
    {
        FName Id;
        FIntPoint Cell = FIntPoint::ZeroValue;
        int32 Depth = 0;
        EDungeonRoomRole Role = EDungeonRoomRole::Combat;
        FGameplayTag Theme;
        TArray<FSurface> Surfaces;
        TWeakObjectPtr<AActor> Marker;
        TWeakObjectPtr<UMaterialInstanceDynamic> Ground;
        TWeakObjectPtr<UMaterialInstanceDynamic> Detail;
        TWeakObjectPtr<UNiagaraComponent> Ambience;
        TWeakObjectPtr<ADungeonRoomLightingRig> Lighting;
        TWeakObjectPtr<UMaterialInterface> GroundSource;
        FGameplayTag PendingTheme;
        FEnvironment Start;
        FEnvironment Target;
        float Elapsed = 0.f;
        float Duration = 1.f;
    };

    // 从现有蓝图 DataAsset 读取美术字段，注册表允许新增资产自动参与主题选择。
    bool LoadPalettes();
    // 从指定子关卡采集地面与装饰的材质槽，记录原材质用于恢复。
    void CollectSurfaces(ULevel& Level, FRoomVisual& Room);
    // 判断生成清单对应的主题，按稳定的种子与 RoomId 选择。
    FGameplayTag ChooseTheme(const FDungeonRoomPlacement& Placement, int32 Seed) const;
    // 保存场景原始环境，运行期间只修改持久关卡内的全局环境。
    void CaptureEnvironment();
    void UpdateEnvironment(const FEnvironment& Value, float Alpha, bool bRestore = false);
    // 仅由本地玩家决定环境目标，独立房间组件直接采用其 Owner 所在的主题。
    int32 FindLocalRoom() const;
    void UpdateRoomMaterials(const FRoomVisual& Room, const FEnvironment& Value, float Alpha) const;
    static FEnvironment Blend(const FEnvironment& From, const FEnvironment& To, float Alpha);

    TMap<FGameplayTag, FPalette> Palettes;
    UPROPERTY(Transient)
    TMap<FGameplayTag, TObjectPtr<UMaterialInterface>> LoadedGrounds;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> MasterMaterial;
    TArray<FGameplayTag> ThemeOrder;
    TArray<FRoomVisual> Rooms;
    TWeakObjectPtr<AExponentialHeightFog> Fog;
    TWeakObjectPtr<ASkyLight> Sky;
    TWeakObjectPtr<ADirectionalLight> Sun;
    TWeakObjectPtr<APostProcessVolume> Post;
    TWeakObjectPtr<class UMaterialParameterCollection> Collection;
    FEnvironment OriginalEnvironment;
    FEnvironment EnvironmentFrom;
    FEnvironment EnvironmentCurrent;
    FEnvironment EnvironmentTarget;
    float Spacing = 2400.f;
    float EnvironmentElapsed = 0.f;
    float EnvironmentDuration = 1.f;
    int32 ActiveRoom = INDEX_NONE;
    bool bStandaloneRoom = false;
    bool bHasEnvironment = false;
    bool bOriginalPostTintOverride = false;
    bool bOriginalBloomOverride = false;
};
