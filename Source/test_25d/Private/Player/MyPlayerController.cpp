// 主要链路：本地输入经标签门禁转交能力系统；拾取、使用、交换和丢弃通过服务器请求进入玩家状态背包，光标与界面表现留在本地。
// Fill out your copyright notice in the Description page of Project Settings.

#include "Player/MyPlayerController.h"
#include <EnhancedInputSubsystems.h>

#include "AbilitySystemBlueprintLibrary.h"
#include "EnhancedInputComponent.h"
#include "MyGameplayTags.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NiagaraFunctionLibrary.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "Components/SplineComponent.h"
#include "Input/MyInputComponent.h"
#include "GameFramework/Character.h"
#include "Actor/MagicCircle.h"
#include "Components/DecalComponent.h"
#include "Interface/HighLightInterface.h"
#include "Interface/IEnemyInterface.h"
#include "Backpack/BackpackComponent.h"
#include "Backpack/WorldPickupComponent.h"
#include "Player/MyPlayerState.h"
#include "UI/Widget/MyUserWidget.h"
#include "UI/Widget/BackpackWidget.h"
#include "UI/Widget/EquipmentPanel.h"
#include "Equipment/EquipmentComponent.h"
#include "Equipment/EquipmentSoulPickup.h"
#include "EngineUtils.h"
#include "Components/SphereComponent.h"
#include "UI/ViewModel/MVVM_Backpack.h"
#include "test_25d/test_25d.h"
#include "TimerManager.h"
#include "UI/Widget/DamageNumberPool.h"

// 启用复制并创建导航样条与光标目标初始状态。
AMyPlayerController::AMyPlayerController()
{
    bReplicates = true;
    LastActor = nullptr;
    ThisActor = nullptr;
    Spline = CreateDefaultSubobject<USplineComponent>("Spline");
}

// 本地定期选择最近装备拾取目标，并更新光标高亮和技能指示器。
void AMyPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    if (IsLocalController())
    {
        if (bBackpackUIOpen && BackpackWidget && !BackpackWidget->IsInViewport())
            CloseBackpackUI();
        EquipmentFocusElapsed += DeltaTime;
        if (EquipmentFocusElapsed >= .1f)
        {
            EquipmentFocusElapsed = 0.f;
            FocusedEquipmentPickup.Reset();
            float Best = MAX_flt;
            if (const APawn* LocalPawn = GetPawn())
                for (TActorIterator<AEquipmentSoulPickup> It(GetWorld()); It; ++It)
                {
                    if (!It->PickupRange->IsOverlappingActor(LocalPawn) ||
                        !It->CanPlayerPickup(GetPlayerState<AMyPlayerState>()))
                        continue;
                    const float Distance = FVector::DistSquared(LocalPawn->GetActorLocation(), It->GetActorLocation());
                    if (Distance < Best)
                    {
                        Best = Distance;
                        FocusedEquipmentPickup = *It;
                    }
                }
        }
    }
    CursorTrace();

    UpdateMagicCircleLocation();
}
// 仅本地控制器将伤害数字排入世界飘字池。
void AMyPlayerController::ShowDamageNumber_Implementation(float DamageAccount, ACharacter* TargetCharacter,
                                                          bool bBlockHit, bool bCriticalHit)
{
    if (IsValid(TargetCharacter) && DamageTextComponentClass && IsLocalController())
    {
        GetWorld()->GetSubsystem<UDamageNumberPool>()->QueueActor(this, DamageTextComponentClass, TargetCharacter,
                                                                  DamageAccount, bBlockHit, bCriticalHit);
    }
}

