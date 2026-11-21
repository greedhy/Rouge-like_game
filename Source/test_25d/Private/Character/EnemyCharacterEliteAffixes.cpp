// 调用链：导演设置词缀 → GAS 出生属性及受伤回调 → 复制标记和移速 → 死亡/销毁清理。
#include "Character/EnemyCharacter.h"

#include "AbilitySystem/MyAbilitySystemComponent.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "Components/CapsuleComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    constexpr float FortifiedHealthMultiplier = 1.5f;
    constexpr float EnragedMovementMultiplier = 1.35f;
    constexpr float RegenerationDelay = 3.f;
    constexpr float RegenerationInterval = 1.f;
    constexpr float RegenerationFraction = 0.03f;
    const FProgressBarStyle EliteHealthStyle =
        FProgressBarStyle()
            .SetBackgroundImage(FSlateColorBrush(FLinearColor(0.17f, 0.09f, 0.09f)))
            .SetFillImage(FSlateColorBrush(FLinearColor::White));
}

bool AEnemyCharacter::ApplyDungeonEliteAffix(EDungeonEliteAffix Affix, float SpawnHealth)
{
    const float Health = Affix == EDungeonEliteAffix::Fortified ? SpawnHealth * FortifiedHealthMultiplier : SpawnHealth;
    if (!HasAuthority() || bDead || DungeonPoolLife != 0 || DungeonEliteAffix != EDungeonEliteAffix::None ||
        Affix <= EDungeonEliteAffix::None || Affix > EDungeonEliteAffix::Protective || !FMath::IsFinite(Health) ||
        Health <= 0.f || !AbilitySystemComponent || !AbilitySystemComponent->GetSet<UMyAttributeSet>())
    {
        return false;
    }

    // 先写出生属性，再订阅受伤，避免初始化血量被误当作一次伤害。
    DungeonEliteAffix = Affix;
    AbilitySystemComponent->SetNumericAttributeBase(UMyAttributeSet::GetMaxHealthAttribute(), Health);
    AbilitySystemComponent->SetNumericAttributeBase(UMyAttributeSet::GetHealthAttribute(), Health);
    if (Affix == EDungeonEliteAffix::Frenzied || Affix == EDungeonEliteAffix::Regenerating)
    {
        DungeonEliteHealthDelegate =
            AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UMyAttributeSet::GetHealthAttribute())
                .AddUObject(this, &AEnemyCharacter::OnDungeonEliteHealthChanged);
    }
    OnRep_DungeonEliteAffix();
    ForceNetUpdate();
    return true;
}

EDungeonEliteAffix AEnemyCharacter::GetDungeonEliteAffix() const
{
    return DungeonEliteAffix;
}

// 供血条和蓝图使用同一套中文说明，狂暴激活后同步更新文案。
FText AEnemyCharacter::GetDungeonEliteAffixDescription() const
{
    switch (DungeonEliteAffix)
    {
        case EDungeonEliteAffix::Fortified:
            return NSLOCTEXT("DungeonElite", "FortifiedDescription", "坚韧 · 生命上限 +50%");
        case EDungeonEliteAffix::Frenzied:
            return bDungeonEliteEnraged ? NSLOCTEXT("DungeonElite", "EnragedDescription", "狂暴中 · 移速 +35%")
                                        : NSLOCTEXT("DungeonElite", "FrenzyDescription", "狂暴 · 半血后移速 +35%");
        case EDungeonEliteAffix::Regenerating:
            return NSLOCTEXT("DungeonElite", "RegeneratingDescription", "再生 · 3 秒未受伤后每秒回复 3%");
        case EDungeonEliteAffix::Protective:
            return NSLOCTEXT("DungeonElite", "ProtectiveDescription", "守护光环 · 附近同伴减伤 25%");
        default:
            return FText::GetEmpty();
    }
}

// 由基础角色在计算移动速度时合并，沿用控制和地形效果的优先级。
float AEnemyCharacter::GetDungeonEliteMovementMultiplier() const
{
    return DungeonEliteAffix == EDungeonEliteAffix::Frenzied && bDungeonEliteEnraged ? EnragedMovementMultiplier : 1.f;
}

// 狂暴只切换一次，再生则在每次受伤后重新计时。
void AEnemyCharacter::OnDungeonEliteHealthChanged(const FOnAttributeChangeData& Data)
{
    if (bDead || Data.NewValue >= Data.OldValue || Data.NewValue <= 0.f)
    {
        return;
    }
    if (DungeonEliteAffix == EDungeonEliteAffix::Frenzied && !bDungeonEliteEnraged &&
        Data.NewValue <= AbilitySystemComponent->GetNumericAttribute(UMyAttributeSet::GetMaxHealthAttribute()) * 0.5f)
    {
        bDungeonEliteEnraged = true;
        OnRep_DungeonEliteAffix();
        ForceNetUpdate();
    }
    else if (DungeonEliteAffix == EDungeonEliteAffix::Regenerating)
    {
        GetWorldTimerManager().SetTimer(DungeonEliteRegenTimer, this, &AEnemyCharacter::RestoreDungeonEliteHealth,
                                        RegenerationInterval, true, RegenerationDelay);
    }
}

