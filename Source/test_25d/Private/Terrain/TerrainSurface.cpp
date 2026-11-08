// 主要调用链：构造/尺寸复制 → 更新地表几何；BeginPlay → 定时 UpdateContacts → CanAffectCharacter → 状态组件 RefreshContact。
#include "Terrain/TerrainSurface.h"
#include "Terrain/TerrainStatusComponent.h"
#include "Character/BaseCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NiagaraComponent.h"
#include "TimerManager.h"
#include "AbilitySystem/MyAbilitySystemLibrary.h"
#include "Net/UnrealNetwork.h"

// 创建范围、地面和粒子组件，启用复制并关闭 Actor Tick。
ATerrainSurface::ATerrainSurface()
{
    bReplicates = true;
    PrimaryActorTick.bCanEverTick = false;
    Area = CreateDefaultSubobject<UBoxComponent>(TEXT("SurfaceArea"));
    SetRootComponent(Area);
    Area->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Ground = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ground"));
    Ground->SetupAttachment(Area);
    Ground->SetCollisionProfileName(TEXT("BlockAll"));
    SurfaceParticles = CreateDefaultSubobject<UNiagaraComponent>(TEXT("SurfaceParticles"));
    SurfaceParticles->SetupAttachment(Area);
    SurfaceParticles->SetAutoActivate(false);
}

// 根据尺寸调整检测范围、网格和粒子，令网格顶面对齐地块平面。
void ATerrainSurface::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    Area->SetBoxExtent(FVector(FMath::Max(50.f, SurfaceSize.X) * .5f, FMath::Max(50.f, SurfaceSize.Y) * .5f,
                               FMath::Max(2.f, ContactHeight)));
    if (!Config)
        return;
    Ground->SetStaticMesh(Config->GroundMesh);
    if (Config->GroundMesh)
    {
        const FBox Bounds = Config->GroundMesh->GetBoundingBox();
        const FVector Size = Bounds.GetSize();
        const FVector Scale(SurfaceSize.X / FMath::Max(1.f, Size.X), SurfaceSize.Y / FMath::Max(1.f, Size.Y), 1.f);
        Ground->SetRelativeScale3D(Scale);
        Ground->SetRelativeLocation(
            FVector(-Bounds.GetCenter().X * Scale.X, -Bounds.GetCenter().Y * Scale.Y, -Bounds.Max.Z));
    }
    SurfaceParticles->SetAsset(Config->SurfaceVFX);
    SurfaceParticles->SetRelativeLocation(FVector(0, 0, 5));
    SurfaceParticles->SetRelativeScale3D(FVector(SurfaceSize.X / 400.f, SurfaceSize.Y / 400.f, 1.f));
}

// 启动本地地表特效，并仅在服务器开启周期接触扫描。
void ATerrainSurface::BeginPlay()
{
    Super::BeginPlay();
    if (bOnlyAffectCasterEnemies)
        Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (GetNetMode() != NM_DedicatedServer && Config && Config->SurfaceVFX && bEnabled)
        SurfaceParticles->Activate(true);
    if (HasAuthority())
    {
        GetWorldTimerManager().SetTimer(ScanTimer, this, &ATerrainSurface::UpdateContacts, .2f, true);
        UpdateContacts();
    }
}

// 验证角色存活、阵营与落地条件，并检查脚底是否位于地块局部范围内。
bool ATerrainSurface::CanAffectCharacter(ABaseCharacter* Character) const
{
    if (!bEnabled || !Config || !IsValid(Character) || Character->IsDead_Implementation() ||
        !Character->GetAbilitySystemComponent())
        return false;
    if (bOnlyAffectCasterEnemies && (!IsValid(GetOwner()) || Character == GetOwner() ||
                                     !UMyAbilitySystemLibrary::IsNotFriend(GetOwner(), Character)))
        return false;
    if (Character->ActorHasTag(TEXT("Player")) && !bAffectPlayers)
        return false;
    if (Character->ActorHasTag(TEXT("Enemy")) && !bAffectEnemies)
        return false;
    if (bRequireGrounded && !Character->GetCharacterMovement()->IsMovingOnGround())
        return false;
    // 将脚底转换到地块局部空间，支持旋转地块的矩形范围判定。
    const FVector Feet =
        Character->GetActorLocation() - FVector(0, 0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    const FVector Local = GetActorTransform().InverseTransformPosition(Feet);
    return FMath::Abs(Local.X) <= SurfaceSize.X * .5f && FMath::Abs(Local.Y) <= SurfaceSize.Y * .5f &&
           FMath::Abs(Local.Z) <= ContactHeight;
}

// 服务器进行宽范围重叠查询，按角色去重后向状态组件续期有效接触。
void ATerrainSurface::UpdateContacts()
{
    if (!HasAuthority() || !bEnabled || !Config)
        return;
    // 先做宽范围查询；精确脚底高度和阵营条件留给 CanAffectCharacter。
    TArray<FOverlapResult> Hits;
    const FVector Scale = GetActorScale3D().GetAbs();
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_Pawn);
    Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    GetWorld()->OverlapMultiByObjectType(Hits, GetActorLocation() + FVector(0, 0, 100), GetActorQuat(), Objects,
                                         FCollisionShape::MakeBox(FVector(SurfaceSize.X * .5f * Scale.X + 100,
                                                                          SurfaceSize.Y * .5f * Scale.Y + 100, 250)),
                                         FCollisionQueryParams(SCENE_QUERY_STAT(TerrainContacts), false, this));
    // 同一角色可能有多个重叠组件，每轮仅续期一次接触。
    TSet<ABaseCharacter*> Seen;
    for (const FOverlapResult& Hit : Hits)
    {
        ABaseCharacter* Character = Cast<ABaseCharacter>(Hit.GetActor());
        if (Character && !Seen.Contains(Character) && CanAffectCharacter(Character))
        {
            Seen.Add(Character);
            if (auto* Status = Character->FindComponentByClass<UTerrainStatusComponent>())
                Status->RefreshContact(this);
        }
    }
}

// 结束生命周期时释放本类持有的运行期资源，再调用父类清理。
void ATerrainSurface::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearTimer(ScanTimer);
    Super::EndPlay(Reason);
}

// 注册本类需要发送给客户端的复制字段。
void ATerrainSurface::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ATerrainSurface, bOnlyAffectCasterEnemies);
    DOREPLIFETIME(ATerrainSurface, SurfaceSize);
}

// 尺寸复制到客户端后重新应用构造期的网格和特效缩放。
void ATerrainSurface::OnRep_SurfaceSize()
{
    OnConstruction(GetActorTransform());
}
