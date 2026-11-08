/**
 * 创建链：服务器StanderWeaponActionAbility::Execute生成弹体 -> Initialize保存伤害/方向与来源代次 -> 服务器Tick扫掠命中 -> 伤害结算或Destroy。
 * 同步：弹体Actor复制且启用移动复制；FlightEffect属性复制后OnRep_FlightEffect更新Niagara组件。服务器Initialize也直接调用OnRep以服务本地表现，不依赖服务器自动收到复制回调。
 * 生命周期：FlightVisual是弹体默认子组件，随Actor销毁，循环飞行特效不能作为无主世界特效单独生成。SetLifeSpan限制最大寿命；施法者、武器代次、RunId、地图Generation和房间失效时中止，防止换装或旅行后的旧弹体伤害。
 * 正常施法结束允许已发射弹体继续飞行；技能取消才主动销毁该次技能记录的弹体。Owner/弱引用及代次校验不能相互替代：引用有效不代表仍属于同一次武器/房间状态。
 */
// 主要调用链：动作 GA 的 Execute 生成 Actor -> Initialize -> 服务器 Tick -> PureMassCombat 扫掠 -> Actor GAS 或 Mass 伤害；OnRep_FlightEffect 同步表现。
#include "Actor/StanderWeaponProjectile.h"
#include "Character/StanderPlayer.h"
#include "Player/SwordShieldConfig.h"
#include "Equipment/EquipmentComponent.h"
#include "GameFramework/PlayerState.h"
#include "PureMass/PureMassCombatTarget.h"
#include "PureMass/PureMassSubsystem.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "Components/SceneComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#include "Roguelike/DungeonRunDirector.h"
#include "Roguelike/DungeonGenerationRuntime.h"
// 开启网络移动复制和 Tick，创建飞行特效组件。
AStanderWeaponProjectile::AStanderWeaponProjectile()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(true);
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    FlightVisual = CreateDefaultSubobject<UNiagaraComponent>(TEXT("FlightVisual"));
    FlightVisual->SetupAttachment(RootComponent);
    FlightVisual->SetAutoActivate(false);
}
// 非专用服务器按复制的特效资源激活或关闭飞行组件。
void AStanderWeaponProjectile::OnRep_FlightEffect()
{
    if (GetNetMode() == NM_DedicatedServer)
        return;
    if (FlightVisual->GetAsset() != FlightEffect)
    {
        FlightVisual->SetAsset(FlightEffect);
        if (FlightEffect)
            FlightVisual->Activate(true);
        else
            FlightVisual->Deactivate();
    }
}
// 注册飞行特效资源的网络复制字段。
void AStanderWeaponProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AStanderWeaponProjectile, FlightEffect);
}
// 服务器校验发射参数，记录施法者、武器代次和地牢归属，设置寿命与飞行特效。
void AStanderWeaponProjectile::Initialize(AStanderPlayer* Source, const FStanderWeaponActionEvent& Event,
                                          const FDamageEffectParams& Params, FVector Direction)
{
    if (!HasAuthority() || !IsValid(Source) || Direction.ContainsNaN() || Direction.IsNearlyZero())
    {
        Destroy();
        return;
    }
    auto* PS = Source->GetPlayerState();
    auto* Equipment = PS ? PS->FindComponentByClass<UEquipmentComponent>() : nullptr;
    if (!Equipment || !FMath::IsFinite(Event.Radius) || Event.Radius < 0 || !FMath::IsFinite(Event.ProjectileSpeed) ||
        Event.ProjectileSpeed <= 0 || !FMath::IsFinite(Event.ProjectileLifetime) || Event.ProjectileLifetime <= 0)
    {
        Destroy();
        return;
    }
    Caster = Source;
    Action = Event;
    DamageParams = Params;
    Forward = Direction.GetSafeNormal();
    SetActorRotation(Forward.Rotation());
    SetLifeSpan(Event.ProjectileLifetime);
    WeaponGeneration = Equipment->GetWeaponGeneration();
    OriginDirector = ADungeonRunDirector::Find(GetWorld());
    bHasDungeon = OriginDirector.IsValid();
    if (auto* Director = OriginDirector.Get())
    {
        OriginRoom = Director->GetRoomIndex(Source->GetActorLocation());
        RunId = Director->RunId;
        if (auto* Generation = Director->GetGeneration())
            DungeonGeneration = Generation->Manifest.GenerationId;
        if (OriginRoom == INDEX_NONE || !AllowsLocation(GetActorLocation()))
        {
            Destroy();
            return;
        }
    }
    FlightEffect = Event.Effect;
    // A looping flight system belongs to the projectile component. Never spawn it
    // as an unowned impact effect that could survive the projectile indefinitely.
    Action.Effect = Source->SwordConfig ? Source->SwordConfig->HitEffect.Get() : nullptr;
    OnRep_FlightEffect();
    ForceNetUpdate();
}
// 检查位置仍属于生成时的地牢运行、地图代次和房间。
bool AStanderWeaponProjectile::AllowsLocation(const FVector& Location) const
{
    if (Location.ContainsNaN())
        return false;
    if (!bHasDungeon)
        return ADungeonRunDirector::Find(GetWorld()) == nullptr;
    const auto* Director = OriginDirector.Get();
    const auto* Generation = Director ? Director->GetGeneration() : nullptr;
    return Director && Director == ADungeonRunDirector::Find(GetWorld()) && Director->RunId == RunId && Generation &&
           Generation->Manifest.GenerationId == DungeonGeneration && Director->GetRoomIndex(Location) == OriginRoom;
}
// 服务器验证来源仍有效，扫掠本帧路径并处理目标、墙体及房间边界命中。
void AStanderWeaponProjectile::Tick(float Delta)
{
    Super::Tick(Delta);
    if (!HasAuthority())
        return;
    if (!Caster.IsValid() || Caster->IsDungeonDowned() || Caster->IsDead_Implementation() ||
        !AllowsLocation(Caster->GetActorLocation()))
    {
        Destroy();
        return;
    }
    auto* PS = Caster->GetPlayerState();
    auto* Equipment = PS ? PS->FindComponentByClass<UEquipmentComponent>() : nullptr;
    if (!Equipment || Equipment->GetWeaponGeneration() != WeaponGeneration || !FMath::IsFinite(Delta) || Delta < 0)
    {
        Destroy();
        return;
    }
    const FVector Start = GetActorLocation();
    FVector End = Start + Forward * Action.ProjectileSpeed * Delta;
    if (!AllowsLocation(Start) || End.ContainsNaN())
    {
        Destroy();
        return;
    }
    const bool ReachesBoundary = !AllowsLocation(End);
    // 将长帧移动终点截断在当前房间边界内，保留边界前的合法命中。
    if (ReachesBoundary)
    {
        // Keep valid hits before the boundary even on a long frame, without querying the next room.
        FVector Inside = Start, Outside = End;
        for (int32 I = 0; I < 16; ++I)
        {
            const FVector Mid = (Inside + Outside) * .5;
            if (AllowsLocation(Mid))
                Inside = Mid;
            else
                Outside = Mid;
        }
        End = Inside;
    }
    float Time = 1;
    FHitResult Wall;
    const auto Target =
        PureMassCombat::Sweep(GetWorld(), Caster.Get(), Start, End, Action.Radius, Time, Wall, nullptr, this);
    if (Target.IsSet())
    {
        FVector TargetLocation;
        if (PureMassCombat::Resolve(GetWorld(), Caster.Get(), Target, TargetLocation) && AllowsLocation(TargetLocation))
        {
            if (Target.IsActor())
            {
                DamageParams.TargetAbilitySystemComponent =
                    UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target.Actor.Get());
                if (DamageParams.TargetAbilitySystemComponent)
                    UMyAbilitySystemLibrary::ApplyDamageEffect(DamageParams);
            }
            else if (auto* Mass = GetWorld()->GetSubsystem<UPureMassSubsystem>())
            {
                FPureMassDamage Damage;
                Damage.Source = Caster.Get();
                Damage.Amount = DamageParams.BaseDamage;
                Damage.DamageType = DamageParams.DamageType;
                Damage.DeathImpulse = DamageParams.DeathImpulse;
                Mass->ApplyDamage(Target.EntityId, Damage);
            }
            if (Caster.IsValid())
                Caster->ShowSwordImpact(Action.Effect, FMath::Lerp(Start, End, Time));
        }
        Destroy();
        return;
    }
    if (Wall.bBlockingHit)
    {
        if (Caster.IsValid())
            Caster->ShowSwordImpact(Action.Effect, Wall.ImpactPoint);
        Destroy();
        return;
    }
    // 将长帧移动终点截断在当前房间边界内，保留边界前的合法命中。
    if (ReachesBoundary)
    {
        Destroy();
        return;
    }
    SetActorLocation(End);
}
