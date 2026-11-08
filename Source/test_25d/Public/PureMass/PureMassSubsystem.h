/**
 * 调用链、网络与生命周期契约（2026-11-06）
 * 作用域：世界级子系统，Initialize声明UMassEntitySubsystem依赖。实体存储属于Mass EntityManager，本类维护ID映射、查询快照与空间索引；不是跨地图的全局单例。
 * 主链路：房间生成 -> SpawnMonster -> Mass处理器更新 -> 快照与空间查询 -> 伤害/移除；Crowd Actor负责把服务器快照发给客户端。
 * 同步边界：本子系统不直接复制；客户端SetClientSnapshots接收Crowd解码后的只读查询数据，不能把本地查询快照视为服务端权威实体。
 * 生命周期：运行中删除实体时根据Manager是否正在处理选择立即或延迟销毁；Deinitialize先解绑Actor生成委托，再清空本地索引与类型引用。世界退出时不再次操作可能已经先被销毁的Mass管理器，底层实体由管理器处理。
 */
// 实体管理入口：生成建立片段与快照；处理器依次开始更新、更新怪物、结束更新；战斗查询与伤害共用快照和空间索引。
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTypes.h"
#include "Mass/EntityHandle.h"
#include "MassArchetypeTypes.h"
#include "Mass/MonsterSpatialIndex.h"
#include "PureMass/PureMassTypes.h"
#include "PureMass/PureMassEncounterTypes.h"
#include "PureMassSubsystem.generated.h"

class UMassEntitySubsystem;
class APawn;
class ATerrainSurface;
class AMyProjectile;

UCLASS()
class TEST_25D_API UPureMassSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    // 初始化时声明实体子系统依赖，供后续生成与逐帧处理使用。
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    // 世界退出时解绑生成回调并清空索引，实体销毁由管理器负责。
    virtual void Deinitialize() override;
    // 仅在游戏与编辑器试玩世界创建子系统。
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    // 启用空间索引和实体原型，并注册旧火球生成适配回调。
    UFUNCTION(BlueprintCallable)
    void Enable();
    // 由生成入口创建实体，校验房间与导航落点后发布首份战斗快照。
    UFUNCTION(BlueprintCallable)
    int32 SpawnMonster(UPureMassMonsterType* Type, FVector Location, FName Room = NAME_None);
    /** Public removal never reports Killed; only the damage path can credit a kill. */
    // 外部回收入口仅允许回收或异常原因，不能伪造击杀奖励。
    UFUNCTION(BlueprintCallable, Category = "PureMass|Encounter")
    bool RemoveMonster(int32 Id, EPureMassRemovalReason Reason = EPureMassRemovalReason::Recycled);
    /** Axis-aligned XY limits; Extent must be finite and positive. Unregistered rooms are unrestricted. */
    // 登记房间平面边界，拒绝无法容纳现存胶囊的范围。
    UFUNCTION(BlueprintCallable, Category = "PureMass|Encounter")
    bool SetRoomBounds(FName Room, FVector Center, FVector Extent);
    // 服务端撤销房间边界限制。
    UFUNCTION(BlueprintCallable, Category = "PureMass|Encounter")
    void ClearRoomBounds(FName Room);
    FOnPureMassRemoved OnMonsterRemoved;
    // 校验目标身份与存活状态并返回当前位置或快照，失败时清空输出。
    UFUNCTION(BlueprintPure)
    bool Resolve(int32 Id, FPureMassView& OutView) const;
    // 查询空间索引中的存活实体并按标识排序。
    UFUNCTION(BlueprintPure)
    TArray<int32> QueryRadius(FVector Center, float Radius) const;
    // 沿线段检测目标胶囊，选择最早有效命中并返回命中时间。
    int32 Sweep(FVector Start, FVector End, float Radius, float& OutTime, const TSet<int32>* Ignored = nullptr) const;
    // 外部伤害入口：校验来源房间、合并状态，再计算并结算生命变化。
    UFUNCTION(BlueprintCallable)
    float ApplyDamage(int32 Id, const FPureMassDamage& Damage);
    // 按实体标识输出当前快照，供群体表现和复制使用。
    UFUNCTION(BlueprintPure)
    TArray<FPureMassView> GetSnapshots() const;
    // 以复制快照重建客户端查询索引并保留类型资源引用。
    void SetClientSnapshots(const TArray<FPureMassView>& Snapshots);
    // 读取子系统启用状态。
    UFUNCTION(BlueprintPure)
    bool IsEnabled() const
    {
        return bEnabled;
    }
    // 处理器每帧更新前收集存活玩家、设置寻路预算并定期刷新地表列表。
    bool BeginUpdate(float DeltaSeconds);
    // 逐实体推进状态、预警攻击、寻路与避让，最后发布查询快照。
    void UpdateMonster(FPureMassFragment& Fragment);
    // 服务端应用石化并停止移动和当前攻击表现。
    bool ApplyPetrify(int32 Id, float Duration, AActor* Source);
    // 服务端按倍率合并减速持续时间，并限制独立减速记录数量。
    bool ApplySlow(int32 Id, float Multiplier, float Duration, AActor* Source);
    // 帧末发现异常失效实体并集中执行或延迟执行实体销毁。
    void FinishUpdate();
    // 将稳定实体标识转换为实体管理器句柄。
    FMassEntityHandle GetEntity(int32 Id) const;
    // 读取经伤害路径确认的累计击杀数。
    UFUNCTION(BlueprintPure)
    int64 GetDeathCount() const
    {
        return DeathCount;
    }

