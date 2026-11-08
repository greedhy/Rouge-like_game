#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Roguelike/DungeonRunTypes.h"
#include "GameplayTagContainer.h"
#include "Backpack/BackpackPersistence.h"
#include "DungeonRunAutomationLibrary.generated.h"

class ADungeonRunController;
class UGameplayAbility;

/** PIE validation bridge. Requests execute on the next game tick, outside editor Python's script guard. */
UCLASS()
class TEST_25D_API UDungeonRunAutomationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category = "Dungeon|Automation")
    static bool QueueRestoreEquipmentSnapshot(ADungeonRunController* Controller, FBackpackSnapshot Snapshot);
    UFUNCTION(BlueprintCallable, Category = "Dungeon|Automation")
    static bool QueueEquipmentAction(ADungeonRunController* Controller, FName Action, int32 BackpackSlot,
                                     FGameplayTag EquipmentSlot);
    /** True only means queued, not accepted by the server. RequestRunId may intentionally be stale. */
    UFUNCTION(BlueprintCallable, Category = "Dungeon|Automation")
    static bool QueueInteraction(ADungeonRunController* Controller, FGuid RequestRunId, FName RoomId,
                                 EDungeonInteraction Action, int32 OfferIndex = -1);

    /** True only means queued, not successfully activated. Existing GAS ownership/cost checks still apply. */
    UFUNCTION(BlueprintCallable, Category = "Dungeon|Automation")
    static bool QueueAbilityActivation(ADungeonRunController* Controller, TSubclassOf<UGameplayAbility> AbilityClass);
};
