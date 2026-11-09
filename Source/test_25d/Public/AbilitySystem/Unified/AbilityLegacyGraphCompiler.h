// 调用链：GA 激活前/资产审计 -> Compile -> 旧配置按深度展开有限 DAG -> GraphDefinition::Validate。
// 不保存或修改源资产；运行参数快照属于编译图，标签/技能路径/存档身份仍属于原 GA。
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AbilityLegacyGraphCompiler.generated.h"

class UAbilityPrototype;
class UAbilityGraphDefinition;
class UAbilityGraphInstance;
class UPrototypeCastContext;
struct FAbilityNodeInput;

UCLASS()
class TEST_25D_API UAbilityLegacyGraphCompiler : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** 按 LegacyActor/Mixed/Extension.v1 编译；旧引用环通过 0..3 深度截断，不产生执行图环。 */
    static UAbilityGraphDefinition* Compile(UObject* Outer, UAbilityPrototype* Root, FName Profile, FString& Error);
    /** 编辑器/Python 使用与运行入口相同的编译和校验，返回只读错误列表，不写源资产。 */
    UFUNCTION(BlueprintCallable, Category = "UnifiedGA|Migration")
    static UAbilityGraphDefinition* CompileForAuthoring(UAbilityPrototype* Root, FName Profile,
                                                        TArray<FString>& Errors);
    /** 创建并交由世界持有。调用前根 GA 必须完成一次 Commit；失败不回退再次执行旧 Runtime。 */
    static UAbilityGraphInstance* Execute(UAbilityGraphDefinition* Graph, UPrototypeCastContext* Context,
                                          const FAbilityNodeInput& Input, FString& Error);
};
