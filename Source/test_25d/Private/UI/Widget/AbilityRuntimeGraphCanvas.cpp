// 调用链：配方面板提供服务器图的值 → 布局生成节点盒、端口区域和线段 → Slate 绘制 / 点击 → 原始 ID 委托。
#include "UI/Widget/AbilityRuntimeGraphCanvas.h"
#include "Algo/Compare.h"
#include "Input/Events.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

namespace
{
    constexpr double CardWidth = 280.0;
    constexpr double CardHeader = 73.0;
    constexpr double PortHeight = 23.0;
    constexpr double ColumnGap = 126.0;
    constexpr double RowGap = 37.0;
    constexpr double Margin = 28.0;
    constexpr int32 CurveSegments = 24;
    const FLinearColor AcceptedAccent(.18f, .66f, .78f);
    const FLinearColor DraftAccent(.55f, .38f, .83f);
    const FLinearColor SelectedAccent(1.f, .64f, .18f);
    const FLinearColor AddedAccent(.24f, .83f, .47f);
    const FLinearColor ChangedAccent(.93f, .45f, .23f);
    const FLinearColor TextPrimary(.89f, .93f, .97f);
    const FLinearColor TextSecondary(.62f, .74f, .84f);

    struct FGraphSelection
    {
        FGuid NodeId;
        FName Port;
        FGuid Destination;

        bool operator==(const FGraphSelection& Other) const
        {
            return NodeId == Other.NodeId && Port == Other.Port && Destination == Other.Destination;
        }

        friend uint32 GetTypeHash(const FGraphSelection& Selection)
        {
            return HashCombine(HashCombine(GetTypeHash(Selection.NodeId), GetTypeHash(Selection.Port)),
                               GetTypeHash(Selection.Destination));
        }
    };

    struct FNodeLayout
    {
        FAbilityRuntimeGraphNodeView View;
        FSlateRect Bounds;
        FLinearColor Accent;
    };

    struct FWireLayout
    {
        FGraphSelection Selection;
        TArray<FVector2D> Route;
        FSlateRect Bounds;
        FLinearColor Color;
    };

    /** 类型是实际端口契约；文字和颜色即使在缩放后也能区分不同载荷。 */
    FLinearColor PayloadColor(EAbilityPortPayload Payload)
    {
        switch (Payload)
        {
        case EAbilityPortPayload::LiveTarget:
            return FLinearColor(.3f, .83f, .55f);
        case EAbilityPortPayload::ConfirmedImpact:
            return FLinearColor(.95f, .56f, .23f);
        case EAbilityPortPayload::DamageResult:
            return FLinearColor(.98f, .38f, .34f);
        case EAbilityPortPayload::TargetSet:
            return FLinearColor(.68f, .52f, .94f);
        case EAbilityPortPayload::PreparedInventory:
            return FLinearColor(.89f, .77f, .32f);
        case EAbilityPortPayload::Origin:
        default:
            return AcceptedAccent;
        }
    }

    FString TextForCard(const FString& Value, int32 MaxCharacters)
    {
        return Value.Len() > MaxCharacters ? Value.Left(MaxCharacters - 1) + TEXT("…") : Value;
    }

    FSlateRect PortBounds(const FNodeLayout& Node, int32 Index)
    {
        const float Top = Node.Bounds.Top + CardHeader + Index * PortHeight;
        return FSlateRect(Node.Bounds.Left + 9.0, Top, Node.Bounds.Right + 7.0, Top + PortHeight - 2.0);
    }

    FVector2D PortCenter(const FNodeLayout& Node, int32 Index)
    {
        const FSlateRect Bounds = PortBounds(Node, Index);
        return FVector2D(Node.Bounds.Right, (Bounds.Top + Bounds.Bottom) * .5);
    }

