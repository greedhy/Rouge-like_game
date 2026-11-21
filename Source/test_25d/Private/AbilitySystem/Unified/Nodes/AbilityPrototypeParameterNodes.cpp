// 调用链：Scope.Start -> Context 原生命/64次逻辑预算 -> immutable frame -> 同图 typed 后继。
// 配方编译只缩放当前Scope实际消费的轴；孩子仍建立自己的帧，不继承父容器的未消费字段。
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityBatchCombatNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeExtensionNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeContactNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityNormalizationNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityExtensionFlowNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityLegacyNodeExecution.h"
#include "AbilitySystem/Unified/AbilityGraphInstance.h"
#include "AbilitySystem/Unified/AbilityNodeRegistry.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "AbilitySystem/Prototypes/PrototypeRuntime.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"

namespace
{
    enum EScopeScalarConsumption
    {
        ScopeDamage = 1,
        ScopeRadius = 2,
        ScopeDuration = 4
    };

    /** 只描述本片段已登记动作的消费者；Extension的父类Kind不决定其行为或参数合同。 */
    int32 ConsumedScopeScalars(const UAbilityPrototypeScopeNodeConfig& Scope)
    {
        if (const auto* Extension = Cast<UExtensionNode>(Scope.Parameters))
        {
            switch (Extension->Behavior)
            {
            case EExtensionKind::Burst:
                return ScopeDamage | ScopeRadius;
            case EExtensionKind::Projectile:
                return ScopeDamage | ScopeDuration;
            case EExtensionKind::Chain:
                return ScopeDamage | ScopeRadius | ScopeDuration;
            case EExtensionKind::Attach:
                return ScopeDuration;
            case EExtensionKind::Displace:
                // single的Radius只缩放表现；真正区域选敌仅由area分支消费这个字段。
                return ScopeDuration | (Extension->bSingleTarget ? 0 : ScopeRadius);
            case EExtensionKind::Surface:
                return ScopeRadius | ScopeDuration;
            case EExtensionKind::Delay:
            case EExtensionKind::Stack:
                // Delay使用ArmDelay，Stack使用StackLifetime/DetonationDelay；孩子拥有自己的伤害。
                return 0;
            default:
                return 0;
            }
        }

        using K = EAbilityPrototypeKind;
        switch (Scope.Parameters->Kind)
        {
        case K::Burst:
        case K::Cone:
        case K::Ring:
        case K::Trap:
        case K::Wave:
        case K::Chain:
            return ScopeDamage | ScopeRadius | ScopeDuration;
        case K::Field:
        {
            const auto* Field = Cast<UBatch6FieldPrototype>(Scope.Parameters);
            const bool bFollowRoot = Field && Field->bFollowCasterToggle && Scope.LogicalDepth == 0 &&
                                     Scope.Profile == TEXT("PrototypeFine.Mixed.v1");
            // 原Mixed随动开关根由输入/耗蓝关闭；它的Duration不裁剪时钟，有限孩子仍消费Duration。
            return ScopeDamage | ScopeRadius | (bFollowRoot ? 0 : ScopeDuration);
        }
        case K::Projectile:
        case K::Homing:
        case K::Piercing:
        case K::Returning:
        case K::Beam:
        case K::Line:
            // 飞行用CollisionRadius，Beam用Range，Line用Range/Width，不把通用Radius暗换成它们。
            return ScopeDamage | ScopeDuration;
        case K::Multishot:
            // 扇形容器只消费Count/Spread；每个single Scope从自己的配置冻结伤害和飞行寿命。
            return Scope.bSingleProjectile ? ScopeDamage | ScopeDuration : 0;
        case K::Orbit:
            // 根Orbit是开关；几何来自OrbitSettings/CollisionRadius，只有逻辑孩子消费有限Duration。
            return ScopeDamage | (Scope.LogicalDepth > 0 ? ScopeDuration : 0);
        case K::Surface:
        case K::Emitter:
            return ScopeRadius | ScopeDuration;
        case K::Delay:
            return 0;
        default:
            return 0;
        }
    }
} // namespace

