#pragma once
#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassEntityQuery.h"
#include "MonsterManagementProcessor.generated.h"

UCLASS()
class TEST_25D_API UMonsterManagementProcessor : public UMassProcessor
{
    GENERATED_BODY()
public:
    UMonsterManagementProcessor();

protected:
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
    virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
    FMassEntityQuery Query;
};
