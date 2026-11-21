// 调用链：开发/运行时配方 -> 细节点默认复原；旧零修正入口仍使用独立兼容编译器。
#include "AbilitySystem/Unified/AbilityPrototypeFineCompiler.h"
#include "AbilitySystem/Unified/AbilityGraphDefinition.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeParameterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypePhaseNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeExtensionNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityExtensionFlowNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeContactNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypePresentationNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeTargetNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeFlightNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeDistributionNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeEmitterNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeWaveNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeFollowFieldNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeSurfaceNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityPrototypeOrbitNodes.h"
#include "AbilitySystem/Unified/Nodes/AbilityInputDamageNode.h"
#include "AbilitySystem/Prototypes/AbilityPrototype.h"
#include "AbilitySystem/PrototypeExtensions/ExtensionNode.h"
#include "AbilitySystem/Batch6/Batch6FieldAbility.h"
#include "Misc/SecureHash.h"

namespace
{
    /** 显式接受旧调用方 profile，输出始终使用经过注册的细节点合同。 */
    FName FineProfile(FName Profile)
    {
        if (Profile == TEXT("LegacyActor.v1") || Profile == TEXT("PrototypeFine.Actor.v1"))
        {
            return TEXT("PrototypeFine.Actor.v1");
        }
        if (Profile == TEXT("LegacyMixed.v1") || Profile == TEXT("PrototypeFine.Mixed.v1"))
        {
            return TEXT("PrototypeFine.Mixed.v1");
        }
        if (Profile == TEXT("LegacyExtension.v1") || Profile == TEXT("PrototypeFine.Extension.v1"))
        {
            return TEXT("PrototypeFine.Extension.v1");
        }
        return NAME_None;
    }

    /** 稳定配置身份+展开深度+片段职责，不使用本次随机 GUID 或内存地址。 */
    FGuid Identity(const UAbilityPrototype& Source, int32 Depth, FName Part)
    {
        const auto Key = FString::Printf(TEXT("%s|%d|%s"), *Source.GetPathName(), Depth, *Part.ToString());
        FGuid Result;
        FGuid::ParseExact(FMD5::HashAnsiString(*Key), EGuidFormats::Digits, Result);
        return Result;
    }

    struct FCompiler
    {
        UAbilityGraphDefinition& Graph;
        FString& Error;
        TMap<FGuid, FGuid> Added;
        bool bExpandLinks = true;
        bool bInsideFragment = false;
        FGuid StableNamespace;
        FName ProfileOverride;
        UAbilityPrototype* PresentationOverride = nullptr;

        FName Profile() const
        {
            return ProfileOverride.IsNone() ? Graph.CompatibilityProfile : ProfileOverride;
        }

        FGuid Key(const UAbilityPrototype& Source, int32 Depth, FName Part) const
        {
            if (!StableNamespace.IsValid())
            {
                return Identity(Source, Depth, Part);
            }
            FGuid Result;
            const auto Text = FString::Printf(TEXT("%s|%d|%s"), *StableNamespace.ToString(EGuidFormats::Digits), Depth,
                                              *Part.ToString());
            FGuid::ParseExact(FMD5::HashAnsiString(*Text), EGuidFormats::Digits, Result);
            return Result;
        }

        /** 子图展开有静态上限，与实际逻辑生成次数独立；错误时不返回半张定义。 */
        FGuid Add(const UAbilityPrototype& Source, int32 Depth, FName Part, FName Type, UAbilityNodeConfig* Config)
        {
            if (Graph.Nodes.Num() >= 512)
            {
                Error = TEXT("Fine prototype topology exceeds 512 definition nodes.");
                return {};
            }
            FAbilityGraphNode Node;
            Node.NodeId = Key(Source, Depth, Part);
            Node.TypeId = Type;
            Node.Config = Config;
            Graph.Nodes.Add(Node);
            return Node.NodeId;
        }

        void Connect(FGuid From, FName Port, FGuid To)
        {
            if (From.IsValid() && To.IsValid())
            {
                FAbilityGraphEdge Edge;
                Edge.From = From;
                Edge.To = To;
                Edge.OutputPort = Port;
                Graph.Edges.Add(Edge);
            }
        }

        /** 逻辑片段唯一参数所有者；后续查询、效果和状态只取得该 Scope 生成的帧。 */
        FGuid Scope(const UAbilityPrototype& Source, int32 Depth, bool bSingle = false)
        {
            auto* Config = NewObject<UAbilityPrototypeScopeNodeConfig>(&Graph);
            Config->Parameters = DuplicateObject<UAbilityPrototype>(&Source, Config);
            Config->StableReference = bExpandLinks ? const_cast<UAbilityPrototype*>(&Source) : PresentationOverride;
            Config->Profile = Profile();
            Config->LogicalDepth = Depth;
            Config->bSingleProjectile = bSingle;
            Config->LogicalRejectionPolicy = EAbilityPrototypeLogicalRejectionPolicy::FailExecution;
            if (!Config->Validate(Error))
            {
                return {};
            }
            const FGuid Entry =
                Add(Source, Depth, bSingle ? TEXT("SingleScope") : TEXT("Scope"), TEXT("Scope.Prototype"), Config);
            Added.Add(Entry, Entry);
            return Entry;
        }

