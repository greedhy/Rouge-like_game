#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "MeshDescription.h"
#include "SkeletalMeshAttributes.h"
#include "PureMass/PureMassTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPureMassMergedAssetTest,"test_25d.PureMass.Assets.MergedGoblin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPureMassMergedAssetTest::RunTest(const FString&)
{
    auto* Body=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/PureMass/Rendering/SKM_MassGoblin.SKM_MassGoblin"));
    auto* Weapon=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/PureMass/Rendering/SKM_MassGoblinSpear.SKM_MassGoblinSpear"));
    auto* Merged=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/PureMass/Rendering/Merged/SKM_MassGoblin_Armed.SKM_MassGoblin_Armed"));
    auto* Type=LoadObject<UPureMassMonsterType>(nullptr,TEXT("/Game/PureMass/DA_MassGoblin.DA_MassGoblin"));
    if(!TestNotNull(TEXT("Body"),Body)||!TestNotNull(TEXT("Weapon"),Weapon)||!TestNotNull(TEXT("Merged"),Merged)||!TestNotNull(TEXT("Type"),Type)) return false;
    TestTrue(TEXT("Original skeleton preserved"),Merged->GetSkeleton()==Body->GetSkeleton());
    TestTrue(TEXT("Original physics preserved"),Merged->GetPhysicsAsset()==Body->GetPhysicsAsset());
    TestTrue(TEXT("Type uses merged mesh without a second weapon"),Type->Mesh==Merged && !Type->WeaponMesh);
    const auto* Socket=Body->FindSocket(Type->WeaponSocket);
    if(!TestNotNull(TEXT("Existing Actor socket"),Socket)) return false;
    const auto& Ref=Body->GetRefSkeleton();
    const int32 Hand=Ref.FindBoneIndex(Socket->BoneName);
    FTransform Reference=Ref.GetRefBonePose()[Hand];
    for(int32 Parent=Ref.GetParentIndex(Hand);Parent!=INDEX_NONE;Parent=Ref.GetParentIndex(Parent)) Reference*=Ref.GetRefBonePose()[Parent];
    const FTransform Attachment=Type->WeaponOffset*Socket->GetSocketLocalTransform()*Reference;
    for(int32 LOD=0;LOD<Body->GetLODNum();++LOD)
    {
        const auto* B=Body->GetMeshDescription(LOD);
        const auto* W=Weapon->GetMeshDescription(FMath::Min(LOD,Weapon->GetLODNum()-1));
        const auto* M=Merged->GetMeshDescription(LOD);
        if(!B||!W||!M){AddError(TEXT("Missing saved source geometry"));return false;}
        FSkeletalMeshConstAttributes BA(*B), MA(*M), WA(*W);
        const auto BW=BA.GetVertexSkinWeights(), MW=MA.GetVertexSkinWeights();
        TestEqual(TEXT("Body plus weapon vertices"),M->Vertices().Num(),B->Vertices().Num()+W->Vertices().Num());
        TestEqual(TEXT("Body plus weapon triangles"),M->Triangles().Num(),B->Triangles().Num()+W->Triangles().Num());
        TSet<FVertexID> Original;
        for(FVertexID V:B->Vertices().GetElementIDs())
        {
            Original.Add(V);
            TestTrue(TEXT("Body position unchanged"),BA.GetVertexPositions()[V].Equals(MA.GetVertexPositions()[V],.001f));
            const auto Before=BW.Get(V),After=MW.Get(V);
            if(!TestEqual(TEXT("Body influence count unchanged"),Before.Num(),After.Num())) return false;
            for(int32 I=0;I<Before.Num();++I) TestEqual(TEXT("Body weights unchanged"),Before.Get(I).ToInt32(),After.Get(I).ToInt32());
        }
        TArray<FVector> BakedPositions;
        for(FVertexID V:M->Vertices().GetElementIDs()) if(!Original.Contains(V))
        {
            const auto Weights=MW.Get(V);
            if(!TestEqual(TEXT("Weapon has one rigid influence"),Weights.Num(),1))return false;
            TestEqual(TEXT("Weapon bound to correct hand"),int32(Weights.Get(0).GetBoneIndex()),Hand);
            TestEqual(TEXT("Weapon full weight"),int32(Weights.Get(0).GetRawWeight()),65535);
            BakedPositions.Add(FVector(MA.GetVertexPositions()[V]));
        }
        for(FVertexID V:W->Vertices().GetElementIDs())
        {
            const FVector Expected=Attachment.TransformPosition(FVector(WA.GetVertexPositions()[V]));
            TestTrue(TEXT("Weapon ref-pose matches Actor socket placement"),BakedPositions.ContainsByPredicate([&](const FVector& P){return P.Equals(Expected,.002);}));
        }
    }
    return true;
}
#endif