UAbilityPrototypeParameterFrame* UAbilityPrototypeParameterFrame::Create(
    UObject* Outer, const UAbilityPrototype& Frozen, int32 Level, FName Profile, UAbilityPrototype* InStableReference,
    FGuid Root, const FAbilitySourceToken& Source, FString& Error, int32 LogicalDepth, bool bSingleProjectile,
    const FAbilityNodeInput* EntryInput)
{
    Error.Reset();
    const auto* Extension = Cast<UExtensionNode>(&Frozen);
    if (!(Extension ? Extension->ValidateGraph() : Frozen.IsValidConfiguration()) ||
        !FAbilityNodeRegistry::IsPrototypeCompatibilityProfile(Profile) || Level < 1 || Level > 100 ||
        ((Profile == TEXT("PrototypeFine.Extension.v1")) != (Extension != nullptr)) || LogicalDepth < 0 ||
        LogicalDepth > 3 || (bSingleProjectile && (Extension || Frozen.Kind != EAbilityPrototypeKind::Multishot)))
    {
        Error = TEXT("Prototype frame requires validated parameters, level and an explicit fine profile.");
        return nullptr;
    }
    auto* Frame = NewObject<UAbilityPrototypeParameterFrame>(Outer);
    if (!Frame->BindExecution(Root, Source))
    {
        Error = TEXT("Prototype parameter frame could not bind the original authoritative execution.");
        return nullptr;
    }
    Frame->Parameters = DuplicateObject<UAbilityPrototype>(&Frozen, Frame);
    Frame->StableReference = InStableReference;
    Frame->CapturedLevel = Level;
    Frame->CapturedDamage = Frozen.DamageAtLevel(Level);
    Frame->CapturedProfile = Profile;
    Frame->CapturedLogicalDepth = LogicalDepth;
    Frame->bCapturedSingleProjectile = bSingleProjectile;
    if (EntryInput)
    {
        if (!Frame->BelongsTo(Root, EntryInput->SourceToken))
        {
            Error = TEXT("Captured legacy entry input must retain this frame's original source life.");
            return nullptr;
        }
        Frame->CapturedEntryInput = *EntryInput;
        Frame->bHasEntryInput = true;
    }
    return Frame;
}

bool UAbilityPrototypeParameterFrame::RestoreEntryInput(const FAbilityNodeInput& Current, FAbilityNodeInput& Output,
                                                        FString& Error) const
{
    if (!bHasEntryInput || Current.ParameterFrame.Get() != this)
    {
        Error = TEXT("Legacy Completed requires its own captured original invocation input.");
        return false;
    }
    Output = CapturedEntryInput;
    // 旧图没有细帧；保留本片段帧供桥接身份校验，后续Scope仍会独立创建自己的参数帧。
    Output.ParameterFrame = Current.ParameterFrame;
    Error.Reset();
    return true;
}

const UAbilityPrototype& UAbilityPrototypeParameterFrame::GetParameters() const
{
    return *Parameters;
}

float UAbilityPrototypeParameterFrame::GetDamage() const
{
    return CapturedDamage;
}

int32 UAbilityPrototypeParameterFrame::GetLevel() const
{
    return CapturedLevel;
}

FName UAbilityPrototypeParameterFrame::GetProfile() const
{
    return CapturedProfile;
}

int32 UAbilityPrototypeParameterFrame::GetLogicalDepth() const
{
    return CapturedLogicalDepth;
}

bool UAbilityPrototypeParameterFrame::IsSingleProjectile() const
{
    return bCapturedSingleProjectile;
}

UAbilityPrototype* UAbilityPrototypeParameterFrame::GetStableReference() const
{
    return StableReference;
}

bool UAbilityPrototypeScopeNodeConfig::Validate(FString& Error) const
{
    Error.Reset();
    const auto* Extension = Cast<UExtensionNode>(Parameters);
    if (!Parameters || !(Extension ? Extension->ValidateGraph() : Parameters->IsValidConfiguration()) ||
        LogicalDepth < 0 || LogicalDepth > 3 || !FAbilityNodeRegistry::IsPrototypeCompatibilityProfile(Profile) ||
        (LogicalRejectionPolicy != EAbilityPrototypeLogicalRejectionPolicy::SkipBranch &&
         LogicalRejectionPolicy != EAbilityPrototypeLogicalRejectionPolicy::FailExecution) ||
        (Profile == TEXT("PrototypeFine.Extension.v1")) != Parameters->IsA<UExtensionNode>() ||
        (bSingleProjectile && Parameters->Kind != EAbilityPrototypeKind::Multishot))
    {
        Error = TEXT("Scope requires explicit compatible parameters, original logical depth and projectile mode.");
        return false;
    }
    return true;
}

