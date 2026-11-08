// 玩家输入入口：鼠标扫掠产生混合目标，按键转发能力系统；实体承伤回调将数字批量发送给所属客户端显示。
#include "PureMass/PureMassPlayerController.h"
#include "PureMass/PureMassCombatTarget.h"
#include "PureMass/PureMassSubsystem.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "Input/MyInputComponent.h"
#include "InputActionValue.h"
#include "MyGameplayTags.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UI/Widget/DamageNumberPool.h"
#include "UObject/ConstructorHelpers.h"

// 加载实体伤害数字所需的默认组件蓝图。
APureMassPlayerController::APureMassPlayerController()
{
    static ConstructorHelpers::FClassFinder<UDamageTextComponent> Feedback(
        TEXT("/Game/BluePrints/UI/FloatingText/BP_DamageTextComponent"));
    MassDamageTextComponentClass = Feedback.Class;
}

// 服务端按实体、格挡与暴击合并伤害数字并启动批量发送计时器。
void APureMassPlayerController::QueueMassDamageNumber(int32 EntityId, FVector Position, float Damage, bool bBlocked,
                                                      bool bCritical)
{
    if (!HasAuthority() || EntityId <= 0 || Position.ContainsNaN() || !FMath::IsFinite(Damage) || Damage <= 0.f)
        return;
    auto* Number = PendingDamageNumbers.FindByPredicate(
        [=](const FPureMassDamageNumber& Item)
        {
            return Item.EntityId == EntityId && Item.bBlocked == bBlocked && Item.bCritical == bCritical;
        });
    if (!Number)
    {
        if (PendingDamageNumbers.Num() >= 256)
            return;
        Number = &PendingDamageNumbers.AddDefaulted_GetRef();
        Number->EntityId = EntityId;
        Number->bBlocked = bBlocked;
        Number->bCritical = bCritical;
    }
    Number->Damage = FMath::Min(MAX_flt - Damage, Number->Damage) + Damage;
    Number->Position = Position;
    // DOT and terrain operate every Mass update. Batch before sending or creating any UI.
    if (!GetWorldTimerManager().IsTimerActive(DamageNumberTimer))
        GetWorldTimerManager().SetTimer(DamageNumberTimer, this, &ThisClass::FlushMassDamageNumbers, .1f, false);
}

// 向所属客户端发送积累的伤害数字后清空批次。
void APureMassPlayerController::FlushMassDamageNumbers()
{
    if (!PendingDamageNumbers.IsEmpty())
        ClientShowMassDamageNumbers(PendingDamageNumbers);
    PendingDamageNumbers.Reset();
}

// 所属客户端通过伤害数字池展示收到的批次。
void APureMassPlayerController::ClientShowMassDamageNumbers_Implementation(const TArray<FPureMassDamageNumber>& Numbers)
{
    if (!IsLocalController() || !MassDamageTextComponentClass || GetNetMode() == NM_DedicatedServer)
        return;
    auto* Pool = GetWorld()->GetSubsystem<UDamageNumberPool>();
    for (int32 Index = 0; Index < FMath::Min(Numbers.Num(), 256); ++Index)
    {
        const auto& Number = Numbers[Index];
        Pool->Show(this, MassDamageTextComponentClass, FVector(Number.Position), Number.Damage, Number.bBlocked,
                   Number.bCritical);
    }
}

// 结束生命周期时清理本对象拥有的资源，再调用父类收尾。
void APureMassPlayerController::EndPlay(EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearTimer(DamageNumberTimer);
    PendingDamageNumbers.Reset();
    Super::EndPlay(Reason);
}

// 从当前受控角色取得项目能力系统。
UMyAbilitySystemComponent* APureMassPlayerController::MassASC() const
{
    return Cast<UMyAbilitySystemComponent>(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetPawn()));
}

