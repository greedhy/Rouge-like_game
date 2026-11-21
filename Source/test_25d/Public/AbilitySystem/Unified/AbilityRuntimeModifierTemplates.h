// 调用链：开发面板可信 ID/有限参数 -> 服务器模板 -> 配方补丁 -> 原 Compile/GraphInstance。
// 模板只创建瞬态配置，既不激活 GA，也不执行伤害；客户端不能上传配置类、图或反射字段。
#pragma once
#include "CoreMinimal.h"

class UAbilityNodeConfig;
class UAbilityGraphDefinition;
class FAbilityNodeRegistry;
struct FAbilityGraphNode;
struct FAbilityRuntimeDeveloperEdit;
struct FAbilityRuntimeRecipePatch;
enum class EAbilityConfigParameter : uint8;
enum class EAbilityPortPayload : uint8;

/** UI只显示可信参数ID/单位/边界；服务器独立校验具体节点是否实际消费此轴。 */
struct TEST_25D_API FAbilityRuntimeParameterTemplate
{
    FName Id;
    EAbilityConfigParameter Parameter;
    const TCHAR* Label;
    float Minimum;
    float Maximum;
    float Step;
};

namespace AbilityRuntimeModifierTemplates
{
    /** UI 和服务器共用的模板 ID 清单；每项必须在 BuildPatch 中有具体类型化实现。 */
    TEST_25D_API const TArray<FName>& GetIds();
    /** 可追加片段的可信ID；只登记实际工厂，不能把任意TypeId/Class当作请求参数。 */
    TEST_25D_API const TArray<FName>& GetFragmentIds();
    /** UI与服务器共用载荷资格，规则片段仅追加，不误承诺能过滤原有边。 */
    TEST_25D_API bool IsRuleFragment(FName Id);
    TEST_25D_API bool SupportsFragmentPayload(FName Id, EAbilityPortPayload Payload);
    /** 可替换已选端口子分支的可信片段；与追加目录分开，不能互换解释。 */
    TEST_25D_API const TArray<FName>& GetReplacementFragmentIds();
    /** 返回静态描述，不向客户端接受任意字段名；为空表示该ID并非单轴赋值模板。 */
    TEST_25D_API const FAbilityRuntimeParameterTemplate* FindParameterTemplate(FName Id);
    /** 共享实际发布能力校验：声明端口只表示类型；可选伤害owner或保留取消端口不能成为无效编辑。 */
    TEST_25D_API bool CanPublishConfiguredPort(const FAbilityGraphNode& Node, FName Port,
                                               const FAbilityNodeRegistry& Registry, FString& Error);
    /** 只以基础图中准确节点及受信参数建立补丁，失败不修改原节点或提交配方。 */
    TEST_25D_API bool BuildPatch(UObject* Outer, const FAbilityGraphNode& Node,
                                 const FAbilityRuntimeDeveloperEdit& Edit, FAbilityRuntimeRecipePatch& OutPatch,
                                 FString& Error, const UAbilityGraphDefinition* BaseGraph = nullptr);
} // namespace AbilityRuntimeModifierTemplates
