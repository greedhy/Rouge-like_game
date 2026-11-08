/**
 * 减速链：APureMassRuntime::Hit -> Apply；Mass 转交 UPureMassSubsystem，Actor 按需创建组件 -> RefreshMovementSpeed/GetMultiplier。Actor 条目记录独立过期时间，取最强减速，不让较弱效果延长旧强效果。组件由 Enemy 持有，Tick/Clear/EndPlay 处理过期、死亡和对象池 Lease 变化；不独立复制 Entries。
 * 说明：上面的箭头描述控制流；目标委托、Tick、RepNotify 是后续回调入口，不是同一帧连续调用栈。
 */
#include "AbilitySystem/Batch6/Batch6Slow.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Character/EnemyCharacter.h"
#include "PureMass/PureMassSubsystem.h"
/** 构造 Infinite 标记 GE；实际倍率由组件条目和 Enemy 速度刷新逻辑读取。 */
UBatch6SlowEffect::UBatch6SlowEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Infinite;
}
/** 构造可 Tick 组件，但初始关闭；首次 Apply 后才启用过期检查。 */
UBatch6SlowComponent::UBatch6SlowComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}
/** 移动速度刷新查询：校验 Enemy 当前 Lease/存活状态，从未过期条目取最小倍率，否则返回 1。 */
float UBatch6SlowComponent::GetMultiplier(const AActor* Actor)
{
    const auto* Enemy = Cast<AEnemyCharacter>(Actor);
    const auto* C = Actor ? Actor->FindComponentByClass<UBatch6SlowComponent>() : nullptr;
    if (!Enemy || !C || Enemy->GetDungeonPoolLease() != C->Lease || Enemy->IsDungeonPoolSleeping() ||
        ICombatInterface::Execute_IsDead(const_cast<AEnemyCharacter*>(Enemy)))
        return 1;
    float Result = 1;
    for (const auto& E : C->Entries)
        if (E.Expires > C->GetWorld()->GetTimeSeconds())
            Result = FMath::Min(Result, E.Multiplier);
    return Result;
}
/** 服务器验证参数及混合目标；Mass 调用 ApplySlow，Actor 创建/复用组件，合并同倍率过期时间并刷新速度。 */
bool UBatch6SlowComponent::Apply(UWorld* World, AActor* Caster, const FPureMassCombatTarget& Target, float Multiplier,
                                 float Duration)
{
    if (!World || !Caster || !Caster->HasAuthority() || !FMath::IsFinite(Multiplier) || !FMath::IsFinite(Duration) ||
        Duration <= 0 || Multiplier <= 0 || Multiplier > 1)
        return false;
    FVector P;
    if (!PureMassCombat::Resolve(World, Caster, Target, P))
        return false;
    if (!Target.IsActor())
    {
        auto* Mass = World->GetSubsystem<UPureMassSubsystem>();
        return Mass && Mass->ApplySlow(Target.EntityId, Multiplier, Duration, Caster);
    }
    auto* Enemy = Cast<AEnemyCharacter>(Target.Actor.Get());
    if (!Enemy)
        return false;
    auto* C = Enemy->FindComponentByClass<UBatch6SlowComponent>();
    if (!C)
    {
        C = NewObject<UBatch6SlowComponent>(Enemy);
        Enemy->AddInstanceComponent(C);
        C->RegisterComponent();
    }
    if (C->Lease != Enemy->GetDungeonPoolLease())
        C->Clear();
    C->Lease = Enemy->GetDungeonPoolLease();
    C->Entries.RemoveAll(
        [&](const FSlow& E)
        {
            return E.Expires <= World->GetTimeSeconds();
        });
    // Bound repeated pulses without extending an old stronger slow with a weaker cast.
    const double End = World->GetTimeSeconds() + Duration;
    bool Merged = false;
    for (auto& E : C->Entries)
        if (FMath::IsNearlyEqual(E.Multiplier, Multiplier))
        {
            E.Expires = FMath::Max(E.Expires, End);
            Merged = true;
            break;
        }
    if (!Merged && C->Entries.Num() < 32)
        C->Entries.Add({Multiplier, End});
    if (!C->Marker.IsValid())
        if (auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Enemy))
            C->Marker = ASC->ApplyGameplayEffectToSelf(GetDefault<UBatch6SlowEffect>(), 1, ASC->MakeEffectContext());
    C->SetComponentTickEnabled(true);
    Enemy->RefreshMovementSpeed();
    return true;
}
/** 禁用 Tick、回收标记 GE 句柄、清空条目并刷新 Enemy 速度；用于过期/复用/销毁。 */
void UBatch6SlowComponent::Clear()
{
    SetComponentTickEnabled(false);
    if (auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner()))
        ASC->RemoveActiveGameplayEffect(Marker);
    Marker.Invalidate();
    Entries.Reset();
    if (auto* Enemy = Cast<AEnemyCharacter>(GetOwner()))
        Enemy->RefreshMovementSpeed();
}
/** 权威端逐帧校验 Lease、睡眠和死亡；删除过期条目，全部清空时 Clear，否则必要时刷新速度。 */
void UBatch6SlowComponent::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta, Type, Function);
    auto* Enemy = Cast<AEnemyCharacter>(GetOwner());
    if (!Enemy || !Enemy->HasAuthority())
        return;
    if (Enemy->GetDungeonPoolLease() != Lease || Enemy->IsDungeonPoolSleeping() ||
        ICombatInterface::Execute_IsDead(Enemy))
    {
        Clear();
        return;
    }
    const int32 Removed = Entries.RemoveAll(
        [&](const FSlow& E)
        {
            return E.Expires <= GetWorld()->GetTimeSeconds();
        });
    if (Entries.IsEmpty())
        Clear();
    else if (Removed)
        Enemy->RefreshMovementSpeed();
}
/** 引擎销毁入口：Clear 移除本组件标记并还原速度计算，再调用父类。 */
void UBatch6SlowComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    Clear();
    Super::EndPlay(Reason);
}