        /** 每目标同步接触图可独立替换；原 OnHit 仍是批末排队的另一条逻辑分支。 */
        void Contact(const UAbilityPrototype& Source, int32 Depth, FGuid Damage)
        {
            const bool bMixed = Profile() == TEXT("PrototypeFine.Mixed.v1");
            const bool bExtension = Profile() == TEXT("PrototypeFine.Extension.v1");
            const bool bActorWave =
                Profile() == TEXT("PrototypeFine.Actor.v1") && Source.Kind == EAbilityPrototypeKind::Wave;
            if (!bMixed && !bExtension && !bActorWave)
            {
                return;
            }
            FGuid Previous = Damage;
            FName Port(TEXT("Contact"));
            if (const auto* Field = Cast<UBatch6FieldPrototype>(&Source);
                bMixed && Field && Field->SlowMultiplier < 1.f)
            {
                const FGuid Slow = Add(Source, Depth, TEXT("ContactSlow"), TEXT("Status.PrototypeSlow"),
                                       NewObject<UAbilityPrototypeSlowNodeConfig>(&Graph));
                Connect(Previous, Port, Slow);
                Previous = Slow;
                Port = TEXT("Completed");
            }
            if (Source.ImpactVFX)
            {
                const FGuid Impact = Add(Source, Depth, TEXT("ContactImpact"), TEXT("Presentation.PrototypeImpact"),
                                         NewObject<UAbilityPrototypeImpactNodeConfig>(&Graph));
                Connect(Previous, Port, Impact);
            }
        }

