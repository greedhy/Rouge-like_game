// 主要调用链：武器动作技能激活调用 Validate -> Move 调用 JumpHeight -> 发射物选敌调用 IsInAimCone。
#include "Player/StanderWeaponActionConfig.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystem/Timeline/AbilityTimelineDefinition.h"
// 检查配置引用、数值范围与时间约束是否可供技能使用。
bool UStanderWeaponActionConfig::Validate() const
{
    if (!Montage || !ActionTag.IsValid() || WeaponFamily.IsNone() || !FMath::IsFinite(PlayRate) || PlayRate <= 0 ||
        !FMath::IsFinite(ManaCost) || ManaCost < 0 || !FMath::IsFinite(Cooldown) || Cooldown < 0 ||
        (Cooldown > 0 && !CooldownTag.IsValid()) || Events.IsEmpty())
        return false;
    if (Timeline)
    {
        FAbilityTimelineSnapshot Snapshot;
        FString Error;
        // 失败在Commit前暴露；这条接入使用原事件表，禁止再配置Notify命中造成双重执行。
        if (!Timeline->ResolveMontage(Montage, Snapshot, Error) || !Timeline->Events.IsEmpty())
        {
            return false;
        }
    }
    float Previous = -1;
    for (const auto& E : Events)
    {
        if (!FMath::IsFinite(E.Time) || E.Time < Previous || E.Time < 0 || E.Time >= Montage->GetPlayLength() ||
            !FMath::IsFinite(E.Damage) || E.Damage < 0 || !FMath::IsFinite(E.Radius) || E.Radius <= 0 ||
            !FMath::IsFinite(E.Angle) || E.Angle <= 0 || E.Angle > 360 || E.DeathImpulse.ContainsNaN() ||
            !FMath::IsFinite(E.Duration) || E.Duration < 0 || !FMath::IsFinite(E.ProjectileSpeed) ||
            E.ProjectileSpeed <= 0 || !FMath::IsFinite(E.ProjectileLifetime) || E.ProjectileLifetime <= 0 ||
            !FMath::IsFinite(E.MoveDistance) || !FMath::IsFinite(E.MoveHeight) || E.MoveHeight < 0 ||
            E.MoveHeight > 300 || !FMath::IsFinite(E.LaunchYaw))
            return false;
        if ((E.Kind == EStanderWeaponActionKind::Move || E.Kind == EStanderWeaponActionKind::Guard) &&
            (E.Duration <= 0 || E.Time + E.Duration > Montage->GetPlayLength()))
            return false;
        Previous = E.Time;
    }
    return true;
}
// 使用首尾为零、中点达峰值的抛物线计算跳跃高度。
float UStanderWeaponActionConfig::JumpHeight(float Fraction, float Height)
{
    const float F = FMath::Clamp(Fraction, 0.f, 1.f);
    return 4.f * F * (1.f - F) * Height;
}
// 检查目标是否处于水平朝向左右各十五度的辅助瞄准锥内。
bool UStanderWeaponActionConfig::IsInAimCone(FVector Forward, FVector TargetOffset)
{
    return !Forward.ContainsNaN() && !TargetOffset.ContainsNaN() &&
           FVector::DotProduct(Forward.GetSafeNormal2D(), TargetOffset.GetSafeNormal2D()) >=
               FMath::Cos(FMath::DegreesToRadians(15.f));
}
