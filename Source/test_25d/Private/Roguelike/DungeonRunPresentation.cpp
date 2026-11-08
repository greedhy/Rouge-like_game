#include "Roguelike/DungeonRunDirector.h"
#include "Roguelike/DungeonGenerationRuntime.h"
#include "Roguelike/DungeonRunController.h"
#include "Roguelike/DungeonRunEconomy.h"
#include "Roguelike/DungeonRunProfile.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Backpack/BackpackComponent.h"
#include "Character/EnemyCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Interface/CombatInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Online/RoomSubsystem.h"
#include "Player/MyPlayerState.h"
#include "PureMass/PureMassSubsystem.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TimerManager.h"

namespace
{
    bool IsLiving(const AActor* Actor)
    {
        return IsValid(Actor) && (!Actor->Implements<UCombatInterface>() || !ICombatInterface::Execute_IsDead(Actor));
    }
    FName ShopReceipt(FName RoomId, int32 Index)
    {
        return FName(*FString::Printf(TEXT("%s_Offer_%d"), *RoomId.ToString(), Index));
    }
    bool IsService(EDungeonRoomRole Role, EDungeonRunPhase Phase)
    {
        return Role == EDungeonRoomRole::Treasure || Role == EDungeonRoomRole::Merchant ||
               Role == EDungeonRoomRole::Rest || (Role == EDungeonRoomRole::Boss && Phase == EDungeonRunPhase::Victory);
    }
    TSharedRef<FJsonObject> PositionJson(const FVector& Position)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetNumberField(TEXT("x"), Position.X);
        Result->SetNumberField(TEXT("y"), Position.Y);
        Result->SetNumberField(TEXT("z"), Position.Z);
        return Result;
    }
    void SetCoordinates(const TSharedRef<FJsonObject>& Object, const FVector& Position)
    {
        Object->SetNumberField(TEXT("x"), Position.X);
        Object->SetNumberField(TEXT("y"), Position.Y);
        Object->SetNumberField(TEXT("z"), Position.Z);
    }
} // namespace

void ADungeonRunDirector::BuildLocalPresentation()
{
    const auto* G = GetGeneration();
    if (bLocalBuilt || !G || G->Phase != EDungeonGenerationPhase::Ready)
        return;
    auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!Cube)
        return;
    for (const auto& Edge : G->Manifest.Layout.Connections)
    {
        const FVector From = GetRoomCenter(Edge.From), To = GetRoomCenter(Edge.To);
        auto* Door = NewObject<UStaticMeshComponent>(this);
        Door->SetupAttachment(RootComponent);
        Door->SetMobility(EComponentMobility::Movable);
        Door->SetStaticMesh(Cube);
        Door->SetCollisionProfileName(TEXT("BlockAll"));
        Door->SetCanEverAffectNavigation(true);
        Door->SetRelativeLocation((From + To) * .5f + FVector(0, 0, 140));
        Door->SetRelativeScale3D(FMath::Abs(To.X - From.X) > FMath::Abs(To.Y - From.Y) ? FVector(1, 8, 2.8f)
                                                                                       : FVector(8, 1, 2.8f));
        Door->RegisterComponent();
        GameplayDoors.Add(Door);
    }
    Markers.SetNum(Rooms.Num());
    for (int32 Index = 0; Index < Rooms.Num(); ++Index)
    {
        FString Label;
        FColor Color = FColor::White;
        switch (Rooms[Index].Role)
        {
        case EDungeonRoomRole::Treasure:
            Label = TEXT("CHEST [F]");
            Color = FColor(255, 215, 70);
            break;
        case EDungeonRoomRole::Merchant:
            Label = TEXT("MERCHANT");
            Color = FColor(80, 210, 255);
            break;
        case EDungeonRoomRole::Rest:
            Label = TEXT("REST [F]");
            Color = FColor(100, 255, 150);
            break;
        case EDungeonRoomRole::Boss:
            Label = TEXT("EXIT [F]");
            Color = FColor(220, 130, 255);
            break;
        default:
            continue;
        }
        auto* Marker = NewObject<UTextRenderComponent>(this);
        Marker->SetupAttachment(RootComponent);
        Marker->SetRelativeLocation(GetRoomCenter(Index) + FVector(0, 0, 160));
        Marker->SetRelativeRotation(FRotator(60, 0, 0));
        Marker->SetHorizontalAlignment(EHTA_Center);
        Marker->SetVerticalAlignment(EVRTA_TextCenter);
        Marker->SetWorldSize(58.f);
        Marker->SetText(FText::FromString(Label));
        Marker->SetTextRenderColor(Color);
        Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Marker->SetCanEverAffectNavigation(false);
        Marker->SetVisibility(false);
        Marker->RegisterComponent();
        Markers[Index] = Marker;
    }
    if (auto* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
    {
        PulseMesh = NewObject<UStaticMeshComponent>(this);
        PulseMesh->SetupAttachment(RootComponent);
        PulseMesh->SetMobility(EComponentMobility::Movable);
        PulseMesh->SetStaticMesh(Cylinder);
        PulseMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        PulseMesh->SetCanEverAffectNavigation(false);
        PulseMesh->SetCastShadow(false);
        PulseMesh->SetVisibility(false);
        PulseMesh->RegisterComponent();
        if (auto* Material = PulseMesh->CreateDynamicMaterialInstance(0))
            Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, .08f, .02f));
    }
    bLocalBuilt = true;
    ApplyDoorPresentation();
}