        /** Extension 的逻辑/状态继续复用共享服务；不把父类 Kind 误当作实际 Behavior。 */
        FGuid Extension(const UExtensionNode& Source, int32 Depth)
        {
            if (!Source.ValidateGraph())
            {
                Error = TEXT("Fine extension requires the original validated graph contract.");
                return {};
            }
            if (Source.Behavior != EExtensionKind::Burst && Source.Behavior != EExtensionKind::Delay &&
                Source.Behavior != EExtensionKind::Attach && Source.Behavior != EExtensionKind::Stack &&
                Source.Behavior != EExtensionKind::Displace && Source.Behavior != EExtensionKind::Projectile &&
                Source.Behavior != EExtensionKind::Chain && Source.Behavior != EExtensionKind::Surface)
            {
                Error = TEXT("This extension behavior still requires its separate registered fine fragments.");
                return {};
            }
            const FGuid Entry = Scope(Source, Depth);
            if (!Entry.IsValid())
            {
                return {};
            }
            if (Source.Behavior == EExtensionKind::Burst || Source.Behavior == EExtensionKind::Delay)
            {
                auto* Time = NewObject<UAbilityPrototypePhaseNodeConfig>(&Graph);
                Time->Phase = Source.Behavior == EExtensionKind::Burst ? EAbilityPrototypePhase::ExtensionBurst
                                                                       : EAbilityPrototypePhase::ExtensionDelay;
                const FGuid Clock =
                    Add(Source, Depth, TEXT("Phase"), AbilityPrototypePhaseNodes::TypeId(Time->Phase), Time);
                Connect(Entry, TEXT("Ready"), Clock);
                if (Source.Behavior == EExtensionKind::Burst)
                {
                    const FGuid Query = Add(Source, Depth, TEXT("Query"), TEXT("Query.PrototypeShape"),
                                            NewObject<UAbilityPrototypeQueryNodeConfig>(&Graph));
                    const FGuid Damage = Add(Source, Depth, TEXT("Damage"), TEXT("Effect.PrototypeDamageBatch"),
                                             NewObject<UAbilityPrototypeDamageNodeConfig>(&Graph));
                    Connect(Clock, TEXT("Action"), Query);
                    Connect(Query, TEXT("Completed"), Damage);
                    Contact(Source, Depth, Damage);
                    Connect(Damage, TEXT("DamageResolved"), Fragment(Source.HitChild, Depth + 1));
                    const FGuid End = Add(Source, Depth, TEXT("End"), TEXT("Control.PrototypeEnd"),
                                          NewObject<UAbilityPrototypeEndNodeConfig>(&Graph));
                    Connect(Damage, TEXT("Completed"), End);
                }
                Connect(Clock, TEXT("Expired"), Fragment(Source.CompleteChild, Depth + 1));
                return Entry;
            }
            if (Source.Behavior == EExtensionKind::Projectile || Source.Behavior == EExtensionKind::Chain)
            {
                const bool bChain = Source.Behavior == EExtensionKind::Chain;
                UAbilityNodeConfig* Control =
                    bChain ? static_cast<UAbilityNodeConfig*>(NewObject<UAbilityExtensionSequenceNodeConfig>(&Graph))
                           : NewObject<UAbilityExtensionProjectileNodeConfig>(&Graph);
                const FGuid Flow =
                    Add(Source, Depth, bChain ? TEXT("ExtensionSequence") : TEXT("ExtensionFlight"),
                        bChain ? TEXT("Control.ExtensionSequence") : TEXT("Delivery.ExtensionProjectile"), Control);
                auto* Payload = NewObject<UAbilityPrototypeDamageNodeConfig>(&Graph);
                Payload->bConsumeInputScales = true;
                const FGuid Damage = Add(Source, Depth, TEXT("Damage"), TEXT("Effect.PrototypeDamageBatch"), Payload);
                const FGuid End = Add(Source, Depth, TEXT("ContactEnd"), TEXT("Control.ExtensionContactEnd"),
                                      NewObject<UAbilityExtensionContactEndNodeConfig>(&Graph));
                Connect(Entry, TEXT("Ready"), Flow);
                Connect(Flow, TEXT("Contact"), Damage);
                Contact(Source, Depth, Damage);
                Connect(Damage, TEXT("DamageResolved"), Fragment(Source.HitChild, Depth + 1));
                Connect(Damage, TEXT("Completed"), End);
                Connect(Flow, TEXT("Expired"), Fragment(Source.CompleteChild, Depth + 1));
                return Entry;
            }
            if (Source.Behavior == EExtensionKind::Surface)
            {
                const FGuid Surface = Add(Source, Depth, TEXT("Surface"), TEXT("State.ExtensionSurface"),
                                          NewObject<UAbilityExtensionSurfaceNodeConfig>(&Graph));
                Connect(Entry, TEXT("Ready"), Surface);
                Connect(Surface, TEXT("Expired"), Fragment(Source.CompleteChild, Depth + 1));
                return Entry;
            }
            if (Source.Behavior == EExtensionKind::Displace && !Source.bSingleTarget)
            {
                const FGuid Area = Add(Source, Depth, TEXT("AreaMove"), TEXT("Movement.PrototypeAreaDisplace"),
                                       NewObject<UAbilityPrototypeAreaDisplaceNodeConfig>(&Graph));
                Connect(Entry, TEXT("Ready"), Area);
                Connect(Area, TEXT("Completed"), Fragment(Source.CompleteChild, Depth + 1));
                return Entry;
            }
            const FGuid Live = Add(Source, Depth, TEXT("Live"), TEXT("Target.PrototypeLive"),
                                   NewObject<UAbilityPrototypeLiveNodeConfig>(&Graph));
            Connect(Entry, TEXT("Ready"), Live);
            if (Source.Behavior == EExtensionKind::Attach)
            {
                const FGuid Attach = Add(Source, Depth, TEXT("Attach"), TEXT("State.PrototypeAttach"),
                                         NewObject<UAbilityPrototypeAttachNodeConfig>(&Graph));
                Connect(Live, TEXT("TargetFound"), Attach);
                Connect(Attach, TEXT("Period"), Fragment(Source.PeriodChild, Depth + 1));
                Connect(Attach, TEXT("Completed"), Fragment(Source.CompleteChild, Depth + 1));
            }
            else if (Source.Behavior == EExtensionKind::Stack)
            {
                const FGuid Payload = Fragment(Source.CompleteChild, Depth + 1);
                for (const bool bHistorical : {false, true})
                {
                    const FGuid Stack =
                        Add(Source, Depth, bHistorical ? TEXT("StackImpact") : TEXT("Stack"),
                            bHistorical ? TEXT("State.PrototypeStackImpact") : TEXT("State.PrototypeStack"),
                            NewObject<UAbilityPrototypeStackNodeConfig>(&Graph));
                    Connect(Live, bHistorical ? TEXT("Confirmed") : TEXT("TargetFound"), Stack);
                    Connect(Stack, TEXT("Threshold"), Payload);
                }
            }
            else
            {
                const FGuid Move = Add(Source, Depth, TEXT("Move"), TEXT("Movement.PrototypeTargetDisplace"),
                                       NewObject<UAbilityPrototypeDisplaceNodeConfig>(&Graph));
                Connect(Live, TEXT("TargetFound"), Move);
                Connect(Move, TEXT("Completed"), Fragment(Source.CompleteChild, Depth + 1));
            }
            return Entry;
        }

