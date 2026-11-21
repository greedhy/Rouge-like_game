// 测试链：实际目录BeginPreview -> 原ServerClosePreview -> 实际ClientCloseAck -> 可观察的精确RequestId。
// 普通inactive状态、Begin和直接EndPreview都不能伪造网络关闭回执；无真实网络的用例只验证该原生协议端点。
#if WITH_DEV_AUTOMATION_TESTS
#include "AbilityActualMageFrontendFixture.h"
#include "AbilitySystem/Prototypes/PrototypeAbility.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeDeveloperExactCloseReadback,
                                 "test_25d.DynamicMage.Developer.ExactCloseAcknowledgementReadback",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeDeveloperExactCloseReadback::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture F;
    FString Error;
    const auto* Row = F.Definition(
        TEXT("/Game/BluePrints/AbilitySystem/Player/Ability/Arcane/ArcaneNova/GA_ArcaneNova.GA_ArcaneNova_C"), Error);
    if (!TestNotNull(TEXT("Actual selectable directory row exists"), Row) ||
        !TestTrue(TEXT("Actual developer preview begins"), F.Developer->BeginPreview(Row->DefinitionId, Error)))
    {
        AddInfo(Error);
        return false;
    }
    TestFalse(TEXT("Preview activation is not a close acknowledgement"),
              F.Developer->LastCloseAcknowledgedId.IsValid());
    TestFalse(TEXT("A new component has not received a successful close"), F.Developer->bLastCloseSucceeded);
    int32 Callbacks = 0;
    FGuid Received;
    bool bReceivedSuccess = false;
    const auto Callback = F.Developer->OnPreviewCloseAcknowledged.AddLambda(
        [&](FGuid Request, bool bSuccess)
        {
            ++Callbacks;
            Received = Request;
            bReceivedSuccess = bSuccess;
        });
    const FGuid Request = FGuid::NewGuid();
    F.Developer->ServerClosePreview(Request, false);
    TestEqual(TEXT("Original actual close transport reaches its acknowledgement handler once"), Callbacks, 1);
    TestTrue(TEXT("Original acknowledgement callback carries the precise submitted request"), Received == Request);
    TestTrue(TEXT("Original acknowledgement callback confirms successful cleanup"), bReceivedSuccess);
    TestFalse(TEXT("Authority really releases the actual preview before acknowledgement"),
              F.Developer->GetDeveloperState().bPreviewActive);
    TestTrue(TEXT("Readback records the actual acknowledgement ID, not just inactive preview state"),
             F.Developer->LastCloseAcknowledgedId == Request);
    TestTrue(TEXT("Readback records the actual acknowledgement success"), F.Developer->bLastCloseSucceeded);
    TestTrue(TEXT("Actual preview can start again after close"), F.Developer->BeginPreview(Row->DefinitionId, Error));
    TestTrue(TEXT("Direct state changes cannot invent a new close request"),
             F.Developer->LastCloseAcknowledgedId == Request);
    TestTrue(TEXT("A direct non-RPC EndPreview still works"), F.Developer->EndPreview(Error));
    TestEqual(TEXT("Direct EndPreview is not falsely counted as a received close ack"), Callbacks, 1);
    TestTrue(TEXT("Direct EndPreview preserves the last genuinely acknowledged request"),
             F.Developer->LastCloseAcknowledgedId == Request);
    F.Developer->OnPreviewCloseAcknowledged.Remove(Callback);
    const auto* Id = FindFProperty<FProperty>(F.Developer->GetClass(), TEXT("LastCloseAcknowledgedId"));
    const auto* Success = FindFProperty<FProperty>(F.Developer->GetClass(), TEXT("bLastCloseSucceeded"));
    TestTrue(TEXT("Acknowledgement readback is transient and read-only to scripting"),
             Id && Success && Id->HasAllPropertyFlags(CPF_Transient | CPF_BlueprintReadOnly) &&
                 Success->HasAllPropertyFlags(CPF_Transient | CPF_BlueprintReadOnly));
    for (const FName Getter : {FName(TEXT("GetDeveloperState")), FName(TEXT("GetDefinitions")),
                               FName(TEXT("GetObservations")), FName(TEXT("GetOwnerPlayerId"))})
    {
        const UFunction* Function = F.Developer->FindFunction(Getter);
        TestTrue(FString::Printf(TEXT("%s is a public read-only reflected panel API"), *Getter.ToString()),
                 Function && Function->HasAllFunctionFlags(FUNC_Public | FUNC_BlueprintPure | FUNC_Const));
    }
    F.State->SetPlayerId(37);
    TestEqual(TEXT("Player identity is read from the actual owning PlayerState"), F.Developer->GetOwnerPlayerId(), 37);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeDeveloperExactFrontendReadiness,
                                 "test_25d.DynamicMage.Developer.ExactFrontendReadinessReadback",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRuntimeDeveloperExactFrontendReadiness::RunTest(const FString&)
{
    FAbilityActualMageFrontendFixture F;
    FString Error;
    TestFalse(TEXT("An absent preview is not permission to cast"), F.Developer->IsPreviewFrontendIdle());
    const auto* Row = F.Definition(
        TEXT(
            "/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Projectile.GA_Prototype_Projectile_C"),
        Error);
    if (!TestNotNull(TEXT("Real prototype frontend has its own cooldown contract"), Row) ||
        !TestTrue(TEXT("The actual preview grant is accepted"), F.Developer->BeginPreview(Row->DefinitionId, Error)))
    {
        AddInfo(Error);
        return false;
    }
    const FGameplayAbilitySpecHandle Handle = F.Developer->GetPreviewHandle();
    auto* Spec = F.PlayerASC->FindAbilitySpecFromHandle(Handle);
    const auto* PrototypeAbility = Spec ? Cast<UPrototypeAbility>(Spec->Ability) : nullptr;
    if (!TestNotNull(TEXT("The temporary spec is real"), Spec) ||
        !TestNotNull(TEXT("Real prototype parameters remain available"),
                     PrototypeAbility ? PrototypeAbility->Prototype.Get() : nullptr))
    {
        return false;
    }
    const FGameplayTag Slot = FGameplayTag::RequestGameplayTag(TEXT("InputTag.DeveloperRecipe"));
    const FGameplayTag Cooldown = PrototypeAbility->Prototype->CooldownTag;
    TestTrue(TEXT("Original prototype cooldown is actually configured"), Cooldown.IsValid());
    TestTrue(TEXT("Exactly one inactive owned grant is observable as idle"), F.Developer->IsPreviewFrontendIdle());

    // 这是只读门的Spec状态协议测试，不冒充实际施法或复制网络；真实输入另有独立前端/PIE病例。
    Spec->ActiveCount = 1;
    TestFalse(TEXT("An active exact grant cannot be reported idle"), F.Developer->IsPreviewFrontendIdle());
    Spec->ActiveCount = 0;
    Spec->DynamicAbilityTags.RemoveTag(Slot);
    TestFalse(TEXT("Source identity without its exact developer slot is insufficient"),
              F.Developer->IsPreviewFrontendIdle());
    Spec->DynamicAbilityTags.AddTag(Slot);
    Spec->SourceObject = F.Pawn;
    TestFalse(TEXT("An unrelated source cannot impersonate this developer's grant"),
              F.Developer->IsPreviewFrontendIdle());
    Spec->SourceObject = F.Developer;

    FGameplayAbilitySpec Duplicate(Row->Base.AbilityClass, 1, INDEX_NONE, F.Developer);
    Duplicate.DynamicAbilityTags.AddTag(Slot);
    const FGameplayAbilitySpecHandle DuplicateHandle = F.PlayerASC->GiveAbility(Duplicate);
    TestFalse(TEXT("Duplicate owned slots are ambiguous rather than ready"), F.Developer->IsPreviewFrontendIdle());
    F.PlayerASC->ClearAbility(DuplicateHandle);
    TestTrue(TEXT("Removing only the extra grant restores unambiguous idle"), F.Developer->IsPreviewFrontendIdle());

    // 只为测试原型独立CooldownTag门加入/撤去准确标签；不称为自然GE过期或实际冷却时长验证。
    F.PlayerASC->AddLooseGameplayTag(Cooldown);
    TestFalse(TEXT("Prototype authored cooldown blocks even when native GetCooldownTags is empty"),
              F.Developer->IsPreviewFrontendIdle());
    F.PlayerASC->RemoveLooseGameplayTag(Cooldown);
    TestTrue(TEXT("Removing that gate exposes the still-inactive exact spec"), F.Developer->IsPreviewFrontendIdle());
    TestTrue(TEXT("Readback never changes or replaces the preview grant"), F.Developer->GetPreviewHandle() == Handle);
    const UFunction* Function = F.Developer->FindFunction(TEXT("IsPreviewFrontendIdle"));
    TestTrue(TEXT("Readiness is a public read-only reflected API"),
             Function && Function->HasAllFunctionFlags(FUNC_Public | FUNC_BlueprintPure | FUNC_Const));
    return true;
}
#endif
