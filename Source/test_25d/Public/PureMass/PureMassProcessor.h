#pragma once
#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassEntityQuery.h"
#include "PureMassProcessor.generated.h"

UCLASS()
class TEST_25D_API UPureMassProcessor : public UMassProcessor
{
    GENERATED_BODY()
public:
    UPureMassProcessor();

protected:
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
    virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
    FMassEntityQuery Query;
};
