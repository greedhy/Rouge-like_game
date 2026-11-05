#include "AbilitySystem/Ability/StanderAbility.h"
#include "AbilitySystem/Ability/SwordShieldAbility.h"
#include "Character/StanderPlayer.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "NativeGameplayTags.h"
#include "TimerManager.h"
#include "Roguelike/DungeonRunDirector.h"
#include "MyGameplayTags.h"
#include "PureMass/PureMassCombatTarget.h"
#include "PureMass/PureMassSubsystem.h"
#include "PureMass/PureMassIncomingDamageEffect.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderComboTag,"Abilities.Stander.Combo");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderGuardTag,"Abilities.Stander.Guard");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderSpinTag,"Abilities.Stander.Spin");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderDashTag,"Abilities.Stander.Dash");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderLiftTag,"Abilities.Stander.Lift");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderLiftCD,"Cooldown.Stander.Lift");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderShieldBashTag,"Abilities.Stander.ShieldBash");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderShieldBashCD,"Cooldown.Stander.ShieldBash");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderBusyTag,"State.Stander.Busy");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderGuardCD,"Cooldown.Stander.Guard");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderSpinCD,"Cooldown.Stander.Spin");
UE_DEFINE_GAMEPLAY_TAG_STATIC(StanderDashCD,"Cooldown.Stander.Dash");