// 服务器从重叠候选中选择拾取物，并交给拾取组件校验执行。
void AMyPlayerController::ServerRequestPickup_Implementation()
{
    if (!HasAuthority())
    {
        return;
    }

    AActor* PickupActor = GetRandomOverlappingInteractPickup();
    if (UWorldPickupComponent* PickupComponent =
            IsValid(PickupActor) ? PickupActor->FindComponentByClass<UWorldPickupComponent>() : nullptr)
    {
        if (PickupComponent->GetPickupMode() != EPickupMode::Interact)
        {
            return;
        }
        APawn* ControlledPawn = GetPawn();
        if (IsValid(ControlledPawn))
        {
            PickupComponent->TryPickup(GetPlayerState<AMyPlayerState>());
        }
        return;
    }
}

// 服务器按角色前方位置请求背包丢弃整组物品。
void AMyPlayerController::ServerRequestDropWholeStack_Implementation(int32 SlotIndex)
{
    if (!HasAuthority())
    {
        return;
    }

    APawn* ControlledPawn = GetPawn();
    AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();
    if (!IsValid(ControlledPawn) || !IsValid(MyPlayerState))
    {
        return;
    }

    const FVector DropLocation = ControlledPawn->GetActorLocation() + ControlledPawn->GetActorForwardVector() * 100.f;
    MyPlayerState->GetBackpackComponent()->DropWholeStack(SlotIndex, DropLocation);
}

// 依据当前开关状态打开或关闭背包。
void AMyPlayerController::ToggleBackpackUI()
{
    if (bBackpackUIOpen)
        CloseBackpackUI();
    else
        OpenBackpackUI();
}

// 创建背包并绑定视图模型与装备面板；首帧布局完成后显示，避免模板闪烁。
void AMyPlayerController::OpenBackpackUI()
{
    if (!IsLocalController() || !BackpackWidgetClass)
        return;
    // TileView creates and binds its entry widgets on the first layout pass.  Keep that
    // initialization frame invisible so the entry template cannot flash on screen.
    if (!IsValid(BackpackWidget))
    {
        BackpackWidget = CreateWidget<UUserWidget>(this, BackpackWidgetClass);
        if (AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>())
        {
            MyPlayerState->GetBackpackComponent()->InventoryVM->OnDropRequested.AddWeakLambda(
                this,
                [this](int32 SlotIndex)
                {
                    ServerRequestDropWholeStack(SlotIndex);
                });
            if (UBackpackWidget* BackpackUI = Cast<UBackpackWidget>(BackpackWidget))
            {
                BackpackUI->SetBackpackViewModel(MyPlayerState->GetBackpackComponent()->InventoryVM);
            }
            else if (UMyUserWidget* WidgetWithController = Cast<UMyUserWidget>(BackpackWidget))
            {
                WidgetWithController->SetWidgetController(MyPlayerState->GetBackpackComponent()->InventoryVM);
            }
        }
    }
    // 等待下一帧布局完成才显示背包，装备面板共用同一个背包视图模型。
    BackpackWidget->SetRenderOpacity(0.f);
    BackpackWidget->AddToViewport();
    if (auto* State = GetPlayerState<AMyPlayerState>())
    {
        auto* Bag = State->GetBackpackComponent();
        Bag->Initialize();
        if (!EquipmentWidget)
            EquipmentWidget = CreateWidget<UEquipmentPanel>(this, UEquipmentPanel::StaticClass());
        EquipmentWidget->InitializeEquipment(State->GetEquipmentComponent(), Bag->InventoryVM, Bag->InventoryModel);
        EquipmentWidget->SetDesiredSizeInViewport(FVector2D(350, 520));
        EquipmentWidget->SetPositionInViewport(FVector2D(28.f, 0.f), false);
        EquipmentWidget->SetAnchorsInViewport(FAnchors(0.f, .5f));
        EquipmentWidget->SetAlignmentInViewport(FVector2D(0.f, .5f));
        EquipmentWidget->AddToViewport(20);
    }
    bBackpackUIOpen = true;
    BackpackWidget->ForceLayoutPrepass();
    GetWorldTimerManager().SetTimerForNextTick(
        FTimerDelegate::CreateWeakLambda(this,
                                         [this]()
                                         {
                                             if (bBackpackUIOpen && IsValid(BackpackWidget))
                                             {
                                                 BackpackWidget->SetRenderOpacity(1.f);
                                             }
                                         }));
    bShowMouseCursor = true;
    FInputModeGameAndUI InputMode;
    InputMode.SetWidgetToFocus(BackpackWidget->TakeWidget());
    InputMode.SetHideCursorDuringCapture(false);
    SetInputMode(InputMode);
}