    /** 采样一次，之后绘制和命中按相同路径处理。画向左侧的异常/回接边时绕过节点盒。 */
    TArray<FVector2D> SampleWire(const FVector2D& Start, const FVector2D& End, float UpperLane)
    {
        if (End.X <= Start.X)
        {
            return {Start,
                    {Start.X + 30.0, Start.Y},
                    {Start.X + 30.0, UpperLane},
                    {End.X - 30.0, UpperLane},
                    {End.X - 30.0, End.Y},
                    End};
        }
        const double Tangent = FMath::Min((End.X - Start.X) * .55, 235.0);
        const FVector2D ControlA = Start + FVector2D(Tangent, 0.0);
        const FVector2D ControlB = End - FVector2D(Tangent, 0.0);
        TArray<FVector2D> Points;
        Points.Reserve(CurveSegments + 1);
        for (int32 Segment = 0; Segment <= CurveSegments; ++Segment)
        {
            const double T = double(Segment) / CurveSegments;
            const double U = 1.0 - T;
            Points.Add(U * U * U * Start + 3.0 * U * U * T * ControlA + 3.0 * U * T * T * ControlB + T * T * T * End);
        }
        return Points;
    }

    /** 同语义草稿可能重新分配节点 ID，因此摘要不能单独代表可点击的画布快照。 */
    bool SameSnapshot(const FAbilityRuntimeGraphView& A, const FAbilityRuntimeGraphView& B)
    {
        const auto SamePort = [](const FAbilityRuntimeGraphPortView& Left, const FAbilityRuntimeGraphPortView& Right)
        {
            return Left.Name == Right.Name && Left.Payload == Right.Payload;
        };
        const auto SameNode =
            [&SamePort](const FAbilityRuntimeGraphNodeView& Left, const FAbilityRuntimeGraphNodeView& Right)
        {
            return Left.NodeId == Right.NodeId && Left.TypeId == Right.TypeId && Left.Label == Right.Label &&
                   Left.ParameterSummary == Right.ParameterSummary &&
                   Left.SupportedModifiers == Right.SupportedModifiers &&
                   Algo::Compare(Left.Ports, Right.Ports, SamePort);
        };
        const auto SameEdge = [](const FAbilityRuntimeGraphEdgeView& Left, const FAbilityRuntimeGraphEdgeView& Right)
        {
            return Left.From == Right.From && Left.To == Right.To && Left.OutputPort == Right.OutputPort &&
                   Left.Payload == Right.Payload;
        };
        return A.bReady == B.bReady && A.Revision == B.Revision && A.ContentHash == B.ContentHash &&
               A.Error == B.Error && A.AppliedSources == B.AppliedSources &&
               Algo::Compare(A.Nodes, B.Nodes, SameNode) && Algo::Compare(A.Edges, B.Edges, SameEdge);
    }
} // namespace

