#include "Equipment/EquipmentSoulPickup.h"
#include "Equipment/EquipmentGenerator.h"
#include "Backpack/WorldPickupComponent.h"
#include "Backpack/ItemDefinition.h"
#include "Components/SphereComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Player/MyPlayerState.h"
#include "Player/MyPlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CombatInterface.h"
#include "Roguelike/DungeonPlayerCharacter.h"
#include "Roguelike/DungeonRunDirector.h"
#include "Roguelike/DungeonGenerationRuntime.h"
#include "UObject/ConstructorHelpers.h"
#include "Character/StanderPlayer.h"

void UEquipmentSoulLabel::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    Text = WidgetTree->ConstructWidget<UTextBlock>();
    auto Font = Text->GetFont();
    Font.Size = 18;
    Text->SetFont(Font);
    Text->SetJustification(ETextJustify::Center);
    Text->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, .86f, .4f)));
    Text->SetShadowColorAndOpacity(FLinearColor::Black);
    Text->SetShadowOffset(FVector2D(1, 1));
    Text->SetVisibility(ESlateVisibility::HitTestInvisible);
    WidgetTree->RootWidget = Text;
}
void UEquipmentSoulLabel::SetLabel(const FText& Name)
{
    if (Text)
        Text->SetText(FText::Format(NSLOCTEXT("Equipment", "PickupPrompt", "{0}\n[E] 拾取"), Name));
}
AEquipmentSoulPickup::AEquipmentSoulPickup()
{
    bReplicates = true;
    SetReplicateMovement(true);
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = .1f;
    PickupRange = CreateDefaultSubobject<USphereComponent>(TEXT("PickupRange"));
    SetRootComponent(PickupRange);
    PickupRange->InitSphereRadius(200.f);
    PickupRange->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    PickupRange->SetCollisionResponseToAllChannels(ECR_Ignore);
    PickupRange->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    PickupRange->SetGenerateOverlapEvents(true);
    PickupRange->SetCanEverAffectNavigation(false);
    Soul = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Soul"));
    Soul->SetupAttachment(RootComponent);
    Soul->SetRelativeLocation(FVector(0, 0, 45));
    Soul->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UNiagaraSystem> SoulAsset(
        TEXT("/Game/Assets/Effects/Soul/NS_Soul.NS_Soul"));
    static ConstructorHelpers::FObjectFinder<UNiagaraSystem> CollectAsset(
        TEXT("/Game/Assets/Effects/Soul/NS_SoulPickup.NS_SoulPickup"));
    Soul->SetAsset(SoulAsset.Object);
    PickupEffect = CollectAsset.Object;
    Pickup = CreateDefaultSubobject<UWorldPickupComponent>(TEXT("Pickup"));
    NameWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("EquipmentName"));
    NameWidget->SetupAttachment(RootComponent);
    NameWidget->SetWidgetSpace(EWidgetSpace::Screen);
    NameWidget->SetWidgetClass(UEquipmentSoulLabel::StaticClass());
    NameWidget->SetDrawSize(FVector2D(300, 64));
    NameWidget->SetRelativeLocation(FVector(0, 0, 120));
    NameWidget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    NameWidget->SetVisibility(false);
}
void AEquipmentSoulPickup::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    if (GetWorld() && GetWorld()->IsGameWorld() && !Pickup->ItemDefinitions)
        Pickup->ItemDefinitions = LoadObject<UBackpackItemDefinitions>(
            nullptr, TEXT("/Game/BluePrints/DA/DA_BackpackItemDefinitions.DA_BackpackItemDefinitions"));
}
void AEquipmentSoulPickup::BeginPlay()
{
    Super::BeginPlay();
    if (GetNetMode() != NM_DedicatedServer)
        NameWidget->InitWidget();
    if (HasAuthority())
        Pickup->OnPickedUp.AddUObject(this, &ThisClass::OnCollected);
    if (GetNetMode() == NM_DedicatedServer)
    {
        Soul->Deactivate();
        SetActorTickEnabled(false);
    }
}
FText AEquipmentSoulPickup::GetEquipmentName() const
{
    const auto* Def =
        Pickup->ItemDefinitions ? Pickup->ItemDefinitions->FindDefinition(Pickup->ItemDefinitionId) : nullptr;
    return Def ? FEquipmentGenerator::MakeDisplayName(Def->DisplayName, Pickup->RollData) : FText::GetEmpty();
}
bool AEquipmentSoulPickup::CanPlayerPickup(const AMyPlayerState* Player) const
{
    const APawn* Pawn = Player ? Player->GetPawn() : nullptr;
    if (!Pawn || FVector::DistSquared(Pawn->GetActorLocation(), GetActorLocation()) >
                     FMath::Square(PickupRange->GetScaledSphereRadius() + 80.f))
        return false;
    if (Pawn->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Pawn))
        return false;
    const auto* Dungeon = Cast<ADungeonPlayerCharacter>(Pawn);
    return !Dungeon || !Dungeon->IsDungeonDowned();
}
void AEquipmentSoulPickup::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (GetNetMode() == NM_DedicatedServer)
        return;
    const auto* PC = Cast<AMyPlayerController>(GetWorld()->GetFirstPlayerController());
    const bool bNear = PC && PC->IsLocalController() && PC->GetFocusedEquipmentPickup() == this &&
                       CanPlayerPickup(PC->GetPlayerState<AMyPlayerState>()) && !GetEquipmentName().IsEmpty();
    NameWidget->SetVisibility(bNear);
    if (bNear)
        if (auto* Label = Cast<UEquipmentSoulLabel>(NameWidget->GetUserWidgetObject()))
            Label->SetLabel(GetEquipmentName());
}
void AEquipmentSoulPickup::OnCollected()
{
    MulticastCollected();
}
void AEquipmentSoulPickup::MulticastCollected_Implementation()
{
    Soul->Deactivate();
    NameWidget->SetVisibility(false);
    if (GetNetMode() != NM_DedicatedServer && PickupEffect)
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, PickupEffect, Soul->GetComponentLocation());
}
AEquipmentEntranceSamples::AEquipmentEntranceSamples()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = .25f;
    EquipmentNames = {TEXT("SapphireRing"), TEXT("RubyRing"), TEXT("IronGuardChest"), TEXT("SapphireRing"),
                      TEXT("RubyRing")};
    StanderWeaponNames = {
        TEXT("StanderWeaponSwordShield"), TEXT("StanderWeaponHammer"),      TEXT("StanderWeaponDualBlades"),
        TEXT("StanderWeaponBow"),         TEXT("StanderWeaponPistol"),      TEXT("StanderWeaponMagic"),
        TEXT("StanderWeaponSpear"),       TEXT("StanderWeaponStaff"),       TEXT("StanderWeaponFighter"),
        TEXT("StanderWeaponRapier"),      TEXT("StanderWeaponDoubleBlades")};
}
void AEquipmentEntranceSamples::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (GetNetMode() == NM_Client || !HasAuthority())
    {
        SetActorTickEnabled(false);
        return;
    }
    auto* Director = ADungeonRunDirector::Find(GetWorld());
    if (!Director || !Director->GetGeneration() || Director->GetGeneration()->Phase != EDungeonGenerationPhase::Ready)
        return;
    auto* Catalog = LoadObject<UBackpackItemDefinitions>(
        nullptr, TEXT("/Game/BluePrints/DA/DA_BackpackItemDefinitions.DA_BackpackItemDefinitions"));
    if (!Catalog)
    {
        SetActorTickEnabled(false);
        return;
    }
    // The entrance can be ready before possession. Wait for a pawn before deciding
    // which character-specific sample set belongs in this run.
    const auto* PC = GetWorld()->GetFirstPlayerController();
    if (!PC || !PC->GetPawn())
        return;
    TArray<FName> Names = EquipmentNames;
    const int32 BaseCount = Names.Num();
    if (Cast<AStanderPlayer>(PC->GetPawn()))
        Names.Append(StanderWeaponNames);
    if (bBaseSamplesSpawned)
        return;
    bBaseSamplesSpawned = true;
    for (int32 I = 0; I < Names.Num(); ++I)
    {
        const FPrimaryAssetId Id(FPrimaryAssetType(TEXT("BackpackItem")), Names[I]);
        if (!Catalog->FindDefinition(Id))
            continue;
        const int32 WeaponIndex = I - BaseCount;
        const FVector Offset =
            WeaponIndex < 0 ? FVector(-360.f + I * 180.f, 330.f, 40.f)
                            : FVector(-270.f + (WeaponIndex % 4) * 180.f, -180.f - (WeaponIndex / 4) * 140.f, 40.f);
        const FVector Position = Director->GetRoomCenter(0) + Offset;
        const FTransform Transform(FRotator::ZeroRotator, Position);
        auto* Item = GetWorld()->SpawnActorDeferred<AEquipmentSoulPickup>(
            AEquipmentSoulPickup::StaticClass(), Transform, this, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Item)
            continue;
        Item->Pickup->ItemDefinitions = Catalog;
        Item->Pickup->ConfigurePickup(Id, 1, true);
        Item->Pickup->ItemInstanceId = FGuid::NewGuid();
        Item->FinishSpawning(Transform);
        ++SpawnedCount;
    }
    SetActorTickEnabled(false);
}