void ADungeonRunDirector::ApplyDoorPresentation()
{
    for (int32 Index = 0; Index < GameplayDoors.Num(); ++Index)
    {
        auto* Door = GameplayDoors[Index].Get();
        if (!IsValid(Door))
            continue;
        const bool bClosed = !OpenDoors.IsValidIndex(Index) || !OpenDoors[Index];
        const auto Collision = bClosed ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision;
        if (Door->GetCollisionEnabled() != Collision)
            Door->SetCollisionEnabled(Collision);
        if (Door->IsVisible() != bClosed)
            Door->SetVisibility(bClosed);
    }
    for (int32 Index = 0; Index < Markers.Num(); ++Index)
        if (auto* Marker = Markers[Index].Get())
        {
            const bool bVisible = Rooms.IsValidIndex(Index) && Rooms[Index].bVisited &&
                                  (Rooms[Index].Role != EDungeonRoomRole::Boss || Phase == EDungeonRunPhase::Victory);
            if (Marker->IsVisible() != bVisible)
                Marker->SetVisibility(bVisible);
        }
    if (PulseMesh)
    {
        const bool bVisible = bPulseWarning && Phase == EDungeonRunPhase::Encounter;
        PulseMesh->SetVisibility(bVisible);
        if (bVisible)
        {
            const float Radius = Profile ? Profile->BossPulseRadius : 550.f;
            PulseMesh->SetWorldLocation(PulseCenter + FVector(0, 0, 4));
            PulseMesh->SetWorldScale3D(FVector(Radius / 50.f, Radius / 50.f, .04f));
        }
    }
}

void ADungeonRunDirector::UpdatePlayerViews()
{
    if (!HasAuthority() || !GetWorld())
        return;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        auto* PC = Cast<ADungeonRunController>(It->Get());
        if (!PC)
            continue;
        const auto* State = PC->GetPlayerState<AMyPlayerState>();
        if (!State)
            continue;
        FDungeonPlayerRunView View;
        View.RunId = RunId;
        View.Message = PC->RunView.Message;
        const int32 PlayerId = State->GetPlayerId();
        View.Coins = Economy ? Economy->GetBalance(PlayerId) : 0;
        const int32 Index = PC->GetPawn() ? GetRoomIndex(PC->GetPawn()->GetActorLocation()) : INDEX_NONE;
        View.Purchased.Init(false, 3);
        if (Rooms.IsValidIndex(Index))
        {
            const auto& Room = Rooms[Index];
            View.CurrentRoom = Room.RoomId;
            View.bCanInteract =
                CanPlayerAct(PC) && Phase != EDungeonRunPhase::Encounter && Room.bVisited &&
                IsService(Room.Role, Phase) &&
                FVector::DistSquared(PC->GetPawn()->GetActorLocation(), GetRoomCenter(Index)) <= FMath::Square(300.f);
            if (Economy)
            {
                if (Room.Role == EDungeonRoomRole::Treasure)
                    View.ChestRemaining = Economy->HasChestRecord(PlayerId, Room.RoomId)
                                              ? Economy->GetChestRemaining(PlayerId, Room.RoomId)
                                              : ChestQuantities.FindRef(Index);
                View.bRested = Room.Role == EDungeonRoomRole::Rest && Economy->HasRested(PlayerId, Room.RoomId);
                if (Room.Role == EDungeonRoomRole::Merchant)
                    for (int32 Offer = 0; Offer < 3; ++Offer)
                        View.Purchased[Offer] = Economy->IsPurchased(PlayerId, ShopReceipt(Room.RoomId, Offer));
            }
        }
        PC->RunView = MoveTemp(View);
        PC->ForceNetUpdate();
    }
}

