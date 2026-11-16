// 专用联机测试地图 -> 权威授予 -> 远端 LocalPredicted 链 -> 真实 GAS RPC、接受/拒绝及 EndAbility。
#include "Tests/StanderComboPredictionProbe.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/StanderPlayer.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NativeGameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "MyGameplayTags.h"
#include "Player/MyPlayerState.h"

namespace
{
    UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ProbeReady2, "Test.ComboPrediction.Ready2");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ProbeReady3, "Test.ComboPrediction.Ready3");
}

AStanderComboPredictionProbe::AStanderComboPredictionProbe()
{
    bReplicates = true;
    bOnlyRelevantToOwner = true;
    PrimaryActorTick.bCanEverTick = true;
    ASC = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("ProbeASC"));
    ASC->SetIsReplicated(true);
}

// 客户端与服务器在各自 Actor 实例上初始化 GAS，Owner 链指向拥有此测试的连接。
void AStanderComboPredictionProbe::BeginPlay()
{
    Super::BeginPlay();
    ASC->InitAbilityActorInfo(this, this);
}

void AStanderComboPredictionProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AStanderComboPredictionProbe, Scenario);
}

TSubclassOf<UGameplayAbility> AStanderComboPredictionProbe::StageClass(int32 Stage) const
{
    if (Stage == 1)
    {
        return UStanderComboProbeFirst::StaticClass();
    }
    const bool bGated = Scenario != 1 && Scenario != 4;
    if (Stage == 2)
    {
        return bGated ? UStanderComboProbeGatedSecond::StaticClass() : UStanderComboProbeSecond::StaticClass();
    }
    return bGated ? UStanderComboProbeGatedThird::StaticClass() : UStanderComboProbeThird::StaticClass();
}

// 五个 Actor 各有独立 ASC 与 Tag 计数，不互相污染实验。
void AStanderComboPredictionProbe::Configure(int32 InScenario)
{
    Scenario = InScenario;
    // 普通复制 Actor 默认是 SimulatedProxy；预测只有拥有者的 AutonomousProxy 能发起。
    SetAutonomousProxy(true);
    if (Scenario == 5)
    {
        ForceNetUpdate();
        return;
    }
    for (int32 Stage : {1, 2, 3})
    {
        ASC->GiveAbility(FGameplayAbilitySpec(StageClass(Stage)));
    }
    ForceNetUpdate();
    Record(TEXT("granted"), 1, {});
}

// 等待实际连接收到三个授予，再由拥有者同步触发整条预测链。
void AStanderComboPredictionProbe::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (Scenario == 5)
    {
        AdvanceProductionCombo();
        return;
    }
    const auto* Controller = Cast<APlayerController>(GetOwner());
    if (HasAuthority() || bStarted || Scenario == INDEX_NONE || !Controller || !Controller->IsLocalController())
    {
        return;
    }
    if (ASC->FindAbilitySpecFromClass(StageClass(1)) && ASC->FindAbilitySpecFromClass(StageClass(2)) &&
        ASC->FindAbilitySpecFromClass(StageClass(3)))
    {
        bStarted = true;
        ASC->InitAbilityActorInfo(this, this);
        Record(TEXT("replicated"), static_cast<int32>(GetLocalRole()), {});
        ActivateNext(1);
    }
}

void AStanderComboPredictionProbe::AdvanceProductionCombo()
{
    const auto* PC = Cast<APlayerController>(GetOwner());
    const auto* Pawn = PC ? Cast<AStanderPlayer>(PC->GetPawn()) : nullptr;
    auto* AbilitySystem = Pawn ? Cast<UMyAbilitySystemComponent>(Pawn->GetAbilitySystemComponent()) : nullptr;
    auto* Anim = Pawn ? Pawn->GetMesh()->GetAnimInstance() : nullptr;
    const auto Input = FMyGameplayTags::Get().InputTag_LMB;
    if (!AbilitySystem || !Anim || !AbilitySystem->GetSpecWithSlot(Input) || SeenStages >= 4)
    {
        return;
    }
    if (const auto* Montage = Anim->GetCurrentActiveMontage())
    {
        const FName Name = Montage->GetFName();
        if (Name != LastMontage && (Montage->GetName().StartsWith(TEXT("AM_Attack")) ||
                                    Montage->GetName().StartsWith(TEXT("AM_Stander_Combo"))))
        {
            LastMontage = Name;
            ++SeenStages;
            Record(TEXT("montage"), FCString::Atoi(*Montage->GetName().Right(1)), {});
        }
        IdleSince = 0.;
        return;
    }
    if (HasAuthority() || !PC->IsLocalController() || Presses != SeenStages || Presses >= 4)
    {
        return;
    }
    const double Now = GetWorld()->GetTimeSeconds();
    if (IdleSince == 0.)
    {
        IdleSince = Now;
    }
    if (Now - IdleSince >= .15)
    {
        // 从拥有者走公开的正式输入入口；服务端必须由正常 RPC 收到。
        ++Presses;
        Record(TEXT("press"), Presses, {});
        AbilitySystem->AbilityInputTagPressed(Input);
    }
}