/** 只拥有绘制布局和视口状态；UWidget 委托用弱 UObject 绑定，Slate 捕获不会延长面板生命周期。 */
class SAbilityRuntimeGraphCanvas : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SAbilityRuntimeGraphCanvas)
    {
    }
    SLATE_EVENT(FAbilityRuntimeGraphSelectionChanged::FDelegate, OnSelection)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        OnSelection = Args._OnSelection;
        SetClipping(EWidgetClipping::ClipToBounds);
    }

    /** 通过最长前驱链分列，按服务器快照的顺序排列同层节点。未连通节点仍有可定位的盒。 */
    void SetView(const FAbilityRuntimeGraphView& View, const FAbilityRuntimeGraphView& Baseline, bool bDraft)
    {
        bReadOnly = bDraft;
        Nodes.Reset();
        Wires.Reset();
        NodeIndices.Reset();
        GraphSize = FVector2D::ZeroVector;
        if (!View.bReady || View.Nodes.IsEmpty())
        {
            EmptyText = View.Error.IsEmpty() ? TEXT("开始技能测试后显示服务器已接受的图") : View.Error;
            Invalidate(EInvalidateWidgetReason::Paint);
            return;
        }

        TArray<int32> Incoming;
        TArray<int32> Depth;
        TArray<TArray<int32>> Outgoing;
        Incoming.SetNumZeroed(View.Nodes.Num());
        Depth.SetNumZeroed(View.Nodes.Num());
        Outgoing.SetNum(View.Nodes.Num());
        for (int32 Index = 0; Index < View.Nodes.Num(); ++Index)
        {
            NodeIndices.Add(View.Nodes[Index].NodeId, Index);
        }
        for (const auto& Edge : View.Edges)
        {
            const auto* From = NodeIndices.Find(Edge.From);
            const auto* To = NodeIndices.Find(Edge.To);
            if (From && To)
            {
                Outgoing[*From].Add(*To);
                ++Incoming[*To];
            }
        }

        TArray<int32> Ready;
        for (int32 Index = 0; Index < Incoming.Num(); ++Index)
        {
            if (Incoming[Index] == 0)
            {
                Ready.Add(Index);
            }
        }
        for (int32 QueueIndex = 0; QueueIndex < Ready.Num(); ++QueueIndex)
        {
            const int32 From = Ready[QueueIndex];
            for (const int32 To : Outgoing[From])
            {
                Depth[To] = FMath::Max(Depth[To], Depth[From] + 1);
                if (--Incoming[To] == 0)
                {
                    Ready.Add(To);
                }
            }
        }

        TMap<FGuid, const FAbilityRuntimeGraphNodeView*> OldNodes;
        TSet<FGraphSelection> OldEdges;
        if (bDraft)
        {
            for (const auto& Node : Baseline.Nodes)
            {
                OldNodes.Add(Node.NodeId, &Node);
            }
            for (const auto& Edge : Baseline.Edges)
            {
                OldEdges.Add({Edge.From, Edge.OutputPort, Edge.To});
            }
        }

        TMap<int32, double> NextRowByDepth;
        for (int32 Index = 0; Index < View.Nodes.Num(); ++Index)
        {
            const auto& Node = View.Nodes[Index];
            const double Left = Margin + Depth[Index] * (CardWidth + ColumnGap);
            double& NextRow = NextRowByDepth.FindOrAdd(Depth[Index]);
            const double Top = Margin + NextRow;
            const double Height = CardHeader + FMath::Max(1, Node.Ports.Num()) * PortHeight + 7.0;
            FLinearColor Accent = bDraft ? DraftAccent : AcceptedAccent;
            if (bDraft)
            {
                const auto* Old = OldNodes.FindRef(Node.NodeId);
                if (!Old)
                {
                    Accent = AddedAccent;
                }
                else if (Old->TypeId != Node.TypeId || Old->ParameterSummary != Node.ParameterSummary ||
                         Old->Label != Node.Label)
                {
                    Accent = ChangedAccent;
                }
            }
            Nodes.Add({Node, FSlateRect(Left, Top, Left + CardWidth, Top + Height), Accent});
            NextRow += Height + RowGap;
            GraphSize.X = FMath::Max(GraphSize.X, Left + CardWidth + Margin);
            GraphSize.Y = FMath::Max(GraphSize.Y, Top + Height + Margin);
        }

        // 多入边使用不同的左侧落点，便于点选共享后继前的单独连线。
        TMap<FGuid, int32> ArrivalCounts;
        for (const auto& Edge : View.Edges)
        {
            ++ArrivalCounts.FindOrAdd(Edge.To);
        }
        TMap<FGuid, int32> NextArrival;
        for (const auto& Edge : View.Edges)
        {
            const auto* From = FindNode(Edge.From);
            const auto* To = FindNode(Edge.To);
            if (!From || !To)
            {
                continue;
            }
            const int32 OutputIndex = From->View.Ports.IndexOfByPredicate(
                [&Edge](const FAbilityRuntimeGraphPortView& Port)
                {
                    return Port.Name == Edge.OutputPort;
                });
            if (OutputIndex == INDEX_NONE)
            {
                continue;
            }
            const int32 ArrivalIndex = ++NextArrival.FindOrAdd(Edge.To);
            const double Fraction = double(ArrivalIndex) / (ArrivalCounts.FindRef(Edge.To) + 1);
            const FVector2D End(To->Bounds.Left,
                                FMath::Lerp(double(To->Bounds.Top) + 10.0, double(To->Bounds.Bottom) - 10.0, Fraction));
            const FGraphSelection Identity{Edge.From, Edge.OutputPort, Edge.To};
            auto Route =
                SampleWire(PortCenter(*From, OutputIndex), End, FMath::Min(From->Bounds.Top, To->Bounds.Top) - 15.f);
            FBox2D Bounds(ForceInit);
            for (const auto& Point : Route)
            {
                Bounds += Point;
            }
            const FLinearColor Color =
                bDraft ? (OldEdges.Contains(Identity) ? DraftAccent : AddedAccent) : PayloadColor(Edge.Payload) * .78f;
            Wires.Add(
                {Identity, MoveTemp(Route), FSlateRect(Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X, Bounds.Max.Y), Color});
        }
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    /** 高亮接受图内的精确选择，草稿保持自己的差异颜色。 */
    void SetSelection(const FGraphSelection& NewSelection)
    {
        Selection = NewSelection;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    void ResetView()
    {
        Zoom = 1.0;
        Pan = FVector2D::ZeroVector;
    }

    /** 适配整个自动布局；最大放大保持原尺寸，滚轮可重新放大局部。 */
    void FitView(const FVector2D& Available)
    {
        if (GraphSize.X <= 0 || GraphSize.Y <= 0 || Available.X <= 0 || Available.Y <= 0)
        {
            return;
        }
        Zoom = FMath::Clamp(FMath::Min(Available.X / GraphSize.X, Available.Y / GraphSize.Y), .01, 1.0);
        Pan = (Available - GraphSize * Zoom) * .5;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    bool GetNodePosition(FGuid Id, FVector2D& Position) const
    {
        if (const auto* Node = FindNode(Id))
        {
            Position = ToCanvas(FVector2D(Node->Bounds.Left + 30.0, Node->Bounds.Top + 18.0));
            return true;
        }
        return false;
    }

    bool GetPortPosition(FGuid Id, FName Port, FVector2D& Position) const
    {
        if (const auto* Node = FindNode(Id))
        {
            const int32 Index = Node->View.Ports.IndexOfByPredicate(
                [Port](const FAbilityRuntimeGraphPortView& Item)
                {
                    return Item.Name == Port;
                });
            if (Index != INDEX_NONE)
            {
                Position = ToCanvas(PortCenter(*Node, Index));
                return true;
            }
        }
        return false;
    }

    bool GetEdgePosition(const FGraphSelection& Edge, float Fraction, FVector2D& Position) const
    {
        if (const auto* Wire = Wires.FindByPredicate(
                [&Edge](const FWireLayout& Item)
                {
                    return Item.Selection == Edge;
                }))
        {
            const double Progress = FMath::Clamp(double(Fraction), 0.0, 1.0) * (Wire->Route.Num() - 1);
            const int32 Segment = FMath::Min(FMath::FloorToInt32(Progress), Wire->Route.Num() - 2);
            Position = ToCanvas(FMath::Lerp(Wire->Route[Segment], Wire->Route[Segment + 1], Progress - Segment));
            return true;
        }
        return false;
    }

    /** 鼠标只向已接受图派发业务选择。拖动背景或中/右键可平移任一图。 */
    virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        const FKey Button = Event.GetEffectingButton();
        if (Button != EKeys::LeftMouseButton && Button != EKeys::RightMouseButton && Button != EKeys::MiddleMouseButton)
        {
            return FReply::Unhandled();
        }

        if (Button == EKeys::LeftMouseButton)
        {
            const auto Hit = HitTest(ToGraph(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())));
            if (Hit.NodeId.IsValid())
            {
                if (!bReadOnly)
                {
                    OnSelection.ExecuteIfBound(Hit.NodeId, Hit.Port, Hit.Destination);
                }
                return FReply::Handled();
            }
        }

        DragButton = Button;
        DragPoint = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }

    virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (!DragButton.IsValid() || !HasMouseCapture())
        {
            return FReply::Unhandled();
        }
        const FVector2D Position = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
        Pan += Position - DragPoint;
        DragPoint = Position;
        Invalidate(EInvalidateWidgetReason::Paint);
        return FReply::Handled();
    }

    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event) override
    {
        if (Event.GetEffectingButton() != DragButton)
        {
            return FReply::Unhandled();
        }
        DragButton = FKey();
        return FReply::Handled().ReleaseMouseCapture();
    }

    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override
    {
        DragButton = FKey();
    }

    /** 以光标下的图坐标为锚，连续滚轮不会使正在检查的区域漂走。 */
    virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (Nodes.IsEmpty())
        {
            return FReply::Unhandled();
        }
        const FVector2D Position = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
        const FVector2D Anchor = ToGraph(Position);
        Zoom = FMath::Clamp(Zoom * FMath::Pow(1.18, double(Event.GetWheelDelta())), .01, 1.7);
        Pan = Position - Anchor * Zoom;
        Invalidate(EInvalidateWidgetReason::Paint);
        return FReply::Handled();
    }

