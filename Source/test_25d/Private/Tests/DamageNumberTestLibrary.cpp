#include "DamageNumberTestLibrary.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
bool UDamageNumberTestLibrary::ScheduleDamagePair(const FDamageEffectParams& Params)
{
#if WITH_EDITOR
 UWorld* World=IsValid(Params.SourceAbilitySystemComponent)?Params.SourceAbilitySystemComponent->GetWorld():nullptr;
 if(!World || World->WorldType!=EWorldType::PIE || World->GetNetMode()==NM_Client || !IsValid(Params.TargetAbilitySystemComponent))return false;
 // Native world-tick execution exercises normal ASC and RPC dispatch, unlike
 // running the damage directly inside the editor Python execution guard.
 FTimerHandle Handle;
 World->GetTimerManager().SetTimer(Handle,FTimerDelegate::CreateWeakLambda(Params.SourceAbilitySystemComponent,[Params]()
 {
  if(IsValid(Params.TargetAbilitySystemComponent))for(int32 I=0;I<2;++I)UMyAbilitySystemLibrary::ApplyDamageEffect(Params);
 }),.1f,false);
 return true;
#else
 return false;
#endif
}