        /** 目标选择/维持节拍与 payload 分开；子 Scope 使用自己的固定伤害，父倍率不污染它。 */
        FGuid TargetFlow(const UAbilityPrototype& Source, int32 Depth)
        {
            const FGuid Entry = Scope(Source, Depth);
            if (!Entry.IsValid())
            {
                return {};
            }
            const bool bBeam = Source.Kind == EAbilityPrototypeKind::Beam;
            UAbilityNodeConfig* Control =
                bBeam ? static_cast<UAbilityNodeConfig*>(NewObject<UAbilityTargetChannelNodeConfig>(&Graph))
                      : NewObject<UAbilityTargetSequenceNodeConfig>(&Graph);
            const FGuid Sequence = Add(Source, Depth, bBeam ? TEXT("Channel") : TEXT("Sequence"),
                                       bBeam ? TEXT("Control.TargetChannel") : TEXT("Control.TargetSequence"), Control);
            auto* Payload = NewObject<UAbilityPrototypeDamageNodeConfig>(&Graph);
            Payload->bConsumeInputScales = true;
            const FGuid Damage = Add(Source, Depth, TEXT("Damage"), TEXT("Effect.PrototypeDamageBatch"), Payload);
            Connect(Entry, TEXT("Ready"), Sequence);
            Connect(Sequence, TEXT("Action"), Damage);
            Contact(Source, Depth, Damage);
            Connect(Damage, TEXT("DamageResolved"), Fragment(Source.OnHit, Depth + 1));
            if (bBeam)
            {
                Connect(Sequence, TEXT("Period"), Fragment(Source.OnPeriod, Depth + 1));
            }
            Connect(Sequence, TEXT("Expired"), Fragment(Source.OnExpire, Depth + 1));
            return Entry;
        }

        /** 原型飞行只拥有运动/碰撞；真实payload和各个逻辑hook继续是独立图节点。 */
        FGuid Flight(const UAbilityPrototype& Source, int32 Depth, bool bSingle = false)
        {
            const FGuid Entry = Scope(Source, Depth, bSingle);
            if (!Entry.IsValid())
            {
                return {};
            }
            const FGuid Delivery = Add(Source, Depth, TEXT("Flight"), TEXT("Delivery.PrototypeFlight"),
                                       NewObject<UAbilityPrototypeFlightNodeConfig>(&Graph));
            auto* Payload = NewObject<UAbilityPrototypeDamageNodeConfig>(&Graph);
            Payload->bConsumeInputScales = true;
            const FGuid Damage = Add(Source, Depth, TEXT("Damage"), TEXT("Effect.PrototypeDamageBatch"), Payload);
            Connect(Entry, TEXT("Ready"), Delivery);
            Connect(Delivery, TEXT("Contact"), Damage);
            Contact(Source, Depth, Damage);
            Connect(Damage, TEXT("DamageResolved"), Fragment(Source.OnHit, Depth + 1));
            Connect(Delivery, TEXT("Period"), Fragment(Source.OnPeriod, Depth + 1));
            Connect(Delivery, TEXT("Expired"), Fragment(Source.OnExpire, Depth + 1));
            return Entry;
        }

        /** 扇射容器只分布方向；每个单弹同深度独占Scope/预算，容器不补发自己的命中或到期。 */
        FGuid Multishot(const UAbilityPrototype& Source, int32 Depth)
        {
            const FGuid Entry = Scope(Source, Depth);
            if (!Entry.IsValid())
            {
                return {};
            }
            const FGuid Fan = Add(Source, Depth, TEXT("Fan"), TEXT("Distribution.PrototypeMultishot"),
                                  NewObject<UAbilityPrototypeMultishotNodeConfig>(&Graph));
            Connect(Entry, TEXT("Ready"), Fan);
            Connect(Fan, TEXT("Pellet"), Fragment(&Source, Depth, true));
            return Entry;
        }

        /** 发射容器只调度已选原生命；每个OnPeriod孩子独立Scope，容器自身不结算伤害。 */
        FGuid Emitter(const UAbilityPrototype& Source, int32 Depth)
        {
            const FGuid Entry = Scope(Source, Depth);
            if (!Entry.IsValid())
            {
                return {};
            }
            const FGuid Clock = Add(Source, Depth, TEXT("Emitter"), TEXT("Control.PrototypeEmitter"),
                                    NewObject<UAbilityPrototypeEmitterNodeConfig>(&Graph));
            Connect(Entry, TEXT("Ready"), Clock);
            Connect(Clock, TEXT("Launch"), Fragment(Source.OnPeriod, Depth + 1));
            Connect(Clock, TEXT("Expired"), Fragment(Source.OnExpire, Depth + 1));
            return Entry;
        }