private:
    const FNodeLayout* FindNode(FGuid Id) const
    {
        const auto* Index = NodeIndices.Find(Id);
        return Index ? &Nodes[*Index] : nullptr;
    }

    FVector2D ToCanvas(const FVector2D& Point) const
    {
        return Point * Zoom + Pan;
    }

    FVector2D ToGraph(const FVector2D& Point) const
    {
        return (Point - Pan) / Zoom;
    }

    /** 优先使用端口和可见节点盒。节点之间按实际折线取最近线，以精确三元组区分扇出。 */
    FGraphSelection HitTest(const FVector2D& Point) const
    {
        for (const auto& Node : Nodes)
        {
            for (int32 Index = 0; Index < Node.View.Ports.Num(); ++Index)
            {
                if (PortBounds(Node, Index).ContainsPoint(Point))
                {
                    return {Node.View.NodeId, Node.View.Ports[Index].Name, FGuid()};
                }
            }
            if (Node.Bounds.ContainsPoint(Point))
            {
                return {Node.View.NodeId, NAME_None, FGuid()};
            }
        }

        const double Tolerance = FMath::Min(8.0 / Zoom, 26.0);
        double NearestDistance = Tolerance * Tolerance;
        FGraphSelection Nearest;
        for (const auto& Wire : Wires)
        {
            const auto& Bounds = Wire.Bounds;
            if (Point.X < Bounds.Left - Tolerance || Point.X > Bounds.Right + Tolerance ||
                Point.Y < Bounds.Top - Tolerance || Point.Y > Bounds.Bottom + Tolerance)
            {
                continue;
            }
            for (int32 Index = 1; Index < Wire.Route.Num(); ++Index)
            {
                const auto Closest = FMath::ClosestPointOnSegment2D(Point, Wire.Route[Index - 1], Wire.Route[Index]);
                const double Distance = FVector2D::DistSquared(Closest, Point);
                if (Distance < NearestDistance)
                {
                    Nearest = Wire.Selection;
                    NearestDistance = Distance;
                }
            }
        }
        return Nearest;
    }

    virtual FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D(800, 315);
    }

    /** 绘制使用自己的裁切区，放大或平移时不会覆盖下方控件。 */
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
                          FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle&, bool) const override
    {
        const auto* Brush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
        const FLinearColor Background =
            bReadOnly ? FLinearColor(.045f, .031f, .070f) : FLinearColor(.024f, .040f, .060f);
        FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(), Brush, ESlateDrawEffect::None,
                                   Background);
        if (Nodes.IsEmpty())
        {
            FSlateDrawElement::MakeText(
                Elements, Layer + 1, Geometry.ToPaintGeometry(FSlateLayoutTransform(FVector2D(22, 22))), EmptyText,
                FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13), ESlateDrawEffect::None, TextSecondary);
            return Layer + 1;
        }

        const FVector2D VisibleStart = ToGraph(FVector2D::ZeroVector);
        const FVector2D VisibleEnd = ToGraph(Geometry.GetLocalSize());
        const FSlateRect Visible(VisibleStart.X, VisibleStart.Y, VisibleEnd.X, VisibleEnd.Y);
        for (const auto& Wire : Wires)
        {
            // 平直线的布局矩形可能没有高度，故这里保留可见性外沿。
            if (Wire.Bounds.Right < Visible.Left - 4.0 || Wire.Bounds.Left > Visible.Right + 4.0 ||
                Wire.Bounds.Bottom < Visible.Top - 4.0 || Wire.Bounds.Top > Visible.Bottom + 4.0)
            {
                continue;
            }
            const bool bSelected = !bReadOnly && Selection == Wire.Selection;
            PaintWire(Geometry, Elements, Layer + (bSelected ? 2 : 1), Wire, bSelected);
        }

        for (const auto& Node : Nodes)
        {
            if (FSlateRect::DoRectanglesIntersect(Node.Bounds, Visible))
            {
                PaintNode(Geometry, Elements, Layer + 3, Node);
            }
        }
        return Layer + 6;
    }

    void PaintWire(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer, const FWireLayout& Wire,
                   bool bSelected) const
    {
        const auto Color = bSelected ? SelectedAccent : Wire.Color;
        TArray<FVector2f> Points;
        Points.Reserve(Wire.Route.Num());
        for (const auto& Point : Wire.Route)
        {
            Points.Add(FVector2f(ToCanvas(Point)));
        }
        const FVector2f End = Points.Last();
        const FVector2f Direction = (End - Points[Points.Num() - 2]).GetSafeNormal();
        const FVector2f Perpendicular(-Direction.Y, Direction.X);
        const float ArrowLength = FMath::Clamp(float(12.0 * Zoom), 4.f, 13.f);
        const FVector2f Base = End - ArrowLength * Direction;
        const TArray<FVector2f> Arrow{Base + Perpendicular * (ArrowLength * .45f), End,
                                      Base - Perpendicular * (ArrowLength * .45f)};
        const float Thickness = bSelected ? 3.8f : 2.2f;
        FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), MoveTemp(Points),
                                     ESlateDrawEffect::None, Color, true, Thickness);
        FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Arrow, ESlateDrawEffect::None, Color,
                                     true, Thickness);
    }

    void PaintNode(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer,
                   const FNodeLayout& Node) const
    {
        const FVector2D Position(Node.Bounds.Left, Node.Bounds.Top);
        const FVector2D Size(CardWidth, Node.Bounds.Bottom - Node.Bounds.Top);
        const auto* Brush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
        const bool bSelected = !bReadOnly && Selection.NodeId == Node.View.NodeId;
        const FLinearColor Accent = bSelected ? SelectedAccent : Node.Accent;
        const auto PaintBox = [&](int32 Offset, const FVector2D& At, const FVector2D& BoxSize, FLinearColor Color)
        {
            FSlateDrawElement::MakeBox(
                Elements, Layer + Offset,
                Geometry.ToPaintGeometry(BoxSize, FSlateLayoutTransform(float(Zoom), ToCanvas(At))), Brush,
                ESlateDrawEffect::None, Color);
        };
        const auto PaintText =
            [&](const FVector2D& At, const FString& Text, int32 Size, FName Style, const FLinearColor& Color)
        {
            if (Zoom >= .23 && !Text.IsEmpty())
            {
                FSlateDrawElement::MakeText(Elements, Layer + 3,
                                            Geometry.ToPaintGeometry(FVector2D(CardWidth, 18.0),
                                                                     FSlateLayoutTransform(float(Zoom), ToCanvas(At))),
                                            Text, FCoreStyle::GetDefaultFontStyle(Style, Size), ESlateDrawEffect::None,
                                            Color);
            }
        };
        PaintBox(0, Position, Size, Accent);
        PaintBox(1, Position + FVector2D(2.0, 2.0), Size - FVector2D(4.0, 4.0), FLinearColor(.067f, .105f, .155f));
        PaintBox(2, Position + FVector2D(2.0, 2.0), FVector2D(CardWidth - 4.0, 29.0),
                 FLinearColor(.099f, .156f, .221f));

        const FString Type = Node.View.TypeId.ToString();
        PaintText(Position + FVector2D(12, 7), TextForCard(Node.View.Label.IsEmpty() ? Type : Node.View.Label, 28), 14,
                  TEXT("Bold"), TextPrimary);
        PaintText(Position + FVector2D(12, 34), TextForCard(Type, 38), 11, TEXT("Regular"), TextSecondary);
        PaintText(Position + FVector2D(12, 51), TextForCard(Node.View.ParameterSummary, 36), 10, TEXT("Regular"),
                  TextSecondary);

        for (int32 Index = 0; Index < Node.View.Ports.Num(); ++Index)
        {
            const auto& Port = Node.View.Ports[Index];
            const FSlateRect Bounds = PortBounds(Node, Index);
            const bool bPortSelected = bSelected && Selection.Port == Port.Name;
            const FLinearColor Color = bPortSelected ? SelectedAccent : PayloadColor(Port.Payload);
            const FVector2D Center = PortCenter(Node, Index);
            PaintBox(2, FVector2D(Bounds.Left, Bounds.Top), FVector2D(CardWidth - 17.0, PortHeight - 2.0),
                     bPortSelected ? FLinearColor(.34f, .22f, .081f) : FLinearColor(.045f, .073f, .111f));
            PaintBox(2, Center - FVector2D(4.5, 4.5), FVector2D(9.0, 9.0), Color);
            const FString PortLabel =
                Port.Name.ToString() + TEXT(" · ") +
                StaticEnum<EAbilityPortPayload>()->GetNameStringByValue(static_cast<int64>(Port.Payload));
            PaintText(FVector2D(Bounds.Left + 5.0, Bounds.Top + 2.0), TextForCard(PortLabel, 37), 11, TEXT("Regular"),
                      Color);
        }
    }

    TArray<FNodeLayout> Nodes;
    TArray<FWireLayout> Wires;
    TMap<FGuid, int32> NodeIndices;
    FGraphSelection Selection;
    FAbilityRuntimeGraphSelectionChanged::FDelegate OnSelection;
    FString EmptyText;
    FVector2D GraphSize = FVector2D::ZeroVector;
    FVector2D Pan = FVector2D::ZeroVector;
    FVector2D DragPoint = FVector2D::ZeroVector;
    FKey DragButton;
    double Zoom = 1.0;
    bool bReadOnly = false;
};