bool UAbilityPrototypeScopeNodeConfig::SupportsPrototypeCompatibilityRoot(FName RequestedProfile, FString& Error) const
{
    if (LogicalDepth != 0 || Profile != RequestedProfile || !Validate(Error))
    {
        if (Error.IsEmpty())
        {
            Error = TEXT("Only a depth-zero prototype scope may admit its own exact compatibility profile.");
        }
        return false;
    }
    return true;
}

bool UAbilityPrototypeScopeNodeConfig::TryScaleRecipeParameter(EAbilityConfigScalar Parameter, float Multiplier)
{
    if (!Parameters || !FMath::IsFinite(Multiplier) || Multiplier < 0.f)
    {
        return false;
    }
    const int32 Requested = Parameter == EAbilityConfigScalar::Damage     ? ScopeDamage
                            : Parameter == EAbilityConfigScalar::Radius   ? ScopeRadius
                            : Parameter == EAbilityConfigScalar::Duration ? ScopeDuration
                                                                          : 0;
    if (Requested == 0 || (ConsumedScopeScalars(*this) & Requested) == 0)
    {
        // 必须在写入任何字段之前拒绝；配方事务随后保留原版本/摘要，而不是提交无效果的修正。
        return false;
    }
    switch (Parameter)
    {
    case EAbilityConfigScalar::Damage:
        Parameters->Damage *= Multiplier;
        Parameters->DamagePerLevel *= Multiplier;
        break;
    case EAbilityConfigScalar::Radius:
        Parameters->Radius *= Multiplier;
        break;
    case EAbilityConfigScalar::Duration:
        Parameters->Duration *= Multiplier;
        break;
    default:
        return false;
    }
    return true;
}