void ADungeonRunDirector::HandleInteraction(ADungeonRunController* Controller, FGuid RequestRun, FName RoomId,
                                            EDungeonInteraction Action, int32 OfferIndex)
{
    if (!HasAuthority() || !IsValid(Controller) || Controller->GetWorld() != GetWorld())
        return;
    auto* State = Controller->GetPlayerState<AMyPlayerState>();
    APawn* Pawn = Controller->GetPawn();
    const auto Respond = [this, Controller](const FString& Message)
    {
        Controller->RunView.Message = Message;
        UpdatePlayerViews();
        Controller->ForceNetUpdate();
    };
    if (!IsValid(State) || State->GetPlayerController() != Controller || !IsValid(Pawn) ||
        Pawn->GetController() != Controller || Pawn->GetPlayerState<AMyPlayerState>() != State || !IsLiving(Pawn))
    {
        Respond(TEXT("玩家当前无法交互。"));
        return;
    }
    if (!RequestRun.IsValid() || RequestRun != RunId || !CanPlayerAct(Controller) ||
        Phase == EDungeonRunPhase::Encounter || !Economy || !Profile)
    {
        Respond(TEXT("当前无法进行房间交互。"));
        return;
    }
    const int32 Index = GetRoomIndex(Pawn->GetActorLocation());
    if (!Rooms.IsValidIndex(Index) || Rooms[Index].RoomId != RoomId || !Rooms[Index].bVisited ||
        Controller->RunView.CurrentRoom != RoomId ||
        FVector::DistSquared(Pawn->GetActorLocation(), GetRoomCenter(Index)) > FMath::Square(300.f))
    {
        Respond(TEXT("请靠近当前房间中央的服务点。"));
        return;
    }
    const auto& Room = Rooms[Index];
    const int32 PlayerId = State->GetPlayerId();
    FDungeonTransactionResult Result;
    switch (Action)
    {
    case EDungeonInteraction::ClaimTreasure:
        if (Room.Role != EDungeonRoomRole::Treasure || !ChestItems.Contains(Index) || !ChestQuantities.Contains(Index))
            break;
        Result = Economy->ClaimChest(PlayerId, RoomId, State->GetBackpackComponent(), ChestItems[Index],
                                     ChestQuantities[Index]);
        if (Result.bSucceeded)
        {
            Respond(FString::Printf(TEXT("领取 %d 件物品；待领取 %d 件。"), Result.Delivered, Result.Remaining));
            return;
        }
        break;
    case EDungeonInteraction::Purchase:
        if (Room.Role != EDungeonRoomRole::Merchant || OfferIndex < 0 || OfferIndex >= 3 ||
            !Profile->Offers.IsValidIndex(OfferIndex))
            break;
        Result = Economy->Purchase(PlayerId, ShopReceipt(RoomId, OfferIndex), State->GetBackpackComponent(),
                                   Profile->Offers[OfferIndex].ItemId, Profile->Offers[OfferIndex].Price);
        if (Result.bSucceeded)
        {
            Respond(TEXT("购买成功，物品已放入背包。"));
            return;
        }
        break;
    case EDungeonInteraction::Rest:
        if (Room.Role != EDungeonRoomRole::Rest)
            break;
        Result = Economy->Rest(PlayerId, RoomId, State->GetAbilitySystemComponent());
        if (Result.bSucceeded)
        {
            Respond(TEXT("休整完成，生命和法力已恢复。"));
            return;
        }
        break;
    case EDungeonInteraction::Exit:
        if (Room.Role == EDungeonRoomRole::Boss && Phase == EDungeonRunPhase::Victory &&
            Room.Phase == EDungeonRoomPhase::Cleared)
        {
            Respond(TEXT("本层完成，队伍正在返回菜单。"));
            ReturnPartyToMenu();
            return;
        }
        break;
    default:
        break;
    }
    FString Message = TEXT("该房间不支持此操作。");
    if (Result.Error == TEXT("InventoryFull"))
        Message = TEXT("背包已满，请整理后重试。未扣款，宝箱奖励保留。");
    else if (Result.Error == TEXT("InsufficientFunds"))
        Message = TEXT("局内金币不足。");
    else if (Result.Error == TEXT("AlreadyPurchased"))
        Message = TEXT("这项商品本局已购买。");
    else if (Result.Error == TEXT("AlreadyClaimed"))
        Message = TEXT("该宝箱奖励已领取。");
    else if (Result.Error == TEXT("AlreadyRested"))
        Message = TEXT("本局已使用此处的休整机会。");
    else if (Result.Error == TEXT("AlreadyFull"))
        Message = TEXT("生命和法力已满，休整机会仍保留。");
    else if (!Result.Error.IsEmpty())
        Message = TEXT("交互未完成，请稍后重试。");
    Respond(Message);
}

