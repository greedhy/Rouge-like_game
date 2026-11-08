// 主要调用链：剑盾技能激活读取 Validate -> ResolveComboIndex 选择连段 -> 命中时 FSwordStrike::Contains 判断扇形。
#include "Player/SwordShieldConfig.h"
#include "Animation/AnimMontage.h"
// 检查配置引用、数值范围与时间约束是否可供技能使用。
bool FSwordStrike::Validate() const
{
    return Montage && FMath::IsFinite(Damage) && Damage >= 0 && FMath::IsFinite(Radius) && Radius > 0 &&
           Radius <= 500 && FMath::IsFinite(Angle) && Angle > 0 && Angle <= 360 && FMath::IsFinite(PlayRate) &&
           PlayRate >= .1f && PlayRate <= 3 && FMath::IsFinite(Contact) && Contact >= 0 &&
           Contact < Montage->GetPlayLength();
}
// 按高度差、水平半径和扇形角判断目标是否位于攻击范围。
bool FSwordStrike::Contains(FVector Origin, FVector Forward, FVector Target) const
{
    if (Origin.ContainsNaN() || Forward.ContainsNaN() || Target.ContainsNaN() || !FMath::IsFinite(Radius) ||
        Radius <= 0 || !FMath::IsFinite(Angle) || Angle <= 0 || Angle > 360)
        return false;
    const FVector Delta = Target - Origin;
    if (FMath::Abs(Delta.Z) > 100 || Delta.SizeSquared2D() > FMath::Square(Radius))
        return false;
    return Angle >= 360 || Delta.SizeSquared2D() < 1 ||
           FVector::DotProduct(Forward.GetSafeNormal2D(), Delta.GetSafeNormal2D()) >=
               FMath::Cos(FMath::DegreesToRadians(Angle * .5f));
}
// 检查配置引用、数值范围与时间约束是否可供技能使用。
bool USwordShieldConfig::Validate() const
{
    if (Combo.Num() != 3 || !Spin.Validate() || !GuardMontage || !FMath::IsFinite(ComboReset) || ComboReset <= 0 ||
        !FMath::IsFinite(GuardDuration) || GuardDuration <= 0 || GuardDuration > 5 || !FMath::IsFinite(GuardCooldown) ||
        GuardCooldown < GuardDuration || !FMath::IsFinite(SpinCooldown) || SpinCooldown < 0 ||
        !FMath::IsFinite(SpinMana) || SpinMana < 0)
        return false;
    for (const auto& Strike : Combo)
        if (!Strike.Validate())
            return false;
    return true;
}
// 在有效连击间隔内保留下一段索引，否则从首段重新开始。
int32 USwordShieldConfig::ResolveComboIndex(int32 Next, double LastEnd, double Now, float Reset)
{
    return Next >= 0 && Next < 3 && FMath::IsFinite(Now) && FMath::IsFinite(LastEnd) && FMath::IsFinite(Reset) &&
                   Reset > 0 && Now >= LastEnd && Now - LastEnd <= Reset
               ? Next
               : 0;
}
