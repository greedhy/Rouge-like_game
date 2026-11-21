// 调用链：房间子关卡装配 → 灯光组配置 → 本地玩家位置裁剪；主题组件 → 局部灯色。
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Roguelike/DungeonGenerationTypes.h"
#include "DungeonRoomLightingRig.generated.h"

class UPointLightComponent;
class USpotLightComponent;
class UStaticMeshComponent;
class UNiagaraComponent;

/** 房间内的纯表现灯光组。随子关卡变换，不增加碰撞、导航或网络 Actor。 */
UCLASS(Blueprintable)
class TEST_25D_API ADungeonRoomLightingRig : public AActor
{
    GENERATED_BODY()

public:
    ADungeonRoomLightingRig();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Lighting")
    EDungeonRoomRole RoomRole = EDungeonRoomRole::Combat;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Lighting", meta = (ClampMin = "0"))
    float TorchLumens = 420.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Lighting", meta = (ClampMin = "0"))
    float AccentLumens = 140.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Lighting", meta = (ClampMin = "0"))
    float RitualLumens = 1800.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Lighting", meta = (ClampMin = "0"))
    float BossLumens = 3000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon Lighting",
              meta = (ClampMin = "0", ClampMax = "0.15"))
    float FlickerAmount = .06f;

    // 重建配方只更新已有组件，编辑器调参或主题切换不会累加灯光。
    void OnConstruction(const FTransform& Transform) override;
    // 当前房间保留阴影与火焰，邻房仅弱预照明，远房停用所有灯光。
    void SetViewState(bool bNearby, bool bCurrent);
    // 主题只改变戏剧层的色彩，不改变房型、灯数和光度单位。
    void SetThemeColor(FLinearColor Color);
    // 使用与主题组件相同的半开网格边界，避免门槛两侧都没有当前房。
    bool ContainsPlayerLocation(FVector Location) const;
    // 汇总本地组件的有效灯、阴影和粒子数，供 PIE 预算回读。
    FString GetLightingReport() const;
    void Tick(float DeltaSeconds) override;

protected:
    void BeginPlay() override;

private:
    // 按固定房间模板设置配方，保留游戏世界的当前裁剪状态。
    void Configure();
    UPROPERTY(VisibleAnywhere, Category = "Dungeon Lighting")
    TArray<TObjectPtr<UPointLightComponent>> Points;
    UPROPERTY(VisibleAnywhere, Category = "Dungeon Lighting")
    TArray<TObjectPtr<USpotLightComponent>> Spots;
    UPROPERTY(VisibleAnywhere, Category = "Dungeon Lighting")
    TArray<TObjectPtr<UStaticMeshComponent>> Bases;
    UPROPERTY(VisibleAnywhere, Category = "Dungeon Lighting")
    TArray<TObjectPtr<UNiagaraComponent>> Flames;
    TArray<float> PointBase;
    TArray<float> SpotBase;
    FLinearColor ThemeColor = FLinearColor(.45f, .12f, .7f);
    float ViewWeight = 0.f;
    bool bCurrentRoom = false;
};