// 移除背包和装备面板，恢复游戏与界面输入模式。
void AMyPlayerController::CloseBackpackUI()
{
    if (!IsLocalController() || !IsValid(BackpackWidget))
        return;
    BackpackWidget->RemoveFromParent();
    if (EquipmentWidget)
        EquipmentWidget->RemoveFromParent();
    bBackpackUIOpen = false;
    bShowMouseCursor = true;
    FInputModeGameAndUI InputMode;
    InputMode.SetHideCursorDuringCapture(false);
    SetInputMode(InputMode);
}

// 服务器将槽位交换请求交给背包组件，并记录结果。
void AMyPlayerController::ServerRequestSwap_Implementation(int32 SourceSlotIndex, int32 TargetSlotIndex)
{
    if (!HasAuthority())
    {
        UE_LOG(LogTemp, Warning, TEXT("[BackpackDrag] Swap RPC ignored without server authority: %d -> %d."),
               SourceSlotIndex, TargetSlotIndex);
        return;
    }

    if (AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>())
    {
        const bool bSwapped = MyPlayerState->GetBackpackComponent()->SwapSlots(SourceSlotIndex, TargetSlotIndex);
        UE_LOG(LogTemp, Log, TEXT("[BackpackDrag] Server received swap RPC %d -> %d; result=%s."), SourceSlotIndex,
               TargetSlotIndex, bSwapped ? TEXT("true") : TEXT("false"));
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("[BackpackDrag] Server rejected swap RPC %d -> %d: PlayerState unavailable."),
           SourceSlotIndex, TargetSlotIndex);
}

// 服务器使用指定槽位物品，目标为当前受控角色。
void AMyPlayerController::ServerRequestUse_Implementation(int32 SlotIndex)
{
    if (!HasAuthority())
    {
        return;
    }

    if (AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>())
    {
        MyPlayerState->GetBackpackComponent()->TryUseItemForPawn(SlotIndex, GetPawn());
    }
}
// 开始游戏后尝试初始化本地输入。
void AMyPlayerController::BeginPlay()
{
    Super::BeginPlay();
    InitializeLocalInput();
}

// 本地玩家连接就绪后补装输入映射。
void AMyPlayerController::ReceivedPlayer()
{
    Super::ReceivedPlayer();
    InitializeLocalInput();
}

// 切换受控角色时清除能力与光标缓存，再初始化本地输入。
void AMyPlayerController::AcknowledgePossession(APawn* P)
{
    Super::AcknowledgePossession(P);
    MyAbilitySystemComponent = nullptr;
    LastActor = nullptr;
    ThisActor = nullptr;
    Spline->ClearSplinePoints();
    InitializeLocalInput();
}

// 仅为已有本地玩家的控制器安装基础输入映射与鼠标模式。
void AMyPlayerController::InitializeLocalInput()
{
    if (!IsLocalController() || !GetLocalPlayer())
        return;
    check(PlayerContext);
    UEnhancedInputLocalPlayerSubsystem* Subsystem =
        ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
    if (Subsystem)
        Subsystem->AddMappingContext(PlayerContext, 0);
    bShowMouseCursor = true;
    DefaultMouseCursor = EMouseCursor::Default;
    FInputModeGameAndUI InputModeData;
    InputModeData.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    InputModeData.SetHideCursorDuringCapture(false);
    SetInputMode(InputModeData);
}

