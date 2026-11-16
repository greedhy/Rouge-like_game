// 调用链：开发控制台 Equipment.SpawnUniques → 当前服务器玩家附近生成Soul → 原E拾取/背包/装备事务。
// 命令不会在正常启动时执行，也不会写入关卡或直接授予背包物品。
#if !UE_BUILD_SHIPPING
#include "Equipment/EquipmentSoulPickup.h"
#include "Equipment/UniqueEquipmentGameplayEffect.h"
#include "Backpack/WorldPickupComponent.h"
#include "Backpack/BackpackComponent.h"
#include "Backpack/ItemDefinition.h"
#include "Player/MyPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
    // 只接受带现有目录的权威玩家。校验全部定义后才生成，配置缺失时不会留下半套样例。
    void SpawnUniqueEquipmentSamples(UWorld* World)
    {
        if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_Client)
        {
            UE_LOG(LogTemp, Warning, TEXT("Equipment.SpawnUniques requires a running server/standalone world."));
            return;
        }
        APlayerController* Controller = World->GetFirstPlayerController();
        APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
        auto* State = Controller ? Controller->GetPlayerState<AMyPlayerState>() : nullptr;
        auto* Bag = State ? State->GetBackpackComponent() : nullptr;
        const auto* Catalog = Bag ? Bag->ItemDefinitions.Get() : nullptr;
        if (!Pawn || !Pawn->HasAuthority() || !Catalog)
        {
            UE_LOG(LogTemp, Warning, TEXT("Equipment.SpawnUniques needs a possessed player with an item catalog."));
            return;
        }
        TArray<FPrimaryAssetId> Candidates;
        for (const auto& Item : Catalog->Items)
        {
            Candidates.AddUnique(Item.ItemDefinitionId);
        }
        if (Catalog->EquipmentTable && Catalog->EquipmentTable->GetRowStruct() == FBackpackItemDefEntry::StaticStruct())
        {
            TArray<FBackpackItemDefEntry*> Rows;
            Catalog->EquipmentTable->GetAllRows(TEXT("UniqueEquipmentSamples"), Rows);
            for (const auto* Row : Rows)
            {
                Candidates.AddUnique(Row->ItemDefinitionId);
            }
        }
        TArray<FPrimaryAssetId> Ids;
        for (const FPrimaryAssetId& Id : Candidates)
        {
            // 和生产背包一样由目录决定表行优先级，避免旧内置条目遮蔽新表行。
            const auto* Def = Catalog->FindDefinition(Id);
            const auto* Unique =
                Def ? Cast<UUniqueEquipmentGameplayEffect>(Def->EquipEffect.GetDefaultObject()) : nullptr;
            if (!Unique)
            {
                continue;
            }
            if (!Def->bEquippable || Def->bRandomizedEquipment || Def->bStackable || Def->MaxStack != 1 ||
                !Unique->IsSafeEquipmentEffect(Def->EquipEffectLevel))
            {
                UE_LOG(LogTemp, Warning,
                       TEXT("Unique sample %s is missing/invalid. Run author_unique_equipment.py first."),
                       *Id.ToString());
                return;
            }
            Ids.Add(Id);
        }
        Ids.Sort(
            [](const FPrimaryAssetId& A, const FPrimaryAssetId& B)
            {
                return A.ToString() < B.ToString();
            });
        if (Ids.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("No authored unique equipment is available in the current catalog."));
            return;
        }
        int32 Spawned = 0;
        for (int32 Index = 0; Index < Ids.Num(); ++Index)
        {
            // 每圈最多八件，增加装备定义时无需再维护第二份样例ID清单。
            const FVector Direction =
                Pawn->GetActorForwardVector().RotateAngleAxis((Index % 8) * 45.f, FVector::UpVector);
            FVector Location = Pawn->GetActorLocation() + Direction * (220.f + (Index / 8) * 160.f);
            FHitResult Floor;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(UniqueEquipmentSampleFloor), false, Pawn);
            if (World->LineTraceSingleByObjectType(Floor, Location + FVector(0, 0, 150), Location - FVector(0, 0, 1500),
                                                   FCollisionObjectQueryParams(ECC_WorldStatic), Query))
            {
                Location.Z = Floor.ImpactPoint.Z + 20.f;
            }
            const FTransform Transform(FRotator::ZeroRotator, Location);
            auto* Soul = World->SpawnActorDeferred<AEquipmentSoulPickup>(
                AEquipmentSoulPickup::StaticClass(), Transform, nullptr, nullptr,
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (!Soul)
            {
                continue;
            }
            Soul->Pickup->ItemDefinitions = Bag->ItemDefinitions;
            Soul->Pickup->ConfigurePickup(Ids[Index], 1, true);
            Soul->FinishSpawning(Transform);
            if (IsValid(Soul))
            {
                ++Spawned;
            }
        }
        UE_LOG(LogTemp, Display, TEXT("Equipment.SpawnUniques spawned %d/%d Soul pickups. Press E nearby to collect."),
               Spawned, Ids.Num());
    }

    FAutoConsoleCommandWithWorld SpawnUniquesCommand(
        TEXT("Equipment.SpawnUniques"),
        TEXT("Server only: spawn all authored fixed uniques from the current item catalog as Soul pickups."),
        FConsoleCommandWithWorldDelegate::CreateStatic(&SpawnUniqueEquipmentSamples));
} // namespace
#endif
