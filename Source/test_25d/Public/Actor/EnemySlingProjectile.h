// 调用链：生产投石发射口 -> 继承弹体伤害/弹反 -> 专属墙面预扫、死亡解绑与终结复制。
#pragma once
#include "CoreMinimal.h"
#include "Actor/MyProjectile.h"
#include "EnemySlingProjectile.generated.h"
class AEnemyCharacter;
UCLASS()
class TEST_25D_API AEnemySlingProjectile : public AMyProjectile
{
    GENERATED_BODY()
public:
    AEnemySlingProjectile();
    void InitializeSource(AEnemyCharacter* Source);
    virtual bool IsValidOverlap(AActor* OtherActor) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

protected:
    virtual void OnParrySourceTransferred(AActor* PreviousSource) override;
    virtual void BeginPlay() override;
    virtual void OnHit() override;
    virtual bool ShouldPlayImpactOnDestroy() const override
    {
        return false;
    }

private:
    bool IsSourceCurrent() const;
    UFUNCTION()
    void SourceDied(AActor* Source);
    UFUNCTION()
    void WorldStopped(const FHitResult& Hit);
    UFUNCTION(NetMulticast, Unreliable)
    void PlayTerminalImpact();
    TWeakObjectPtr<AEnemyCharacter> FiringEnemy;
    int32 FiringLease = 0;
    bool bImpactSent = false;
};
