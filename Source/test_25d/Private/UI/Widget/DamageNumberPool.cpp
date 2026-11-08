/**
 * 调用链、网络与生命周期契约（2026-11-06）
 * 调用链：伤害反馈RPC到本地控制器 -> QueueActor合并或Show直接显示 -> UDamageTextComponent播放表现 -> DestroyComponent覆盖入口 -> Release复用。
 * 作用域与同步：UWorldSubsystem，每个世界一份，只为本地控制器显示；专用服务器不创建飘字表现。本池不复制，伤害值先由外部反馈通路送达，再由本地池显示，不能反向参与伤害结算。
 * 创建与持有：Show首次为玩家Spawn临时Anchor Actor，以Anchor为Outer NewObject文字组件并RegisterComponent；UPROPERTY数组保持组件与Anchor引用。待显示请求弱引用Player/Target，不延长战斗目标寿命。
 * 复用与退出：Release隐藏组件、停动画和Tick但保留对象；下次Show重置蓝图标量及渲染状态。Deinitialize先置bClosing并清定时器/队列，再解除组件回指、DestroyComponent、销毁Anchor并清空引用，避免销毁被再次转成入池。
 */
#include "UI/Widget/DamageNumberPool.h"
#include "UI/Widget/DamageTextComponent.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "UObject/UnrealType.h"

namespace
{
    // The existing floating-number Blueprint stores intermediate block/critical
    // values and output color in instance variables. Reuse must emulate a fresh
    // instance for these scalar values, while retaining the widget tree/animation.
    void ResetPresentationScalars(UObject* Object)
    {
        if (!Object)
            return;
        const UObject* Defaults = Object->GetClass()->GetDefaultObject();
        for (UClass* Class = Object->GetClass(); Cast<UBlueprintGeneratedClass>(Class); Class = Class->GetSuperClass())
            for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
            {
                const auto* Struct = CastField<FStructProperty>(*It);
                if (CastField<FNumericProperty>(*It) || CastField<FBoolProperty>(*It) ||
                    CastField<FEnumProperty>(*It) ||
                    (Struct && (Struct->Struct == TBaseStructure<FLinearColor>::Get() ||
                                Struct->Struct == FSlateColor::StaticStruct())))
                    It->CopyCompleteValue_InContainer(Object, Defaults);
            }
    }
} // namespace
static TAutoConsoleVariable<int32> CVarPool(TEXT("DamageNumbers.Pool"), 1,
                                            TEXT("0=create/destroy baseline; 1=reuse local cosmetic components."));
static TAutoConsoleVariable<int32> CVarCapacity(
    TEXT("DamageNumbers.Capacity"), 256, TEXT("Maximum active numbers per local player; identical in both modes."));
static TAutoConsoleVariable<float> CVarMerge(
    TEXT("DamageNumbers.MergeSeconds"), .1f,
    TEXT("Local Actor display batching window; 0 disables. Mass is already batched."));
static TAutoConsoleVariable<int32> CVarReserved(
    TEXT("DamageNumbers.Reserved"), 8,
    TEXT("Slots reserved for critical or blocked feedback, limited to 25 percent of capacity."));
static TAutoConsoleVariable<int32> CVarDiagnostic(
    TEXT("DamageNumbers.Diagnostic"), 0,
    TEXT("Profiling only: 0 normal, 1 static visible, 2 static hidden. Does not change damage."));

