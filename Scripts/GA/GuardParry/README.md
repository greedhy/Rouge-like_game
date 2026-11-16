# 标准玩家格挡弹反资产接入

这些脚本只维护已核实的 Stander 右键格挡配置。反弹与眩晕的运行逻辑属于原生共享 `Status.GuardWindow` 动作；成功奖励复用完美闪避的独立世界时间服务。脚本不创建另一套 GA、不占用技能槽，也不改动画、时间轴或原费用。

2026-11-16 的只读基线为 `Saved/GAAuthoring/GuardParry/inspect-20261116T104617-961f4c2b/inspection.json`：

- `RightMouseButton → IA_RMB → InputTag.RMB`。
- `BP_StanderPlayer.SwordConfig` 指向 `DA_Stander`；初始授予包含 `GA_Stander_Guard`，其 `StartupInputTag` 为 `InputTag.RMB`。原 `GuardDuration=0.8`、`GuardCooldown=3`，保留原反击配置。
- Bow、DoubleBlades、DualBlades、Fighter、Hammer、Magic、Pistol、Rapier、Spear、Staff、Sword_Shield 共 11 组 `DA_WeaponSet` 的 RMB 都指向本组 `GA_Block → DA_Block`。检查依据是实际 `Events.Kind=Guard`，不依据名称猜测；本次 Bow 也确实是 Guard。
- 每个 `DA_Block` 只有一个 Guard 事件，持续 1.6 秒；Pistol、Sword_Shield 的事件起点为 0，其余为 0.2。全部耗蓝 15、冷却 3，使用已有蒙太奇及 Timeline；Timeline 的事件数组为空，阶段仍保留。窗口从实际 Guard 打开时算起，并非统一从按下鼠标算起。

白名单只有 `DA_Stander` 和上述 11 个 `DA_Block`，共 12 个现有 DataAsset。设置 `DA_Stander.GuardParry` 与各真实 `Guard` 事件的 `Parry`：`Enabled=True, Window=0.15, MeleeStunDuration=1.0, RewardWorldSlow=True, RewardLock=3.0`，并显式设置嵌套 `Reward` 的 `Scale=0.2, BlendIn=0.05, Hold=1.2, BlendOut=0.15, CompensateSource=True`。其余参数逐字段比较；WeaponSet、GA、玩家、输入、Timeline、格挡蒙太奇还比较包 SHA-256。没有 Blueprint 修改，因此不执行 Blueprint 编译。

每次 Guard 只在首次成功时尝试奖励；已有任意减速请求或自身奖励锁未结束时跳过奖励，弹反本身仍成立，同一 Guard 不重试。成功获得的独立租约在格挡正常结束、取消和换装后继续到自然结束；死亡、生命失效、显式 Invalidate 或 EndPlay 只释放自己的奖励租约。具体行为和验收项见[弹反指南](../../../docs/guard-parry-guide.md)。

## 执行顺序

先完整构建 Editor target，确保当前原生反射包含 `FGuardParrySettings`、`UStanderCombatConfig.GuardParry`、`FStanderWeaponActionEvent.Parry`，再用新 commandlet 进程执行。每一步应等待进程完成，检查本次新报告 `success`，禁止把日志收到命令当作成功。

```powershell
$ProjectRoot = (Get-Location).Path
$Editor = 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$Project = Join-Path $ProjectRoot 'test_25d.uproject'
$InspectScript = (Join-Path $ProjectRoot 'Scripts/GA/GuardParry/inspect.py').Replace('\', '/')
& $Editor $Project -unattended -NullRHI -nosplash -nop4 -run=pythonscript ("-script=" + $InspectScript)
```

将日志 `GUARD_PARRY_INSPECTION` 输出的本次路径赋给环境变量；不要自动选择其他任务或旧时间戳的成功报告。`author.py` 默认只读；它先校验实际资产和基线、所有反射字段及嵌套结构体赋值。

```powershell
$env:GUARD_PARRY_INSPECTION = '本次完整 inspection.json 路径'
$AuthorScript = (Join-Path $ProjectRoot 'Scripts/GA/GuardParry/author.py').Replace('\', '/')
& $Editor $Project -unattended -NullRHI -nosplash -nop4 -run=pythonscript ("-script=" + $AuthorScript)
```

预检成功后执行已授权的写入。每次使用唯一输出目录，先按原路径备份本次实际将改动的 `.uasset/.uexp/.ubulk`，校验备份哈希，然后逐包保存；发现白名单外 dirty 包立即中止。已达到目标值时不重新保存，支持幂等重跑。失败报告及 journal 记录已保存列表；脚本不自动覆写回滚用户文件。

```powershell
$env:GUARD_PARRY_APPLY = '1'
& $Editor $Project -unattended -NullRHI -nosplash -nop4 -run=pythonscript ("-script=" + $AuthorScript)
Remove-Item Env:GUARD_PARRY_APPLY
$env:GUARD_PARRY_VERIFY_AUTHOR_REPORT = '本次完整 authoring-result.json 路径'
$VerifyScript = (Join-Path $ProjectRoot 'Scripts/GA/GuardParry/verify.py').Replace('\', '/')
& $Editor $Project -unattended -NullRHI -nosplash -nop4 -run=pythonscript ("-script=" + $VerifyScript)
Remove-Item Env:GUARD_PARRY_INSPECTION
Remove-Item Env:GUARD_PARRY_VERIFY_AUTHOR_REPORT
```

独立读回要求不同 PID，检查保存后哈希、原字段保持、弹反和嵌套世界减速参数、真实输入/授予路由和零 dirty 包。它不证明真实鼠标输入、反射弹道飞行、怪物眩晕、奖励租约生命周期、视觉或联机行为；这些仍需要原生自动化和 PIE 验收。命令行资产验证不冒充玩家视觉验收。
