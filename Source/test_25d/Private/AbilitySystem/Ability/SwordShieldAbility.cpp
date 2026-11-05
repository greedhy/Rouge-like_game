#include "AbilitySystem/Ability/SwordShieldAbility.h"
#include "Character/SwordShieldPlayer.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "NativeGameplayTags.h"
#include "TimerManager.h"
#include "Roguelike/DungeonRunDirector.h"
#include "MyGameplayTags.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
UE_DEFINE_GAMEPLAY_TAG_STATIC(SwordComboTag,"Abilities.SwordShield.Combo");
UE_DEFINE_GAMEPLAY_TAG_STATIC(SwordGuardTag,"Abilities.SwordShield.Guard");
UE_DEFINE_GAMEPLAY_TAG_STATIC(SwordSpinTag,"Abilities.SwordShield.Spin");
UE_DEFINE_GAMEPLAY_TAG_STATIC(SwordBusyTag,"State.SwordShield.Busy");
UE_DEFINE_GAMEPLAY_TAG_STATIC(SwordGuardCD,"Cooldown.SwordShield.Guard");
UE_DEFINE_GAMEPLAY_TAG_STATIC(SwordSpinCD,"Cooldown.SwordShield.Spin");
UE_DEFINE_GAMEPLAY_TAG_STATIC(SwordManaTag,"Data.SwordShield.Mana");
UE_DEFINE_GAMEPLAY_TAG_STATIC(SwordGuardActiveTag,"State.SwordShield.Guarding");
FGameplayTag USwordGuardEffect::StateTag(){return SwordGuardActiveTag;}
USwordTimedEffect::USwordTimedEffect(){DurationPolicy=EGameplayEffectDurationType::HasDuration;DurationMagnitude=FScalableFloat(1);}
USwordGuardEffect::USwordGuardEffect()
{
 DurationPolicy=EGameplayEffectDurationType::Infinite;
 // Player secondary-stat effects already override BlockChance. A separate tag
 // guarantees guard without replacing or competing with equipment attributes.
 auto* Component=CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("GuardTags"));GEComponents.Add(Component);
 FInheritedTagContainer Tags;Tags.AddTag(SwordGuardActiveTag);Component->SetAndApplyTargetTagChanges(Tags);
}
USwordManaEffect::USwordManaEffect()
{
 DurationPolicy=EGameplayEffectDurationType::Instant;
 FGameplayModifierInfo Mod;Mod.Attribute=UMyAttributeSet::GetManaAttribute();Mod.ModifierOp=EGameplayModOp::Additive;
 FSetByCallerFloat Value;Value.DataTag=SwordManaTag;Mod.ModifierMagnitude=FGameplayEffectModifierMagnitude(Value);Modifiers.Add(Mod);
}
USwordShieldAbility::USwordShieldAbility()
{
 InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;NetExecutionPolicy=EGameplayAbilityNetExecutionPolicy::ServerOnly;
 ActivationOwnedTags.AddTag(SwordBusyTag);ActivationBlockedTags.AddTag(SwordBusyTag);
 DebuffChance=0;KnockbackChance=0;DeathImpulseMagnitude=0;KnockbackForceMagnitude=0;
}
USwordComboAbility::USwordComboAbility(){SetAssetTags(FGameplayTagContainer(SwordComboTag));}
USwordGuardAbility::USwordGuardAbility(){Mode=EMode::Guard;SetAssetTags(FGameplayTagContainer(SwordGuardTag));CooldownIdentity=SwordGuardCD;}
USwordSpinAbility::USwordSpinAbility(){Mode=EMode::Spin;SetAssetTags(FGameplayTagContainer(SwordSpinTag));CooldownIdentity=SwordSpinCD;}
const USwordShieldConfig* USwordShieldAbility::Config(const FGameplayAbilityActorInfo* Info) const
{auto* P=Info?Cast<ASwordShieldPlayer>(Info->AvatarActor.Get()):nullptr;return P?P->SwordConfig.Get():nullptr;}
bool USwordShieldAbility::CanActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* I,const FGameplayTagContainer* S,const FGameplayTagContainer* T,FGameplayTagContainer* R) const
{
 const auto* C=Config(I);auto* P=I?Cast<ASwordShieldPlayer>(I->AvatarActor.Get()):nullptr;
 if(!P || !C || !C->Validate() || !I->AbilitySystemComponent.IsValid() || P->IsDungeonDowned() || P->IsDead_Implementation() || P->bIsStunned
  || !P->GetMesh()->GetAnimInstance() || !DamageEffectClass || !DamageType.IsValid()) return false;
 return Super::CanActivateAbility(H,I,S,T,R);
}
bool USwordShieldAbility::CheckCooldown(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo* I,FGameplayTagContainer*) const
{return I && I->AbilitySystemComponent.IsValid() && (!CooldownIdentity.IsValid() || !I->AbilitySystemComponent->HasMatchingGameplayTag(CooldownIdentity));}
void USwordShieldAbility::ApplyCooldown(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo* I,FGameplayAbilityActivationInfo) const
{
 const auto* C=Config(I);if(!C || !I->IsNetAuthority() || !CooldownIdentity.IsValid()) return;
 auto* ASC=I->AbilitySystemComponent.Get();auto Spec=ASC->MakeOutgoingSpec(USwordTimedEffect::StaticClass(),1,ASC->MakeEffectContext());
 if(Spec.IsValid()){Spec.Data->SetDuration(Mode==EMode::Guard?C->GuardCooldown:C->SpinCooldown,true);Spec.Data->DynamicGrantedTags.AddTag(CooldownIdentity);ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);}
}
bool USwordShieldAbility::CheckCost(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo* I,FGameplayTagContainer*) const
{const auto* C=Config(I);return C && I->AbilitySystemComponent.IsValid() && (Mode!=EMode::Spin || I->AbilitySystemComponent->GetNumericAttribute(UMyAttributeSet::GetManaAttribute())>=C->SpinMana);}
void USwordShieldAbility::ApplyCost(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo* I,FGameplayAbilityActivationInfo) const
{
 const auto* C=Config(I);if(!C || !I->IsNetAuthority() || Mode!=EMode::Spin) return;
 auto* ASC=I->AbilitySystemComponent.Get();auto Spec=ASC->MakeOutgoingSpec(USwordManaEffect::StaticClass(),1,ASC->MakeEffectContext());
 if(Spec.IsValid()){Spec.Data->SetSetByCallerMagnitude(SwordManaTag,-C->SpinMana);ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);}
}
void USwordShieldAbility::ActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* I,FGameplayAbilityActivationInfo A,const FGameplayEventData*)
{
 bOwnMovement=false;bHit=false;GuardHandle.Invalidate();Player=I?Cast<ASwordShieldPlayer>(I->AvatarActor.Get()):nullptr;
 const auto* C=Config(I);
 if(!Player.IsValid() || !C || !C->Validate() || !I->IsNetAuthority()){EndAbility(H,I,A,true,true);return;}
 Started=GetWorld()->GetTimeSeconds();CurrentCombo=USwordShieldConfig::ResolveComboIndex(NextCombo,LastComboEnd,Started,C->ComboReset);
 Strike=Mode==EMode::Spin?C->Spin:C->Combo[CurrentCombo];GuardTime=C->GuardDuration;
 if(Mode==EMode::Guard){Strike.Montage=C->GuardMontage;Strike.PlayRate=Strike.Montage->GetPlayLength()/GuardTime;}
 auto* ASC=I->AbilitySystemComponent.Get();
 if(!CommitAbility(H,I,A)){EndAbility(H,I,A,true,true);return;}
 FVector Aim=Player->GetActorForwardVector();
 if(auto* PC=I->PlayerController.Get()) if(PC->IsLocalController())
 {
  FHitResult HitResult;if(PC->GetHitResultUnderCursor(ECC_Visibility,false,HitResult) && !HitResult.ImpactPoint.ContainsNaN())
  {const FVector Delta=(HitResult.ImpactPoint-Player->GetActorLocation()).GetSafeNormal2D();if(!Delta.IsNearlyZero()) Aim=Delta;}
 }
 LockedForward=Aim.GetSafeNormal2D();Player->SetActorRotation(LockedForward.Rotation());
 auto* Movement=Player->GetCharacterMovement();PreviousMovement=Movement->MovementMode;bOwnMovement=true;
 Movement->StopMovementImmediately();Movement->DisableMovement();
 if(ASC->PlayMontage(this,A,Strike.Montage,Strike.PlayRate)<=0){EndAbility(H,I,A,true,true);return;}
 if(Mode==EMode::Guard)
 {
  auto Spec=ASC->MakeOutgoingSpec(USwordGuardEffect::StaticClass(),1,ASC->MakeEffectContext());
  if(Spec.IsValid()) GuardHandle=ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
 }
 GetWorld()->GetTimerManager().SetTimer(Timer,this,&ThisClass::Advance,.01f,true);
}
void USwordShieldAbility::Advance()
{
 if(!IsActive()) return;
 auto Stop=[this](bool Cancel){EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,Cancel);};
 if(!Player.IsValid() || Player->IsDungeonDowned() || Player->IsDead_Implementation() || Player->bIsStunned){Stop(true);return;}
 auto* Anim=Player->GetMesh()->GetAnimInstance();const float Elapsed=GetWorld()->GetTimeSeconds()-Started;
 if(Mode==EMode::Guard && Elapsed>=GuardTime){Stop(false);return;}
 if(!Anim || !Anim->Montage_IsActive(Strike.Montage)){Stop(Mode!=EMode::Guard?!bHit:Elapsed<GuardTime-.12f);return;}
 if(Mode!=EMode::Guard && !bHit && Anim->Montage_GetPosition(Strike.Montage)>=Strike.Contact)
 {
  if(Anim->Montage_GetPosition(Strike.Montage)>Strike.Contact+.15f){Stop(true);return;}
  bHit=true;Hit();if(!IsActive()) return;
 }
 if(Elapsed>Strike.Montage->GetPlayLength()/Strike.PlayRate+.2f) Stop(Mode!=EMode::Guard && !bHit);
}
void USwordShieldAbility::Hit()
{
 if(!Player.IsValid()) return;TArray<AActor*> Targets;
 UMyAbilitySystemLibrary::GetLivePlayersWithinRadius(Player.Get(),Targets,{Player.Get()},Strike.Radius+100,Player->GetActorLocation());
 for(auto* Target:Targets)
 {
  // Damage/death delegates may cancel this ability while processing an earlier target.
  if(!IsActive() || !Player.IsValid() || Player->IsDungeonDowned() || Player->IsDead_Implementation()) return;
  if(!IsValid(Target) || !Target->Implements<UCombatInterface>() || ICombatInterface::Execute_IsDead(Target)
   || !UMyAbilitySystemLibrary::IsNotFriend(Player.Get(),Target) || !ADungeonRunDirector::AllowsCombat(Player.Get(),Target)
   || !Strike.Contains(Player->GetActorLocation(),LockedForward,Target->GetActorLocation())) continue;
  FHitResult Wall;FCollisionQueryParams Query(SCENE_QUERY_STAT(SwordMeleeWall),false,Player.Get());Query.AddIgnoredActor(Target);
  if(GetWorld()->LineTraceSingleByObjectType(Wall,Player->GetActorLocation(),Target->GetActorLocation(),FCollisionObjectQueryParams(ECC_WorldStatic),Query)) continue;
  auto Params=MakeDamageEffectParamsFromClassDefaults(Target);if(!Params.TargetAbilitySystemComponent) continue;
  Params.BaseDamage=Strike.Damage;Params.DebuffChance=0;Params.bIsRadialDamage=false;ReportOffensiveAttack(Target);
  UMyAbilitySystemLibrary::ApplyDamageEffect(Params);
  if(Player.IsValid() && Player->SwordConfig) Player->ShowSwordImpact(Player->SwordConfig->HitEffect,Target->GetActorLocation());
 }
}
void USwordShieldAbility::EndAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* I,FGameplayAbilityActivationInfo A,bool Replicate,bool Cancelled)
{
 if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(Timer);
 if(I && I->AbilitySystemComponent.IsValid())
 {
  if(GuardHandle.IsValid()) I->AbilitySystemComponent->RemoveActiveGameplayEffect(GuardHandle);
  if(I->AbilitySystemComponent->GetAnimatingAbility()==this) I->AbilitySystemComponent->CurrentMontageStop(.08f);
 }
 GuardHandle.Invalidate();
 if(bOwnMovement && Player.IsValid())
 {
  if(!Player->IsDungeonDowned() && !Player->IsDead_Implementation()) Player->GetCharacterMovement()->SetMovementMode(EMovementMode(PreviousMovement));
  if(Mode==EMode::Combo){NextCombo=Cancelled?0:(CurrentCombo+1)%3;LastComboEnd=GetWorld()->GetTimeSeconds();}
 }
 bOwnMovement=false;Player.Reset();Super::EndAbility(H,I,A,Replicate,Cancelled);
}
FString USwordShieldAbility::GetDescription(int32)
{
 if(Mode==EMode::Guard) return TEXT("<Title>盾防</>\n<Default>短暂举盾，期间格挡伤害并减半；不能同时攻击或移动。</>");
 if(Mode==EMode::Spin) return TEXT("<Title>旋转斩</>\n<Default>原地挥剑，对周围敌人各造成一次物理伤害。</>");
 return TEXT("<Title>剑盾连击</>\n<Default>连续攻击衔接三段挥砍，停止攻击后重置连段。</>");
}