void UAbilityRuntimeGraphCanvas::SetGraphViews(const FAbilityRuntimeGraphView& Accepted,
                                               const FAbilityRuntimeGraphView& Draft)
{
    if (SameSnapshot(AcceptedView, Accepted) && SameSnapshot(DraftView, Draft))
    {
        return;
    }
    if (SlateCanvas && !AcceptedView.bReady && Accepted.bReady)
    {
        SlateCanvas->ResetView();
    }
    AcceptedView = Accepted;
    DraftView = Draft;
    bShowingDraft = bShowingDraft && DraftView.bReady;
    RefreshSlateGraph();
}

void UAbilityRuntimeGraphCanvas::SetSelection(FGuid NodeId, FName PortId, FGuid EdgeTo)
{
    if (SelectedNode == NodeId && SelectedPort == PortId && SelectedDestination == EdgeTo)
    {
        return;
    }
    SelectedNode = NodeId;
    SelectedPort = PortId;
    SelectedDestination = EdgeTo;
    if (SlateCanvas)
    {
        SlateCanvas->SetSelection({SelectedNode, SelectedPort, SelectedDestination});
    }
}

void UAbilityRuntimeGraphCanvas::ShowDraft(bool bShow)
{
    const bool bNext = bShow && DraftView.bReady;
    if (bShowingDraft != bNext)
    {
        bShowingDraft = bNext;
        RefreshSlateGraph();
    }
}

