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
#include "UI/HUD/MyHUD.h"
#include "Equipment/EquipmentComponent.h"
#include "Equipment/EquipmentSoulPickup.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerInput.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "Components/SphereComponent.h"
#include "UI/ViewModel/MVVM_Backpack.h"
#include "test_25d/test_25d.h"
#include "TimerManager.h"
#include "UI/Widget/DamageNumberPool.h"

namespace
{
    bool IsMenuMouseButton(const FKey& Key)
    {
        return Key == EKeys::LeftMouseButton || Key == EKeys::RightMouseButton;
    }

    // 控件点击可以取得 Slate 焦点。仅当焦点在本玩家的菜单内时，把键盘事件送回游戏视口。
    class FGameplayMenuKeyboardRouting final : public IInputProcessor
    {
    public:
        FGameplayMenuKeyboardRouting(AMyPlayerController& InOwner,
                                     const TArray<TWeakObjectPtr<UUserWidget>>& InMenus)
            : Owner(&InOwner), Menus(InMenus)
        {
        }

        virtual void Tick(float, FSlateApplication&, TSharedRef<ICursor>) override
        {
        }

        virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
        {
            return Route(Event, Event.IsRepeat() ? IE_Repeat : IE_Pressed);
        }

        virtual bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& Event) override
        {
            return Route(Event, IE_Released);
        }

    private:
        bool Route(const FKeyEvent& Event, EInputEvent Type) const
        {
            const FKey Key = Event.GetKey();
            if (!Owner.IsValid() || Key.IsMouseButton() || Key.IsGamepadKey() || Key.IsTouch())
            {
                return false;
            }

            ULocalPlayer* Player = Owner->GetLocalPlayer();
            UGameViewportClient* Viewport = Player ? Player->ViewportClient.Get() : nullptr;
            const TSharedPtr<FSlateUser> User = Player ? Player->GetSlateUser() : nullptr;
            if (!User || User->GetUserIndex() != Event.GetUserIndex() || !Viewport || !Viewport->Viewport)
            {
                return false;
            }

            const FWeakWidgetPath& Focus = User->GetWeakFocusPath();
            for (const TWeakObjectPtr<UUserWidget>& Entry : Menus)
            {
                const UUserWidget* Menu = Entry.Get();
                if (Menu && Menu->IsInViewport() && Menu->IsVisible() &&
                    Focus.ContainsWidget(Menu->GetCachedWidget().Get()))
                {
                    Viewport->InputKey(FInputKeyEventArgs(Viewport->Viewport, Event.GetInputDeviceId(), Key, Type,
                                                         Event.GetEventTimestamp()));
                    return true;
                }
            }
            return false;
        }

        TWeakObjectPtr<AMyPlayerController> Owner;
        const TArray<TWeakObjectPtr<UUserWidget>>& Menus;
    };
}

// 只拦 Slate 未消费的左右键按下。已进入 PlayerInput 的按键仍允许松开。
bool AMyPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
    if (IsMenuMouseButton(Params.Key) && Params.Event != IE_Released && IsGameplayMenuVisible())
    {
        return true;
    }
    return Super::InputKey(Params);
}

bool AMyPlayerController::IsGameplayMenuVisible() const
{
    return bBackpackUIOpen || GameplayMenus.ContainsByPredicate(
        [](const TWeakObjectPtr<UUserWidget>& Entry)
        {
            const UUserWidget* Menu = Entry.Get();
            return Menu && Menu->IsInViewport() && Menu->IsVisible();
        });
}

bool AMyPlayerController::IsMenuMouseAbility(FGameplayTag InputTag) const
{
    const auto& InputTags = FMyGameplayTags::Get();
    return IsGameplayMenuVisible() && (InputTag == InputTags.InputTag_LMB || InputTag == InputTags.InputTag_RMB);
}

// 注册控件范围。打开菜单时释放游戏侧已经按住的鼠标，避免 UMG 吃掉松开事件后技能卡住。
void AMyPlayerController::RegisterGameplayMenu(UUserWidget* Menu)
{
    if (!IsLocalController() || !IsValid(Menu) || GameplayMenus.Contains(Menu))
    {
        return;
    }
    const bool bFirstMenu = GameplayMenus.IsEmpty();
    GameplayMenus.Add(Menu);
    if (!MenuKeyboardRouting && FSlateApplication::IsInitialized())
    {
        MenuKeyboardRouting = MakeShared<FGameplayMenuKeyboardRouting>(*this, GameplayMenus);
        FSlateApplication::Get().RegisterInputPreProcessor(MenuKeyboardRouting);
    }
    if (bFirstMenu)
    {
        ReleaseGameplayMouseButtons();
        ApplyGameplayInputMode();
        if (ULocalPlayer* LocalPlayer = GetLocalPlayer(); LocalPlayer && FSlateApplication::IsInitialized())
        {
            if (const TSharedPtr<FSlateUser> User = LocalPlayer->GetSlateUser())
            {
                FSlateApplication::Get().SetUserFocusToGameViewport(User->GetUserIndex());
            }
        }
    }
}

