// 调用链：传播/查询 → PureMassCombat敌对与可见性 → 捕获原生命 → 有界排序；无持久对象或委托。
#include "AbilitySystem/Unified/AbilityTargetSelectionService.h"
#include "AbilitySystem/MyAttributeSet.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "PureMass/PureMassCombatTarget.h"
#include "PureMass/PureMassSubsystem.h"

TOptional<float> AbilityTargetSelectionService::ReadHealthRatio(const FAbilityTargetToken& Target)
{
    float Health = 0.f;
    float Maximum = 0.f;
    if (Target.Target.IsActor())
    {
        const auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target.Target.Actor.Get());
        if (!ASC || !ASC->HasAttributeSetForAttribute(UMyAttributeSet::GetHealthAttribute()) ||
            !ASC->HasAttributeSetForAttribute(UMyAttributeSet::GetMaxHealthAttribute()))
        {
            return {};
        }
        Health = ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());
        Maximum = ASC->GetNumericAttribute(UMyAttributeSet::GetMaxHealthAttribute());
    }
    else
    {
        FPureMassView View;
        const auto* Mass = Target.World.IsValid() ? Target.World->GetSubsystem<UPureMassSubsystem>() : nullptr;
        if (!Mass || !Mass->Resolve(Target.Target.EntityId, View))
        {
            return {};
        }
        Health = View.Health;
        Maximum = View.MaxHealth;
    }
    if (!FMath::IsFinite(Health) || !FMath::IsFinite(Maximum) || Maximum <= 0.f)
    {
        return {};
    }
    return FMath::Clamp(Health / Maximum, 0.f, 1.f);
}

bool AbilityTargetSelectionService::Select(const FAbilitySourceToken& Source, FVector Center, float Radius,
                                           int32 MaxTargets, EAbilityTargetSelectionPolicy Policy,
                                           TFunctionRef<bool(const FAbilityTargetToken&)> Exclude,
                                           TArray<FAbilitySelectedCandidate>& Out, FString& Error)
{
    Out.Reset();
    Error.Reset();
    AActor* Caster = nullptr;
    if (Center.ContainsNaN() || !FMath::IsFinite(Radius) || Radius <= 0.f || Radius > 3000.f || MaxTargets < 1 ||
        MaxTargets > 64 ||
        static_cast<uint8>(Policy) > static_cast<uint8>(EAbilityTargetSelectionPolicy::LowestHealthRatio) ||
        !AbilityExecutionIdentity::ResolveSource(Source, Caster))
    {
        Error = TEXT("Target selection requires a live source and bounded radius/count/policy.");
        return false;
    }
    struct FCandidate
    {
        FAbilitySelectedCandidate Value;
        double Distance = 0.;
        double Health = 0.;
        int32 Ordinal = 0;
    };
    const auto Gathered = PureMassCombat::Gather(Source.World.Get(), Caster, Center, Radius);
    if (Gathered.Num() > 4096)
    {
        Error = TEXT("Target selection exceeds the 4096-candidate query budget.");
        return false;
    }
    TArray<FCandidate> Candidates;
    for (const auto& Candidate : Gathered)
    {
        FVector Position;
        FAbilityTargetToken Token;
        FString CaptureError;
        if (!PureMassCombat::Resolve(Source.World.Get(), Caster, Candidate, Position) ||
            FVector::DistSquared(Center, Position) > FMath::Square(Radius) ||
            !PureMassCombat::Visible(Source.World.Get(), Caster, Center, Candidate) ||
            !AbilityExecutionIdentity::CaptureTarget(Source, Candidate, Token, CaptureError) || Exclude(Token))
        {
            continue;
        }
        Candidates.Add({{Token, Position},
                        FVector::DistSquared(Center, Position),
                        Policy == EAbilityTargetSelectionPolicy::LowestHealthRatio
                            ? ReadHealthRatio(Token).Get(TNumericLimits<float>::Max())
                            : 0.,
                        Candidates.Num()});
    }
    if (Policy != EAbilityTargetSelectionPolicy::Original)
    {
        Candidates.Sort(
            [Policy](const FCandidate& A, const FCandidate& B)
            {
                if (Policy == EAbilityTargetSelectionPolicy::LowestHealthRatio && A.Health != B.Health)
                {
                    return A.Health < B.Health;
                }
                if (A.Distance != B.Distance)
                {
                    return Policy == EAbilityTargetSelectionPolicy::Farthest ? A.Distance > B.Distance
                                                                             : A.Distance < B.Distance;
                }
                return A.Ordinal < B.Ordinal;
            });
    }
    for (int32 Index = 0; Index < FMath::Min(MaxTargets, Candidates.Num()); ++Index)
    {
        Out.Add(Candidates[Index].Value);
    }
    return true;
}
