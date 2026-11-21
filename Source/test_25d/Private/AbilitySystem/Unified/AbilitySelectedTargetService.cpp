// 调用链：批效果/独立状态 -> ResolveSelectedTarget -> 原生命、原空间谓词与原 LOS；不结算效果。
#include "AbilitySystem/Unified/AbilitySelectedTargetService.h"
#include "AbilitySystem/Unified/AbilityBehaviorPrimitives.h"
#include "AbilitySystem/Unified/AbilityPrototypeTargetServices.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"

FAbilitySelectionAdmissionLedger::FAbilitySelectionAdmissionLedger(FGuid InRoot, const FAbilitySourceToken& InSource)
    : Root(InRoot), Source(InSource)
{
}

bool FAbilitySelectionAdmissionLedger::BelongsTo(FGuid ExpectedRoot, const FAbilitySourceToken& ExpectedSource) const
{
    return Root.IsValid() && Root == ExpectedRoot && Source.World == ExpectedSource.World &&
           Source.Avatar == ExpectedSource.Avatar && Source.ASC == ExpectedSource.ASC &&
           Source.DungeonRunId == ExpectedSource.DungeonRunId && Source.EnemyLease == ExpectedSource.EnemyLease;
}

bool FAbilitySelectionAdmissionLedger::Contains(const FPureMassCombatTarget& Identity) const
{
    return AdmittedSteps.Contains(Identity);
}

bool FAbilitySelectionAdmissionLedger::AdmittedInStep(const FPureMassCombatTarget& Identity, FGuid Step) const
{
    const auto* Existing = AdmittedSteps.Find(Identity);
    return Existing && *Existing == Step;
}

bool FAbilitySelectionAdmissionLedger::Admit(const FPureMassCombatTarget& Identity, FGuid Step)
{
    if (!Identity.IsSet() || !Step.IsValid())
    {
        return false;
    }
    if (const auto* Existing = AdmittedSteps.Find(Identity))
    {
        return *Existing == Step;
    }
    if (AdmittedSteps.Num() >= 65536)
    {
        return false;
    }
    AdmittedSteps.Add(Identity, Step);
    return true;
}

bool AbilitySelectedTargetService::ValidateSelectionOwner(FGuid Root, const FAbilitySourceToken& Source,
                                                          const FAbilityTargetSetSnapshot& Selection)
{
    if (!Selection.SelectionReceipt)
    {
        return true;
    }
    const auto& Receipt = *Selection.SelectionReceipt;
    return Source.World == Selection.Source.World && Source.Avatar == Selection.Source.Avatar &&
           Source.ASC == Selection.Source.ASC && Source.DungeonRunId == Selection.Source.DungeonRunId &&
           Source.EnemyLease == Selection.Source.EnemyLease && Receipt.Step.IsValid() && Receipt.Admissions &&
           Receipt.Admissions->BelongsTo(Root, Source);
}

bool AbilitySelectedTargetService::ResolveSelectedTarget(const UAbilityGraphInstance& Owner,
                                                         const FAbilitySourceToken& Source,
                                                         const FAbilityTargetSetSnapshot& Selection,
                                                         const FAbilityTargetToken& Target, FVector& OutLocation)
{
    // 普通快照不附带空间凭据，继续保留已有批效果的精确语义。
    if (!Selection.SelectionReceipt)
    {
        return AbilityExecutionIdentity::ResolveTarget(Source, Target, OutLocation);
    }
    OutLocation = FVector::ZeroVector;
    AActor* Avatar = nullptr;
    const auto& Receipt = *Selection.SelectionReceipt;
    if (!ValidateSelectionOwner(Owner.GetExecutionId(), Source, Selection) ||
        !AbilityExecutionIdentity::ResolveSource(Source, Avatar) ||
        !AbilityExecutionIdentity::ResolveTarget(Source, Target, OutLocation) || Receipt.Origin.ContainsNaN() ||
        !FMath::IsFinite(Receipt.MaximumRadius) || Receipt.MaximumRadius <= 0.f ||
        !FMath::IsFinite(Receipt.PreviousRadius) || Receipt.PreviousRadius < 0.f ||
        !FMath::IsFinite(Receipt.CurrentRadius) || Receipt.CurrentRadius < Receipt.PreviousRadius ||
        Receipt.CurrentRadius > Receipt.MaximumRadius || !Receipt.Step.IsValid() || !Receipt.Admissions.IsValid())
    {
        return false;
    }
    // 同一步的Contact已成立后，GE造成的位置变化不能撤销这次历史接触；状态只重验原生命。
    if (Receipt.Admissions->AdmittedInStep(Target.Target, Receipt.Step))
    {
        return true;
    }
    if (Receipt.Admissions->Contains(Target.Target))
    {
        return false;
    }
    const bool bActor = Receipt.Profile == EAbilitySelectionReceiptProfile::ActorWave;
    if ((!bActor && Receipt.Profile != EAbilitySelectionReceiptProfile::MixedWave &&
         Receipt.Profile != EAbilitySelectionReceiptProfile::MixedFrontWave) ||
        (bActor && (!Target.Target.IsActor() ||
                    FVector::DistSquared(Receipt.Origin, OutLocation) > FMath::Square(Receipt.MaximumRadius))) ||
        (!bActor &&
         !AbilityBehaviorPrimitives::ContainsWavePoint(Receipt.Origin, OutLocation, Receipt.PreviousRadius,
                                                       Receipt.CurrentRadius, EAbilityWaveCoverage::GrowingSphere3D)) ||
        ((bActor || Receipt.Profile == EAbilitySelectionReceiptProfile::MixedFrontWave) &&
         !AbilityBehaviorPrimitives::ContainsWavePoint(Receipt.Origin, OutLocation, Receipt.PreviousRadius,
                                                       Receipt.CurrentRadius, EAbilityWaveCoverage::SweptShell3D)))
    {
        return false;
    }
    const bool bVisible =
        bActor ? AbilityPrototypeTargetServices::ActorLineOfSight(Source.World.Get(), Avatar, Target.Target.Actor.Get(),
                                                                  Receipt.Origin, false, Receipt.IgnoredEffect.Get())
               : PureMassCombat::Visible(Source.World.Get(), Avatar, Receipt.Origin, Target.Target,
                                         Receipt.IgnoredEffect.Get());
    return bVisible && Receipt.Admissions->Admit(Target.Target, Receipt.Step);
}