UStanderAbility::UStanderAbility()
{
 InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;NetExecutionPolicy=EGameplayAbilityNetExecutionPolicy::ServerOnly;
 ActivationOwnedTags.AddTag(StanderBusyTag);ActivationBlockedTags.AddTag(StanderBusyTag);
 DebuffChance=0;KnockbackChance=0;DeathImpulseMagnitude=1000;KnockbackForceMagnitude=0;
}
UStanderComboAbility::UStanderComboAbility(){SetAssetTags(FGameplayTagContainer(StanderComboTag));}
UStanderGuardAbility::UStanderGuardAbility(){Mode=EMode::Guard;SetAssetTags(FGameplayTagContainer(StanderGuardTag));CooldownIdentity=StanderGuardCD;}
UStanderSpinAbility::UStanderSpinAbility(){Mode=EMode::Spin;SetAssetTags(FGameplayTagContainer(StanderSpinTag));CooldownIdentity=StanderSpinCD;}
UStanderDashAbility::UStanderDashAbility(){Mode=EMode::Dash;SetAssetTags(FGameplayTagContainer(StanderDashTag));CooldownIdentity=StanderDashCD;}
UStanderLiftAbility::UStanderLiftAbility(){Mode=EMode::Lift;SetAssetTags(FGameplayTagContainer(StanderLiftTag));CooldownIdentity=StanderLiftCD;}
UStanderShieldBashAbility::UStanderShieldBashAbility(){Mode=EMode::ShieldBash;SetAssetTags(FGameplayTagContainer(StanderShieldBashTag));CooldownIdentity=StanderShieldBashCD;}
const UStanderCombatConfig* UStanderAbility::Config(const FGameplayAbilityActorInfo* I) const
{auto* P=I?Cast<AStanderPlayer>(I->AvatarActor.Get()):nullptr;return P?Cast<UStanderCombatConfig>(P->SwordConfig):nullptr;}
float UStanderAbility::Cost(const UStanderCombatConfig* C) const {return Mode==EMode::Lift?C->LiftMana:Mode==EMode::ShieldBash?C->ShieldBashMana:Mode==EMode::Spin?C->SpinMana:Mode==EMode::Dash?C->DashMana:0;}
float UStanderAbility::Cooldown(const UStanderCombatConfig* C) const {return Mode==EMode::Lift?C->LiftCooldown:Mode==EMode::ShieldBash?C->ShieldBashCooldown:Mode==EMode::Guard?C->GuardCooldown:Mode==EMode::Spin?C->SpinCooldown:Mode==EMode::Dash?C->DashCooldown:0;}
bool UStanderAbility::CanActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* I,const FGameplayTagContainer* S,const FGameplayTagContainer* T,FGameplayTagContainer* R) const
{
 auto* P=I?Cast<AStanderPlayer>(I->AvatarActor.Get()):nullptr;const auto* C=Config(I);
 if(!P || !C || !C->ValidateStander() || !I->AbilitySystemComponent.IsValid() || P->IsDungeonDowned() || P->IsDead_Implementation() || P->bIsStunned
  || !P->GetCharacterMovement()->IsMovingOnGround() || !P->GetMesh()->GetAnimInstance() || !DamageEffectClass || !DamageType.IsValid()) return false;
 return Super::CanActivateAbility(H,I,S,T,R);
}
bool UStanderAbility::CheckCooldown(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo* I,FGameplayTagContainer*) const
{return I && I->AbilitySystemComponent.IsValid() && (!CooldownIdentity.IsValid() || !I->AbilitySystemComponent->HasMatchingGameplayTag(CooldownIdentity));}
void UStanderAbility::ApplyCooldown(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo* I,FGameplayAbilityActivationInfo) const
{
 const auto* C=Config(I);if(!C || !I->IsNetAuthority() || !CooldownIdentity.IsValid() || Cooldown(C)<=0) return;
 auto* ASC=I->AbilitySystemComponent.Get();auto Spec=ASC->MakeOutgoingSpec(USwordTimedEffect::StaticClass(),1,ASC->MakeEffectContext());
 if(Spec.IsValid()){Spec.Data->SetDuration(Cooldown(C),true);Spec.Data->DynamicGrantedTags.AddTag(CooldownIdentity);ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);}
}
bool UStanderAbility::CheckCost(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo* I,FGameplayTagContainer*) const
{const auto* C=Config(I);return C && I->AbilitySystemComponent.IsValid() && I->AbilitySystemComponent->GetNumericAttribute(UMyAttributeSet::GetManaAttribute())>=Cost(C);}
void UStanderAbility::ApplyCost(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo* I,FGameplayAbilityActivationInfo) const
{
 const auto* C=Config(I);if(!C || !I->IsNetAuthority() || Cost(C)<=0) return;
 auto* ASC=I->AbilitySystemComponent.Get();auto Spec=ASC->MakeOutgoingSpec(USwordManaEffect::StaticClass(),1,ASC->MakeEffectContext());
 if(Spec.IsValid()){Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Data.SwordShield.Mana")),-Cost(C));ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);}
}
bool UStanderAbility::StartStrike(const FSwordStrike& NewStrike)
{
 Strike=NewStrike;bHit=false;Started=GetWorld()->GetTimeSeconds();
 return CurrentActorInfo->AbilitySystemComponent->PlayMontage(this,CurrentActivationInfo,Strike.Montage,Strike.PlayRate)>0;
}
void UStanderAbility::ActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* I,FGameplayAbilityActivationInfo A,const FGameplayEventData*)
{
 bOwnMovement=bHit=bCounter=bCounterPending=bDashBlocked=false;DashApplied=0;GuardHandle.Invalidate();GuardListener.Reset();
 Player=I?Cast<AStanderPlayer>(I->AvatarActor.Get()):nullptr;const auto* C=Config(I);
 if(!Player.IsValid() || !C || !C->ValidateStander() || !I->IsNetAuthority()){EndAbility(H,I,A,true,true);return;}
 CurrentCombo=USwordShieldConfig::ResolveComboIndex(NextCombo,LastComboEnd,GetWorld()->GetTimeSeconds(),C->ComboReset);
 FSwordStrike Selected=Mode==EMode::Lift?C->Lift:Mode==EMode::ShieldBash?C->ShieldBash:Mode==EMode::Spin?C->Spin:Mode==EMode::Dash?C->Dash:C->Combo[CurrentCombo];
 if(Mode==EMode::Guard){Selected.Montage=C->GuardMontage;Selected.PlayRate=Selected.Montage->GetPlayLength()/C->GuardDuration;}
 if(!CommitAbility(H,I,A) || !IsActive() || !Player.IsValid()){EndAbility(H,I,A,true,true);return;}
 LockedForward=Player->GetActorForwardVector().GetSafeNormal2D();
 if(auto* PC=I->PlayerController.Get();PC && PC->IsLocalController())
 {
  FHitResult HitResult;
  if(PC->GetHitResultUnderCursor(ECC_Visibility,false,HitResult) && !HitResult.ImpactPoint.ContainsNaN())
  {FVector Aim=(HitResult.ImpactPoint-Player->GetActorLocation()).GetSafeNormal2D();if(!Aim.IsNearlyZero()) LockedForward=Aim;}
 }
 Player->SetActorRotation(LockedForward.Rotation());auto* Movement=Player->GetCharacterMovement();PreviousMovement=Movement->MovementMode;
 bOwnMovement=true;Movement->StopMovementImmediately();Movement->DisableMovement();
 OriginRoom=INDEX_NONE;if(auto* D=ADungeonRunDirector::Find(GetWorld())) OriginRoom=D->GetRoomIndex(Player->GetActorLocation());
 if(!StartStrike(Selected)){EndAbility(H,I,A,true,true);return;}
 if(Mode==EMode::Guard)
 {
  auto* ASC=I->AbilitySystemComponent.Get();auto Spec=ASC->MakeOutgoingSpec(USwordGuardEffect::StaticClass(),1,ASC->MakeEffectContext());
  if(Spec.IsValid()) GuardHandle=ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
  GuardListener=ASC->OnGameplayEffectAppliedDelegateToSelf.AddUObject(this,&ThisClass::GuardHit);
 }
 GetWorld()->GetTimerManager().SetTimer(Timer,this,&ThisClass::Advance,.01f,true);
}
bool UStanderAbility::IsCounterDamage(const FGameplayEffectSpec& Spec)
{
 if(!UMyAbilitySystemLibrary::IsBlockedHit(Spec.GetContext())) return false;
 for(const auto& Pair:FMyGameplayTags::Get().DamageTypesToResistance)
  if(Spec.GetSetByCallerMagnitude(Pair.Key,false,0)>0) return true;
 return false;
}
bool UStanderAbility::IsMassCounterDamage(const FGameplayEffectSpec& Spec)
{
 const FHitResult* HitResult=Spec.GetContext().GetHitResult();
 return Spec.Def && Spec.Def->IsA<UPureMassIncomingDamageEffect>() && IsCounterDamage(Spec) && HitResult && HitResult->Item>0;
}
void UStanderAbility::GuardHit(UAbilitySystemComponent*,const FGameplayEffectSpec& Spec,FActiveGameplayEffectHandle)
{
 if(!IsActive() || Mode!=EMode::Guard || bCounter || bCounterPending || !GuardHandle.IsValid() || !Player.IsValid() || Player->IsDungeonDowned()) return;
 auto* Source=Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent();auto* Attacker=Source?Source->GetAvatarActor():nullptr;
 if(IsValid(Attacker) && UMyAbilitySystemLibrary::IsNotFriend(Player.Get(),Attacker) && ADungeonRunDirector::AllowsCombat(Player.Get(),Attacker) && IsCounterDamage(Spec))
  bCounterPending=true; // Defer montage switching until after damage callbacks unwind.
 else if(IsMassCounterDamage(Spec))
 {
  const FHitResult* HitResult=Spec.GetContext().GetHitResult();FVector Location;
  if(HitResult && HitResult->Item>0 && PureMassCombat::Resolve(GetWorld(),Player.Get(),FPureMassCombatTarget(HitResult->Item),Location))
  {
   auto* Director=ADungeonRunDirector::Find(GetWorld());
   if(!Director || Director->GetRoomIndex(Player->GetActorLocation())==Director->GetRoomIndex(Location)) bCounterPending=true;
  }
 }
}
void UStanderAbility::MoveDash(float Position)
{
 const auto* C=Config(CurrentActorInfo);if(!C || bDashBlocked || !Player.IsValid()) return;
 float Remaining=FMath::Max(0.f,C->DashFraction(Position)*C->DashDistance-DashApplied);
 while(Remaining>KINDA_SMALL_NUMBER && Player.IsValid() && IsActive())
 {
  const float Step=FMath::Min(Remaining,20.f);const FVector From=Player->GetActorLocation();FVector Next=From+LockedForward*Step;
  if(auto* D=ADungeonRunDirector::Find(GetWorld());D && (OriginRoom==INDEX_NONE || D->GetRoomIndex(Next)!=OriginRoom)){bDashBlocked=true;return;}
  FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(StanderDashFloor),false,Player.Get());
  const float Half=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
  if(!GetWorld()->LineTraceSingleByObjectType(Floor,Next+FVector(0,0,30),Next-FVector(0,0,Half+70),FCollisionObjectQueryParams(ECC_WorldStatic),Query)
   || Floor.ImpactNormal.Z<.7f || FMath::Abs(Floor.ImpactPoint.Z+Half+2-From.Z)>35){bDashBlocked=true;return;}
  Next.Z=Floor.ImpactPoint.Z+Half+2;FHitResult Block;
  Player->SetActorLocation(Next,true,&Block,ETeleportType::None);
  DashApplied+=Step;Remaining-=Step;
  if(Block.bBlockingHit){bDashBlocked=true;return;}
 }
}
void UStanderAbility::Advance()
{
 if(!IsActive()) return;
 auto Stop=[this](bool Cancel){EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,Cancel);};
 if(!Player.IsValid() || Player->IsDungeonDowned() || Player->IsDead_Implementation() || Player->bIsStunned){Stop(true);return;}
 auto* Anim=Player->GetMesh()->GetAnimInstance();const auto* C=Config(CurrentActorInfo);
 if(!C || !Anim || !Anim->Montage_IsActive(Strike.Montage)){Stop(!bHit);return;}
 if(Mode==EMode::Guard && !bCounter)
 {
  if(bCounterPending)
  {
   bCounter=true;bCounterPending=false;auto* ASC=CurrentActorInfo->AbilitySystemComponent.Get();
   ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(GuardListener);GuardListener.Reset();ASC->RemoveActiveGameplayEffect(GuardHandle);GuardHandle.Invalidate();
   if(!IsActive() || !Player.IsValid()) return;
   if(!StartStrike(C->Counter)) Stop(true);
   return;
  }
  if(GetWorld()->GetTimeSeconds()-Started>=C->GuardDuration) Stop(false);
  return;
 }
 const float Position=Anim->Montage_GetPosition(Strike.Montage);
 if(Mode==EMode::Dash) MoveDash(Position);
 if(!IsActive() || !Player.IsValid()) return;
 if(!bHit && Position>=Strike.Contact)
 {
  if(Position>Strike.Contact+.15f){Stop(true);return;}
  bHit=true;Hit();
 }
 if(IsActive() && GetWorld()->GetTimeSeconds()-Started>Strike.Montage->GetPlayLength()/Strike.PlayRate+.2f) Stop(!bHit);
}
void UStanderAbility::Hit()
{
 if(!Player.IsValid()) return;const auto* C=Config(CurrentActorInfo);
 if(C && C->SwingEffect) Player->ShowSwordImpact(C->SwingEffect,Player->GetActorLocation()+LockedForward*80);
 if(!C || !C->ValidateDeathImpulses()) return;
 const FVector LocalImpulse=Mode==EMode::Lift?C->LiftDeathImpulse:Mode==EMode::ShieldBash?C->ShieldBashDeathImpulse:
  Mode==EMode::Guard?C->CounterDeathImpulse:Mode==EMode::Spin?C->SpinDeathImpulse:Mode==EMode::Dash?C->DashDeathImpulse:C->ComboDeathImpulses[CurrentCombo];
 const FVector Origin=Player->GetActorLocation();
 TSet<FPureMassCombatTarget> Seen;
 for(const auto& Target:PureMassCombat::Gather(GetWorld(),Player.Get(),Origin,Strike.Radius+100))
 {
  if(!IsActive() || !Player.IsValid() || Player->IsDungeonDowned() || Player->IsDead_Implementation()) return;
  FVector Location;
  if(Seen.Contains(Target)) continue;
  Seen.Add(Target);
  if(!PureMassCombat::Resolve(GetWorld(),Player.Get(),Target,Location) || !Strike.Contains(Origin,LockedForward,Location)
   || !PureMassCombat::Visible(GetWorld(),Player.Get(),Origin,Target)) continue;
  if(auto* Director=ADungeonRunDirector::Find(GetWorld());Director && Director->GetRoomIndex(Origin)!=Director->GetRoomIndex(Location)) continue;
  // A circular sweep throws each victim away from the player, including those behind.
  const FVector RadialForward=(Location-Origin).GetSafeNormal2D();
  const FVector ImpulseForward=Mode==EMode::Spin && !RadialForward.IsNearlyZero()?RadialForward:LockedForward;
  const FVector Impulse=UStanderCombatConfig::WorldDeathImpulse(LocalImpulse,ImpulseForward);
  bool Applied=false;
  if(Target.IsActor())
  {
   auto Params=MakeDamageEffectParamsFromClassDefaults(Target.Actor.Get());if(!Params.TargetAbilitySystemComponent) continue;
   Params.BaseDamage=Strike.Damage;Params.DebuffChance=0;Params.bIsRadialDamage=false;
   Params.DeathImpulse=Impulse;
   ReportOffensiveAttack(Target.Actor.Get());
   if(!IsActive() || !Player.IsValid()) return;
   UMyAbilitySystemLibrary::ApplyDamageEffect(Params);Applied=true;
  }
  else if(auto* Mass=GetWorld()->GetSubsystem<UPureMassSubsystem>())
  {
   FPureMassDamage DamageParams;DamageParams.Source=Player.Get();DamageParams.Amount=Strike.Damage;DamageParams.DamageType=DamageType;DamageParams.DeathImpulse=Impulse;
   Applied=Mass->ApplyDamage(Target.EntityId,DamageParams)>0;
  }
  if(Applied && IsActive() && Player.IsValid() && C) Player->ShowSwordImpact(C->HitEffect,Location);
 }
}
void UStanderAbility::EndAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* I,FGameplayAbilityActivationInfo A,bool Replicate,bool Cancelled)
{
 if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(Timer);
 if(I && I->AbilitySystemComponent.IsValid())
 {
  auto* ASC=I->AbilitySystemComponent.Get();ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(GuardListener);GuardListener.Reset();
  if(GuardHandle.IsValid()){auto Handle=GuardHandle;GuardHandle.Invalidate();ASC->RemoveActiveGameplayEffect(Handle);}
  if(ASC->GetAnimatingAbility()==this) ASC->CurrentMontageStop(.08f);
 }
 if(bOwnMovement && Player.IsValid())
 {
  bOwnMovement=false;
  if(!Player->IsDungeonDowned() && !Player->IsDead_Implementation()) Player->GetCharacterMovement()->SetMovementMode(EMovementMode(PreviousMovement));
  if(Mode==EMode::Combo){NextCombo=Cancelled?0:(CurrentCombo+1)%3;LastComboEnd=GetWorld()->GetTimeSeconds();}
 }
 Player.Reset();bCounterPending=false;Super::EndAbility(H,I,A,Replicate,Cancelled);
}
FString UStanderAbility::GetDescription(int32)
{
 if(Mode==EMode::Lift) return TEXT("<Title>上挑斩</>\n<Default>向前上挑斩击，击杀时将敌人向上抛起。</>");
 if(Mode==EMode::ShieldBash) return TEXT("<Title>盾击</>\n<Default>近距离盾击，击杀时将敌人向前击飞。</>");
 if(Mode==EMode::Guard) return TEXT("<Title>盾防反击</>\n<Default>短暂举盾减半伤害；受到敌方攻击后反击一次。</>");
 if(Mode==EMode::Spin) return TEXT("<Title>回旋斩</>\n<Default>挥剑攻击周围敌人，每个目标受到一次物理伤害。</>");
 if(Mode==EMode::Dash) return TEXT("<Title>突进斩</>\n<Default>朝锁定方向冲刺并斩击；障碍物阻挡位移。</>");
 return TEXT("<Title>Stander剑盾连击</>\n<Default>连续施放衔接三段挥砍，停顿后重置。</>");
}