bool UAbilityPrototypeScopeNodeConfig::TrySetRecipeParameter(EAbilityConfigParameter Parameter, float Value,
                                                             FString& Error)
{
    Error.Reset();
    if (!Parameters || !FMath::IsFinite(Value))
    {
        Error = TEXT("Typed scope configuration needs private parameters and a finite value.");
        return false;
    }
    using K = EAbilityPrototypeKind;
    const auto Kind = Parameters->Kind;
    auto* Extension = Cast<UExtensionNode>(Parameters);
    const bool bFlight = !Extension && (Kind == K::Projectile || Kind == K::Homing || Kind == K::Piercing ||
                                        Kind == K::Returning || (Kind == K::Multishot && bSingleProjectile));
    const bool bInteger = Value >= 1.f && Value <= 16.f && Value == FMath::RoundToFloat(Value);
    switch (Parameter)
    {
    case EAbilityConfigParameter::Count:
        if (!bInteger || (Extension ? Extension->Behavior != EExtensionKind::Chain
                                    : !((Kind == K::Multishot && !bSingleProjectile) || Kind == K::Emitter ||
                                        Kind == K::Chain || Kind == K::Piercing || Kind == K::Returning)))
        {
            break;
        }
        Parameters->Count = static_cast<int32>(Value);
        return Validate(Error);
    case EAbilityConfigParameter::Spread:
        if (Extension || Value < 0.f || Value > 360.f ||
            !((Kind == K::Multishot && !bSingleProjectile) || Kind == K::Cone))
        {
            break;
        }
        Parameters->SpreadDegrees = Value;
        return Validate(Error);
    case EAbilityConfigParameter::Period:
        if (Value < .1f || Value > 60.f ||
            (Extension ? (Extension->Behavior != EExtensionKind::Attach && Extension->Behavior != EExtensionKind::Chain)
                       : (Kind != K::Field && Kind != K::Beam && Kind != K::Chain && Kind != K::Emitter &&
                          !(Parameters->OnPeriod && Kind == K::Surface))))
        {
            break;
        }
        Parameters->Period = Value;
        return Validate(Error);
    case EAbilityConfigParameter::ArmDelay:
        if (Value < .1f || Value > 60.f ||
            (Extension ? Extension->Behavior != EExtensionKind::Delay : (Kind != K::Delay && Kind != K::Trap)))
        {
            break;
        }
        Parameters->ArmDelay = Value;
        return Validate(Error);
    case EAbilityConfigParameter::Range:
        if (Value <= 0.f || Value > 10000.f ||
            (Extension ? Extension->Behavior != EExtensionKind::Projectile
                       : (!bFlight && Kind != K::Line && Kind != K::Beam && Kind != K::Chain)))
        {
            break;
        }
        Parameters->Range = Value;
        return Validate(Error);
    case EAbilityConfigParameter::CollisionRadius:
        if (Value <= 0.f || Value > 200.f ||
            (Extension ? Extension->Behavior != EExtensionKind::Projectile
                       : (!bFlight && !(Kind == K::Orbit && Profile == TEXT("PrototypeFine.Mixed.v1")))))
        {
            break;
        }
        Parameters->CollisionRadius = Value;
        return Validate(Error);
    case EAbilityConfigParameter::Duration:
        if (Value <= 0.f || Value > 60.f || !(ConsumedScopeScalars(*this) & ScopeDuration))
        {
            break;
        }
        Parameters->Duration = Value;
        return Validate(Error);
    case EAbilityConfigParameter::Radius:
        if (Value <= 0.f || Value > 10000.f || !(ConsumedScopeScalars(*this) & ScopeRadius))
        {
            break;
        }
        Parameters->Radius = Value;
        return Validate(Error);
    case EAbilityConfigParameter::InnerRadius:
        if (Extension || Kind != K::Ring || Value < 0.f || Value > 10000.f)
        {
            break;
        }
        Parameters->InnerRadius = Value;
        return Validate(Error);
    case EAbilityConfigParameter::Width:
        if (Extension || Kind != K::Line || Value < 1.f || Value > 10000.f)
        {
            break;
        }
        Parameters->Width = Value;
        return Validate(Error);
    case EAbilityConfigParameter::HalfHeight:
        if (Extension || (Kind != K::Ring && Kind != K::Line) || Value < 1.f || Value > 2000.f)
        {
            break;
        }
        Parameters->ShapeHalfHeight = Value;
        return Validate(Error);
    case EAbilityConfigParameter::Speed:
        if (Value < 1.f || Value > 10000.f ||
            (Extension ? Extension->Behavior != EExtensionKind::Projectile : (!bFlight && Kind != K::Wave)))
        {
            break;
        }
        Parameters->Speed = Value;
        return Validate(Error);
    case EAbilityConfigParameter::HomingTurnRate:
        if (Extension || Kind != K::Homing || Value < 1.f || Value > 720.f)
        {
            break;
        }
        Parameters->HomingTurnRate = Value;
        return Validate(Error);
    case EAbilityConfigParameter::DamageFalloff:
        if (Value < 0.f || Value > 1.f ||
            (Extension ? Extension->Behavior != EExtensionKind::Chain
                       : (Kind != K::Piercing && Kind != K::Returning && Kind != K::Chain)))
        {
            break;
        }
        Parameters->DamageFalloff = Value;
        return Validate(Error);
    case EAbilityConfigParameter::TransferRadius:
        if (!Extension || Extension->Behavior != EExtensionKind::Attach || Value < 0.f || Value > 10000.f)
        {
            break;
        }
        Extension->TransferRadius = Value;
        return Validate(Error);
    case EAbilityConfigParameter::MaxTransfers:
        if (!Extension || Extension->Behavior != EExtensionKind::Attach || Value < 0.f || Value > 16.f ||
            Value != FMath::RoundToFloat(Value))
        {
            break;
        }
        Extension->MaxTransfers = static_cast<int32>(Value);
        return Validate(Error);
    case EAbilityConfigParameter::StackThreshold:
        if (!Extension || Extension->Behavior != EExtensionKind::Stack || Value < 1.f || Value > 64.f ||
            Value != FMath::RoundToFloat(Value))
        {
            break;
        }
        Extension->StackThreshold = static_cast<int32>(Value);
        return Validate(Error);
    case EAbilityConfigParameter::StackLifetime:
        if (!Extension || Extension->Behavior != EExtensionKind::Stack || Value <= 0.f || Value > 60.f)
        {
            break;
        }
        Extension->StackLifetime = Value;
        return Validate(Error);
    case EAbilityConfigParameter::DetonationDelay:
        if (!Extension || Extension->Behavior != EExtensionKind::Stack || Value < 0.f || Value > 60.f)
        {
            break;
        }
        Extension->DetonationDelay = Value;
        return Validate(Error);
    case EAbilityConfigParameter::MoveDistance:
        if (!Extension || Extension->Behavior != EExtensionKind::Displace || Value < 0.f || Value > 10000.f)
        {
            break;
        }
        Extension->MoveDistance = Value;
        return Validate(Error);
    case EAbilityConfigParameter::SafeDistance:
        if (!Extension || Extension->Behavior != EExtensionKind::Displace || Value < 0.f || Value > 10000.f)
        {
            break;
        }
        Extension->SafeDistance = Value;
        return Validate(Error);
    default:
        break;
    }
    Error = TEXT("This scope does not consume that typed axis or its value exceeds the registered bound.");
    return false;
}

