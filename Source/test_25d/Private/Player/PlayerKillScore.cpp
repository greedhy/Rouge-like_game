// 调用链：伤害结算识别真实死亡 → 定位玩家 → UnLua 计数 → 发布当前世界与房间快照。
#include "Player/PlayerKillScore.h"

#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Misc/ScopeExit.h"
#include "Player/MyPlayerState.h"
#include "UnLuaModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlayerKillScore, Log, All);

namespace
{
    // 对脚本调用使用受保护的 Lua 栈，构建错误或运行时错误不会打断伤害结算。
    bool Call(lua_State* Lua, int32 Arguments, int32 Results)
    {
        if (lua_pcall(Lua, Arguments, Results, 0) == LUA_OK)
        {
            return true;
        }
        const char* Error = lua_tostring(Lua, -1);
        UE_LOG(LogPlayerKillScore, Error, TEXT("Gameplay.PlayerKillScore: %s"),
               Error ? UTF8_TO_TCHAR(Error) : TEXT("unknown Lua error"));
        return false;
    }

    // 每次取当前模块函数，以便热重载后的逻辑用于后续事件。
    bool PushFunction(lua_State* Lua, const char* Function)
    {
        lua_getglobal(Lua, "require");
        lua_pushliteral(Lua, "Gameplay.PlayerKillScore");
        if (!Call(Lua, 1, 1) || !lua_istable(Lua, -1))
        {
            return false;
        }
        lua_pushstring(Lua, Function);
        lua_rawget(Lua, -2);
        return lua_isfunction(Lua, -1);
    }

    // 空字符串表示走廊或没有房间系统的地图，避免把 NAME_None 当作房间 ID。
    void PushRoom(lua_State* Lua, FName Room)
    {
        lua_pushstring(Lua, Room.IsNone() ? "" : TCHAR_TO_UTF8(*Room.ToString()));
    }

    // 仅使用 UnLua 已为当前对象定位的环境，退出期间没有环境时可安全跳过。
    lua_State* GetLuaState(AMyPlayerState& Player)
    {
        auto* Module = FModuleManager::GetModulePtr<IUnLuaModule>(TEXT("UnLua"));
        auto* Env = Module ? Module->GetEnv(&Player) : nullptr;
        return Env ? Env->GetMainState() : nullptr;
    }
}

void PlayerKillScore::Credit(AActor* Source, const UWorld* World, FName VictimRoom, bool bElite)
{
    if (!IsValid(Source) || Source->GetWorld() != World)
    {
        return;
    }

    auto* State = Cast<AMyPlayerState>(Source);
    const auto* Pawn = Cast<APawn>(Source);
    if (!State && Pawn)
    {
        State = Pawn->GetPlayerState<AMyPlayerState>();
    }
    if (!State)
    {
        const auto* Controller = Cast<AController>(Source);
        if (!Controller)
        {
            Controller = Pawn ? Pawn->GetController() : Source->GetInstigatorController();
        }
        State = Controller ? Controller->GetPlayerState<AMyPlayerState>() : nullptr;
    }
    if (IsValid(State) && State->HasAuthority() && !State->IsABot() && State->GetWorld() == World)
    {
        State->RecordMonsterKill(VictimRoom, bElite);
    }
}

// 首次进入房间或房间改变时投影该房间历史；逐帧调用时，同房间不访问 Lua。
void AMyPlayerState::SetKillScoreRoom(FName Room)
{
    if (HasAuthority() && (KillScoreWorld.Get() != GetWorld() || KillScore.Room != Room))
    {
        UpdateKillScore(Room, NAME_None, 0);
    }
}

// 一次调用同时记击杀和投影 HUD，避免先复制新总数再复制旧房间小计。
void AMyPlayerState::RecordMonsterKill(FName VictimRoom, bool bElite)
{
    if (HasAuthority())
    {
        const FName CurrentRoom = KillScoreWorld.Get() == GetWorld() ? KillScore.Room : NAME_None;
        UpdateKillScore(CurrentRoom, VictimRoom, bElite ? 2 : 1);
    }
}

// Session 随世界更换，允许同一个 PlayerState 参与无缝旅行而不继承旧世界计数。
void AMyPlayerState::UpdateKillScore(FName Room, FName VictimRoom, int32 Kind)
{
    if (KillScoreWorld.Get() != GetWorld() || KillScoreSession.IsEmpty())
    {
        ForgetKillScore();
        KillScoreWorld = GetWorld();
        KillScoreSession = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        KillScore = {};
        ForceNetUpdate();
    }
    auto* Lua = GetLuaState(*this);
    if (!Lua)
    {
        return;
    }
    const int32 StackTop = lua_gettop(Lua);
    ON_SCOPE_EXIT { lua_settop(Lua, StackTop); };
    if (!PushFunction(Lua, "Update"))
    {
        return;
    }
    lua_pushstring(Lua, TCHAR_TO_UTF8(*KillScoreSession));
    PushRoom(Lua, Room);
    PushRoom(Lua, VictimRoom);
    lua_pushinteger(Lua, Kind);
    if (!Call(Lua, 4, 4))
    {
        return;
    }

    FPlayerKillScore View;
    View.Room = Room;
    int32* Counts[] = {&View.WorldNormal, &View.WorldElite, &View.RoomNormal, &View.RoomElite};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Counts); ++Index)
    {
        int bIsInteger = 0;
        const lua_Integer Count = lua_tointegerx(Lua, -4 + Index, &bIsInteger);
        if (!bIsInteger || Count < 0 || Count > MAX_int32)
        {
            UE_LOG(LogPlayerKillScore, Error, TEXT("Lua returned an invalid kill count."));
            return;
        }
        *Counts[Index] = static_cast<int32>(Count);
    }
    if (KillScore != View)
    {
        KillScore = View;
        ForceNetUpdate();
    }
}

// 结束 PlayerState 或切换世界时解除 Lua 模块对该会话的引用。
void AMyPlayerState::ForgetKillScore()
{
    if (KillScoreSession.IsEmpty())
    {
        return;
    }
    if (auto* Lua = GetLuaState(*this))
    {
        const int32 StackTop = lua_gettop(Lua);
        ON_SCOPE_EXIT { lua_settop(Lua, StackTop); };
        if (PushFunction(Lua, "Forget"))
        {
            lua_pushstring(Lua, TCHAR_TO_UTF8(*KillScoreSession));
            Call(Lua, 1, 0);
        }
    }
    KillScoreSession.Reset();
}