// 结合背包界面状态与能力标签判定输入是否被阻止。
bool APureMassPlayerController::IsBlocked(FGameplayTag Tag) const
{
    auto* ASC = MassASC();
    return IsBackpackUIOpen() || (ASC && ASC->HasMatchingGameplayTag(Tag));
}

// 绑定移动动作以及技能按下、持续和松开输入。
void APureMassPlayerController::SetupInputComponent()
{
    // Keep the Mass input asset bindings and cursor targeting, sharing the same keyboard-only controls.
    APlayerController::SetupInputComponent();
    auto* Input = CastChecked<UMyInputComponent>(InputComponent);
    if (MassMoveAction)
        Input->BindAction(MassMoveAction, ETriggerEvent::Triggered, this, &ThisClass::MoveMass);
    if (MassInputConfig)
        Input->BindAbilityActions(MassInputConfig, this, &ThisClass::Pressed, &ThisClass::Released, &ThisClass::Held);
}

// 将鼠标反投影为射线，经统一扫掠更新角色与实体悬停目标。
void APureMassPlayerController::UpdateCursor()
{
    HoveredEntity = 0;
    HoveredActor.Reset();
    if (IsBlocked(FMyGameplayTags::Get().Player_Block_CursorTrace))
        return;
    FVector Origin, Direction;
    if (!DeprojectMousePositionToWorld(Origin, Direction))
        return;
    FVector End = Origin + Direction * 100000.f;
    FHitResult WorldHit;
    float Time = 1.f;
    const auto Target = PureMassCombat::Sweep(GetWorld(), GetPawn(), Origin, End, 0.f, Time, WorldHit);
    HoveredEntity = FMath::Max(0, Target.EntityId);
    HoveredActor = Target.Actor;
}

// 本地控制器每帧刷新鼠标目标。
void APureMassPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    if (!IsLocalController())
        return;
    UpdateCursor();
}

// 把二维移动输入转换为控制器朝向下的世界移动。
void APureMassPlayerController::MoveMass(const FInputActionValue& Value)
{
    if (IsBlocked(FMyGameplayTags::Get().Player_Block_InputPressed))
        return;
    if (APawn* ControlledPawn = GetPawn())
    {
        const FVector2D Axis = Value.Get<FVector2D>();
        const FRotationMatrix Rotation(FRotator(0, GetControlRotation().Yaw, 0));
        ControlledPawn->AddMovementInput(Rotation.GetUnitAxis(EAxis::X), Axis.Y);
        ControlledPawn->AddMovementInput(Rotation.GetUnitAxis(EAxis::Y), Axis.X);
    }
}

// 优先处理背包与拾取，再把技能按下事件交给能力系统。
void APureMassPlayerController::Pressed(FGameplayTag Tag)
{
    if (Tag.MatchesTagExact(FMyGameplayTags::Get().InputTag_Backpack))
    {
        ToggleBackpackUI();
        return;
    }
    if (IsBlocked(FMyGameplayTags::Get().Player_Block_InputPressed))
        return;
    if (Tag.MatchesTagExact(FMyGameplayTags::Get().InputTag_Pickup))
    {
        ServerRequestPickup();
        return;
    }
    UpdateCursor();
    if (auto* ASC = MassASC())
        ASC->AbilityInputTagPressed(Tag);
}

// 未被阻止时向能力系统转发持续按住事件。
void APureMassPlayerController::Held(FGameplayTag Tag)
{
    if (IsBlocked(FMyGameplayTags::Get().Player_Block_InputHeld))
        return;
    if (auto* ASC = MassASC())
        ASC->AbilityInputTagHeld(Tag);
}

// 始终转发松开事件，保证引导技能能在界面打开后结束。
void APureMassPlayerController::Released(FGameplayTag Tag)
{
    // Release must still reach GAS when UI opens or a block tag appears during a channel.
    if (auto* ASC = MassASC())
        ASC->AbilityInputTagReleased(Tag);
}
