#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class USubobjectEditorMenuContext;
struct FToolMenuSection;

class FSelectedComponentMergerModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterMenus();

    static void AddMergeMenuEntry(FToolMenuSection& Section);
    static void ExecuteMerge(TWeakObjectPtr<USubobjectEditorMenuContext> WeakContext);
};
