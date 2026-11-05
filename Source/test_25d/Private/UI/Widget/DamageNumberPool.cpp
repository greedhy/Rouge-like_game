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
 if(!Object)return;
 const UObject* Defaults=Object->GetClass()->GetDefaultObject();
 for(UClass* Class=Object->GetClass();Cast<UBlueprintGeneratedClass>(Class);Class=Class->GetSuperClass())
  for(TFieldIterator<FProperty> It(Class,EFieldIteratorFlags::ExcludeSuper);It;++It)
  {
   const auto* Struct=CastField<FStructProperty>(*It);
   if(CastField<FNumericProperty>(*It) || CastField<FBoolProperty>(*It) || CastField<FEnumProperty>(*It)
      || (Struct && (Struct->Struct==TBaseStructure<FLinearColor>::Get() || Struct->Struct==FSlateColor::StaticStruct())))
    It->CopyCompleteValue_InContainer(Object,Defaults);
  }
}
}
static TAutoConsoleVariable<int32> CVarPool(TEXT("DamageNumbers.Pool"),1,TEXT("0=create/destroy baseline; 1=reuse local cosmetic components."));
static TAutoConsoleVariable<int32> CVarCapacity(TEXT("DamageNumbers.Capacity"),256,TEXT("Maximum active numbers per local player; identical in both modes."));
static TAutoConsoleVariable<float> CVarMerge(TEXT("DamageNumbers.MergeSeconds"),.1f,TEXT("Local Actor display batching window; 0 disables. Mass is already batched."));
static TAutoConsoleVariable<int32> CVarReserved(TEXT("DamageNumbers.Reserved"),8,TEXT("Slots reserved for critical or blocked feedback, limited to 25 percent of capacity."));
static TAutoConsoleVariable<int32> CVarDiagnostic(TEXT("DamageNumbers.Diagnostic"),0,TEXT("Profiling only: 0 normal, 1 static visible, 2 static hidden. Does not change damage."));