bool UAbilityPrototypeScopeNodeConfig::SupportsOriginalNode(FName OriginalType, FName OriginalProfile,
                                                            FString& Error) const
{
    const FName ExpectedProfile = OriginalProfile == TEXT("LegacyActor.v1")   ? FName(TEXT("PrototypeFine.Actor.v1"))
                                  : OriginalProfile == TEXT("LegacyMixed.v1") ? FName(TEXT("PrototypeFine.Mixed.v1"))
                                  : OriginalProfile == TEXT("LegacyExtension.v1")
                                      ? FName(TEXT("PrototypeFine.Extension.v1"))
                                      : NAME_None;
    if (!Validate(Error) || ExpectedProfile.IsNone() || Profile != ExpectedProfile ||
        AbilityLegacyNodes::TypeId(*Parameters, OriginalProfile) != OriginalType)
    {
        Error = TEXT("Scope does not prove this original macro type/profile from its real parameters.");
        return false;
    }
    Error.Reset();
    return true;
}

int32 UAbilityPrototypeScopeNodeConfig::MaximumOriginalLifetimeNodes() const
{
    const auto* Extension = Cast<UExtensionNode>(Parameters);
    return Extension && Extension->Behavior == EExtensionKind::Stack ? 2 : 1;
}

bool UAbilityPrototypeScopeNodeConfig::SupportsOriginalLifetime(FName OriginalType, FName OriginalProfile,
                                                                FName LifetimeType, FString& Error) const
{
    if (!SupportsOriginalNode(OriginalType, OriginalProfile, Error))
    {
        return false;
    }
    FName Expected;
    if (const auto* Extension = Cast<UExtensionNode>(Parameters))
    {
        switch (Extension->Behavior)
        {
        case EExtensionKind::Attach:
            Expected = TEXT("State.PrototypeAttach");
            break;
        case EExtensionKind::Stack:
            Expected =
                LifetimeType == TEXT("State.PrototypeStackImpact") ? LifetimeType : FName(TEXT("State.PrototypeStack"));
            break;
        case EExtensionKind::Displace:
            Expected = Extension->bSingleTarget ? TEXT("Movement.PrototypeTargetDisplace")
                                                : TEXT("Movement.PrototypeAreaDisplace");
            break;
        case EExtensionKind::Projectile:
            Expected = TEXT("Delivery.ExtensionProjectile");
            break;
        case EExtensionKind::Chain:
            Expected = TEXT("Control.ExtensionSequence");
            break;
        case EExtensionKind::Burst:
            Expected = TEXT("Control.ExtensionBurst");
            break;
        case EExtensionKind::Delay:
            Expected = TEXT("Control.ExtensionDelay");
            break;
        case EExtensionKind::Surface:
            Expected = TEXT("State.ExtensionSurface");
            break;
        }
    }
    else
    {
        using K = EAbilityPrototypeKind;
        switch (Parameters->Kind)
        {
        case K::Projectile:
        case K::Homing:
        case K::Piercing:
        case K::Returning:
            Expected = TEXT("Delivery.PrototypeFlight");
            break;
        case K::Multishot:
            Expected = bSingleProjectile ? TEXT("Delivery.PrototypeFlight") : TEXT("Distribution.PrototypeMultishot");
            break;
        case K::Chain:
            Expected = TEXT("Control.TargetSequence");
            break;
        case K::Beam:
            Expected = TEXT("Control.TargetChannel");
            break;
        case K::Wave:
            Expected = TEXT("Spatial.PrototypeWave");
            break;
        case K::Emitter:
            Expected = TEXT("Control.PrototypeEmitter");
            break;
        case K::Orbit:
            Expected = TEXT("Control.PrototypeOrbitInventory");
            break;
        case K::Surface:
            Expected = TEXT("Control.PrototypeSurfaceClock");
            break;
        case K::Delay:
            Expected = TEXT("Control.PrototypeDelay");
            break;
        case K::Trap:
            Expected = TEXT("Control.PrototypeTrap");
            break;
        case K::Field:
        {
            const auto* Field = Cast<UBatch6FieldPrototype>(Parameters);
            Expected =
                Field && Field->bFollowCasterToggle && Profile == TEXT("PrototypeFine.Mixed.v1") && LogicalDepth == 0
                    ? TEXT("Control.PrototypeFollowField")
                    : TEXT("Control.FieldCadence");
            break;
        }
        default:
            Expected = TEXT("Control.PrototypeLifetime");
            break;
        }
    }
    if (LifetimeType != Expected)
    {
        Error = TEXT("Helper is not the registered main lifetime of this original macro.");
        return false;
    }
    Error.Reset();
    return true;
}