        /** 库存、实际投递、唯一伤害owner和逻辑hooks分别连接；原参数和forces profile由实际接触携带。 */
        FGuid Orbit(const UAbilityPrototype& Source, int32 Depth)
        {
            const FGuid Entry = Scope(Source, Depth);
            if (!Entry.IsValid())
            {
                return {};
            }
            const FGuid Inventory = Add(Source, Depth, TEXT("OrbitInventory"), TEXT("Control.PrototypeOrbitInventory"),
                                        NewObject<UAbilityPrototypeOrbitInventoryNodeConfig>(&Graph));
            const FGuid Delivery = Add(Source, Depth, TEXT("OrbitDelivery"), TEXT("Delivery.PrototypeOrbit"),
                                       NewObject<UAbilityPrototypeOrbitDeliveryNodeConfig>(&Graph));
            auto* Payload = NewObject<UAbilityInputDamageNodeConfig>(&Graph);
            Payload->bCaptureLocationAfterDamage = Profile() == TEXT("PrototypeFine.Actor.v1");
            const FGuid Damage = Add(Source, Depth, TEXT("OrbitDamage"), TEXT("Effect.InputDamage"), Payload);
            Connect(Entry, TEXT("Ready"), Inventory);
            Connect(Inventory, TEXT("Launch"), Delivery);
            Connect(Delivery, TEXT("Contact"), Damage);
            if (Profile() == TEXT("PrototypeFine.Mixed.v1") && Source.ImpactVFX)
            {
                const FGuid Impact =
                    Add(Source, Depth, TEXT("OrbitContactImpact"), TEXT("Presentation.PrototypeOrbitImpact"),
                        NewObject<UAbilityPrototypeImpactNodeConfig>(&Graph));
                Connect(Damage, TEXT("Completed"), Impact);
            }
            Connect(Delivery, TEXT("OnHit"), Fragment(Source.OnHit, Depth + 1));
            Connect(Inventory, TEXT("Period"), Fragment(Source.OnPeriod, Depth + 1));
            Connect(Inventory, TEXT("Expired"), Fragment(Source.OnExpire, Depth + 1));
            return Entry;
        }

        /** 波形只产出本步原生命集合，命中/状态/表现由显式批接触子图拥有。 */
        FGuid Wave(const UAbilityPrototype& Source, int32 Depth)
        {
            const FGuid Entry = Scope(Source, Depth);
            if (!Entry.IsValid())
            {
                return {};
            }
            const FGuid Spatial = Add(Source, Depth, TEXT("Wave"), TEXT("Spatial.PrototypeWave"),
                                      NewObject<UAbilityPrototypeWaveNodeConfig>(&Graph));
            const FGuid Damage = Add(Source, Depth, TEXT("Damage"), TEXT("Effect.PrototypeDamageBatch"),
                                     NewObject<UAbilityPrototypeDamageNodeConfig>(&Graph));
            Connect(Entry, TEXT("Ready"), Spatial);
            Connect(Spatial, TEXT("Contact"), Damage);
            Contact(Source, Depth, Damage);
            Connect(Damage, TEXT("DamageResolved"), Fragment(Source.OnHit, Depth + 1));
            Connect(Spatial, TEXT("Period"), Fragment(Source.OnPeriod, Depth + 1));
            Connect(Spatial, TEXT("Expired"), Fragment(Source.OnExpire, Depth + 1));
            return Entry;
        }

        /** 仅Mixed根随动Field使用持续时钟；场位置/费用与Query/Payload分离，无正常OnExpire。 */
        FGuid FollowField(const UAbilityPrototype& Source, int32 Depth)
        {
            const FGuid Entry = Scope(Source, Depth);
            if (!Entry.IsValid())
            {
                return {};
            }
            const FGuid Clock = Add(Source, Depth, TEXT("Follow"), TEXT("Control.PrototypeFollowField"),
                                    NewObject<UAbilityPrototypeFollowFieldNodeConfig>(&Graph));
            const FGuid Query = Add(Source, Depth, TEXT("Query"), TEXT("Query.PrototypeShape"),
                                    NewObject<UAbilityPrototypeQueryNodeConfig>(&Graph));
            const FGuid Damage = Add(Source, Depth, TEXT("Damage"), TEXT("Effect.PrototypeDamageBatch"),
                                     NewObject<UAbilityPrototypeDamageNodeConfig>(&Graph));
            Connect(Entry, TEXT("Ready"), Clock);
            Connect(Clock, TEXT("Action"), Query);
            Connect(Query, TEXT("Completed"), Damage);
            Contact(Source, Depth, Damage);
            Connect(Damage, TEXT("DamageResolved"), Fragment(Source.OnHit, Depth + 1));
            Connect(Clock, TEXT("Period"), Fragment(Source.OnPeriod, Depth + 1));
            return Entry;
        }

