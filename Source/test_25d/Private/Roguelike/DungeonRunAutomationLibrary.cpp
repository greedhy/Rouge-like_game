#include "Roguelike/DungeonRunAutomationLibrary.h"
#include "Roguelike/DungeonRunController.h"
#include "Roguelike/DungeonRunDirector.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Player/MyPlayerState.h"
#include "TimerManager.h"
#include "Equipment/EquipmentComponent.h"
#include "Backpack/BackpackComponent.h"

#if WITH_EDITOR
namespace
{
    bool IsSoleOwningPIEController(const ADungeonRunController* Controller)
    {
        if (!IsValid(Controller) || Controller->IsActorBeingDestroyed() || !Controller->IsLocalController())
            return false;
        const auto* World = Controller->GetWorld();
        const auto* LocalPlayer = Controller->GetLocalPlayer();
        if (!World || World->WorldType != EWorldType::PIE || !LocalPlayer ||
            LocalPlayer->GetPlayerController(World) != Controller)
            return false;
        int32 LocalCount = 0;
        for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
            if (const auto* Candidate = It->Get();
                Candidate && Candidate->IsLocalController() && Candidate->GetLocalPlayer())
                ++LocalCount;
        return LocalCount == 1;
    }

    struct FQueuedDungeonPlayer
    {
        TWeakObjectPtr<ADungeonRunController> Controller;
        TWeakObjectPtr<APawn> Pawn;
        TWeakObjectPtr<UWorld> World;
        FGuid CurrentRunId;

        explicit FQueuedDungeonPlayer(ADungeonRunController* InController)
            : Controller(InController), Pawn(InController ? InController->GetPawn() : nullptr),
              World(InController ? InController->GetWorld() : nullptr),
              CurrentRunId(InController ? InController->RunView.RunId : FGuid())
        {
        }

        bool IsCurrent() const
        {
            const auto* PC = Controller.Get();
            if (!IsSoleOwningPIEController(PC) || !CurrentRunId.IsValid() || !Pawn.IsValid() ||
                Pawn->IsActorBeingDestroyed() || PC->GetWorld() != World.Get() || PC->GetPawn() != Pawn.Get() ||
                Pawn->GetController() != PC || PC->RunView.RunId != CurrentRunId)
                return false;
            const auto* Director = ADungeonRunDirector::Find(World.Get());
            return Director && Director->RunId == CurrentRunId;
        }
    };
} // namespace
#endif

bool UDungeonRunAutomationLibrary::QueueInteraction(ADungeonRunController* Controller, FGuid RequestRunId, FName RoomId,
                                                    EDungeonInteraction Action, int32 OfferIndex)
{
#if WITH_EDITOR
    const FQueuedDungeonPlayer Queued(Controller);
    if (!Queued.IsCurrent())
        return false;
    Controller->GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(
        Controller,
        [Queued, RequestRunId, RoomId, Action, OfferIndex]()
        {
            if (!Queued.IsCurrent())
                return;
            // Use the real RPC. Capture the actual run separately so stale-request negative tests remain possible.
            Queued.Controller->ServerRoomInteraction(RequestRunId, RoomId, Action, OfferIndex);
        }));
    return true;
#else
    return false;
#endif
}

bool UDungeonRunAutomationLibrary::QueueAbilityActivation(ADungeonRunController* Controller,
                                                          TSubclassOf<UGameplayAbility> AbilityClass)
{
#if WITH_EDITOR
    const FQueuedDungeonPlayer Queued(Controller);
    if (!Queued.IsCurrent() || !AbilityClass)
        return false;
    const TWeakObjectPtr<UClass> WeakAbilityClass(AbilityClass.Get());
    Controller->GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(
        Controller,
        [Queued, WeakAbilityClass]()
        {
            if (!Queued.IsCurrent() || !WeakAbilityClass.IsValid())
                return;
            const auto* State = Queued.Controller->GetPlayerState<AMyPlayerState>();
            auto* ASC = State ? State->GetAbilitySystemComponent() : nullptr;
            if (!ASC || ASC->GetOwnerActor() != State || ASC->GetAvatarActor() != Queued.Pawn.Get())
                return;
            ASC->TryActivateAbilityByClass(WeakAbilityClass.Get());
        }));
    return true;
#else
    return false;
#endif
}

bool UDungeonRunAutomationLibrary::QueueEquipmentAction(ADungeonRunController* Controller, FName Action,
                                                        int32 BackpackSlot, FGameplayTag EquipmentSlot)
{
#if WITH_EDITOR
    const FQueuedDungeonPlayer Queued(Controller);
    if (!Queued.IsCurrent())
        return false;
    Controller->GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(
        Controller,
        [Queued, Action, BackpackSlot, EquipmentSlot]()
        {
            if (!Queued.IsCurrent())
                return;
            auto* PC = Queued.Controller.Get();
            auto* State = PC->GetPlayerState<AMyPlayerState>();
            if (!State)
                return;
            if (Action == TEXT("Pickup"))
                PC->ServerRequestPickup();
            else if (Action == TEXT("Open"))
                PC->OpenBackpackUI();
            else if (Action == TEXT("Close"))
                PC->CloseBackpackUI();
            else if (Action == TEXT("Equip"))
                State->GetEquipmentComponent()->RequestEquip(BackpackSlot, EquipmentSlot);
            else if (Action == TEXT("Unequip"))
                State->GetEquipmentComponent()->RequestUnequip(EquipmentSlot);
        }));
    return true;
#else
    return false;
#endif
}

bool UDungeonRunAutomationLibrary::QueueRestoreEquipmentSnapshot(ADungeonRunController* Controller,
                                                                 FBackpackSnapshot Snapshot)
{
#if WITH_EDITOR
    const FQueuedDungeonPlayer Queued(Controller);
    if (!Queued.IsCurrent())
        return false;
    Controller->GetWorldTimerManager().SetTimerForNextTick(
        FTimerDelegate::CreateWeakLambda(Controller,
                                         [Queued, Snapshot]()
                                         {
                                             if (!Queued.IsCurrent())
                                                 return;
                                             auto* State = Queued.Controller->GetPlayerState<AMyPlayerState>();
                                             if (State && State->ImportBackpackSnapshot(Snapshot))
                                                 State->GetBackpackComponent()->OnBackpackStateChanged.Broadcast();
                                         }));
    return true;
#else
    return false;
#endif
}