bool UAbilityPrototypeScopeNodeExecution::Start(UAbilityGraphInstance& Owner, const FGuid& Id,
                                                const UAbilityNodeConfig& Configuration,
                                                const FAbilityNodeInput& InInput, FString& Error)
{
    if (!Super::Start(Owner, Id, Configuration, InInput, Error))
    {
        return false;
    }
    const auto* Settings = Cast<UAbilityPrototypeScopeNodeConfig>(&Configuration);
    auto* Context = Cast<UPrototypeCastContext>(Input.Context.Get());
    if (!Settings || !Settings->Validate(Error) || !Context || !Context->CaptureSourceLife(Error) ||
        !Context->ValidateSourceLife())
    {
        Error = TEXT("Prototype scope requires its captured original cast context and validated parameters.");
        return false;
    }
    const auto& Source = Context->GetSourceLife();
    if (Source.World != Input.SourceToken.World || Source.Avatar != Input.SourceToken.Avatar ||
        Source.ASC != Input.SourceToken.ASC || Source.EnemyLease != Input.SourceToken.EnemyLease ||
        Source.DungeonRunId != Input.SourceToken.DungeonRunId)
    {
        Error = TEXT("Prototype scope cannot use a different cast context/source life.");
        return false;
    }
    auto Reject = [&](const TCHAR* Reason)
    {
        if (Settings->LogicalRejectionPolicy == EAbilityPrototypeLogicalRejectionPolicy::FailExecution)
        {
            // 原实际Legacy节点获取失败会Stop(Failed)，同时封闭共享Context；不能留下早先弹体晚命中。
            Context->bCancelled = true;
            Error = Reason;
            return false;
        }
        Owner.FinishInvocation(Id);
        return true;
    };
    auto Payload = Input;
    if (const auto* Extension = Cast<UExtensionNode>(Settings->Parameters);
        Extension && !AbilityExtensionFlowNodes::PrepareSurfaceEntry(*Context, *Extension, Payload.Origin))
    {
        return Reject(TEXT("Prototype surface projection or shared surface quota rejected admission."));
    }
    if (!Settings->Parameters->IsA<UExtensionNode>() && Settings->Parameters->Kind == EAbilityPrototypeKind::Surface &&
        !APrototypeRuntime::ProjectSurfaceToGround(Context->Caster.Get(), Payload.Origin, Payload.Origin))
    {
        return Reject(TEXT("Ordinary prototype surface ground projection rejected admission."));
    }
    if (Context->Spawned >= Owner.GetLogicalInvocationLimit() || !Context->ReserveSpawn(Settings->LogicalDepth))
    {
        return Reject(TEXT("Prototype logical spawn quota or original depth rejected admission."));
    }
    auto* Frame = UAbilityPrototypeParameterFrame::Create(
        this, *Settings->Parameters, Context->Level, Settings->Profile, Settings->StableReference,
        Owner.GetExecutionId(), Input.SourceToken, Error, Settings->LogicalDepth, Settings->bSingleProjectile, &Input);
    if (!Frame)
    {
        return false;
    }
    Payload.ParameterFrame = Frame;
    Payload.Level = Frame->GetLevel();
    // 子原型的固定伤害从自己的帧重新开始；根GE、来源、等级和击退模板沿同一输入保留。
    Payload.DamageParams.BaseDamage = Frame->GetDamage();
    Payload.DamageParams.DamageType = Frame->GetParameters().DamageType;
    Payload.DamageScale = 1.f;
    Payload.DebuffDamageScale = 1.f;
    return Owner.Publish(Id, TEXT("Ready"), Payload);
}

