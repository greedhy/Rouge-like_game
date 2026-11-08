// 主要链路：地下城控制器读取局内导演状态以控制输入和界面；房间交互携带局标识交给服务器导演校验，退出通过房间子系统执行。
#include "Roguelike/DungeonRunController.h"
#include "Roguelike/DungeonRunDirector.h"
#include "Roguelike/DungeonRunWidget.h"
#include "Roguelike/DungeonPlayerCharacter.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Online/RoomSubsystem.h"
#include "MyGameplayTags.h"
#include "InputCoreTypes.h"
#include "Net/UnrealNetwork.h"

namespace
{
    // 构造局流程封锁玩家输入时使用的标签集合。
    FGameplayTagContainer DungeonRunInputTags()
    {
        const auto& Tags = FMyGameplayTags::Get();
        FGameplayTagContainer Result;
        Result.AddTag(Tags.Player_Block_InputPressed);
        Result.AddTag(Tags.Player_Block_InputHeld);
        Result.AddTag(Tags.Player_Block_InputReleased);
        Result.AddTag(Tags.Player_Block_CursorTrace);
        return Result;
    }
    // 经本地玩家的房间子系统退出当前房间。
    void LeaveLocalDungeon(ADungeonRunController* Controller)
    {
        if (!Controller || !Controller->IsLocalController())
            return;
        if (auto* LocalPlayer = Controller->GetLocalPlayer())
            if (auto* Instance = LocalPlayer->GetGameInstance())
                if (auto* Rooms = Instance->GetSubsystem<URoomSubsystem>())
                    Rooms->LeaveRoom();
    }
} // namespace

// 刷新交互映射与局流程行动门禁，并为本地玩家确保局内界面可见。
void ADungeonRunController::PlayerTick(float DeltaTime)
{
    // The local player may attach after SetupInputComponent (notably on a joining client).
    UpdateInteractionMapping();
    const auto* Director = ADungeonRunDirector::Find(GetWorld());
    const bool bHold = !Director || !Director->CanPlayerAct(this);
    if (IsLocalController() && bHold != bHoldingRunMovement)
    {
        // GenerationController owns a separate counter contribution.
        SetIgnoreMoveInput(bHold);
        bHoldingRunMovement = bHold;
    }
    UpdateActionBlock(bHold);
    Super::PlayerTick(DeltaTime);
    if (IsLocalController() && GetLocalPlayer() && GetNetMode() != NM_DedicatedServer)
    {
        if (!IsValid(RunWidget))
            RunWidget = CreateWidget<UDungeonRunWidget>(this, UDungeonRunWidget::StaticClass());
        if (RunWidget && !RunWidget->IsInViewport())
            RunWidget->AddToPlayerScreen(100);
    }
}

// 回收交互绑定、映射、界面和本控制器贡献的移动及能力门禁。
void ADungeonRunController::EndPlay(EEndPlayReason::Type Reason)
{
    if (auto* Input = InteractionInput.Get())
        Input->RemoveBindingByHandle(InteractionBindingHandle);
    InteractionInput.Reset();
    if (auto* Subsystem = InteractionSubsystem.Get(); Subsystem && InteractionContext)
        Subsystem->RemoveMappingContext(InteractionContext);
    InteractionSubsystem.Reset();
    if (RunWidget)
    {
        RunWidget->RemoveFromParent();
        RunWidget = nullptr;
    }
    if (bHoldingRunMovement)
    {
        SetIgnoreMoveInput(false);
        bHoldingRunMovement = false;
    }
    UpdateActionBlock(false);
    Super::EndPlay(Reason);
}

// 保留父类输入绑定，为交互键创建动作、映射和可回收绑定。
void ADungeonRunController::SetupInputComponent()
{
    if (auto* Previous = InteractionInput.Get())
        Previous->RemoveBindingByHandle(InteractionBindingHandle);
    InteractionInput.Reset();
    Super::SetupInputComponent();
    auto* Input = Cast<UEnhancedInputComponent>(InputComponent);
    if (!Input)
        return;
    if (!InteractionAction)
    {
        InteractionAction = NewObject<UInputAction>(this, TEXT("DungeonInteractionAction"));
        InteractionAction->ValueType = EInputActionValueType::Boolean;
        InteractionAction->bConsumeInput = false;
    }
    if (!InteractionContext)
    {
        InteractionContext = NewObject<UInputMappingContext>(this, TEXT("DungeonInteractionContext"));
        InteractionContext->MapKey(InteractionAction, EKeys::F);
    }
    InteractionBindingHandle =
        Input->BindAction(InteractionAction, ETriggerEvent::Started, this, &ThisClass::InteractNearest).GetHandle();
    InteractionInput = Input;
    UpdateInteractionMapping();
}

