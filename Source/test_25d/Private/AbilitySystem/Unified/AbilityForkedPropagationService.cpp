// 调用链：旧同步BFS或新投递回执 -> SelectChildren -> PureMass Gather/Resolve/Visible -> 原生命令牌。
#include "AbilitySystem/Unified/AbilityForkedPropagationService.h"
#include "AbilitySystem/Batch6/Batch6ChainAbility.h"

TArray<FAbilityForkedSelection> AbilityForkedPropagationService::SelectChildren(
    const FAbilitySourceToken& Source, FVector Center, int32 NextDepth, int32 AlreadyReserved,
    const FAbilityForkedSelectionSettings& Settings, TFunctionRef<bool(const FPureMassCombatTarget&)> ExcludeIdentity,
    TFunctionRef<bool(const FAbilityTargetToken&)> ExcludeLife)
{
    TArray<FAbilityForkedSelection> Result;
    AActor* Caster = nullptr;
    if (!AbilityExecutionIdentity::ResolveSource(Source, Caster))
    {
        return Result;
    }
    const float Radius = FMath::Clamp(Settings.Radius, 1.f, 2000.f);
    for (const auto& Candidate : PureMassCombat::Gather(Source.World.Get(), Caster, Center, Radius))
    {
        if (!FBatch6ChainBudget::Allows(AlreadyReserved + Result.Num(), NextDepth, Result.Num(), Settings.MaxTargets,
                                        Settings.MaxDepth, Settings.BranchesPerTarget))
        {
            break;
        }
        FVector Position;
        FAbilityTargetToken Token;
        FString Error;
        if (ExcludeIdentity(Candidate) || !PureMassCombat::Resolve(Source.World.Get(), Caster, Candidate, Position) ||
            FVector::DistSquared(Center, Position) > FMath::Square(Radius) ||
            !PureMassCombat::Visible(Source.World.Get(), Caster, Center, Candidate) ||
            !AbilityExecutionIdentity::CaptureTarget(Source, Candidate, Token, Error) || ExcludeLife(Token))
        {
            continue;
        }
        Result.Add({Token, Position});
    }
    return Result;
}
