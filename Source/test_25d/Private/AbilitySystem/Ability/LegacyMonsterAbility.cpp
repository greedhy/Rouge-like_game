// 主要调用链：哥布林/食尸鬼复用公共预警 GA；投石在接触窗口生成弹体，由弹体负责后续碰撞伤害。
#include "AbilitySystem/Ability/LegacyMonsterAbility.h"
#include "Actor/EnemySlingProjectile.h"
#include "Character/TelegraphedEnemyCharacter.h"
#include "NativeGameplayTags.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Spear, "Abilities.Goblin.Spear");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SpearCD, "Cooldown.Goblin.Spear");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Sling, "Abilities.Goblin.Sling");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SlingCD, "Cooldown.Goblin.Sling");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GhoulL, "Abilities.Ghoul.Left");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GhoulLCD, "Cooldown.Ghoul.Left");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GhoulR, "Abilities.Ghoul.Right");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GhoulRCD, "Cooldown.Ghoul.Right");
UGoblinSpearAbility::UGoblinSpearAbility()
{
    SetAssetTags(FGameplayTagContainer(TAG_Spear));
    CooldownIdentity = TAG_SpearCD;
}
UGhoulLeftAbility::UGhoulLeftAbility()
{
    SetAssetTags(FGameplayTagContainer(TAG_GhoulL));
    CooldownIdentity = TAG_GhoulLCD;
}
UGhoulRightAbility::UGhoulRightAbility()
{
    SetAssetTags(FGameplayTagContainer(TAG_GhoulR));
    CooldownIdentity = TAG_GhoulRCD;
}
UGoblinSlingAbility::UGoblinSlingAbility()
{
    SetAssetTags(FGameplayTagContainer(TAG_Sling));
    CooldownIdentity = TAG_SlingCD;
}
// 检查弹体类型、速度和寿命是否处于允许范围。
bool USlingAttackConfig::ValidateProjectile() const
{
    return ProjectileClass && FMath::IsFinite(Speed) && Speed >= 100 && Speed <= 3000 && FMath::IsFinite(Lifetime) &&
           Lifetime >= .1f && Lifetime <= 5;
}
// 要求投石参数有效后复用公共激活条件。
bool UGoblinSlingAbility::CanActivateAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* I,
                                             const FGameplayTagContainer* S, const FGameplayTagContainer* T,
                                             FGameplayTagContainer* R) const
{
    const auto* C = Cast<USlingAttackConfig>(AttackConfig);
    return C && C->ValidateProjectile() && Super::CanActivateAbility(H, I, S, T, R);
}
// 在公共瞄准成功后缓存目标位置，供锁定后的发射方向使用。
bool UGoblinSlingAbility::UpdateAim()
{
    if (!Super::UpdateAim() || !SelectedTarget.IsValid())
        return false;
    LockedAim = SelectedTarget->GetActorLocation();
    return true;
}
// 检查发射口遮挡，延迟生成弹体并注入伤害快照、速度与寿命。
void UGoblinSlingAbility::ResolveHit()
{
    const auto* C = Cast<USlingAttackConfig>(AttackConfig);
    if (!C || !C->ValidateProjectile() || !Golem.IsValid() || !Golem->IsAttackCurrent(Serial, Lease))
    {
        Abort();
        return;
    }
    auto* Source = Golem.Get();
    const FVector Start = Source->GetMesh()->GetSocketLocation(C->MuzzleSocket);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(SlingMuzzle), false, Source);
    FHitResult Hit;
    if (GetWorld()->LineTraceSingleByObjectType(Hit, Source->GetActorLocation(), Start,
                                                FCollisionObjectQueryParams(ECC_WorldStatic), Query))
    {
        Abort();
        return;
    }
    const FTransform Transform((LockedAim - Start).Rotation(), Start);
    auto* Shot = GetWorld()->SpawnActorDeferred<AEnemySlingProjectile>(C->ProjectileClass, Transform, Source, Source,
                                                                       ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Shot)
    {
        Abort();
        return;
    }
    Shot->InitializeSource(Source);
    Shot->DamageEffectParams = MakeDamageEffectParamsFromClassDefaults();
    Shot->DamageEffectParams.DamageGameplayEffectClass = SnapshotDamageEffect;
    Shot->DamageEffectParams.DamageType = SnapshotDamageType;
    Shot->DamageEffectParams.BaseDamage = SnapshotDamage;
    Shot->DamageEffectParams.DebuffChance = 0;
    Shot->DamageEffectParams.bIsRadialDamage = false;
    Shot->ProjectileMovement->InitialSpeed = C->Speed;
    Shot->ProjectileMovement->MaxSpeed = C->Speed;
    Shot->ProjectileMovement->bInitialVelocityInLocalSpace = false;
    Shot->ProjectileMovement->Velocity = Transform.GetRotation().GetForwardVector() * C->Speed;
    Shot->Sphere->IgnoreActorWhenMoving(Source, true);
    Launched = Shot;
    UGameplayStatics::FinishSpawningActor(Shot, Transform);
    if (IsValid(Shot))
        Shot->SetLifeSpan(C->Lifetime);
    if (Golem.IsValid() && Golem->IsAttackCurrent(Serial, Lease))
        Golem->ShowImpact(Serial, Start);
}
// 取消时销毁已发射弹体，释放引用后执行公共收尾。
void UGoblinSlingAbility::EndAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* I,
                                     FGameplayAbilityActivationInfo A, bool R, bool C)
{
    if (C && Launched.IsValid())
        Launched->Destroy();
    Launched.Reset();
    Super::EndAbility(H, I, A, R, C);
}