private:
    friend class FPureMassMovementIntegrationTest;
    // 判断当前世界是否拥有服务端模拟权限。
    bool IsAuthority() const;
    // 统一移除活体身份、通知死亡表现并排队销毁，再广播遭遇移除事件。
    bool RemoveMonsterInternal(int32 Id, EPureMassRemovalReason Reason, FVector DeathImpulse = FVector::ZeroVector);
    // 按半径检查平面边界，未登记房间不限制位置。
    bool IsInsideRoom(FName Room, FVector Location, float Radius = 0.f) const;
    // 阻止已登记房间接收房间外有效来源造成的伤害。
    bool IsRoomDamageSourceAllowed(FName Room, const AActor* Source) const;
    // 按胶囊半径将目标位置夹紧到房间平面范围。
    FVector ConstrainToRoom(FName Room, FVector Location, float Radius) const;
    // 把存活实体片段同步到查询快照和空间索引。
    void Publish(const FPureMassFragment& Fragment);
    // 扣除生命并汇总伤害数字；致死时先移除查询身份，再发放经验。
    float DamageFragment(FPureMassFragment& Fragment, float Amount, AActor* Source, bool bBlocked = false,
                         bool bCritical = false, FVector DeathImpulse = FVector::ZeroVector);
    // 按抗性、格挡、暴击和护甲顺序计算实体实际承伤。
    float EvaluateDamage(const FPureMassFragment& Fragment, const FPureMassDamage& Damage, bool& bBlocked,
                         bool& bCritical) const;
    // 把实体攻击转换为玩家自身施加的即时效果，复用玩家承伤与死亡回调。
    void AttackPlayer(FPureMassFragment& Fragment, APawn* Player);
    // 更新地表接触持续时间与伤害，返回地表提供的移动倍率。
    float UpdateTerrain(FPureMassFragment& Fragment);
    // 约束房间范围，处理世界滑移、实体避碰和地面贴合后更新速度位置。
    void Move(FPureMassFragment& Fragment, FVector Delta);
    // 只为白名单火球补装实体命中适配组件，避免重复注册。
    void AdaptLegacyProjectile(AActor* Actor);
    FDelegateHandle ProjectileSpawnHandle;
    UPROPERTY(Transient)
    TSubclassOf<AMyProjectile> LegacyFireBoltClass;
    UPROPERTY(Transient)
    TObjectPtr<UMassEntitySubsystem> Mass;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UPureMassMonsterType>> RetainedTypes;
    UPROPERTY(Transient)
    TMap<int32, FPureMassView> Views;
    TMap<int32, FMassEntityHandle> Entities;
    // Independent identity survives unexpected external destruction of an entity fragment.
    TMap<int32, FName> EntityRooms;
    TMap<FName, FBox> RoomBounds;
    FMassArchetypeHandle Archetype;
    FMonsterSpatialIndex Grid;
    TArray<TWeakObjectPtr<APawn>> Players;
    TArray<TWeakObjectPtr<ATerrainSurface>> Terrain;
    TArray<int32> Dead;
    bool bEnabled = false;
    bool bRefreshTerrain = false;
    int64 NextId = 1;
    float DeltaTime = 0.f;
    float NextTerrainScan = 0.f;
    int32 PathsRemaining = 0;
    float LargestRadius = 32.f;
    float LargestHalfHeight = 60.f;
    uint64 DeathCount = 0;
};