// 仅本地控制器有效；按玩家、目标、类和暴击/格挡标志合并，首次排队启动单次计时器。目标弱引用失效不延长敌人寿命，已缓存的位置仍可用于显示。
void UDamageNumberPool::QueueActor(APlayerController* Player, TSubclassOf<UDamageTextComponent> Class, AActor* Target,
                                   float Damage, bool Blocked, bool Critical)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(DamageNumbers_QueueActor);
    if (bClosing || !IsValid(Player) || !Player->IsLocalController() || !Class || !IsValid(Target) ||
        !FMath::IsFinite(Damage) || Damage <= 0)
        return;
    ++ActorInputs;
    const float Window = FMath::Clamp(CVarMerge.GetValueOnGameThread(), 0.f, .5f);
    if (Window <= 0)
    {
        Show(Player, Class, Target->GetActorLocation(), Damage, Blocked, Critical);
        return;
    }
    auto* Item = Pending.FindByPredicate(
        [&](const FPendingDamageNumber& N)
        {
            return N.Player == Player && N.Target == Target && N.Class == Class && N.Blocked == Blocked &&
                   N.Critical == Critical;
        });
    if (Item)
    {
        Item->Damage = FMath::Min(MAX_flt - Damage, Item->Damage) + Damage;
        Item->Position = Target->GetActorLocation();
        ++Merged;
    }
    else
    {
        if (Pending.Num() >= 256)
        {
            ++PendingDropped;
            return;
        }
        auto& N = Pending.AddDefaulted_GetRef();
        N.Player = Player;
        N.Target = Target;
        N.Class = Class;
        N.Position = Target->GetActorLocation();
        N.Damage = Damage;
        N.Blocked = Blocked;
        N.Critical = Critical;
    }
    if (!GetWorld()->GetTimerManager().IsTimerActive(FlushTimer))
        GetWorld()->GetTimerManager().SetTimer(FlushTimer, this, &ThisClass::FlushActorNumbers, Window, false);
}
// 先MoveTemp取走队列，避免Show或回调重入原队列；优先处理暴击/格挡，再显示普通数字。
void UDamageNumberPool::FlushActorNumbers()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(DamageNumbers_FlushActor);
    auto Batch = MoveTemp(Pending);
    Pending.Reset();
    // Important feedback gets first access to the reserved capacity.
    for (int32 Pass = 0; Pass < 2; ++Pass)
        for (const auto& N : Batch)
            if ((N.Critical || N.Blocked) == (Pass == 0) && N.Player.IsValid())
                Show(N.Player.Get(), N.Class, N.Position, N.Damage, N.Blocked, N.Critical);
}
// 本地表现入口：按玩家和组件类查找空闲项；不足时创建临时Anchor与注册组件。重新使用前恢复标量、动画和Transform，防止上一条数字状态污染。
void UDamageNumberPool::Show(APlayerController* Player, TSubclassOf<UDamageTextComponent> Class, FVector Position,
                             float Damage, bool Blocked, bool Critical)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(DamageNumbers_Show);
    const double Begin = FPlatformTime::Seconds();
    ON_SCOPE_EXIT
    {
        ShowMs += (FPlatformTime::Seconds() - Begin) * 1000;
    };
    if (bClosing || !Player || !Player->IsLocalController() || !Class || Position.ContainsNaN() ||
        !FMath::IsFinite(Damage) || Damage <= 0 || GetWorld()->GetNetMode() == NM_DedicatedServer)
        return;
    ++Requests;
    Components.RemoveAll(
        [](const auto& C)
        {
            return !IsValid(C);
        });
    const bool Pool = CVarPool.GetValueOnGameThread() != 0;
    UDamageTextComponent* Text = nullptr;
    int32 Active = 0, Owned = 0;
    for (UDamageTextComponent* C : Components)
        if (C->GetOwner() && C->GetOwner()->GetOwner() == Player)
        {
            ++Owned;
            if (C->bNumberActive)
                ++Active;
            else if (Pool && C->GetClass() == Class && !Text)
                Text = C;
        }
    const int32 Capacity = FMath::Clamp(CVarCapacity.GetValueOnGameThread(), 1, 4096);
    const int32 Reserved = FMath::Clamp(CVarReserved.GetValueOnGameThread(), 0, Capacity / 4);
    if (!Blocked && !Critical && Active >= Capacity - Reserved)
    {
        ++Dropped;
        return;
    }
    if (Active >= Capacity || (!Text && Pool && Owned >= Capacity))
    {
        ++Dropped;
        return;
    }
    if (Text)
        ++Reused;
    else
    {
        AActor* Anchor = nullptr;
        for (AActor* A : Anchors)
            if (IsValid(A) && A->GetOwner() == Player)
            {
                Anchor = A;
                break;
            }
        if (!Anchor)
        {
            FActorSpawnParameters P;
            P.Owner = Player;
            P.ObjectFlags |= RF_Transient;
            Anchor = GetWorld()->SpawnActor<AActor>(P);
            if (!Anchor)
                return;
            Anchor->SetActorHiddenInGame(false);
            Anchor->SetActorEnableCollision(false);
            Anchors.Add(Anchor);
        }
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(DamageNumbers_Create);
            const double T = FPlatformTime::Seconds();
            Text = NewObject<UDamageTextComponent>(Anchor, Class);
            CreateMs += (FPlatformTime::Seconds() - T) * 1000;
        }
        Text->SetOwnerPlayer(Player->GetLocalPlayer());
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(DamageNumbers_Register);
            const double T = FPlatformTime::Seconds();
            Text->RegisterComponent();
            RegisterMs += (FPlatformTime::Seconds() - T) * 1000;
        }
        Components.Add(Text);
        ++Created;
    }
    Text->NumberPool = this;
    Text->bNumberPooled = Pool;
    Text->bNumberActive = true;
    Text->SetVisibility(true);
    Text->SetHiddenInGame(false);
    Text->SetComponentTickEnabled(true);
    Text->SetWorldTransform(Class->GetDefaultObject<UDamageTextComponent>()->GetRelativeTransform());
    Text->SetWorldLocation(Position + Class->GetDefaultObject<UDamageTextComponent>()->GetRelativeLocation());
    ResetPresentationScalars(Text);
    if (auto* Widget = Text->GetUserWidgetObject())
    {
        Widget->StopAllAnimations();
        ResetPresentationScalars(Widget);
        Widget->SetRenderOpacity(1);
        Widget->SetRenderTransform(FWidgetTransform());
    }
    Text->SetDamageText(Damage, Blocked, Critical);
    const int32 Diagnostic = CVarDiagnostic.GetValueOnGameThread();
    if (Diagnostic > 0)
        if (auto* W = Text->GetUserWidgetObject())
        {
            W->StopAllAnimations();
            W->SetRenderOpacity(1);
            W->SetRenderTransform(FWidgetTransform());
        }
    if (Diagnostic == 2)
        Text->SetVisibility(false);
}
// 只结束一次显示：清活动标记、停止动画、隐藏并停Tick，保留已注册组件供后续Show复用。真正销毁由组件覆盖入口或子系统退出处理。
void UDamageNumberPool::Release(UDamageTextComponent* C)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(DamageNumbers_Release);
    if (!C || !C->bNumberActive)
        return;
    C->bNumberActive = false;
    ++Released;
    if (auto* W = C->GetUserWidgetObject())
        W->StopAllAnimations();
    C->SetVisibility(false);
    C->SetHiddenInGame(true);
    C->SetComponentTickEnabled(false);
}
// 先禁止新请求、清定时器与待处理队列，再清组件弱回指并真正销毁组件；最后销毁Anchor并清强引用。顺序避免DestroyComponent再次回流Release。
void UDamageNumberPool::Deinitialize()
{
    bClosing = true;
    GetWorld()->GetTimerManager().ClearTimer(FlushTimer);
    Pending.Reset();
    for (UDamageTextComponent* C : Components)
        if (IsValid(C))
        {
            C->NumberPool.Reset();
            C->bNumberPooled = false;
            C->DestroyComponent();
        }
    Components.Reset();
    for (AActor* A : Anchors)
        if (IsValid(A))
            A->Destroy();
    Anchors.Reset();
    Super::Deinitialize();
}
// 仅重置诊断计数与耗时，不销毁池对象或改变正在播放的数字。
void UDamageNumberPool::ResetCounters()
{
    Requests = Created = Reused = Released = Dropped = ActorInputs = Merged = PendingDropped = 0;
    ShowMs = CreateMs = RegisterMs = 0;
}
// 读取本地池状态并序列化JSON供性能验证，不发送网络消息，也不改变池生命周期。
FString UDamageNumberPool::GetReport() const
{
    auto J = MakeShared<FJsonObject>();
    int32 Active = 0, Retained = 0;
    J->SetNumberField(TEXT("actor_inputs"), ActorInputs);
    J->SetNumberField(TEXT("merged"), Merged);
    J->SetNumberField(TEXT("pending_dropped"), PendingDropped);
    J->SetNumberField(TEXT("pending"), Pending.Num());
    for (UDamageTextComponent* C : Components)
        if (IsValid(C))
        {
            ++Retained;
            if (C->bNumberActive)
                ++Active;
        }
    J->SetNumberField(TEXT("requests"), Requests);
    J->SetNumberField(TEXT("created"), Created);
    J->SetNumberField(TEXT("reused"), Reused);
    J->SetNumberField(TEXT("released"), Released);
    J->SetNumberField(TEXT("dropped"), Dropped);
    J->SetNumberField(TEXT("active"), Active);
    J->SetNumberField(TEXT("retained"), Retained);
    J->SetNumberField(TEXT("show_ms"), ShowMs);
    J->SetNumberField(TEXT("create_ms"), CreateMs);
    J->SetNumberField(TEXT("register_ms"), RegisterMs);
    FString S;
    FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&S));
    return S;
}