// 绑定移动动作与能力标签的按下、松开、持续事件。
void AMyPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    UMyInputComponent* MyInputComponent = CastChecked<UMyInputComponent>(InputComponent);

    MyInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMyPlayerController::Move);

    MyInputComponent->BindAbilityActions(InputConfig, this, &ThisClass::AbilityInputTagPressed,
                                         &ThisClass::AbilityInputTagReleased, &ThisClass::AbilityInputTagHeld);
}

// 在背包和状态门禁允许时，将二维输入转换为控制器朝向的移动向量。
void AMyPlayerController::Move(const FInputActionValue& InputActionValue)
{
    if (IsBackpackUIOpen() ||
        (GetASC() && GetASC()->HasMatchingGameplayTag(FMyGameplayTags::Get().Player_Block_InputPressed)))
    {
        return;
    }

    const FVector2D MoveVector = InputActionValue.Get<FVector2D>();
    const FRotator Rotation = GetControlRotation();
    const FRotator YawRotation(0.f, Rotation.Yaw, 0.f);

    const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
    const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

    if (APawn* ControlledPawn = GetPawn<APawn>())
    {
        ControlledPawn->AddMovementInput(ForwardDirection, MoveVector.Y);
        ControlledPawn->AddMovementInput(RightDirection, MoveVector.X);
    }
}

// 根据技能指示器选择射线通道，并维护新旧目标高亮；封锁时清除目标。
void AMyPlayerController::CursorTrace()
{
    if (GetASC() && GetASC()->HasMatchingGameplayTag(FMyGameplayTags::Get().Player_Block_CursorTrace))
    {
        UnHighlightActor(LastActor);
        UnHighlightActor(ThisActor);
        if (IsValid(ThisActor) && ThisActor->Implements<UHighLightInterface>())

            LastActor = nullptr;
        ThisActor = nullptr;
        return;
    }
    const ECollisionChannel TraceChannel = IsValid(MagicCircle) ? ECC_ExcludePlayers : ECC_Visibility;
    GetHitResultUnderCursor(TraceChannel, false, CursorHit);
    if (!CursorHit.bBlockingHit)
        return;

    LastActor = ThisActor;
    if (IsValid(CursorHit.GetActor()) && CursorHit.GetActor()->Implements<UHighLightInterface>())
    {
        ThisActor = CursorHit.GetActor();
    }
    else
    {
        ThisActor = nullptr;
    }

    if (LastActor != ThisActor)
    {
        UnHighlightActor(LastActor);
        HighlightActor(ThisActor);
    }
}

// 优先处理背包与拾取，其余按下事件经状态门禁送入能力系统。
void AMyPlayerController::AbilityInputTagPressed(FGameplayTag InputTag)
{
    if (InputTag.MatchesTagExact(FMyGameplayTags::Get().InputTag_Backpack))
    {
        ToggleBackpackUI();
        return;
    }
    if (IsBackpackUIOpen() ||
        (GetASC() && GetASC()->HasMatchingGameplayTag(FMyGameplayTags::Get().Player_Block_InputPressed)))
        return;
    if (InputTag.MatchesTagExact(FMyGameplayTags::Get().InputTag_Pickup))
    {
        RequestPickupFocusedActor();
        return;
    }
    if (GetASC())
        GetASC()->AbilityInputTagPressed(InputTag);
}

// 向服务器提交拾取请求，具体候选由服务器决定。
void AMyPlayerController::RequestPickupFocusedActor()
{
    ServerRequestPickup();
}

// 按拾取演员累计重叠次数，避免多个碰撞组件产生重复候选。
void AMyPlayerController::RegisterOverlappingInteractPickup(AActor* PickupActor)
{
    if (IsValid(PickupActor))
    {
        ++OverlappingInteractPickups.FindOrAdd(PickupActor);
    }
}

