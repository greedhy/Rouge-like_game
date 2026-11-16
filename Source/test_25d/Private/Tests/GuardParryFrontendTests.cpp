// 测试链：真实格挡GA/武器DA -> PlayerState ASC与输入 -> 蒙太奇Guard事件 -> 共享弹反窗口与取消清理。
// 只创建临时世界和物品目录，不修改CDO、资产或玩家存档。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilityTimelineTestAbility.h"
#include "AbilitySystem/Ability/StanderAbility.h"
#include "AbilitySystem/Ability/StanderWeaponActionAbility.h"
#include "AbilitySystem/Ability/SwordShieldAbility.h"
#include "AbilitySystem/Defense/GuardParryComponent.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/Time/AbilityWorldTimeSubsystem.h"
#include "Backpack/BackpackComponent.h"
#include "Backpack/ItemDefinition.h"
#include "Equipment/EquipmentComponent.h"
#include "Equipment/WeaponSetConfig.h"
#include "Tests/EquipmentTestEffects.h"
#include "Animation/AnimInstance.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"
#include "UObject/Script.h"

namespace
{
    /** 与已有Timeline运行测试相同，显式初始化无BeginPlay夹具的ASC和动画。 */
    AAbilityTimelineTestStander* SpawnParryPlayer(UWorld* World, UWeaponSetConfig* Set)
    {
        auto* Player = World->SpawnActor<AAbilityTimelineTestStander>();
        auto* State = World->SpawnActor<AMyPlayerState>();
        Player->AttachState(State);
        Player->Tags.AddUnique(TEXT("Player"));
        Player->GetMesh()->SetSkeletalMeshAsset(
            LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/PlatformAnimation/Models/Stander.Stander")));
        Player->ApplyWeaponSetVisuals(Set);
        auto* ASC = State->GetAbilitySystemComponent();
        ASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 100.f);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 100.f);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 1000.f);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 1000.f);
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        return Player;
    }

    /** 使用正式装备事务授予所选资产中的所有技能，覆盖换武器后右键输入路由。 */
    bool EquipParryWeapon(AAbilityTimelineTestStander* Player, UWeaponSetConfig* Set)
    {
        auto* State = Player->GetPlayerState<AMyPlayerState>();
        auto* Equipment = State->GetEquipmentComponent();
        auto* Backpack = State->GetBackpackComponent();
        const auto Slot = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Weapon"));
        Equipment->AvailableSlots.AddTag(Slot);
        auto* Catalog = NewObject<UBackpackItemDefinitions>(State);
        auto& Item = Catalog->Items.AddDefaulted_GetRef();
        Item.ItemDefinitionId = FPrimaryAssetId(TEXT("BackpackItem"), TEXT("GuardParryTestWeapon"));
        Item.bEquippable = true;
        Item.bStackable = false;
        Item.MaxStack = 1;
        Item.AllowedEquipmentSlots.AddTag(Slot);
        Item.EquipEffect = UEquipmentTestArmor::StaticClass();
        Item.WeaponSet = Set;
        Backpack->SetItemDefinitions(Catalog);
        Backpack->Initialize();
        Backpack->SetCapacity(1);
        if (!Equipment->ReconcileEffects())
        {
            return false;
        }
        Backpack->TryAddItem(Item.ItemDefinitionId, 1);
        return Equipment->TryEquip(0, Slot, Backpack->GetInventoryModel()->GetSlots()[0].ItemInstanceId,
                                   Backpack->GetRevision(), Equipment->Revision);
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardParryFallbackFrontendTest, "test_25d.UnifiedGA.GuardParry.ActualFallbackGA",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 验证原初始格挡的实际GA资产立即打开弹反窗口，取消时不留下可用防御。 */
bool FGuardParryFallbackFrontendTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    FAbilityGraphTestFixture F;
    auto* Set = LoadObject<UWeaponSetConfig>(
        nullptr, TEXT("/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/DA_WeaponSet.DA_WeaponSet"));
    auto* Config =
        LoadObject<UStanderCombatConfig>(nullptr, TEXT("/Game/BluePrints/Characters/Stander/DA_Stander.DA_Stander"));
    auto* GuardClass = LoadClass<UStanderGuardAbility>(
        nullptr, TEXT("/Game/BluePrints/Characters/Stander/GA_Stander_Guard.GA_Stander_Guard_C"));
    if (!TestNotNull(TEXT("Actual default weapon animations"), Set) ||
        !TestNotNull(TEXT("Actual fallback config"), Config) ||
        !TestNotNull(TEXT("Actual startup guard GA"), GuardClass))
    {
        return false;
    }
    auto* Player = SpawnParryPlayer(F.World, Set);
    Player->SwordConfig = Config;
    auto* ASC = CastChecked<UMyAbilitySystemComponent>(Player->GetAbilitySystemComponent());
    FAbilitySourceToken Attack;
    FString Error;
    TestTrue(TEXT("Enemy attack source captured"), AbilityExecutionIdentity::CaptureSource(F.Target, Attack, Error));
    auto* Clock = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
    Clock->SampleClockForTesting(10., false);
    FGameplayAbilitySpec Spec(GuardClass, 1);
    const auto RMB = FGameplayTag::RequestGameplayTag(TEXT("InputTag.RMB"));
    Spec.DynamicAbilityTags.AddTag(RMB);
    const auto Handle = ASC->GiveAbility(Spec);
    ASC->AbilityInputTagHeld(RMB);
    TestTrue(TEXT("Actual fallback input activates"), ASC->FindAbilitySpecFromHandle(Handle)->IsActive());
    TestTrue(TEXT("Fallback starts with real parry protection"),
             UGuardParryComponent::CanParryProjectile(Player, Attack));
    TestTrue(TEXT("Fallback accepts actual melee parry"),
             UGuardParryComponent::TryParryMelee(Player, Attack, FGuid::NewGuid()));
    const FGuid Reward = Player->FindComponentByClass<UGuardParryComponent>()->GetRewardLease();
    TestTrue(TEXT("Actual fallback config grants the independent world slow reward"), Clock->IsActive(Reward));
    Clock->SampleClockForTesting(10.16, false);
    TestFalse(TEXT("Fallback window expires independently of animation"),
              UGuardParryComponent::CanParryProjectile(Player, Attack));
    TestTrue(TEXT("Ordinary guard remains after precise window"),
             ASC->HasMatchingGameplayTag(USwordGuardEffect::StateTag()));
    ASC->CancelAbilityHandle(Handle);
    TestFalse(TEXT("Cancel removes original guard GE"), ASC->HasMatchingGameplayTag(USwordGuardEffect::StateTag()));
    TestFalse(TEXT("Cancelled fallback cannot parry"), UGuardParryComponent::CanParryProjectile(Player, Attack));
    TestTrue(TEXT("Earned parry reward survives guard cancellation"), Clock->IsActive(Reward));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardParryWeaponFrontendTest, "test_25d.UnifiedGA.GuardParry.AllAuthoredWeaponInputs",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 逐组装备真实11套武器；窗口必须跟随Guard事件打开，而不是在GA激活时提前消耗。 */
bool FGuardParryWeaponFrontendTest::RunTest(const FString&)
{
    FEditorScriptExecutionGuard ScriptGuard;
    const TCHAR* Families[] = {TEXT("Bow"),    TEXT("DoubleBlades"), TEXT("DualBlades"),  TEXT("Fighter"),
                               TEXT("Hammer"), TEXT("Magic"),        TEXT("Pistol"),      TEXT("Rapier"),
                               TEXT("Spear"),  TEXT("Staff"),        TEXT("Sword_Shield")};
    for (const TCHAR* Family : Families)
    {
        const FString Label(Family);
        FAbilityGraphTestFixture F;
        const FString Path =
            FString::Printf(TEXT("/Game/BluePrints/Characters/Stander/Weapons/%s/DA_WeaponSet.DA_WeaponSet"), Family);
        auto* Set = LoadObject<UWeaponSetConfig>(nullptr, *Path);
        if (!TestNotNull(Label + TEXT(" actual weapon loads"), Set))
        {
            continue;
        }
        auto* Player = SpawnParryPlayer(F.World, Set);
        if (!TestTrue(Label + TEXT(" equipment transaction succeeds"), EquipParryWeapon(Player, Set)))
        {
            continue;
        }
        auto* ASC = CastChecked<UMyAbilitySystemComponent>(Player->GetAbilitySystemComponent());
        const auto RMB = FGameplayTag::RequestGameplayTag(TEXT("InputTag.RMB"));
        auto* Spec = ASC->GetSpecWithSlot(RMB);
        auto* Ability = Spec ? Cast<UStanderWeaponActionAbility>(Spec->GetPrimaryInstance()) : nullptr;
        if (!TestNotNull(Label + TEXT(" actual RMB weapon GA"), Ability))
        {
            continue;
        }
        const auto Handle = Spec->Handle;
        FAbilitySourceToken Attack;
        FString Error;
        TestTrue(Label + TEXT(" captures enemy life"),
                 AbilityExecutionIdentity::CaptureSource(F.Target, Attack, Error));
        auto* Clock = F.World->GetSubsystem<UAbilityWorldTimeSubsystem>();
        Clock->SampleClockForTesting(20., false);
        ASC->AbilityInputTagHeld(RMB);
        TestTrue(Label + TEXT(" activates through RMB"), Ability->IsActive());
        bool bSawGuard = false;
        for (int32 Frame = 1; Frame <= 300 && Ability->IsActive(); ++Frame)
        {
            Clock->SampleClockForTesting(20. + Frame * .01, false);
            Player->GetMesh()->TickAnimation(.01f, false);
            Player->GetMesh()->GetAnimInstance()->DispatchQueuedAnimEvents();
            if (auto* Task = Ability->GetActiveTimelineTask())
            {
                Task->TickTask(.01f);
            }
            F.World->GetTimerManager().Tick(.01f);
            bSawGuard = ASC->HasMatchingGameplayTag(USwordGuardEffect::StateTag());
            if (bSawGuard)
            {
                TestTrue(Label + TEXT(" guard event opens fresh parry window"),
                         UGuardParryComponent::CanParryProjectile(Player, Attack));
                TestTrue(Label + TEXT(" actual melee contact parries"),
                         UGuardParryComponent::TryParryMelee(Player, Attack, FGuid::NewGuid()));
                TestTrue(Label + TEXT(" authored weapon grants world slow reward"),
                         Clock->IsActive(Player->FindComponentByClass<UGuardParryComponent>()->GetRewardLease()));
                Clock->SampleClockForTesting(20. + Frame * .01 + .16, false);
                TestFalse(Label + TEXT(" holding RMB does not prolong precise window"),
                          UGuardParryComponent::CanParryProjectile(Player, Attack));
                TestTrue(Label + TEXT(" window expiry retains ordinary guard"),
                         ASC->HasMatchingGameplayTag(USwordGuardEffect::StateTag()));
                break;
            }
            TestFalse(Label + TEXT(" no parry before actual guard event"),
                      UGuardParryComponent::CanParryProjectile(Player, Attack));
        }
        TestTrue(Label + TEXT(" animation reaches actual guard event"), bSawGuard);
        ASC->CancelAbilityHandle(Handle);
        TestFalse(Label + TEXT(" cancellation removes guard"),
                  ASC->HasMatchingGameplayTag(USwordGuardEffect::StateTag()));
        TestFalse(Label + TEXT(" cancelled weapon cannot parry"),
                  UGuardParryComponent::CanParryProjectile(Player, Attack));
    }
    return true;
}
#endif