bool UAbilityRuntimeGraphCanvas::IsShowingDraft() const
{
    return bShowingDraft;
}

void UAbilityRuntimeGraphCanvas::FitGraph()
{
    if (SlateCanvas)
    {
        SlateCanvas->FitView(GetCachedGeometry().GetLocalSize());
    }
}

bool UAbilityRuntimeGraphCanvas::GetCanvasNodePosition(FGuid NodeId, FVector2D& Position) const
{
    return SlateCanvas && SlateCanvas->GetNodePosition(NodeId, Position);
}

bool UAbilityRuntimeGraphCanvas::GetCanvasPortPosition(FGuid NodeId, FName PortId, FVector2D& Position) const
{
    return SlateCanvas && SlateCanvas->GetPortPosition(NodeId, PortId, Position);
}

bool UAbilityRuntimeGraphCanvas::GetCanvasEdgePosition(FGuid From, FName PortId, FGuid To, float Fraction,
                                                       FVector2D& Position) const
{
    return SlateCanvas && SlateCanvas->GetEdgePosition({From, PortId, To}, Fraction, Position);
}

void UAbilityRuntimeGraphCanvas::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    SlateCanvas.Reset();
}

TSharedRef<SWidget> UAbilityRuntimeGraphCanvas::RebuildWidget()
{
    SlateCanvas = SNew(SAbilityRuntimeGraphCanvas)
                      .OnSelection(FAbilityRuntimeGraphSelectionChanged::FDelegate::CreateUObject(
                          this, &ThisClass::SelectFromSlate));
    RefreshSlateGraph();
    return SlateCanvas.ToSharedRef();
}

void UAbilityRuntimeGraphCanvas::RefreshSlateGraph()
{
    if (SlateCanvas)
    {
        SlateCanvas->SetView(bShowingDraft ? DraftView : AcceptedView, AcceptedView, bShowingDraft);
        SlateCanvas->SetSelection({SelectedNode, SelectedPort, SelectedDestination});
    }
}

void UAbilityRuntimeGraphCanvas::SelectFromSlate(FGuid NodeId, FName PortId, FGuid EdgeTo)
{
    if (!bShowingDraft && AcceptedView.bReady)
    {
        SetSelection(NodeId, PortId, EdgeTo);
        OnSelectionChanged.Broadcast(NodeId, PortId, EdgeTo);
    }
}
