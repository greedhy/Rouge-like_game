#include "Player/StanderCombatConfig.h"
#include "Animation/AnimMontage.h"
bool UStanderCombatConfig::ValidateStander() const
{
 return Validate() && ValidateDeathImpulses() && Lift.Validate() && ShieldBash.Validate()
  && FMath::IsFinite(LiftMana) && LiftMana>=0 && FMath::IsFinite(LiftCooldown) && LiftCooldown>=0
  && FMath::IsFinite(ShieldBashMana) && ShieldBashMana>=0 && FMath::IsFinite(ShieldBashCooldown) && ShieldBashCooldown>=0
  && Counter.Validate() && Dash.Validate() && GuardMontage->GetPlayLength()>0
  && FMath::IsFinite(DashDistance) && DashDistance>0 && DashDistance<=800
  && FMath::IsFinite(DashStart) && FMath::IsFinite(DashEnd) && DashStart>=0 && DashEnd>DashStart
  && DashEnd<=Dash.Contact && FMath::IsFinite(DashCooldown) && DashCooldown>=0
  && FMath::IsFinite(DashMana) && DashMana>=0;
}
float UStanderCombatConfig::DashFraction(float Position) const
{
 if(!FMath::IsFinite(Position) || !FMath::IsFinite(DashStart) || !FMath::IsFinite(DashEnd) || DashEnd<=DashStart) return 0;
 return FMath::Clamp((Position-DashStart)/(DashEnd-DashStart),0.f,1.f);
}

FVector UStanderCombatConfig::WorldDeathImpulse(FVector LocalImpulse,FVector Forward)
{
 if(LocalImpulse.ContainsNaN() || Forward.ContainsNaN()) return FVector::ZeroVector;
 const FVector Facing=Forward.GetSafeNormal2D();
 return Facing*LocalImpulse.X+FVector::CrossProduct(FVector::UpVector,Facing)*LocalImpulse.Y+FVector::UpVector*LocalImpulse.Z;
}
bool UStanderCombatConfig::ValidateDeathImpulses() const
{
 auto Valid=[](FVector V){return !V.ContainsNaN() && V.SizeSquared()<=FMath::Square(10000.f);};
 if(ComboDeathImpulses.Num()!=3) return false;
 for(FVector V:ComboDeathImpulses) if(!Valid(V)) return false;
 return Valid(CounterDeathImpulse) && Valid(SpinDeathImpulse) && Valid(DashDeathImpulse) && Valid(LiftDeathImpulse) && Valid(ShieldBashDeathImpulse);
}