// 本地玩家就绪或变化时迁移交互映射，以高于基础映射的优先级注册。
void ADungeonRunController::UpdateInteractionMapping()
{
    if (!InteractionContext)
        return;
    auto* Desired = IsLocalController() && GetLocalPlayer()
                        ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer())
                        : nullptr;
    if (InteractionSubsystem.Get() != Desired)
    {
        if (auto* Previous = InteractionSubsystem.Get())
            Previous->RemoveMappingContext(InteractionContext);
        InteractionSubsystem = Desired;
    }
    // The inherited player mapping uses priority zero; this context owns only the F interaction.
    if (Desired && !Desired->HasMappingContext(InteractionContext))
        Desired->AddMappingContext(InteractionContext, 1);
}
// 注册本类需要复制的状态，同时保留父类复制字段。
void ADungeonRunController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADungeonRunController, RunView);
}
// 为房间交互附带当前局标识并发送服务器请求。
void ADungeonRunController::RequestRoomInteraction(FName RoomId, EDungeonInteraction Action, int32 OfferIndex)
{
    if (IsLocalController() || HasAuthority())
        ServerRoomInteraction(RunView.RunId, RoomId, Action, OfferIndex);
}

// 将交互交给局内导演进行权威校验与执行。
void ADungeonRunController::ServerRoomInteraction_Implementation(FGuid RequestRunId, FName RoomId,
                                                                 EDungeonInteraction Action, int32 OfferIndex)
{
    if (auto* Director = ADungeonRunDirector::Find(GetWorld()))
        Director->HandleInteraction(this, RequestRunId, RoomId, Action, OfferIndex);
}

// 通知所属客户端退出地下城房间。
void ADungeonRunController::ClientReturnFromDungeon_Implementation()
{
    LeaveLocalDungeon(this);
}

// 根据当前房间角色发送宝箱、休息或胜利出口交互。
void ADungeonRunController::InteractNearest()
{
    const auto* Director = ADungeonRunDirector::Find(GetWorld());
    if (!Director || !RunView.bCanInteract || !Director->CanPlayerAct(this))
        return;
    const auto* Room = Director->Rooms.FindByPredicate(
        [this](const FDungeonRoomProgress& Entry)
        {
            return Entry.RoomId == RunView.CurrentRoom;
        });
    if (!Room)
        return;
    switch (Room->Role)
    {
    case EDungeonRoomRole::Treasure:
        RequestRoomInteraction(Room->RoomId, EDungeonInteraction::ClaimTreasure);
        break;
    case EDungeonRoomRole::Rest:
        RequestRoomInteraction(Room->RoomId, EDungeonInteraction::Rest);
        break;
    case EDungeonRoomRole::Boss:
        if (Director->Phase == EDungeonRunPhase::Victory)
            RequestRoomInteraction(Room->RoomId, EDungeonInteraction::Exit);
        break;
    default:
        break; // Nearby merchant stock is shown as three explicit purchase buttons.
    }
}

// 仅在胜利、结束或失败阶段允许本地离开房间。
void ADungeonRunController::ReturnToMenu()
{
    const auto* Director = ADungeonRunDirector::Find(GetWorld());
    if (Director && (Director->Phase == EDungeonRunPhase::Victory || Director->Phase == EDungeonRunPhase::Finished ||
                     Director->Phase == EDungeonRunPhase::Failed))
        LeaveLocalDungeon(this);
}

// 迁移局流程持有的输入标签贡献，解除时只回收自身添加的计数。
void ADungeonRunController::UpdateActionBlock(bool bBlocked)
{
    auto* Desired = bBlocked ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetPawn()) : nullptr;
    if (BlockedASC.Get() == Desired)
        return;
    if (auto* Previous = BlockedASC.Get())
        Previous->RemoveLooseGameplayTags(DungeonRunInputTags());
    BlockedASC = Desired;
    if (Desired)
        Desired->AddLooseGameplayTags(DungeonRunInputTags());
}
