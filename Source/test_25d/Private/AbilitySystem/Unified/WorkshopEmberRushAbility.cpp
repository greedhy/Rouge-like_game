// 调用链：权威 Ground TargetData → GAS Commit → Movement.Character → Landed → World.ElementSurface。
// 位移随本次 GA 封闭并归还移动租约，已落地的地块由来源生命和独立期限管理。
#include "AbilitySystem/Unified/WorkshopEmberRushAbility.h"

#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/Nodes/AbilityMovementNodeExecutions.h"
#include "AbilitySystem/Unified/Nodes/AbilityWorkshopTerrainNodes.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EmberRush, "Abilities.Mage.EmberRush");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EmberRushCooldown, "Cooldown.Mage.EmberRush");

UWorkshopEmberRush::UWorkshopEmberRush()
{
    SetAssetTags(FGameplayTagContainer(TAG_EmberRush));
    CooldownTags.AddTag(TAG_EmberRushCooldown);
    TargetMode = EUnifiedAbilityTargetMode::Ground;
    CastRange = 1250.f;
    ManaCost = 16.f;
    Cooldown = 7.f;
    SpellName = NSLOCTEXT("SkillCards", "EmberRush", "焰落突进");
    SpellSummary = NSLOCTEXT("SkillCards", "EmberRushSummary",
                             "朝准星冲刺最多 6.2 米，受地面、墙体和房间边界限制。在实际停止处留下熔岩。");

    const auto& Initializer = FObjectInitializer::Get();
    auto* Move = Initializer.CreateDefaultSubobject<UAbilityCharacterMoveNodeConfig>(Graph, TEXT("RushMove"));
    Move->bUseGroundTarget = true;
    Move->Distance = 620.f;
    Move->Duration = .28f;
    auto* Surface =
        Initializer.CreateDefaultSubobject<UAbilityElementSurfaceNodeConfig>(Graph, TEXT("RushLandedSurface"));
    Surface->Kind = ETerrainSurfaceType::Lava;
    Surface->Radius = 150.f;
    Surface->Duration = 4.f;

    Graph->EntryNodeId = FGuid(0, 0, 0, 1);
    const FGuid SurfaceId(0, 0, 0, 2);
    Graph->Nodes = {{Graph->EntryNodeId, TEXT("Movement.Character"), Move},
                    {SurfaceId, TEXT("World.ElementSurface"), Surface}};
    Graph->Edges = {{Graph->EntryNodeId, SurfaceId, TEXT("Landed")}};
}

bool UWorkshopEmberRush::CheckCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                   FGameplayTagContainer* Relevant) const
{
    if (!Super::CheckCost(Handle, Info, Relevant))
    {
        return false;
    }
    if (!Info || !Info->IsNetAuthority())
    {
        return true;
    }
    const auto* Character = Cast<ACharacter>(Info->AvatarActor.Get());
    const auto* Movement = Character ? Character->GetCharacterMovement() : nullptr;
    return Movement && Movement->MovementMode != MOVE_None &&
           !UAbilityMovementOwnershipComponent::HasActiveClaim(Character);
}