// 通过同一个 ASC 修改生命，让血条和生命属性复制沿用原来的更新路径。
void AEnemyCharacter::RestoreDungeonEliteHealth()
{
    const auto HealthAttribute = UMyAttributeSet::GetHealthAttribute();
    const float Health = AbilitySystemComponent->GetNumericAttribute(HealthAttribute);
    const float MaxHealth = AbilitySystemComponent->GetNumericAttribute(UMyAttributeSet::GetMaxHealthAttribute());
    if (bDead || Health <= 0.f || Health >= MaxHealth)
    {
        GetWorldTimerManager().ClearTimer(DungeonEliteRegenTimer);
        return;
    }
    const float Restore = FMath::Min(MaxHealth * RegenerationFraction, MaxHealth - Health);
    AbilitySystemComponent->ApplyModToAttribute(HealthAttribute, EGameplayModOp::Additive, Restore);
    if (AbilitySystemComponent->GetNumericAttribute(HealthAttribute) >= MaxHealth)
    {
        GetWorldTimerManager().ClearTimer(DungeonEliteRegenTimer);
    }
}

void AEnemyCharacter::ClearDungeonEliteAffix()
{
    if (DungeonEliteAffix == EDungeonEliteAffix::None && !bHasDungeonEliteHealthBar &&
        !DungeonEliteHealthDelegate.IsValid() && !DungeonEliteRegenTimer.IsValid())
    {
        return;
    }
    if (HasAuthority())
    {
        GetWorldTimerManager().ClearTimer(DungeonEliteRegenTimer);
        if (DungeonEliteHealthDelegate.IsValid())
        {
            AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UMyAttributeSet::GetHealthAttribute())
                .Remove(DungeonEliteHealthDelegate);
            DungeonEliteHealthDelegate.Reset();
        }
        ForceNetUpdate();
    }
    DungeonEliteAffix = EDungeonEliteAffix::None;
    bDungeonEliteEnraged = false;
    OnRep_DungeonEliteAffix();
}

// 复用角色头顶 HealthBar 组件，在同一个 Slate 布局绘制词缀和 ASC 当前生命。
void AEnemyCharacter::OnRep_DungeonEliteAffix()
{
    RefreshMovementSpeed();
    if (DungeonEliteAffix == EDungeonEliteAffix::None || bDead)
    {
        if (bHasDungeonEliteHealthBar)
        {
            HealthBar->SetVisibility(false);
            HealthBar->SetSlateWidget(nullptr);
            bHasDungeonEliteHealthBar = false;
        }
        return;
    }
    if (GetNetMode() == NM_DedicatedServer || !FSlateApplication::IsInitialized())
    {
        return;
    }

    FLinearColor Color;
    switch (DungeonEliteAffix)
    {
        case EDungeonEliteAffix::Fortified:
            Color = FLinearColor(1.f, 0.75f, 0.25f);
            break;
        case EDungeonEliteAffix::Frenzied:
            Color = bDungeonEliteEnraged ? FLinearColor(1.f, 0.25f, 0.18f) : FLinearColor(1.f, 0.54f, 0.4f);
            break;
        case EDungeonEliteAffix::Regenerating:
            Color = FLinearColor(0.44f, 1.f, 0.42f);
            break;
        case EDungeonEliteAffix::Protective:
            Color = FLinearColor(0.41f, 0.8f, 1.f);
            break;
        default:
            return;
    }

    if (!bHasDungeonEliteHealthBar)
    {
        HealthBar->SetWidgetClass(nullptr);
        HealthBar->SetRelativeLocation(FVector(0, 0, GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() + 65.f));
        HealthBar->SetWidgetSpace(EWidgetSpace::Screen);
        HealthBar->SetDrawAtDesiredSize(true);
        HealthBar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        HealthBar->SetVisibility(true);
        bHasDungeonEliteHealthBar = true;
    }

    const auto HealthPercent = TAttribute<TOptional<float>>::CreateLambda(
        [WeakEnemy = TWeakObjectPtr<AEnemyCharacter>(this)]() -> TOptional<float>
        {
            const auto* Enemy = WeakEnemy.Get();
            const auto* ASC = Enemy ? Enemy->GetAbilitySystemComponent() : nullptr;
            if (!ASC)
            {
                return 0.f;
            }
            const float MaxHealth = ASC->GetNumericAttribute(UMyAttributeSet::GetMaxHealthAttribute());
            const float Health = ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
            return MaxHealth > 0.f ? FMath::Clamp(Health / MaxHealth, 0.f, 1.f) : 0.f;
        });
    HealthBar->SetSlateWidget(
        SNew(SBorder)
        .Visibility(EVisibility::HitTestInvisible)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.86f))
        .Padding(FMargin(7.f, 3.f))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 3.f)
            [
                SNew(STextBlock)
                .Text(GetDungeonEliteAffixDescription())
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
                .ColorAndOpacity(FSlateColor(Color))
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBox).MinDesiredWidth(130.f).HeightOverride(7.f)
                [
                    SNew(SProgressBar)
                    .Style(&EliteHealthStyle)
                    .BarFillStyle(EProgressBarFillStyle::Scale)
                    .Percent(HealthPercent)
                    .FillColorAndOpacity(FLinearColor(0.87f, 0.16f, 0.17f))
                ]
            ]
        ]);
}