// 递减重叠次数，归零时移除拾取候选。
void AMyPlayerController::UnregisterOverlappingInteractPickup(AActor* PickupActor)
{
    if (int32* OverlapCount = OverlappingInteractPickups.Find(PickupActor))
    {
        if (--(*OverlapCount) <= 0)
        {
            OverlappingInteractPickups.Remove(PickupActor);
        }
    }
}

// 清理失效候选并按距离排序，实际返回最近候选。
AActor* AMyPlayerController::GetRandomOverlappingInteractPickup()
{
    TArray<AActor*> Candidates;
    for (auto It = OverlappingInteractPickups.CreateIterator(); It; ++It)
    {
        if (IsValid(It.Key().Get()) && It.Value() > 0)
        {
            Candidates.Add(It.Key().Get());
        }
        else
        {
            It.RemoveCurrent();
        }
    }

    if (Candidates.IsEmpty())
        return nullptr;
    if (const APawn* ControlledPickupPawn = GetPawn())
        Candidates.Sort(
            [ControlledPickupPawn](const AActor& A, const AActor& B)
            {
                return FVector::DistSquared(A.GetActorLocation(), ControlledPickupPawn->GetActorLocation()) <
                       FVector::DistSquared(B.GetActorLocation(), ControlledPickupPawn->GetActorLocation());
            });
    return Candidates[0];
}

// 读取已登记候选数量。
int32 AMyPlayerController::GetOverlappingInteractPickupCount() const
{
    return OverlappingInteractPickups.Num();
}

// 始终转交松开事件，使已激活技能能在界面或眩晕封锁期间结束。
void AMyPlayerController::AbilityInputTagReleased(FGameplayTag InputTag)
{
    // Always release an active ability, even when UI or a stun blocks new input.
    if (GetASC())
        GetASC()->AbilityInputTagReleased(InputTag);
}

// 持续输入通过背包和状态门禁后交给能力系统。
void AMyPlayerController::AbilityInputTagHeld(FGameplayTag InputTag)
{
    if (IsBackpackUIOpen() ||
        (GetASC() && GetASC()->HasMatchingGameplayTag(FMyGameplayTags::Get().Player_Block_InputHeld)))
        return;
    if (GetASC())
        GetASC()->AbilityInputTagHeld(InputTag);
}

// 根据当前受控角色刷新并返回自定义能力系统。
UMyAbilitySystemComponent* AMyPlayerController::GetASC()
{
    MyAbilitySystemComponent =
        Cast<UMyAbilitySystemComponent>(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetPawn()));
    return MyAbilitySystemComponent;
}

// 将技能范围指示器移动到当前光标命中点。
void AMyPlayerController::UpdateMagicCircleLocation()
{
    if (IsValid(MagicCircle))
    {
        MagicCircle->SetActorLocation(CursorHit.ImpactPoint);
    }
}
// 创建或显示技能范围指示器，使用可选贴花材质。
void AMyPlayerController::ShowMagicCircle(UMaterialInterface* DecalMaterial)
{
    if (!IsValid(MagicCircle))
    {
        MagicCircle = GetWorld()->SpawnActor<AMagicCircle>(MagicCircleClass);
        if (DecalMaterial)
        {
            MagicCircle->MagicCircleDecal->SetMaterial(0, DecalMaterial);
        }
    }
}

// 移除技能范围指示器。
void AMyPlayerController::HideMagicCircle()
{
    if (IsValid(MagicCircle))
    {
        MagicCircle->Destroy();
    }
}

// 通过高亮接口启用有效目标的高亮表现。
void AMyPlayerController::HighlightActor(AActor* InActor)
{
    if (IsValid(InActor) && InActor->Implements<UHighLightInterface>())
    {
        IHighLightInterface::Execute_HighLightActor(InActor);
    }
}

// 通过高亮接口取消有效目标的高亮表现。
void AMyPlayerController::UnHighlightActor(AActor* InActor)
{
    if (IsValid(InActor) && InActor->Implements<UHighLightInterface>())
    {
        IHighLightInterface::Execute_UnHighLightActor(InActor);
    }
}
