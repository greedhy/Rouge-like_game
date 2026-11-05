#include "Player/SwordShieldConfig.h"
#include "Animation/AnimMontage.h"
bool FSwordStrike::Validate() const
{
 return Montage && FMath::IsFinite(Damage) && Damage>=0 && FMath::IsFinite(Radius) && Radius>0 && Radius<=500
  && FMath::IsFinite(Angle) && Angle>0 && Angle<=360 && FMath::IsFinite(PlayRate) && PlayRate>=.1f && PlayRate<=3
  && FMath::IsFinite(Contact) && Contact>=0 && Contact<Montage->GetPlayLength();
}
bool FSwordStrike::Contains(FVector Origin,FVector Forward,FVector Target) const
{
 if(Origin.ContainsNaN() || Forward.ContainsNaN() || Target.ContainsNaN() || !FMath::IsFinite(Radius) || Radius<=0 || !FMath::IsFinite(Angle) || Angle<=0 || Angle>360) return false;
 const FVector Delta=Target-Origin;
 if(FMath::Abs(Delta.Z)>100 || Delta.SizeSquared2D()>FMath::Square(Radius)) return false;
 return Angle>=360 || Delta.SizeSquared2D()<1 || FVector::DotProduct(Forward.GetSafeNormal2D(),Delta.GetSafeNormal2D())>=FMath::Cos(FMath::DegreesToRadians(Angle*.5f));
}
bool USwordShieldConfig::Validate() const
{
 if(Combo.Num()!=3 || !Spin.Validate() || !GuardMontage || !FMath::IsFinite(ComboReset) || ComboReset<=0
  || !FMath::IsFinite(GuardDuration) || GuardDuration<=0 || GuardDuration>5 || !FMath::IsFinite(GuardCooldown) || GuardCooldown<GuardDuration
  || !FMath::IsFinite(SpinCooldown) || SpinCooldown<0 || !FMath::IsFinite(SpinMana) || SpinMana<0) return false;
 for(const auto& Strike:Combo) if(!Strike.Validate()) return false;
 return true;
}
int32 USwordShieldConfig::ResolveComboIndex(int32 Next,double LastEnd,double Now,float Reset)
{
 return Next>=0 && Next<3 && FMath::IsFinite(Now) && FMath::IsFinite(LastEnd) && FMath::IsFinite(Reset)
  && Reset>0 && Now>=LastEnd && Now-LastEnd<=Reset ? Next : 0;
}