        /** 地表资源独立取得/释放；先销毁准确Actor再进入正常到期孩子，外层时钟不拥有地表接触逻辑。 */
        FGuid Surface(const UAbilityPrototype& Source, int32 Depth)
        {
            const FGuid Entry = Scope(Source, Depth);
            if (!Entry.IsValid())
            {
                return {};
            }
            const FGuid Resource = Add(Source, Depth, TEXT("SurfaceResource"), TEXT("World.PrototypeSurface"),
                                       NewObject<UAbilityPrototypeSurfaceNodeConfig>(&Graph));
            auto* Settings = NewObject<UAbilityPrototypePhaseNodeConfig>(&Graph);
            Settings->Phase = EAbilityPrototypePhase::Surface;
            const FGuid Clock =
                Add(Source, Depth, TEXT("Phase"), AbilityPrototypePhaseNodes::TypeId(Settings->Phase), Settings);
            const FGuid Release = Add(Source, Depth, TEXT("SurfaceRelease"), TEXT("World.ReleasePrototypeSurface"),
                                      NewObject<UAbilityPrototypeSurfaceReleaseNodeConfig>(&Graph));
            Connect(Entry, TEXT("Ready"), Resource);
            Connect(Resource, TEXT("Acquired"), Clock);
            Connect(Clock, TEXT("Expired"), Release);
            Connect(Clock, TEXT("Period"), Fragment(Source.OnPeriod, Depth + 1));
            Connect(Release, TEXT("Released"), Fragment(Source.OnExpire, Depth + 1));
            return Entry;
        }

        /** 每个片段显式连接参数、时序、查询、效果及 hooks；绝不回退成完整旧 Driver 宏。 */
        FGuid Fragment(const UAbilityPrototype* Source, int32 Depth, bool bSingle = false)
        {
            if (!Source || Depth > 3 || !Error.IsEmpty() || (!bExpandLinks && bInsideFragment))
            {
                return {};
            }
            TGuardValue<bool> Guard(bInsideFragment, true);
            const FGuid ScopeId = Key(*Source, Depth, bSingle ? TEXT("SingleScope") : TEXT("Scope"));
            if (const auto* Existing = Added.Find(ScopeId))
            {
                return *Existing;
            }
            if (const auto* ExtensionSource = Cast<UExtensionNode>(Source))
            {
                return Extension(*ExtensionSource, Depth);
            }
            if (Source->Kind == EAbilityPrototypeKind::Chain || Source->Kind == EAbilityPrototypeKind::Beam)
            {
                return TargetFlow(*Source, Depth);
            }
            if (Source->Kind == EAbilityPrototypeKind::Multishot)
            {
                return bSingle ? Flight(*Source, Depth, true) : Multishot(*Source, Depth);
            }
            if (Source->Kind == EAbilityPrototypeKind::Emitter)
            {
                return Emitter(*Source, Depth);
            }
            if (Source->Kind == EAbilityPrototypeKind::Orbit)
            {
                return Orbit(*Source, Depth);
            }
            if (Source->Kind == EAbilityPrototypeKind::Surface)
            {
                return Surface(*Source, Depth);
            }
            if (Source->Kind == EAbilityPrototypeKind::Wave)
            {
                return Wave(*Source, Depth);
            }
            const auto* Follow = Cast<UBatch6FieldPrototype>(Source);
            if (Source->Kind == EAbilityPrototypeKind::Field && Follow && Follow->bFollowCasterToggle &&
                Profile() == TEXT("PrototypeFine.Mixed.v1") && Depth == 0)
            {
                return FollowField(*Source, Depth);
            }
            if (Source->Kind == EAbilityPrototypeKind::Projectile || Source->Kind == EAbilityPrototypeKind::Homing ||
                Source->Kind == EAbilityPrototypeKind::Piercing || Source->Kind == EAbilityPrototypeKind::Returning)
            {
                return Flight(*Source, Depth);
            }
            EAbilityPrototypePhase Phase;
            switch (Source->Kind)
            {
            case EAbilityPrototypeKind::Burst:
            case EAbilityPrototypeKind::Cone:
            case EAbilityPrototypeKind::Ring:
            case EAbilityPrototypeKind::Line:
                Phase = EAbilityPrototypePhase::Lifetime;
                break;
            case EAbilityPrototypeKind::Field:
                Phase = EAbilityPrototypePhase::Field;
                break;
            case EAbilityPrototypeKind::Trap:
                Phase = EAbilityPrototypePhase::Trap;
                break;
            case EAbilityPrototypeKind::Delay:
                Phase = EAbilityPrototypePhase::Delay;
                break;
            default:
                Error = FString::Printf(
                    TEXT("Fine compiler has no registered fragments for prototype kind %s."),
                    *StaticEnum<EAbilityPrototypeKind>()->GetNameStringByValue(static_cast<int64>(Source->Kind)));
                return {};
            }
            if (Source->IsA<UExtensionNode>() || !Source->IsValidConfiguration())
            {
                Error = TEXT("Fine finite fragments require valid parameters and a registered finite contract.");
                return {};
            }
            const FGuid Entry = Scope(*Source, Depth);
            auto* Time = NewObject<UAbilityPrototypePhaseNodeConfig>(&Graph);
            Time->Phase = Phase;
            const FGuid Clock = Add(*Source, Depth, TEXT("Phase"), AbilityPrototypePhaseNodes::TypeId(Phase), Time);
            Connect(Entry, TEXT("Ready"), Clock);
            FGuid Damage;
            if (Phase != EAbilityPrototypePhase::Delay)
            {
                const FGuid Query = Add(*Source, Depth, TEXT("Query"), TEXT("Query.PrototypeShape"),
                                        NewObject<UAbilityPrototypeQueryNodeConfig>(&Graph));
                Damage = Add(*Source, Depth, TEXT("Damage"), TEXT("Effect.PrototypeDamageBatch"),
                             NewObject<UAbilityPrototypeDamageNodeConfig>(&Graph));
                Connect(Clock, TEXT("Action"), Query);
                Connect(Query, TEXT("Completed"), Damage);
                Contact(*Source, Depth, Damage);
                if (Profile() == TEXT("PrototypeFine.Actor.v1") && Source->ImpactVFX &&
                    (Source->Kind == EAbilityPrototypeKind::Field || Source->Kind == EAbilityPrototypeKind::Trap))
                {
                    const FGuid Pulse =
                        Add(*Source, Depth, TEXT("PulsePresentation"), TEXT("Presentation.PrototypePulse"),
                            NewObject<UAbilityPrototypePulseNodeConfig>(&Graph));
                    Connect(Damage, TEXT("Completed"), Pulse);
                }
            }
            Connect(Damage, TEXT("DamageResolved"), Fragment(Source->OnHit, Depth + 1));
            Connect(Clock, TEXT("Period"), Fragment(Source->OnPeriod, Depth + 1));
            Connect(Clock, TEXT("Expired"), Fragment(Source->OnExpire, Depth + 1));
            return Entry;
        }
    };
} // namespace