void AMyPlayerController::UnregisterGameplayMenu(UUserWidget* Menu)
{
    const bool bWasRegistered = GameplayMenus.Contains(Menu);
    GameplayMenus.RemoveAll(
        [Menu](const TWeakObjectPtr<UUserWidget>& Entry)
        {
            return !Entry.IsValid() || Entry.Get() == Menu;
        });
    if (GameplayMenus.IsEmpty())
    {
        RemoveMenuKeyboardRouting();
    }
    if (bWasRegistered && IsValid(Menu) && FSlateApplication::IsInitialized())
    {
        if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
        {
            const TSharedPtr<FSlateUser> User = LocalPlayer->GetSlateUser();
            if (User && User->GetWeakFocusPath().ContainsWidget(Menu->GetCachedWidget().Get()))
            {
                FSlateApplication::Get().SetUserFocusToGameViewport(User->GetUserIndex());
            }
        }
    }
}

void AMyPlayerController::RemoveMenuKeyboardRouting()
{
    if (MenuKeyboardRouting && FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().UnregisterInputPreProcessor(MenuKeyboardRouting);
    }
    MenuKeyboardRouting.Reset();
}

void AMyPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (ObservedBackpack.IsValid())
    {
        ObservedBackpack->OnSlotClicked.Remove(BackpackClickHandle);
    }
    RemoveMenuKeyboardRouting();
    GameplayMenus.Reset();
    Super::EndPlay(EndPlayReason);
}

void AMyPlayerController::ReleaseGameplayMouseButtons()
{
    if (PlayerInput)
    {
        const auto Device = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(GetPlatformUserId());
        for (const FKey& Key : {EKeys::LeftMouseButton, EKeys::RightMouseButton})
        {
            if (IsInputKeyDown(Key))
            {
                PlayerInput->InputKey(FInputKeyEventArgs(nullptr, Device, Key, IE_Released, FPlatformTime::Cycles64()));
            }
        }
    }
}

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

// 创建背包并绑定视图模型；首帧布局完成后显示，避免模板闪烁。
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
    // 等待下一帧布局完成才显示背包，人物装备页共用同一个背包视图模型。
    BackpackWidget->SetRenderOpacity(0.f);
    BackpackWidget->AddToViewport();
    if (auto* State = GetPlayerState<AMyPlayerState>())
    {
        auto* Bag = State->GetBackpackComponent();
        Bag->Initialize();
        if (ObservedBackpack.Get() != Bag->InventoryVM)
        {
            if (ObservedBackpack.IsValid())
            {
                ObservedBackpack->OnSlotClicked.Remove(BackpackClickHandle);
            }
            ObservedBackpack = Bag->InventoryVM;
            BackpackClickHandle = Bag->InventoryVM->OnSlotClicked.AddUObject(this, &ThisClass::ShowSelectedBackpackEquipment);
        }
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
    RegisterGameplayMenu(BackpackWidget);
}

// 只有主动点选可装备物品时才切换到人物窗口，快照更新不会打断当前页面。
void AMyPlayerController::ShowSelectedBackpackEquipment()
{
    AMyPlayerState* State = GetPlayerState<AMyPlayerState>();
    AMyHUD* HUD = GetHUD<AMyHUD>();
    if (!bBackpackUIOpen || !State || !HUD || !ObservedBackpack.IsValid())
    {
        return;
    }
    const int32 Index = ObservedBackpack->GetCurrentSelectedIndex();
    const UInventoryModel* Inventory = State->GetBackpackComponent()->InventoryModel;
    for (const FGameplayTag& Slot : State->GetEquipmentComponent()->AvailableSlots)
    {
        if (UEquipmentPanel::CanEquipToSlot(Inventory, Index, Slot))
        {
            HUD->ShowCharacterMenu(1);
            return;
        }
    }
}

// 只解除背包输入注册，人物窗口可独立保持打开。
void AMyPlayerController::CloseBackpackUI()
{
    if (!IsLocalController() || !IsValid(BackpackWidget))
        return;
    UnregisterGameplayMenu(BackpackWidget);
    BackpackWidget->RemoveFromParent();
    bBackpackUIOpen = false;
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
    ApplyGameplayInputMode();
}

// 光标可操作 UMG，未被 UI 消费的事件仍进入游戏输入。
void AMyPlayerController::ApplyGameplayInputMode()
{
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

// 在角色状态允许时，将二维输入转换为控制器朝向的移动向量。
void AMyPlayerController::Move(const FInputActionValue& InputActionValue)
{
    if (GetASC() && GetASC()->HasMatchingGameplayTag(FMyGameplayTags::Get().Player_Block_InputPressed))
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
    if (IsMenuMouseAbility(InputTag) ||
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

// 持续输入通过鼠标菜单和角色状态门禁后交给能力系统。
void AMyPlayerController::AbilityInputTagHeld(FGameplayTag InputTag)
{
    if (IsMenuMouseAbility(InputTag) ||
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