void ADungeonRunDirector::ReturnPartyToMenu()
{
    if (!HasAuthority() || Phase == EDungeonRunPhase::Finished || GetWorldTimerManager().IsTimerActive(ReturnTimer))
        return;
    Phase = EDungeonRunPhase::Finished;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        if (auto* PC = Cast<ADungeonRunController>(It->Get()))
        {
            PC->RunView.Message = TEXT("本层完成，正在返回菜单。");
            if (!PC->IsLocalController())
                PC->ClientReturnFromDungeon();
        }
    UpdatePlayerViews();
    ForceNetUpdate();
    GetWorldTimerManager().SetTimer(
        ReturnTimer,
        FTimerDelegate::CreateWeakLambda(this,
                                         [this]()
                                         {
                                             if (auto* Instance = GetGameInstance())
                                                 if (auto* RoomService = Instance->GetSubsystem<URoomSubsystem>())
                                                     RoomService->LeaveRoom();
                                         }),
        .35f, false);
}

FString ADungeonRunDirector::GetRunReport() const
{
    auto Root = MakeShared<FJsonObject>();
    Root->SetBoolField(TEXT("authority"), HasAuthority());
    Root->SetStringField(TEXT("run_id"), RunId.ToString(EGuidFormats::DigitsWithHyphens));
    Root->SetStringField(TEXT("phase"), StaticEnum<EDungeonRunPhase>()->GetNameStringByValue(int64(Phase)));
    Root->SetStringField(TEXT("error"), FailureReason);
    Root->SetNumberField(TEXT("pending_enemy_spawns"), SpawnQueue.Num());
    Root->SetNumberField(TEXT("last_spawn_ms"), LastSpawnMs);
    Root->SetNumberField(TEXT("last_spawn_count"), LastSpawnCount);
    Root->SetObjectField(TEXT("performance"), MakePerformanceReport());
    Root->SetNumberField(TEXT("active_room"), ActiveRoom);
    Root->SetBoolField(TEXT("boss_pulse_warning"), bPulseWarning);
    Root->SetBoolField(TEXT("boss_summoned"), bBossSummoned);
    int32 Gathering = INDEX_NONE;
    for (int32 Index = 0; Index < Rooms.Num(); ++Index)
        if (Rooms[Index].Phase == EDungeonRoomPhase::Gathering)
            Gathering = Index;
    Root->SetNumberField(TEXT("gathering_room"), Gathering);
    TArray<TSharedPtr<FJsonValue>> RoomValues, Connections, Players, Targets;
    const auto* G = GetGeneration();
    for (int32 Index = 0; Index < Rooms.Num(); ++Index)
    {
        const auto& Room = Rooms[Index];
        auto Object = MakeShared<FJsonObject>();
        Object->SetNumberField(TEXT("index"), Index);
        Object->SetStringField(TEXT("id"), Room.RoomId.ToString());
        Object->SetStringField(TEXT("role"), StaticEnum<EDungeonRoomRole>()->GetNameStringByValue(int64(Room.Role)));
        Object->SetStringField(TEXT("phase"), StaticEnum<EDungeonRoomPhase>()->GetNameStringByValue(int64(Room.Phase)));
        Object->SetBoolField(TEXT("visited"), Room.bVisited);
        Object->SetNumberField(TEXT("wave"), Room.Wave);
        Object->SetNumberField(TEXT("pending_waves"), Room.PendingWaves);
        Object->SetNumberField(TEXT("remaining_required"), Room.RemainingRequired);
        SetCoordinates(Object, GetRoomCenter(Index));
        RoomValues.Add(MakeShared<FJsonValueObject>(Object));
    }
    if (G)
        for (int32 Index = 0; Index < G->Manifest.Layout.Connections.Num(); ++Index)
        {
            const auto& Edge = G->Manifest.Layout.Connections[Index];
            auto Object = MakeShared<FJsonObject>();
            Object->SetNumberField(TEXT("from"), Edge.From);
            Object->SetNumberField(TEXT("to"), Edge.To);
            Object->SetBoolField(TEXT("open"), OpenDoors.IsValidIndex(Index) && OpenDoors[Index]);
            Connections.Add(MakeShared<FJsonValueObject>(Object));
        }
    TArray<ADungeonRunController*> Controllers;
    if (GetWorld())
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
            if (auto* PC = Cast<ADungeonRunController>(It->Get()); PC && PC->PlayerState)
                Controllers.Add(PC);
    Controllers.Sort(
        [](const ADungeonRunController& A, const ADungeonRunController& B)
        {
            return A.PlayerState->GetPlayerId() < B.PlayerState->GetPlayerId();
        });
    for (const auto* PC : Controllers)
    {
        const auto* State = PC->GetPlayerState<AMyPlayerState>();
        if (!State)
            continue;
        const auto& View = PC->RunView;
        auto Object = MakeShared<FJsonObject>();
        Object->SetNumberField(TEXT("id"), State->GetPlayerId());
        Object->SetStringField(TEXT("controller"), PC->GetPathName());
        Object->SetBoolField(TEXT("local"), PC->IsLocalController());
        Object->SetBoolField(TEXT("alive"), IsLiving(PC->GetPawn()));
        Object->SetObjectField(TEXT("pos"),
                               PositionJson(PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : FVector::ZeroVector));
        Object->SetStringField(TEXT("room_id"), View.CurrentRoom.ToString());
        Object->SetNumberField(TEXT("room_index"),
                               PC->GetPawn() ? GetRoomIndex(PC->GetPawn()->GetActorLocation()) : INDEX_NONE);
        Object->SetNumberField(TEXT("coins"), View.Coins);
        Object->SetStringField(TEXT("message"), View.Message);
        Object->SetNumberField(TEXT("chest_remaining"), View.ChestRemaining);
        Object->SetBoolField(TEXT("rested"), View.bRested);
        Object->SetBoolField(TEXT("can_interact"), View.bCanInteract);
        TArray<TSharedPtr<FJsonValue>> Purchased;
        for (bool bPurchased : View.Purchased)
            Purchased.Add(MakeShared<FJsonValueBoolean>(bPurchased));
        Object->SetArrayField(TEXT("purchased"), Purchased);
        if (const auto* ASC = State->GetAbilitySystemComponent())
        {
            Object->SetNumberField(TEXT("health"), ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()));
            Object->SetNumberField(TEXT("max_health"),
                                   ASC->GetNumericAttribute(UMyAttributeSet::GetMaxHealthAttribute()));
            Object->SetNumberField(TEXT("mana"), ASC->GetNumericAttribute(UMyAttributeSet::GetManaAttribute()));
            Object->SetNumberField(TEXT("max_mana"), ASC->GetNumericAttribute(UMyAttributeSet::GetMaxManaAttribute()));
        }
        if (const auto* Bag = State->GetBackpackComponent())
        {
            auto Backpack = MakeShared<FJsonObject>();
            auto Counts = MakeShared<FJsonObject>();
            Backpack->SetNumberField(TEXT("capacity"), Bag->GetCapacity());
            Backpack->SetNumberField(TEXT("revision"), Bag->GetRevision());
            TArray<TSharedPtr<FJsonValue>> Slots;
            TMap<FString, int32> Totals;
            const auto& Items = Bag->GetReplicatedSlots().Items;
            for (int32 Index = 0; Index < Items.Num(); ++Index)
            {
                const auto& Item = Items[Index];
                if (Item.IsEmpty())
                    continue;
                auto Slot = MakeShared<FJsonObject>();
                const FString Id = Item.ItemDefinitionId.ToString();
                Slot->SetNumberField(TEXT("index"), Index);
                Slot->SetStringField(TEXT("item_id"), Id);
                Slot->SetNumberField(TEXT("quantity"), Item.Quantity);
                Slots.Add(MakeShared<FJsonValueObject>(Slot));
                Totals.FindOrAdd(Id) += Item.Quantity;
            }
            TArray<FString> Keys;
            Totals.GetKeys(Keys);
            Keys.Sort();
            for (const auto& Id : Keys)
                Counts->SetNumberField(Id, Totals[Id]);
            Backpack->SetObjectField(TEXT("counts"), Counts);
            Backpack->SetArrayField(TEXT("slots"), Slots);
            Object->SetObjectField(TEXT("backpack"), Backpack);
        }
        Players.Add(MakeShared<FJsonValueObject>(Object));
    }
    if (Mass)
    {
        TArray<int32> Ids;
        MassTargets.GetKeys(Ids);
        Ids.Sort();
        for (int32 Id : Ids)
        {
            FPureMassView View;
            if (!Mass->Resolve(Id, View))
                continue;
            auto Object = MakeShared<FJsonObject>();
            Object->SetStringField(TEXT("kind"), TEXT("mass"));
            Object->SetNumberField(TEXT("id"), Id);
            Object->SetStringField(TEXT("path"), FString::Printf(TEXT("Mass:%d"), Id));
            Object->SetStringField(TEXT("encounter_id"), MassTargets.FindRef(Id).ToString());
            Object->SetNumberField(TEXT("health"), View.Health);
            Object->SetNumberField(TEXT("max_health"), View.MaxHealth);
            Object->SetBoolField(TEXT("required"), Model.Encounter.Living.FindRef(MassTargets.FindRef(Id)));
            SetCoordinates(Object, View.Location);
            Targets.Add(MakeShared<FJsonValueObject>(Object));
        }
    }
    TArray<AActor*> Actors;
    for (const auto& Pair : ActorTargets)
        if (auto* Actor = Pair.Key.Get())
            Actors.Add(Actor);
    Actors.Sort(
        [](const AActor& A, const AActor& B)
        {
            return A.GetPathName() < B.GetPathName();
        });
    for (auto* Actor : Actors)
    {
        const auto* Enemy = Cast<AEnemyCharacter>(Actor);
        if (!Enemy)
            continue;
        const FName TargetId = ActorTargets.FindRef(Actor);
        auto Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("kind"), TEXT("actor"));
        Object->SetStringField(TEXT("id"), TargetId.ToString());
        Object->SetStringField(TEXT("class"), Enemy->GetClass()->GetPathName());
        FName Archetype = NAME_None;
        if (Profile && Actor != Boss)
            for (const auto& Entry : Profile->NormalRoster)
                if (Entry.EnemyClass.Get() == Enemy->GetClass())
                {
                    Archetype = Entry.Id;
                    break;
                }
        Object->SetStringField(TEXT("archetype"), Archetype.ToString());
        Object->SetNumberField(TEXT("pool_lease"), Enemy->GetDungeonPoolLease());
        Object->SetStringField(TEXT("path"), Actor->GetPathName());
        Object->SetBoolField(TEXT("required"), Model.Encounter.Living.FindRef(TargetId));
        if (const auto* ASC = Enemy->GetAbilitySystemComponent())
        {
            Object->SetNumberField(TEXT("health"), ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute()));
            Object->SetNumberField(TEXT("max_health"),
                                   ASC->GetNumericAttribute(UMyAttributeSet::GetMaxHealthAttribute()));
        }
        SetCoordinates(Object, Actor->GetActorLocation());
        Targets.Add(MakeShared<FJsonValueObject>(Object));
    }
    Root->SetArrayField(TEXT("rooms"), RoomValues);
    Root->SetArrayField(TEXT("connections"), Connections);
    Root->SetArrayField(TEXT("players"), Players);
    Root->SetArrayField(TEXT("targets"), Targets);
    FString Result;
    auto Writer = TJsonWriterFactory<>::Create(&Result);
    FJsonSerializer::Serialize(Root, Writer);
    return Result;
}
