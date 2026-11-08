#include "Character/TelegraphedEnemyCharacter.h"
#include "AbilitySystem/Ability/TelegraphedEnemyAbility.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/CapsuleComponent.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

ATelegraphedEnemyCharacter::ATelegraphedEnemyCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    Tags.AddUnique(TEXT("Enemy"));
    Footprint = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("AttackFootprint"));
    Footprint->SetupAttachment(GetRootComponent());
    Footprint->SetAbsolute(true, true, true);
    Footprint->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Footprint->CastShadow = false;
    Exclamation = CreateDefaultSubobject<UTextRenderComponent>(TEXT("AttackExclamation"));
    Exclamation->SetupAttachment(GetRootComponent());
    Exclamation->SetText(FText::FromString(TEXT("!")));
    Exclamation->SetTextRenderColor(FColor::Red);
    Exclamation->SetWorldSize(60);
    Exclamation->SetHorizontalAlignment(EHTA_Center);
    Exclamation->SetVisibility(false);
    GetMesh()->SetRelativeLocation(FVector(0, 0, -89));
    GetMesh()->SetRelativeRotation(FRotator(0, -90, 0));
    GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    GetCapsuleComponent()->InitCapsuleSize(45, 89);
}
void ATelegraphedEnemyCharacter::ApplyConfig()
{
    if (!GetCombatConfig())
        return;
    if (GetCombatConfig()->Mesh && GetMesh()->GetSkeletalMeshAsset() != GetCombatConfig()->Mesh)
        GetMesh()->SetSkeletalMeshAsset(GetCombatConfig()->Mesh);
    if (GetCombatConfig()->AnimClass && GetMesh()->GetAnimClass() != GetCombatConfig()->AnimClass)
        GetMesh()->SetAnimInstanceClass(GetCombatConfig()->AnimClass);
    BehaviorTree = GetCombatConfig()->Behavior;
    BaseWalkSpeed = GetCombatConfig()->WalkSpeed;
    GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
}
void ATelegraphedEnemyCharacter::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    ApplyConfig();
}
void ATelegraphedEnemyCharacter::BeginPlay()
{
    ApplyConfig();
    Super::BeginPlay();
}
void ATelegraphedEnemyCharacter::GrantEnemyAbilities()
{
    // A previous life may have died while its stationary attack disabled controller rotation.
    GetCharacterMovement()->bUseControllerDesiredRotation = GetClass()
                                                                ->GetDefaultObject<ATelegraphedEnemyCharacter>()
                                                                ->GetCharacterMovement()
                                                                ->bUseControllerDesiredRotation;
    if (!HasAuthority() || !GetCombatConfig())
        return;
    auto Give = [this](TSubclassOf<UGameplayAbility> Class)
    {
        if (Class && !AbilitySystemComponent->FindAbilitySpecFromClass(Class))
            AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(Class, Level));
    };
    // This elite deliberately has no generic hit-react montage: ordinary hits do not interrupt attacks.
    Give(UTelegraphedSelectAttack::StaticClass());
    for (const auto Class : GetCombatConfig()->Attacks)
        Give(Class);
    bAttackActive = false;
}
bool ATelegraphedEnemyCharacter::TryAttackIndex(int32 Index)
{
    if (!HasAuthority() || !GetCombatConfig() || !GetCombatConfig()->Attacks.IsValidIndex(Index))
        return false;
    const auto Class = GetCombatConfig()->Attacks[Index];
    return Class && AbilitySystemComponent->TryActivateAbilityByClass(Class);
}
bool ATelegraphedEnemyCharacter::TryChooseAttack()
{
    if (!HasAuthority() || bDead || bAttackActive || !GetCombatConfig())
        return false;
    TArray<TPair<int32, float>> Choices;
    float Total = 0;
    for (int32 Index = 0; Index < GetCombatConfig()->Attacks.Num(); ++Index)
    {
        auto* Spec = AbilitySystemComponent->FindAbilitySpecFromClass(GetCombatConfig()->Attacks[Index]);
        const auto* Ability = Spec ? Cast<UTelegraphedEnemyAbility>(Spec->Ability) : nullptr;
        if (Ability && Ability->AttackConfig &&
            Ability->CanActivateAbility(Spec->Handle, AbilitySystemComponent->AbilityActorInfo.Get()))
        {
            Total += Ability->AttackConfig->Tuning.SelectionWeight;
            Choices.Emplace(Index, Total);
        }
    }
    if (Choices.IsEmpty())
        return false;
    const float Roll = FMath::FRand() * Total;
    for (const auto& Choice : Choices)
        if (Roll <= Choice.Value)
            return TryAttackIndex(Choice.Key);
    return TryAttackIndex(Choices.Last().Key);
}
int32 ATelegraphedEnemyCharacter::BeginAttack(const FStoneGolemAttackTuning& Tuning, const FVector& Center, float Yaw)
{
    if (!HasAuthority() || bDead || bAttackActive)
        return 0;
    ClearImpact();
    bAttackActive = true;
    Presentation.Serial = ++Generation;
    Presentation.Lease = GetDungeonPoolLease();
    Presentation.Phase = 1;
    Presentation.Tuning = Tuning;
    Presentation.Center = Center;
    Presentation.Yaw = Yaw;
    OnRep_Presentation();
    ForceNetUpdate();
    return Generation;
}
bool ATelegraphedEnemyCharacter::IsAttackCurrent(int32 Serial, int32 Lease) const
{
    return Serial != 0 && bAttackActive && !bDead && !IsDungeonPoolSleeping() && Generation == Serial &&
           GetDungeonPoolLease() == Lease;
}
void ATelegraphedEnemyCharacter::UpdateWarning(int32 Serial, const FVector& Center, float Yaw,
                                               const FStoneGolemAttackTuning* Tuning)
{
    if (Serial != Generation || !bAttackActive || Presentation.Phase != 1)
        return;
    if (Tuning)
        Presentation.Tuning = *Tuning;
    Presentation.Center = Center;
    Presentation.Yaw = Yaw;
    OnRep_Presentation();
    ForceNetUpdate();
}
void ATelegraphedEnemyCharacter::ShowActiveFootprint(int32 Serial, const FVector* Impact)
{
    if (Serial != Generation || !bAttackActive)
        return;
    // One replicated state carries the persistent field and its initial effect.
    Presentation.Phase = Impact ? 4 : 3;
    if (Impact)
        Presentation.Impact = *Impact;
    OnRep_Presentation();
    ForceNetUpdate();
}
void ATelegraphedEnemyCharacter::ShowImpact(int32 Serial, const FVector& Impact)
{
    if (Serial != Generation || !bAttackActive)
        return;
    Presentation.Phase = 2;
    Presentation.Impact = Impact;
    OnRep_Presentation();
    ForceNetUpdate();
}
void ATelegraphedEnemyCharacter::FinishAttack(int32 Serial)
{
    if (Serial != Generation)
        return;
    bAttackActive = false;
    Presentation.Phase = 0;
    OnRep_Presentation();
    ForceNetUpdate();
}
void ATelegraphedEnemyCharacter::InvalidateEnemyAttack()
{
    // Clients must not overwrite a newer presentation that arrived before its life RepNotify.
    if (HasAuthority())
    {
        ++Generation;
        bAttackActive = false;
        Presentation.Serial = Generation;
        Presentation.Phase = 0;
        ForceNetUpdate();
    }
    Footprint->ClearAllMeshSections();
    Exclamation->SetVisibility(false);
    ClearImpact();
}
void ATelegraphedEnemyCharacter::StunTagChanged(FGameplayTag Tag, int32 Count)
{
    Super::StunTagChanged(Tag, Count);
    if (HasAuthority() && Count > 0)
        InvalidateEnemyAttack();
}
void ATelegraphedEnemyCharacter::ClearImpact()
{
    GetWorldTimerManager().ClearTimer(ImpactExpiry);
    if (ImpactComponent)
    {
        ImpactComponent->DestroyComponent();
        ImpactComponent = nullptr;
    }
}
void ATelegraphedEnemyCharacter::OnRep_Presentation()
{
    if (GetNetMode() == NM_DedicatedServer)
        return;
    const bool Valid = !bDead && !IsDungeonPoolSleeping() && GetDungeonPoolLease() == Presentation.Lease;
    const bool Warning = Valid && Presentation.Phase == 1;
    Exclamation->SetVisibility(Warning && Presentation.Tuning.bExclamation);
    Exclamation->SetRelativeLocation(Presentation.Tuning.HeadOffset);
    if (Valid && (Warning || Presentation.Phase == 3 || Presentation.Phase == 4) && Presentation.Tuning.bDrawFootprint)
        BuildFootprint();
    else
        Footprint->ClearAllMeshSections();
    if (!Valid || Presentation.Phase == 0)
        ClearImpact();
    if (Valid && (Presentation.Phase == 2 || Presentation.Phase == 4) && LastImpact != Presentation.Serial)
    {
        LastImpact = Presentation.Serial;
        ClearImpact();
        if (Presentation.Tuning.ImpactEffect)
        {
            ImpactComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                this, Presentation.Tuning.ImpactEffect, Presentation.Impact, FRotator::ZeroRotator,
                FVector(Presentation.Tuning.ImpactScale), false, true);
            GetWorldTimerManager().SetTimer(ImpactExpiry, this, &ThisClass::ClearImpact,
                                            Presentation.Tuning.ImpactLifetime, false);
        }
    }
}
void ATelegraphedEnemyCharacter::BuildFootprint()
{
    const auto& T = Presentation.Tuning;
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    StoneGolemGeometry::BuildSurface(GetWorld(), this, T, Presentation.Center, Presentation.Yaw, Vertices, Triangles);
    for (auto& Vertex : Vertices)
    {
        Vertex.Z += 2;
        Normals.Add(FVector::UpVector);
        UV.Add(FVector2D::ZeroVector);
        Colors.Add(FLinearColor::Red);
        Tangents.Add(FProcMeshTangent(1, 0, 0));
    }
    Footprint->SetWorldTransform(FTransform::Identity);
    Footprint->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UV, Colors, Tangents, false);
    Footprint->SetMaterial(0, T.WarningMaterial);
}
void ATelegraphedEnemyCharacter::Tick(float Delta)
{
    Super::Tick(Delta);
    if (Exclamation->IsVisible())
        if (const auto* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
            Exclamation->SetWorldRotation(
                (Camera->GetCameraLocation() - Exclamation->GetComponentLocation()).Rotation());
}
void ATelegraphedEnemyCharacter::EndPlay(EEndPlayReason::Type Reason)
{
    ClearImpact();
    Super::EndPlay(Reason);
}
void ATelegraphedEnemyCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ATelegraphedEnemyCharacter, Presentation);
}
