// 调用链：自动化 RunTest → UMG 构建的 Slate 输入 → 与绘制共用的命中区域 → 实际选择委托。
#include "Misc/AutomationTest.h"
#include "UI/Widget/AbilityRuntimeGraphCanvas.h"
#include "Input/Events.h"
#include "Widgets/SWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeGraphCanvasSelection,
                                 "test_25d.UnifiedGA.RuntimeGraphWorkshop.CanvasSelectsExactWire",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimeGraphCanvasSelection::RunTest(const FString&)
{
    const FGuid From(1, 0, 0, 0);
    const FGuid ToA(2, 0, 0, 0);
    const FGuid ToB(3, 0, 0, 0);
    const FGuid Inserted(4, 0, 0, 0);
    const FGuid RepeatedInsert(5, 0, 0, 0);
    const FName Port(TEXT("Hit"));
    FAbilityRuntimeGraphView Accepted;
    Accepted.bReady = true;
    Accepted.Revision = 10;
    Accepted.ContentHash = TEXT("Accepted");
    for (const FGuid Id : {From, ToA, ToB})
    {
        auto& Node = Accepted.Nodes.AddDefaulted_GetRef();
        Node.NodeId = Id;
        Node.TypeId = TEXT("Test.Type");
    }
    auto& Output = Accepted.Nodes[0].Ports.AddDefaulted_GetRef();
    Output.Name = Port;
    Output.Payload = EAbilityPortPayload::LiveTarget;
    for (const FGuid Id : {ToA, ToB})
    {
        auto& Edge = Accepted.Edges.AddDefaulted_GetRef();
        Edge.From = From;
        Edge.To = Id;
        Edge.OutputPort = Port;
        Edge.Payload = Output.Payload;
    }
    auto Draft = Accepted;
    Draft.ContentHash = TEXT("Unapplied");
    auto& Extra = Draft.Nodes.AddDefaulted_GetRef();
    Extra.NodeId = Inserted;
    Extra.TypeId = TEXT("Test.Inserted");
    auto& InsertEdge = Draft.Edges.AddDefaulted_GetRef();
    InsertEdge.From = From;
    InsertEdge.To = Inserted;
    InsertEdge.OutputPort = Port;
    InsertEdge.Payload = EAbilityPortPayload::LiveTarget;

    auto* Widget = NewObject<UAbilityRuntimeGraphCanvas>();
    Widget->SetGraphViews(Accepted, Draft);
    const TSharedRef<SWidget> Slate = Widget->TakeWidget();
    TArray<TTuple<FGuid, FName, FGuid>> Selected;
    Widget->OnSelectionChanged.AddLambda(
        [&](FGuid NodeId, FName PortId, FGuid Target)
        {
            Selected.Emplace(NodeId, PortId, Target);
        });
    const FGeometry Geometry = FGeometry::MakeRoot(FVector2D(1000, 420), FSlateLayoutTransform());
    const auto Click = [&](const FVector2D& Position)
    {
        const FPointerEvent Event(0, Position, Position, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton,
                                  0.f, FModifierKeysState());
        return Slate->OnMouseButtonDown(Geometry, Event).IsEventHandled();
    };
    FVector2D Position;
    if (TestTrue(TEXT("Node has a visible click point"), Widget->GetCanvasNodePosition(From, Position)))
    {
        TestTrue(TEXT("Slate consumes node press"), Click(Position));
    }
    if (TestTrue(TEXT("Output port has a visible click point"), Widget->GetCanvasPortPosition(From, Port, Position)))
    {
        TestTrue(TEXT("Slate consumes port press"), Click(Position));
    }
    for (const FGuid Id : {ToA, ToB})
    {
        if (TestTrue(TEXT("Fanout edge has a distinct spline"),
                     Widget->GetCanvasEdgePosition(From, Port, Id, .8f, Position)))
        {
            TestTrue(TEXT("Slate consumes exact wire press"), Click(Position));
        }
    }
    if (TestEqual(TEXT("Slate dispatched all four selections"), Selected.Num(), 4))
    {
        TestTrue(TEXT("Node selection preserves only node identity"),
                 Selected[0] == MakeTuple(From, FName(NAME_None), FGuid()));
        TestTrue(TEXT("Port selection preserves declared output identity"),
                 Selected[1] == MakeTuple(From, Port, FGuid()));
        TestTrue(TEXT("First sibling selects the first destination"), Selected[2] == MakeTuple(From, Port, ToA));
        TestTrue(TEXT("Second sibling selects the second destination"), Selected[3] == MakeTuple(From, Port, ToB));
    }

    Widget->ShowDraft(true);
    if (TestTrue(TEXT("Draft-only node is drawn"), Widget->GetCanvasNodePosition(Inserted, Position)))
    {
        Click(Position);
        TestEqual(TEXT("Draft press cannot dispatch an accepted edit selection"), Selected.Num(), 4);
    }
    // The same edit can compile to an equivalent hash and revision with newly allocated graph identities.
    auto RepeatedDraft = Draft;
    RepeatedDraft.Nodes.Last().NodeId = RepeatedInsert;
    RepeatedDraft.Edges.Last().To = RepeatedInsert;
    Widget->SetGraphViews(Accepted, RepeatedDraft);
    TestFalse(TEXT("A repeated draft retires the old node identity"),
              Widget->GetCanvasNodePosition(Inserted, Position));
    TestFalse(TEXT("A repeated draft retires the old wire identity"),
              Widget->GetCanvasEdgePosition(From, Port, Inserted, .8f, Position));
    TestTrue(TEXT("A repeated draft displays its actual new node identity"),
             Widget->GetCanvasNodePosition(RepeatedInsert, Position));
    TestTrue(TEXT("A repeated draft displays its actual new wire identity"),
             Widget->GetCanvasEdgePosition(From, Port, RepeatedInsert, .8f, Position));
    Widget->ShowDraft(false);
    TestFalse(TEXT("Unapplied node is absent from the accepted view"),
              Widget->GetCanvasNodePosition(Inserted, Position));
    const FVector2D ZoomAnchor(170, 160);
    const TSet<FKey> NoButtons;
    const FPointerEvent Wheel(0, ZoomAnchor, ZoomAnchor, NoButtons, FKey(), 1.f, FModifierKeysState());
    TestTrue(TEXT("Canvas handles scroll as a cursor-anchored zoom"),
             Slate->OnMouseWheel(Geometry, Wheel).IsEventHandled());
    RepeatedDraft.Revision = 11;
    Widget->SetGraphViews(RepeatedDraft, FAbilityRuntimeGraphView());
    if (TestTrue(TEXT("Applied node is in the accepted view"), Widget->GetCanvasNodePosition(RepeatedInsert, Position)))
    {
        TestTrue(TEXT("New node can be the next accepted target"), Click(Position));
        TestTrue(TEXT("New node dispatches its server identity"),
                 Selected.Num() == 5 && Selected.Last() == MakeTuple(RepeatedInsert, FName(NAME_None), FGuid()));
    }
    Widget->ReleaseSlateResources(true);
    return true;
}

#endif