void UDamageNumberPool::QueueActor(APlayerController* Player,TSubclassOf<UDamageTextComponent> Class,AActor* Target,float Damage,bool Blocked,bool Critical)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(DamageNumbers_QueueActor);
 if(bClosing || !IsValid(Player) || !Player->IsLocalController() || !Class || !IsValid(Target) || !FMath::IsFinite(Damage) || Damage<=0) return;
 ++ActorInputs;
 const float Window=FMath::Clamp(CVarMerge.GetValueOnGameThread(),0.f,.5f);
 if(Window<=0){Show(Player,Class,Target->GetActorLocation(),Damage,Blocked,Critical);return;}
 auto* Item=Pending.FindByPredicate([&](const FPendingDamageNumber& N){return N.Player==Player && N.Target==Target && N.Class==Class && N.Blocked==Blocked && N.Critical==Critical;});
 if(Item){Item->Damage=FMath::Min(MAX_flt-Damage,Item->Damage)+Damage;Item->Position=Target->GetActorLocation();++Merged;}
 else
 {
  if(Pending.Num()>=256){++PendingDropped;return;}
  auto& N=Pending.AddDefaulted_GetRef();N.Player=Player;N.Target=Target;N.Class=Class;N.Position=Target->GetActorLocation();N.Damage=Damage;N.Blocked=Blocked;N.Critical=Critical;
 }
 if(!GetWorld()->GetTimerManager().IsTimerActive(FlushTimer)) GetWorld()->GetTimerManager().SetTimer(FlushTimer,this,&ThisClass::FlushActorNumbers,Window,false);
}
void UDamageNumberPool::FlushActorNumbers()
{
 TRACE_CPUPROFILER_EVENT_SCOPE(DamageNumbers_FlushActor);
 auto Batch=MoveTemp(Pending);Pending.Reset();
 // Important feedback gets first access to the reserved capacity.
 for(int32 Pass=0;Pass<2;++Pass) for(const auto& N:Batch)
  if((N.Critical||N.Blocked)==(Pass==0) && N.Player.IsValid()) Show(N.Player.Get(),N.Class,N.Position,N.Damage,N.Blocked,N.Critical);
}
void UDamageNumberPool::Show(APlayerController* Player,TSubclassOf<UDamageTextComponent> Class,FVector Position,float Damage,bool Blocked,bool Critical)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(DamageNumbers_Show);
 const double Begin=FPlatformTime::Seconds(); ON_SCOPE_EXIT {ShowMs+=(FPlatformTime::Seconds()-Begin)*1000;};
 if(bClosing || !Player || !Player->IsLocalController() || !Class || Position.ContainsNaN() || !FMath::IsFinite(Damage) || Damage<=0 || GetWorld()->GetNetMode()==NM_DedicatedServer) return;
 ++Requests;
 Components.RemoveAll([](const auto& C){return !IsValid(C);});
 const bool Pool=CVarPool.GetValueOnGameThread()!=0;
 UDamageTextComponent* Text=nullptr; int32 Active=0,Owned=0;
 for(UDamageTextComponent* C:Components) if(C->GetOwner() && C->GetOwner()->GetOwner()==Player)
 {
  ++Owned; if(C->bNumberActive) ++Active;
  else if(Pool && C->GetClass()==Class && !Text) Text=C;
 }
 const int32 Capacity=FMath::Clamp(CVarCapacity.GetValueOnGameThread(),1,4096);
 const int32 Reserved=FMath::Clamp(CVarReserved.GetValueOnGameThread(),0,Capacity/4);
 if(!Blocked && !Critical && Active>=Capacity-Reserved){++Dropped;return;}
 if(Active>=Capacity || (!Text && Pool && Owned>=Capacity)){++Dropped;return;}
 if(Text) ++Reused;
 else
 {
  AActor* Anchor=nullptr;
  for(AActor* A:Anchors) if(IsValid(A) && A->GetOwner()==Player){Anchor=A;break;}
  if(!Anchor){FActorSpawnParameters P;P.Owner=Player;P.ObjectFlags|=RF_Transient;Anchor=GetWorld()->SpawnActor<AActor>(P);if(!Anchor)return;Anchor->SetActorHiddenInGame(false);Anchor->SetActorEnableCollision(false);Anchors.Add(Anchor);}
  {TRACE_CPUPROFILER_EVENT_SCOPE(DamageNumbers_Create);const double T=FPlatformTime::Seconds();Text=NewObject<UDamageTextComponent>(Anchor,Class);CreateMs+=(FPlatformTime::Seconds()-T)*1000;}
  Text->SetOwnerPlayer(Player->GetLocalPlayer());
  {TRACE_CPUPROFILER_EVENT_SCOPE(DamageNumbers_Register);const double T=FPlatformTime::Seconds();Text->RegisterComponent();RegisterMs+=(FPlatformTime::Seconds()-T)*1000;}
  Components.Add(Text);++Created;
 }
 Text->NumberPool=this;Text->bNumberPooled=Pool;Text->bNumberActive=true;
 Text->SetVisibility(true);Text->SetHiddenInGame(false);Text->SetComponentTickEnabled(true);
 Text->SetWorldTransform(Class->GetDefaultObject<UDamageTextComponent>()->GetRelativeTransform());
 Text->SetWorldLocation(Position+Class->GetDefaultObject<UDamageTextComponent>()->GetRelativeLocation());
 ResetPresentationScalars(Text);
 if(auto* Widget=Text->GetUserWidgetObject()){Widget->StopAllAnimations();ResetPresentationScalars(Widget);Widget->SetRenderOpacity(1);Widget->SetRenderTransform(FWidgetTransform());}
 Text->SetDamageText(Damage,Blocked,Critical);
 const int32 Diagnostic=CVarDiagnostic.GetValueOnGameThread();
 if(Diagnostic>0) if(auto* W=Text->GetUserWidgetObject()){W->StopAllAnimations();W->SetRenderOpacity(1);W->SetRenderTransform(FWidgetTransform());}
 if(Diagnostic==2) Text->SetVisibility(false);
}
void UDamageNumberPool::Release(UDamageTextComponent* C)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(DamageNumbers_Release);
 if(!C || !C->bNumberActive)return;
 C->bNumberActive=false;++Released;
 if(auto* W=C->GetUserWidgetObject())W->StopAllAnimations();
 C->SetVisibility(false);C->SetHiddenInGame(true);C->SetComponentTickEnabled(false);
}
void UDamageNumberPool::Deinitialize()
{
 bClosing=true;
 GetWorld()->GetTimerManager().ClearTimer(FlushTimer);Pending.Reset();
 for(UDamageTextComponent* C:Components)if(IsValid(C)){C->NumberPool.Reset();C->bNumberPooled=false;C->DestroyComponent();}
 Components.Reset();
 for(AActor* A:Anchors)if(IsValid(A))A->Destroy();
 Anchors.Reset();Super::Deinitialize();
}
void UDamageNumberPool::ResetCounters(){Requests=Created=Reused=Released=Dropped=ActorInputs=Merged=PendingDropped=0;ShowMs=CreateMs=RegisterMs=0;}
FString UDamageNumberPool::GetReport() const
{
 auto J=MakeShared<FJsonObject>();int32 Active=0,Retained=0;
 J->SetNumberField(TEXT("actor_inputs"),ActorInputs);J->SetNumberField(TEXT("merged"),Merged);J->SetNumberField(TEXT("pending_dropped"),PendingDropped);J->SetNumberField(TEXT("pending"),Pending.Num());
 for(UDamageTextComponent* C:Components)if(IsValid(C)){++Retained;if(C->bNumberActive)++Active;}
 J->SetNumberField(TEXT("requests"),Requests);J->SetNumberField(TEXT("created"),Created);J->SetNumberField(TEXT("reused"),Reused);J->SetNumberField(TEXT("released"),Released);J->SetNumberField(TEXT("dropped"),Dropped);J->SetNumberField(TEXT("active"),Active);J->SetNumberField(TEXT("retained"),Retained);J->SetNumberField(TEXT("show_ms"),ShowMs);J->SetNumberField(TEXT("create_ms"),CreateMs);J->SetNumberField(TEXT("register_ms"),RegisterMs);
 FString S;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&S));return S;
}