const UAbilityPrototypeParameterFrame* AbilityPrototypeParameterNodes::ResolveFrame(const UAbilityGraphInstance& Owner,
                                                                                    const FAbilityNodeInput& Input,
                                                                                    FString& Error)
{
    const auto* Frame = Cast<UAbilityPrototypeParameterFrame>(Input.ParameterFrame.Get());
    if (!Frame || !Frame->BelongsTo(Owner.GetExecutionId(), Input.SourceToken))
    {
        Error = TEXT("Fine prototype helper requires this root's immutable parameter frame.");
        return nullptr;
    }
    Error.Reset();
    return Frame;
}

void AbilityPrototypeParameterNodes::Register(FAbilityNodeRegistry& Registry)
{
    FAbilityNodeDescriptor Scope;
    Scope.TypeId = TEXT("Scope.Prototype");
    Scope.ConfigClass = UAbilityPrototypeScopeNodeConfig::StaticClass();
    Scope.ExecutionClass = UAbilityPrototypeScopeNodeExecution::StaticClass();
    Scope.InputPayload = EAbilityPortPayload::Origin;
    Scope.Lifetime = EAbilityExecutionLifetime::IndependentCastEffect;
    Scope.bSupportsActorTargets = Scope.bSupportsMassTargets = Scope.bSupportsSnapshotTargets = true;
    Scope.bPrototypeCompatibilityRoot = Scope.bPrototypeCompatibilitySafe = Scope.bPrototypeLogicalEntry = true;
    // Scope只冻结UObject参数；它不会启动Driver或创建表现/地形Actor。
    Scope.bRuntimeFiniteSafe = true;
    Scope.Ports = {{TEXT("Ready"), EAbilityPortPayload::Origin, true}};
    FAbilityNodeDescriptor Query = Scope;
    Query.TypeId = TEXT("Query.PrototypeShape");
    Query.ConfigClass = UAbilityPrototypeQueryNodeConfig::StaticClass();
    Query.ExecutionClass = UAbilityQueryBatchNodeExecution::StaticClass();
    Query.bPrototypeCompatibilityRoot = Query.bPrototypeLogicalEntry = false;
    Query.bRuntimeFiniteSafe = true; // 唯一帧查询不生成Actor，完整集合仍由根后继预算消费。
    Query.Ports = {{TEXT("Completed"), EAbilityPortPayload::TargetSet, true}};
    FAbilityNodeDescriptor Damage = Query;
    Damage.TypeId = TEXT("Effect.PrototypeDamageBatch");
    Damage.ConfigClass = UAbilityPrototypeDamageNodeConfig::StaticClass();
    Damage.ExecutionClass = UAbilityDamageBatchNodeExecution::StaticClass();
    Damage.InputPayload = EAbilityPortPayload::TargetSet;
    Damage.bIsAsync = true;
    Damage.bRuntimeFiniteSafe = true; // 批接触/延续组均使用同图预算，没有内部Actor生成循环。
    Damage.Ports = {{TEXT("Contact"), EAbilityPortPayload::DamageResult, false},
                    {TEXT("DamageResolved"), EAbilityPortPayload::DamageResult, false},
                    {TEXT("Completed"), EAbilityPortPayload::Origin, true}};
    for (const auto& Descriptor : {Scope, Query, Damage})
    {
        if (!Registry.Find(Descriptor.TypeId))
        {
            FString Error;
            ensureAlwaysMsgf(Registry.Register(Descriptor, Error), TEXT("Prototype parameter registration: %s"),
                             *Error);
        }
    }
    AbilityPrototypeExtensionNodes::Register(Registry);
    AbilityPrototypeContactNodes::Register(Registry);
    AbilityNormalizationNodes::Register(Registry);
}
