// 主要调用链：构造/尺寸复制 → 更新地表几何；BeginPlay → 定时 UpdateContacts → CanAffectCharacter → 状态组件 RefreshContact。
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TerrainSurfaceConfig.h"
#include "TerrainSurface.generated.h"

class ABaseCharacter;
class UBoxComponent;
class UStaticMeshComponent;
class UNiagaraComponent;

/** Place this actor instead of a cosmetic-only mesh to enable terrain gameplay. */
UCLASS()
class TEST_25D_API ATerrainSurface : public AActor
{
    GENERATED_BODY()
public:
    // 创建范围、地面和粒子组件，启用复制并关闭 Actor Tick。
    ATerrainSurface();
    // 根据尺寸调整检测范围、网格和粒子，令网格顶面对齐地块平面。
    virtual void OnConstruction(const FTransform& Transform) override;
    // 启动本地地表特效，并仅在服务器开启周期接触扫描。
    virtual void BeginPlay() override;
    // 结束生命周期时释放本类持有的运行期资源，再调用父类清理。
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
    TObjectPtr<UTerrainSurfaceConfig> Config;
    UPROPERTY(ReplicatedUsing = OnRep_SurfaceSize, EditAnywhere, BlueprintReadOnly, Category = "Terrain",
              meta = (ClampMin = "50"))
    FVector2D SurfaceSize = FVector2D(400.f);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain", meta = (ClampMin = "2"))
    float ContactHeight = 20.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
    bool bAffectPlayers = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
    bool bAffectEnemies = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
    bool bRequireGrounded = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
    bool bEnabled = true;
    // World terrain keeps its existing rules. Skill-created surfaces filter against their caster.
    UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Terrain")
    bool bOnlyAffectCasterEnemies = false;
    // 注册本类需要发送给客户端的复制字段。
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terrain")
    TObjectPtr<UBoxComponent> Area;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terrain")
    TObjectPtr<UStaticMeshComponent> Ground;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terrain")
    TObjectPtr<UNiagaraComponent> SurfaceParticles;

    // 验证角色存活、阵营与落地条件，并检查脚底是否位于地块局部范围内。
    UFUNCTION(BlueprintPure, Category = "Terrain")
    bool CanAffectCharacter(ABaseCharacter* Character) const;
    // 服务器进行宽范围重叠查询，按角色去重后向状态组件续期有效接触。
    UFUNCTION(BlueprintCallable, Category = "Terrain")
    void UpdateContacts();

private:
    // 尺寸复制到客户端后重新应用构造期的网格和特效缩放。
    UFUNCTION()
    void OnRep_SurfaceSize();
    FTimerHandle ScanTimer;
};