UAbilityGraphDefinition* AbilityPrototypeFineCompiler::Compile(UObject* Outer, const UAbilityPrototype* Root,
                                                               FName Profile, FString& Error)
{
    Error.Reset();
    const FName CanonicalProfile = FineProfile(Profile);
    if (!Root || CanonicalProfile.IsNone())
    {
        Error = TEXT("Fine compilation requires a real prototype and an explicit supported source profile.");
        return nullptr;
    }
    auto* Graph = NewObject<UAbilityGraphDefinition>(Outer ? Outer : GetTransientPackage());
    Graph->SchemaVersion = 2;
    Graph->CompatibilityProfile = CanonicalProfile;
    Graph->BudgetPolicy = EAbilityGraphBudgetPolicy::PrototypeCompatibility;
    FCompiler Compiler{*Graph, Error};
    Graph->EntryNodeId = Compiler.Fragment(Root, 0);
    if (!Error.IsEmpty() || !Graph->EntryNodeId.IsValid())
    {
        return nullptr;
    }
    TArray<FString> Errors;
    if (!Graph->ValidateForAuthoring(Errors))
    {
        Error = FString::Join(Errors, TEXT("\n"));
        return nullptr;
    }
    return Graph;
}

bool AbilityPrototypeFineCompiler::AppendIsolated(UAbilityGraphDefinition& Destination,
                                                  const FAbilityFineFragmentSource& Source,
                                                  FAbilityFineFragmentBindings& Out, FString& Error)
{
    Out = {};
    Error.Reset();
    const FName Profile = FineProfile(Source.Profile);
    if (!Source.Parameters || Profile.IsNone() || !Source.StableNamespace.IsValid() || Source.LogicalDepth < 0 ||
        Source.LogicalDepth > 3)
    {
        Error = TEXT("Isolated expansion requires actual parameters, stable old identity and original metadata.");
        return false;
    }
    const int32 First = Destination.Nodes.Num();
    FCompiler Compiler{Destination, Error};
    Compiler.bExpandLinks = false;
    Compiler.StableNamespace = Source.StableNamespace;
    Compiler.ProfileOverride = Profile;
    Compiler.PresentationOverride = Source.Presentation;
    Out.Entry = Compiler.Fragment(Source.Parameters, Source.LogicalDepth, Source.bSingleProjectile);
    if (!Out.Entry.IsValid() || !Error.IsEmpty())
    {
        return false;
    }
    auto Bind = [&](FName LegacyPort, FName Type, FName Port, EAbilityPortPayload Payload)
    {
        for (int32 Index = First; Index < Destination.Nodes.Num(); ++Index)
        {
            if (Destination.Nodes[Index].TypeId == Type)
            {
                Out.Outputs.FindOrAdd(LegacyPort).Add({Destination.Nodes[Index].NodeId, Port, Payload});
            }
        }
    };
    Bind(TEXT("OnHit"), TEXT("Effect.PrototypeDamageBatch"), TEXT("DamageResolved"), EAbilityPortPayload::DamageResult);
    for (FName Type : {FName(TEXT("Control.TargetSequence")), FName(TEXT("Control.TargetChannel")),
                       FName(TEXT("Delivery.PrototypeFlight")), FName(TEXT("Control.ExtensionSequence")),
                       FName(TEXT("Delivery.ExtensionProjectile")), FName(TEXT("State.ExtensionSurface")),
                       FName(TEXT("Control.PrototypeEmitter")), FName(TEXT("Spatial.PrototypeWave"))})
    {
        Bind(TEXT("OnExpire"), Type, TEXT("Expired"), EAbilityPortPayload::Origin);
        Bind(TEXT("Completed"), Type, TEXT("Completed"), EAbilityPortPayload::Origin);
        if (Type == TEXT("Control.TargetChannel") || Type == TEXT("Delivery.PrototypeFlight") ||
            Type == TEXT("Spatial.PrototypeWave"))
        {
            Bind(TEXT("OnPeriod"), Type, TEXT("Period"), EAbilityPortPayload::Origin);
        }
    }
    Bind(TEXT("OnPeriod"), TEXT("Control.PrototypeEmitter"), TEXT("Launch"), EAbilityPortPayload::LiveTarget);
    Bind(TEXT("OnHit"), TEXT("Delivery.PrototypeOrbit"), TEXT("OnHit"), EAbilityPortPayload::ConfirmedImpact);
    Bind(TEXT("OnPeriod"), TEXT("Control.PrototypeOrbitInventory"), TEXT("Period"), EAbilityPortPayload::Origin);
    Bind(TEXT("OnExpire"), TEXT("Control.PrototypeOrbitInventory"), TEXT("Expired"), EAbilityPortPayload::Origin);
    Bind(TEXT("Completed"), TEXT("Control.PrototypeOrbitInventory"), TEXT("Completed"), EAbilityPortPayload::Origin);
    for (int32 Index = First; Index < Destination.Nodes.Num(); ++Index)
    {
        const auto& Node = Destination.Nodes[Index];
        if (Node.Config->IsA<UAbilityPrototypePhaseNodeConfig>())
        {
            if (CastChecked<UAbilityPrototypePhaseNodeConfig>(Node.Config)->Phase != EAbilityPrototypePhase::Surface)
            {
                Out.Outputs.FindOrAdd(TEXT("OnExpire"))
                    .Add({Node.NodeId, TEXT("Expired"), EAbilityPortPayload::Origin});
            }
            Out.Outputs.FindOrAdd(TEXT("OnPeriod")).Add({Node.NodeId, TEXT("Period"), EAbilityPortPayload::Origin});
            Out.Outputs.FindOrAdd(TEXT("Completed")).Add({Node.NodeId, TEXT("Completed"), EAbilityPortPayload::Origin});
        }
    }
    Bind(TEXT("OnExpire"), TEXT("World.ReleasePrototypeSurface"), TEXT("Released"), EAbilityPortPayload::Origin);
    Bind(TEXT("OnPeriod"), TEXT("Control.PrototypeFollowField"), TEXT("Period"), EAbilityPortPayload::Origin);
    Bind(TEXT("Completed"), TEXT("Control.PrototypeFollowField"), TEXT("Completed"), EAbilityPortPayload::Origin);
    Bind(TEXT("OnPeriod"), TEXT("State.PrototypeAttach"), TEXT("Period"), EAbilityPortPayload::LiveTarget);
    for (FName Type : {FName(TEXT("State.PrototypeAttach")), FName(TEXT("Movement.PrototypeTargetDisplace")),
                       FName(TEXT("Movement.PrototypeAreaDisplace"))})
    {
        Bind(TEXT("OnExpire"), Type, TEXT("Completed"), EAbilityPortPayload::Origin);
        Bind(TEXT("Completed"), Type, TEXT("Completed"), EAbilityPortPayload::Origin);
    }
    for (FName Port : {FName(TEXT("Rejected")), FName(TEXT("Replaced")), FName(TEXT("TargetLost"))})
    {
        Bind(TEXT("Completed"), TEXT("Movement.PrototypeTargetDisplace"), Port, EAbilityPortPayload::Origin);
    }
    for (FName Type : {FName(TEXT("State.PrototypeStack")), FName(TEXT("State.PrototypeStackImpact"))})
    {
        Bind(TEXT("OnExpire"), Type, TEXT("Threshold"), EAbilityPortPayload::Origin);
        Bind(TEXT("Completed"), Type, TEXT("Released"), EAbilityPortPayload::Origin);
    }
    Bind(TEXT("Multishot"), TEXT("Distribution.PrototypeMultishot"), TEXT("Pellet"), EAbilityPortPayload::Origin);
    Bind(TEXT("Completed"), TEXT("Distribution.PrototypeMultishot"), TEXT("Completed"), EAbilityPortPayload::Origin);
    return true;
}
