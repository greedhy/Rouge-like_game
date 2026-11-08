/**
 * 自动化回归入口：Automation RunTests -> 注册用例的 RunTest -> 被测模块 -> Test 系列断言。
 * 本文件注册的验收范围（可用这些路径单独执行）：
 * - test_25d.Abilities.Network.EffectContextConcreteType
 * - test_25d.Abilities.Network.EffectContextRadialRoundTrip
 * 测试夹具与生产入口分离；此文件不由正常游戏 Tick 调用。
 */
#include "Misc/AutomationTest.h"
#include "MyAbilityTypes.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMyEffectContextNetworkType, "test_25d.Abilities.Network.EffectContextConcreteType",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Abilities.Network.EffectContextConcreteType 回归，失败信息由下方断言交给自动化框架收集。 */
bool FMyEffectContextNetworkType::RunTest(const FString&)
{
    FGameplayEffectContextHandle Handle(new FMyGameplayEffectContext());
    TestEqual(TEXT("Receiver must allocate the same concrete context that the sender serializes"),
              Handle.Get()->GetScriptStruct(), FMyGameplayEffectContext::StaticStruct());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMyEffectContextRadialRoundTrip,
                                 "test_25d.Abilities.Network.EffectContextRadialRoundTrip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
/** 执行 test_25d.Abilities.Network.EffectContextRadialRoundTrip 回归，失败信息由下方断言交给自动化框架收集。 */
bool FMyEffectContextRadialRoundTrip::RunTest(const FString&)
{
    FMyGameplayEffectContext Sent;
    Sent.SetIsBlockedHit(true);
    Sent.SetIsCriticalHit(true);
    Sent.SetIsSuccessfulDebuff(true);
    Sent.SetDebuffDamage(7.f);
    Sent.SetDebuffDuration(4.f);
    Sent.SetDebuffFrequency(1.f);
    Sent.SetDeathImpulse(FVector(100, 200, 300));
    Sent.SetKnockbackForce(FVector(0, 0, 400));
    Sent.SetIsRadialDamage(true);
    Sent.SetRadialDamageInnerRadius(125.f);
    Sent.SetRadialDamageOuterRadius(450.f);
    Sent.SetRadialDamageOrigin(FVector(123, -456, 789));
    FBitWriter Writer(4096, true);
    bool bSuccess = false;
    Sent.NetSerialize(Writer, nullptr, bSuccess);
    TestTrue(TEXT("Sender succeeds"), bSuccess && !Writer.IsError());
    uint32 Sentinel = 0x5A19C3D7;
    Writer << Sentinel;
    FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
    FMyGameplayEffectContext Received;
    Received.NetSerialize(Reader, nullptr, bSuccess);
    TestTrue(TEXT("Receiver succeeds"), bSuccess && !Reader.IsError());
    TestTrue(TEXT("Hit and debuff flags survive"),
             Received.IsBlockedHit() && Received.IsCriticalHit() && Received.IsSuccessfulDebuff());
    TestEqual(TEXT("Debuff magnitude survives"), Received.GetDebuffDamage(), 7.f);
    TestTrue(TEXT("Radial flag survives"), Received.IsRadialDamage());
    TestEqual(TEXT("Inner radius survives"), Received.GetRadialDamageInnerRadius(), 125.f);
    TestEqual(TEXT("Outer radius survives"), Received.GetRadialDamageOuterRadius(), 450.f);
    TestEqual(TEXT("Highest-bit radial origin survives"), Received.GetRadialDamageOrigin(),
              Sent.GetRadialDamageOrigin());
    uint32 ReceivedSentinel = 0;
    Reader << ReceivedSentinel;
    TestEqual(TEXT("Following network field stays aligned"), ReceivedSentinel, Sentinel);
    TestEqual(TEXT("No unread context payload"), Reader.GetPosBits(), Writer.GetNumBits());
    return true;
}
#endif
