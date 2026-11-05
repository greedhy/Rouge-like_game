#include "PureMass/PureMassAssetAuthoring.h"
#include "Engine/SkeletalMesh.h"
#if WITH_EDITOR
#include "Engine/SkeletalMeshSocket.h"
#include "MeshDescription.h"
#include "SkeletalMeshAttributes.h"
#include "StaticMeshOperations.h"
#endif

FString UPureMassAssetAuthoring::BakeRigidWeapon(USkeletalMesh* Destination, USkeletalMesh* Body,
    USkeletalMesh* Weapon, FName SocketName, FTransform WeaponOffset)
{
#if WITH_EDITOR
    if (!Destination || !Body || !Weapon || Destination == Body || Destination == Weapon
        || !Destination->GetPathName().StartsWith(TEXT("/Game/PureMass/Rendering/Merged/")))
        return TEXT("ERROR: destination must be an isolated merged asset");
    const USkeletalMeshSocket* Socket = Body->FindSocket(SocketName);
    if (!Socket) return TEXT("ERROR: missing body socket");
    const FReferenceSkeleton& Ref = Body->GetRefSkeleton();
    const int32 Hand = Ref.FindBoneIndex(Socket->BoneName);
    if (Hand == INDEX_NONE || Destination->GetSkeleton() != Body->GetSkeleton())
        return TEXT("ERROR: invalid hand or mismatched skeleton");
    FTransform HandReference = Ref.GetRefBonePose()[Hand];
    for (int32 Parent=Ref.GetParentIndex(Hand); Parent!=INDEX_NONE; Parent=Ref.GetParentIndex(Parent))
        HandReference *= Ref.GetRefBonePose()[Parent];
    const FTransform WeaponToBody = WeaponOffset * Socket->GetSocketLocalTransform() * HandReference;
    TArray<FMeshDescription> Descriptions;
    FString Report = FString::Printf(TEXT("OK hand=%s bone=%d"), *Socket->BoneName.ToString(), Hand);
    FBox Bounds(ForceInit);
    for (int32 LOD=0; LOD<Body->GetLODNum(); ++LOD)
    {
        const FMeshDescription* BodyDescription = Body->GetMeshDescription(LOD);
        const FMeshDescription* WeaponDescription = Weapon->GetMeshDescription(FMath::Min(LOD,Weapon->GetLODNum()-1));
        if (!BodyDescription || !WeaponDescription) return TEXT("ERROR: missing editable LOD source");
        FMeshDescription Merged = *BodyDescription;
        FMeshDescription Spear = *WeaponDescription;
        TSet<FVertexID> BodyVertices;
        for (FVertexID Vertex : Merged.Vertices().GetElementIDs()) BodyVertices.Add(Vertex);
        FStaticMeshAttributes SpearAttributes(Spear);
        auto SlotNames = SpearAttributes.GetPolygonGroupMaterialSlotNames();
        const auto& WeaponMaterials = Weapon->GetMaterials();
        for (FPolygonGroupID Group : Spear.PolygonGroups().GetElementIDs())
        {
            int32 Index = WeaponMaterials.IndexOfByPredicate([&](const FSkeletalMaterial& M)
                { return M.ImportedMaterialSlotName == SlotNames[Group]; });
            if (Index == INDEX_NONE) Index = Group.GetValue();
            if (!WeaponMaterials.IsValidIndex(Index)) return TEXT("ERROR: unknown weapon material slot");
            SlotNames[Group] = FName(*FString::Printf(TEXT("BakedWeapon_%d"),Index));
        }
        FStaticMeshOperations::FAppendSettings Append;
        Append.MeshTransform = WeaponToBody;
        for (bool& UV : Append.bMergeUVChannels) UV = true;
        FStaticMeshOperations::AppendMeshDescription(Spear, Merged, Append);
        FSkeletalMeshAttributes Attributes(Merged);
        auto Weights = Attributes.GetVertexSkinWeights();
        const UE::AnimationCore::FBoneWeight HandWeight(Hand, 1.f);
        int32 BakedVertices = 0;
        for (FVertexID Vertex : Merged.Vertices().GetElementIDs())
        {
            if (!BodyVertices.Contains(Vertex))
            {
                Weights.Set(Vertex, MakeArrayView(&HandWeight,1));
                ++BakedVertices;
            }
            if (LOD==0) Bounds += FVector(Attributes.GetVertexPositions()[Vertex]);
        }
        if (BakedVertices != Spear.Vertices().Num()) return TEXT("ERROR: weapon vertex count mismatch");
        Report += FString::Printf(TEXT(" LOD%d(body=%d weapon=%d triangles=%d)"),LOD,BodyVertices.Num(),BakedVertices,Merged.Triangles().Num());
        Descriptions.Add(MoveTemp(Merged));
    }
    // Validate all inputs before mutating the isolated destination.
    Destination->PreEditChange(nullptr);
    Destination->GetMaterials() = Body->GetMaterials();
    for (int32 Index=0; Index<Weapon->GetMaterials().Num(); ++Index)
    {
        FSkeletalMaterial Material = Weapon->GetMaterials()[Index];
        Material.MaterialSlotName = Material.ImportedMaterialSlotName = FName(*FString::Printf(TEXT("BakedWeapon_%d"),Index));
        Destination->GetMaterials().Add(Material);
    }
    for (int32 LOD=0; LOD<Descriptions.Num(); ++LOD)
    {
        Destination->CreateMeshDescription(LOD,MoveTemp(Descriptions[LOD]));
        Destination->CommitMeshDescription(LOD);
    }
    Destination->SetImportedBounds(FBoxSphereBounds(Bounds));
    Destination->Build();
    Destination->PostEditChange();
    Destination->MarkPackageDirty();
    return Report;
#else
    return TEXT("ERROR: editor only");
#endif
}