void AStanderComboPredictionProbe::ActivateNext(int32 Stage)
{
    if (const auto* Spec = ASC->FindAbilitySpecFromClass(StageClass(Stage)))
    {
        ASC->TryActivateAbility(Spec->Handle);
    }
}

bool AStanderComboPredictionProbe::RejectOnAuthority(int32 Stage) const
{
    return HasAuthority() && ((Stage == 1 && (Scenario == 1 || Scenario == 2)) ||
                              (Stage == 2 && (Scenario == 3 || Scenario == 4)));
}

void AStanderComboPredictionProbe::Record(const TCHAR* Event, int32 Stage, FPredictionKey Key, bool bCancelled) const
{
    UE_LOG(LogTemp, Display, TEXT("COMBO_PREDICTION case=%d side=%s event=%s stage=%d key=%d base=%d cancelled=%d"),
           Scenario, HasAuthority() ? TEXT("server") : TEXT("client"), Event, Stage, Key.Current, Key.Base, bCancelled);
}

void AStanderComboPredictionGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("StanderComboProductionProbe")))
    {
        const auto* PresetClass = LoadClass<AMyGameModeBase>(
            nullptr, TEXT("/Game/BluePrints/Characters/Stander/BP_StanderGameMode.BP_StanderGameMode_C"));
        const auto* Preset = PresetClass ? PresetClass->GetDefaultObject<AMyGameModeBase>() : nullptr;
        check(Preset && Preset->AbilityInfo && Preset->CharacterClassInfo);
        AbilityInfo = Preset->AbilityInfo;
        CharacterClassInfo = Preset->CharacterClassInfo;
        DefaultPawnClass = LoadClass<AStanderPlayer>(
            nullptr, TEXT("/Game/BluePrints/Characters/Stander/BP_StanderPlayer.BP_StanderPlayer_C"));
        PlayerStateClass = AMyPlayerState::StaticClass();
    }
#endif
    Super::InitGame(MapName, Options, ErrorMessage);
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("StanderComboProductionProbe")))
    {
        auto* Floor = GetWorld()->SpawnActor<AStaticMeshActor>(FVector(0, 0, -10), FRotator::ZeroRotator);
        Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Floor->GetStaticMeshComponent()->SetStaticMesh(
            LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        Floor->SetActorScale3D(FVector(15, 15, .2));
        Floor->SetReplicates(true);
        Floor->SetReplicateMovement(true);
        GetWorld()->SpawnActor<APlayerStart>(FVector(0, 0, 105), FRotator::ZeroRotator);
    }
#endif
}

void AStanderComboPredictionGameMode::PostLogin(APlayerController* NewPlayer)
{
    if (auto* State = NewPlayer->GetPlayerState<AMyPlayerState>())
    {
        State->Profession = EPlayerProfession::Stander;
    }
    AGameModeBase::PostLogin(NewPlayer);
#if !UE_BUILD_SHIPPING
    if (!FParse::Param(FCommandLine::Get(), TEXT("StanderComboPredictionProbe")))
    {
        return;
    }
    const bool bProduction = FParse::Param(FCommandLine::Get(), TEXT("StanderComboProductionProbe"));
    for (int32 Scenario = 0; Scenario < (bProduction ? 6 : 5); ++Scenario)
    {
        FActorSpawnParameters Params;
        Params.Owner = NewPlayer;
        auto* Probe = GetWorld()->SpawnActor<AStanderComboPredictionProbe>(Params);
        Probe->Configure(Scenario);
    }
#endif
}

