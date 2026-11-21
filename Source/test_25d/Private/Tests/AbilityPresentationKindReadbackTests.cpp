// 测试链：三种实际Runtime的纯表现复制接收入口 -> 冻结Kind只读API；不以旧DA字段猜测新宿主身份。
// 此处只验证投影读取合同；真实Niagara/发射数量由Rendered及实际UMG场景分别验收。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityGraphTestFixture.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionRuntime.h"
#include "PureMass/PureMassRuntime.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPresentationFrozenKindReadback,
                                 "test_25d.DynamicMage.Presentation.FrozenKindReadbackNeverUsesLegacyPrototype",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPresentationFrozenKindReadback::RunTest(const FString&)
{
    FAbilityGraphTestFixture F;
    auto Check = [&](auto* Actor, EAbilityPrototypePresentationProfile Profile)
    {
        const FString Prefix = Actor->GetClass()->GetName() + TEXT(": ");
        TestEqual(Prefix + TEXT("A new host cannot invent a kind before its frozen snapshot is ready"),
                  Actor->GetPresentationKind(), INDEX_NONE);
        const auto* Getter = Actor->FindFunction(TEXT("GetPresentationKind"));
        TestTrue(Prefix + TEXT("The reflected diagnostic is public, const and read-only"),
                 Getter && Getter->HasAllFunctionFlags(FUNC_Public | FUNC_BlueprintPure | FUNC_Const));
        FAbilityPrototypeVisualSnapshot Snapshot;
        Snapshot.bReady = true;
        Snapshot.Profile = Profile;
        Snapshot.Kind = EAbilityPrototypeKind::Beam;
        FAbilityPrototypePresentationFrame Frame;
        Frame.bReady = true;
        Frame.Revision = 1;
        TestTrue(Prefix + TEXT("The receiver receives a valid ready frame"),
                 AbilityPrototypePresentationService::IsValidFrame(Frame));
        Actor->ApplyPresentationReplica(Snapshot, Frame);
        TestEqual(Prefix + TEXT("A pure host reports its actual frozen Beam with no legacy Prototype reference"),
                  Actor->GetPresentationKind(), static_cast<int32>(EAbilityPrototypeKind::Beam));
        Snapshot.Kind = EAbilityPrototypeKind::Wave;
        Frame.Revision = 2;
        Actor->ApplyPresentationReplica(Snapshot, Frame);
        TestEqual(Prefix + TEXT("Readback follows the received frozen value instead of a CDO or asset fallback"),
                  Actor->GetPresentationKind(), static_cast<int32>(EAbilityPrototypeKind::Wave));
        TestEqual(Prefix + TEXT("Both receiver frames were accepted, including the second revision"),
                  Actor->GetPresentationStats().Updates, 1);
    };
    Check(F.World->SpawnActor<APrototypeRuntime>(), EAbilityPrototypePresentationProfile::Actor);
    Check(F.World->SpawnActor<APureMassRuntime>(), EAbilityPrototypePresentationProfile::Mixed);
    Check(F.World->SpawnActor<AExtensionRuntime>(), EAbilityPrototypePresentationProfile::Extension);
    return true;
}
#endif
