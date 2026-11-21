// 调用链：真实 PlayerState -> 开发组件 -> 原控制器 InputKey -> 原 ASC 激活/松键；不直接调用伤害。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "Tests/DungeonPlayerRunTestTypes.h"
#include "Player/MyPlayerController.h"
#include "PureMass/PureMassPlayerController.h"
#include "AbilitySystem/Unified/AbilityRuntimeRecipeDeveloperComponent.h"
#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "InputCoreTypes.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeRecipePlayerIntegration,
                                 "test_25d.UnifiedGA.DynamicMage.Developer.RealControllerPreviewInput",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeRecipePlayerIntegration::RunTest(const FString&)
{
    for (const bool bMass : {false, true})
    {
        FAbilityGraphTestFixture F;
        auto* State = F.World->SpawnActor<AMyPlayerState>();
        State->Profession = EPlayerProfession::Mage;
        auto* Developer = State->FindComponentByClass<UAbilityRuntimeRecipeDeveloperComponent>();
        if (!TestNotNull(TEXT("PlayerState owns the actual developer extension of its recipe component"), Developer))
        {
            continue;
        }
        auto* Pawn = F.World->SpawnActor<ADungeonPlayerRunTestPawn>();
        Pawn->AttachState(State);
        auto* PC = bMass ? static_cast<AMyPlayerController*>(F.World->SpawnActor<APureMassPlayerController>())
                         : F.World->SpawnActor<AMyPlayerController>();
        PC->SetPawn(Pawn);
        auto* ASC = CastChecked<UMyAbilitySystemComponent>(State->GetAbilitySystemComponent());
        ASC->AddAttributeSetSubobject(CastChecked<UMyAttributeSet>(State->GetAttributeSet()));
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), 1000.f);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), 1000.f);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetMaxManaAttribute(), 100.f);
        ASC->SetNumericAttributeBase(UMyAttributeSet::GetManaAttribute(), 100.f);
        FString Error;
        TestTrue(TEXT("Player preview reads actual catalog"), Developer->InitializeCatalog(nullptr, Error));
        const auto* Definition = Developer->GetDefinitions().FindByPredicate(
            [](const FAbilityRuntimeDeveloperDefinition& Item)
            {
                return GetPathNameSafe(Item.Base.AbilityClass.Get()) ==
                       TEXT(
                           "/Game/BluePrints/AbilitySystem/Player/Ability/PassiveSpell/OrbitingFireBolt/GA_OrbitingFireBolt.GA_OrbitingFireBolt_C");
            });
        if (!TestNotNull(TEXT("Actual maintained frontend is available for press/release observation"), Definition) ||
            !TestTrue(TEXT("Temporary preview is granted through real component"),
                      Developer->BeginPreview(Definition->DefinitionId, Error)))
        {
            continue;
        }
        const auto Handle = Developer->GetPreviewHandle();
        const auto Device = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(PC->GetPlatformUserId());
        PC->InputKey(FInputKeyEventArgs(nullptr, Device, EKeys::T, IE_Pressed, FPlatformTime::Cycles64()));
        const auto* Spec = ASC->FindAbilitySpecFromHandle(Handle);
        TestTrue(TEXT("Dedicated physical test key reaches original ASC activation"), Spec && Spec->IsActive());
        PC->InputKey(FInputKeyEventArgs(nullptr, Device, EKeys::T, IE_Released, FPlatformTime::Cycles64()));
        Spec = ASC->FindAbilitySpecFromHandle(Handle);
        TestTrue(TEXT("Physical release reaches same preview spec"), Spec && !Spec->InputPressed);
        TestTrue(TEXT("Preview cleanup removes its grant after real input"), Developer->EndPreview(Error));
        TestNull(TEXT("Preview handle does not survive cleanup"), ASC->FindAbilitySpecFromHandle(Handle));
    }
    return true;
}
#endif