// 只复用正式数据类型和目录查询。隔离测试登陆不进入背包持久化或房间鉴权。
void AStanderComboPredictionGameMode::PreLogin(const FString& Options, const FString& Address,
                                               const FUniqueNetIdRepl& Id, FString& ErrorMessage)
{
    AGameModeBase::PreLogin(Options, Address, Id, ErrorMessage);
}

APlayerController* AStanderComboPredictionGameMode::Login(UPlayer* Client, ENetRole InRemoteRole, const FString& Portal,
                                                            const FString& Options, const FUniqueNetIdRepl& Id,
                                                            FString& ErrorMessage)
{
    return AGameModeBase::Login(Client, InRemoteRole, Portal, Options, Id, ErrorMessage);
}

void AStanderComboPredictionGameMode::Logout(AController* Exiting)
{
    AGameModeBase::Logout(Exiting);
}

UClass* AStanderComboPredictionGameMode::GetDefaultPawnClassForController_Implementation(AController* Controller)
{
    return AGameModeBase::GetDefaultPawnClassForController_Implementation(Controller);
}

void AStanderComboPredictionGameMode::InitializeHUDForPlayer_Implementation(APlayerController* Controller)
{
    AGameModeBase::InitializeHUDForPlayer_Implementation(Controller);
}

AActor* AStanderComboPredictionGameMode::FindPlayerStart_Implementation(AController* Controller,
                                                                       const FString& IncomingName)
{
    return AGameModeBase::FindPlayerStart_Implementation(Controller, IncomingName);
}

AActor* AStanderComboPredictionGameMode::ChoosePlayerStart_Implementation(AController* Controller)
{
    return AGameModeBase::ChoosePlayerStart_Implementation(Controller);
}

UStanderComboProbeAbility::UStanderComboProbeAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

UStanderComboProbeFirst::UStanderComboProbeFirst()
{
    ActivationOwnedTags.AddTag(TAG_ProbeReady2);
}

UStanderComboProbeSecond::UStanderComboProbeSecond()
{
    Stage = 2;
    ActivationOwnedTags.AddTag(TAG_ProbeReady3);
}

UStanderComboProbeThird::UStanderComboProbeThird()
{
    Stage = 3;
}

UStanderComboProbeGatedSecond::UStanderComboProbeGatedSecond()
{
    ActivationRequiredTags.AddTag(TAG_ProbeReady2);
}

UStanderComboProbeGatedThird::UStanderComboProbeGatedThird()
{
    ActivationRequiredTags.AddTag(TAG_ProbeReady3);
}

// 对照唯一变量是后续段的 ActivationRequiredTags。两组受到同样的服务端故障注入。
bool UStanderComboProbeAbility::CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                                   const FGameplayTagContainer* SourceTags,
                                                   const FGameplayTagContainer* TargetTags,
                                                   FGameplayTagContainer* RelevantTags) const
{
    const auto* Probe = Info ? Cast<AStanderComboPredictionProbe>(Info->AvatarActor.Get()) : nullptr;
    if (!Probe)
    {
        return false;
    }
    if (Probe->RejectOnAuthority(Stage) || !Super::CanActivateAbility(Handle, Info, SourceTags, TargetTags, RelevantTags))
    {
        Probe->Record(TEXT("can_reject"), Stage, {});
        return false;
    }
    return true;
}

// 只客户端在活跃预测作用域继续，服务器逐条处理原生激活 RPC。
void UStanderComboProbeAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                                FGameplayAbilityActivationInfo Activation, const FGameplayEventData*)
{
    auto* Probe = CastChecked<AStanderComboPredictionProbe>(Info->AvatarActor.Get());
    FPredictionKey Key = Activation.GetActivationPredictionKey();
    Probe->Record(TEXT("activate"), Stage, Key);
    if (IsPredictingClient())
    {
        Key.NewRejectedDelegate().BindWeakLambda(Probe, [Probe, Step = Stage, Key]()
        {
            Probe->Record(TEXT("key_rejected"), Step, Key);
        });
        if (Stage < 3)
        {
            Probe->ActivateNext(Stage + 1);
        }
    }
}

// 收集实际 EndAbility，区分预测键回调和 GA 真正结束。
void UStanderComboProbeAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
                                           FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled)
{
    if (auto* Probe = Cast<AStanderComboPredictionProbe>(Info->AvatarActor.Get()))
    {
        Probe->Record(TEXT("end"), Stage, Activation.GetActivationPredictionKey(), bCancelled);
    }
    Super::EndAbility(Handle, Info, Activation, bReplicate, bCancelled);
}
