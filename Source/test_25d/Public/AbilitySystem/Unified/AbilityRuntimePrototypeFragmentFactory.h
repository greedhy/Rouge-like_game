// 调用链：服务器可信编辑模板 -> 本工厂验证准确BaseGraph/来源Scope -> 私有Beam参数 -> 同一细节点编译器。
// 不接收资产路径或上传图；生成对象由事务图保活，运行时帧/资源仍由同根执行器清理。
#pragma once

#include "CoreMinimal.h"

class UAbilityGraphDefinition;
struct FAbilityGraphNode;
struct FAbilityRuntimeDeveloperEdit;

namespace AbilityRuntimePrototypeFragmentFactory
{
    /** 只为已证明来源的Emitter.Launch构建受信Beam分支；失败不返回半初始化图或修改基础配置。 */
    TEST_25D_API bool BuildBeamBranch(UObject* Outer, const UAbilityGraphDefinition& BaseGraph,
                                      const FAbilityGraphNode& Selected, const FAbilityRuntimeDeveloperEdit& Edit,
                                      UAbilityGraphDefinition*& Out, FString& Error);
} // namespace AbilityRuntimePrototypeFragmentFactory
