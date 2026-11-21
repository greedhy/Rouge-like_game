"""Render the actual UE read-only audit; never infer usability from TypeId names."""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[3]
source = root / "Saved/GAAuthoring/GraphExpansion/20261122/capabilities.json"
audit = json.loads(source.read_text(encoding="utf-8-sig"))
target = root / "docs/ga-node-capability-coverage.json"
target.write_text(json.dumps(audit, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
types, definitions = audit["types"], audit["definitions"]
lines = ["# GA 节点能力覆盖表", "",
         "本表由真实注册表和实际法师目录只读审计生成。完整节点、具体配置、逐节点参数及端口见[机器表](ga-node-capability-coverage.json)。", "",
         f"审计时间（UTC）：`{audit['generatedUtc']}`。共 {len(types)} 个注册类型、{len(definitions)} 个目录条目，"
         f"其中 {sum(x['selectable'] for x in definitions)} 个可选；实际默认图观察到 {sum(x['observedCatalogInstances'] > 0 for x in types)} 种节点。", "",
         "登记了逻辑不等于允许任意连接。运行时仍需配置校验、真实端口载荷、目标原生命、有限/持续合同及共享预算。表中0表示本次目录未观察到，不能据此断言策略不可用。", "",
         "“实际可改参数”是实际默认配置经 GraphView/模板校验得到的并集；同类型的其他配置可能不同。新加入的传播、查询和条件节点通常不在原默认图，因此其0/空值需结合下方工作台合同阅读。", "",
         "## 当前工作台合同", "",
         "- `Propagation.Sequential`：原位设置总投递数、逐跳等待、搜索半径、衰减和选敌策略。",
         "- `Propagation.Forked`：上述参数加层数、每次命中分支数和并发开关。旧配置默认串行，工作台新建默认并发；并发位不足排队，累计额度耗尽终结。",
         "- `Query.SelectTargets`：Origin → TargetFound/NoTarget/Completed；半径(0,3000]、数量1..16，最近/最远/最低生命比例。",
         "- `Condition.Killed`：真实DamageResult → Passed或Rejected；0表示未击杀，1表示击杀。输出仅位置，不恢复目标权限。",
         "- `Condition.Chance`：Origin → Passed或Rejected；概率[0,1]由服务器每次调用决定。",
         "- 新规则以 Append.Registered 追加后可以继续选节点调参、选输出口追加。新查询/条件不提供 Insert.Registered，防止分支拒绝时仍无条件续接原线。", "",
         "可信追加片段：" + "、".join(f"`{x}`" for x in audit["appendFragments"]) + "。", "",
         "实际操作、生命周期和本轮验证见[扩展指南](ga-graph-gameplay-expansion.md)。全表的“有限/纯”是注册声明，实际配置及入边仍需二次检查；Actor/Mass列也不代表逐类型联机视觉验收。", "",
         "## 全部注册类型", "",
         "| 类型 | 目录实例 | 输入 → 声明输出 | Actor/Mass | 有限/纯 | 实际默认图可改参数 |",
         "|---|---:|---|---|---|---|"]
for item in types:
    outputs = ", ".join(p["name"] + ":" + p["payload"] + ("*" if p["terminal"] else "") for p in item["declaredPorts"])
    mods = ", ".join(item["observedModifiers"]) or "未观察到"
    lines.append(f"| `{item['type']}` | {item['observedCatalogInstances']} | {item['input']} → {outputs} | "
                 f"{int(item['actor'])}/{int(item['mass'])} | {int(item['runtimeFinite'])}/{int(item['runtimePure'])} | {mods} |")
lines += ["", "输出端口后的 `*` 表示该策略声明的终态口，不代表所有调用都会发出它。", "",
          "复现：完整构建后运行 `test_25d.GraphExpansion.Capabilities.ActualRegistryAndCatalogAudit`，再执行本脚本；对应验证报告和DLL指纹保存在本轮验证记录。"]
(root / "docs/ga-node-capability-coverage.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
print(f"Rendered {len(types)} real registry rows and {len(definitions)} actual catalog definitions")
