#include "AbilitySystem/Batch6/Batch6ChainAbility.h"
#include "PureMass/PureMassAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/AbilityTask/TargetDataUnderMouse.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "PureMass/PureMassCombatTarget.h"
#include "PureMass/PureMassSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"

ABatch6ChainPresentation::ABatch6ChainPresentation()
{
    bReplicates=true; bAlwaysRelevant=true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    FallbackMesh=Mesh.Object;
}
void ABatch6ChainPresentation::BeginPlay()
{ Super::BeginPlay(); Present(); if(HasAuthority()) SetLifeSpan(1.5f); }
void ABatch6ChainPresentation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ABatch6ChainPresentation,Segments); DOREPLIFETIME(ABatch6ChainPresentation,BeamSystem); }
void ABatch6ChainPresentation::Present()
{
    if (GetNetMode()==NM_DedicatedServer) return;
    for(const auto& Component:PresentationComponents) if(IsValid(Component.Get())) Component->DestroyComponent();
    PresentationComponents.Reset();
    for (const auto& S:Segments)
    {
        // Real render geometry remains visible in packaged builds and if the selected Niagara has no beam parameters.
        if(FallbackMesh)
        {
            auto* Cylinder=NewObject<UStaticMeshComponent>(this);
            Cylinder->SetStaticMesh(FallbackMesh); Cylinder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Cylinder->SetupAttachment(GetRootComponent()); Cylinder->RegisterComponent();
            Cylinder->SetWorldLocation((S.Start+S.End)*.5f);
            Cylinder->SetWorldRotation(FRotationMatrix::MakeFromZ(S.End-S.Start).Rotator());
            Cylinder->SetWorldScale3D(FVector(.035f,.035f,FVector::Distance(S.Start,S.End)/100.f));
            PresentationComponents.Add(Cylinder);
        }
        if(BeamSystem) if(auto* V=UNiagaraFunctionLibrary::SpawnSystemAttached(BeamSystem,GetRootComponent(),NAME_None,S.Start,FRotator::ZeroRotator,EAttachLocation::KeepWorldPosition,true))
        {
            V->SetVariableVec3(TEXT("User.Start"),S.Start); V->SetVariableVec3(TEXT("User.End"),S.End); V->SetVariableVec3(TEXT("User.BeamEnd"),S.End);
            PresentationComponents.Add(V);
        }
    }
}
UBatch6ChainAbility::UBatch6ChainAbility()
{ InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor; NetExecutionPolicy=EGameplayAbilityNetExecutionPolicy::LocalPredicted; DebuffChance=0; KnockbackChance=0; }
void UBatch6ChainAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo Activation,const FGameplayEventData* Event)
{
    bResolved=false;
    GetWorld()->GetTimerManager().SetTimer(Timeout,this,&ThisClass::Abort,3.f);
    auto* Task=UTargetDataUnderMouse::CreateTargetDataUnderMouse(this);
    Task->ValidData.AddDynamic(this,&ThisClass::ReceiveTarget); Task->ReadyForActivation();
}
void UBatch6ChainAbility::Abort()
{ if (IsActive()) EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true); }
void UBatch6ChainAbility::ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info, FGameplayAbilityActivationInfo Activation) const
{
    if (Info && Info->IsNetAuthority() && Prototype) UPureMassAbility::SpendMana(Info->AvatarActor.Get(),Prototype->ManaCost);
}
FString UBatch6ChainAbility::GetDescription(int32 Level)
{
    if (!Prototype) return Super::GetDescription(Level);
    return FString::Printf(TEXT("<Title>%s</>\n<Small>Lv.%d | MP %.0f | CD %.1fs</>\n<Default>%.0f damage; %d branches, %d hops, up to %d foes.</>"),
        *Prototype->DisplayName.ToString(), Level, Prototype->ManaCost, Prototype->Cooldown,
        Prototype->DamageAtLevel(Level), BranchesPerTarget, MaxDepth, MaxTargets);
}
void UBatch6ChainAbility::EndAbility(FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo Activation,bool Replicate,bool Cancelled)
{ if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(Timeout); Super::EndAbility(Handle,Info,Activation,Replicate,Cancelled); }
void UBatch6ChainAbility::ReceiveTarget(const FGameplayAbilityTargetDataHandle& Data)
{
    if (!IsActive() || bResolved || !CurrentActorInfo->IsNetAuthority()) return;
    bResolved=true;
    const auto* Hit=Data.Num()==1 && Data.Get(0)?Data.Get(0)->GetHitResult():nullptr;
    AActor* Caster=GetAvatarActorFromActorInfo();
    if (!Hit || Hit->ImpactPoint.ContainsNaN() || !Caster || !DamageEffectClass) { Abort(); return; }
    FVector Origin=Caster->GetActorLocation();
    auto Candidates=PureMassCombat::Gather(GetWorld(),Caster,Hit->ImpactPoint,150.f);
    Candidates.Sort([&](const FPureMassCombatTarget& A,const FPureMassCombatTarget& B){ FVector X,Y; PureMassCombat::Resolve(GetWorld(),Caster,A,X); PureMassCombat::Resolve(GetWorld(),Caster,B,Y); return FVector::DistSquared(X,Hit->ImpactPoint)<FVector::DistSquared(Y,Hit->ImpactPoint); });
    FPureMassCombatTarget First;
    for (auto T:Candidates) { FVector P; if(PureMassCombat::Resolve(GetWorld(),Caster,T,P) && FVector::DistSquared(Origin,P)<=FMath::Square(FMath::Clamp(CastRange,1.f,5000.f)) && PureMassCombat::Visible(GetWorld(),Caster,Origin,T)) { First=T; break; } }
    SetOffensiveAttackTarget(First.Actor.Get());
    if (!First.IsSet() || !CanActivateAbility(CurrentSpecHandle,CurrentActorInfo) || !CommitAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo)) { Abort(); return; }
    struct FPending { FPureMassCombatTarget Target; FVector From; int32 Depth; };
    TArray<FPending> Queue; Queue.Add({First,Origin,0});
    TSet<FPureMassCombatTarget> Visited; Visited.Add(First);
    TArray<FBatch6ChainSegment> Segments;
    for(int32 Index=0; Index<Queue.Num() && Index<128; ++Index)
    {
        const auto Entry=Queue[Index]; FVector P;
        if(!PureMassCombat::Resolve(GetWorld(),Caster,Entry.Target,P)) continue;
        FBatch6ChainSegment S; S.Start=Entry.From; S.End=P; Segments.Add(S);
        auto Params=MakeDamageEffectParamsFromClassDefaults();
        if (Prototype) { Params.BaseDamage=Prototype->DamageAtLevel(GetAbilityLevel()) * FMath::Pow(Prototype->DamageFalloff, Entry.Depth); Params.DamageType=Prototype->DamageType; }
        Params.DebuffChance=0; Params.bIsRadialDamage=false;
        if(Entry.Target.IsActor()) { Params.TargetAbilitySystemComponent=UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Entry.Target.Actor.Get()); if(Params.TargetAbilitySystemComponent) UMyAbilitySystemLibrary::ApplyDamageEffect(Params); }
        else if(auto* Mass=GetWorld()->GetSubsystem<UPureMassSubsystem>()) { FPureMassDamage D; D.Source=Caster; D.Amount=Params.BaseDamage; D.DamageType=Params.DamageType; Mass->ApplyDamage(Entry.Target.EntityId,D); }
        auto Next=PureMassCombat::Gather(GetWorld(),Caster,P,FMath::Clamp(JumpRadius,1.f,2000.f));
        int32 Siblings=0;
        for(auto T:Next)
        {
            if(!FBatch6ChainBudget::Allows(Visited.Num(),Entry.Depth+1,Siblings,MaxTargets,MaxDepth,BranchesPerTarget)) break;
            FVector Q;
            if(Visited.Contains(T) || !PureMassCombat::Resolve(GetWorld(),Caster,T,Q) || FVector::DistSquared(P,Q)>FMath::Square(FMath::Clamp(JumpRadius,1.f,2000.f)) || !PureMassCombat::Visible(GetWorld(),Caster,P,T)) continue;
            Visited.Add(T); Queue.Add({T,P,Entry.Depth+1}); ++Siblings;
        }
    }
    FActorSpawnParameters Spawn; Spawn.Owner=Caster;
    if(auto* Visual=GetWorld()->SpawnActor<ABatch6ChainPresentation>(Origin,FRotator::ZeroRotator,Spawn))
    { Visual->Segments=Segments; Visual->BeamSystem=BeamSystem; Visual->Present(); Visual->ForceNetUpdate(); }
    EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,false);
}

