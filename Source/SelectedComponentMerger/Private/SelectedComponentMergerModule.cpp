#include "SelectedComponentMergerModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/StaticMeshComponent.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IContentBrowserSingleton.h"
#include "IMeshMergeUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MeshMerge/MeshMergingSettings.h"
#include "MeshMergeModule.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "SubobjectEditorMenuContext.h"
#include "ToolMenus.h"
#include "UObject/UnrealType.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "SelectedComponentMerger"

namespace SelectedComponentMerger
{
    static void Notify(const FText& Text, SNotificationItem::ECompletionState State)
    {
        FNotificationInfo Info(Text);
        Info.ExpireDuration = 6.0f;
        Info.bUseSuccessFailIcons = true;
        TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
        if (Item.IsValid())
        {
            Item->SetCompletionState(State);
        }
    }

    static UBlueprint* FindOwningBlueprint(const UActorComponent* ComponentTemplate)
    {
        if (!ComponentTemplate)
        {
            return nullptr;
        }

        if (const UBlueprintGeneratedClass* BPGC = ComponentTemplate->GetTypedOuter<UBlueprintGeneratedClass>())
        {
            return Cast<UBlueprint>(BPGC->ClassGeneratedBy);
        }

        return ComponentTemplate->GetTypedOuter<UBlueprint>();
    }

    static USCS_Node* FindNodeForTemplate(UBlueprint* Blueprint, const UActorComponent* ComponentTemplate)
    {
        if (!Blueprint || !Blueprint->SimpleConstructionScript || !ComponentTemplate)
        {
            return nullptr;
        }

        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node && Node->ComponentTemplate == ComponentTemplate)
            {
                return Node;
            }
        }
        return nullptr;
    }

    static USCS_Node* FindParentNode(UBlueprint* Blueprint, USCS_Node* ChildNode)
    {
        if (!Blueprint || !Blueprint->SimpleConstructionScript || !ChildNode)
        {
            return nullptr;
        }

        for (USCS_Node* Candidate : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Candidate && Candidate->GetChildNodes().Contains(ChildNode))
            {
                return Candidate;
            }
        }
        return nullptr;
    }

    static UStaticMeshComponent* ResolveSpawnedComponent(AActor* SpawnedActor, const FName VariableName)
    {
        if (!SpawnedActor || VariableName.IsNone())
        {
            return nullptr;
        }

        if (FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(SpawnedActor->GetClass(), VariableName))
        {
            return Cast<UStaticMeshComponent>(Property->GetObjectPropertyValue_InContainer(SpawnedActor));
        }

        TInlineComponentArray<UStaticMeshComponent*> Components;
        SpawnedActor->GetComponents(Components);
        for (UStaticMeshComponent* Component : Components)
        {
            if (!Component)
            {
                continue;
            }

            FString InstanceName = Component->GetName();
            InstanceName.RemoveFromEnd(TEXT("_GEN_VARIABLE"));
            if (InstanceName == VariableName.ToString())
            {
                return Component;
            }
        }

        return nullptr;
    }

    static void CopyComponentTemplateProperties(const UStaticMeshComponent* Source, UStaticMeshComponent* Destination)
    {
        if (!Source || !Destination)
        {
            return;
        }

        for (TFieldIterator<FProperty> It(Source->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            FProperty* Property = *It;
            if (!Property)
            {
                continue;
            }

            if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient))
            {
                continue;
            }

            Property->CopyCompleteValue_InContainer(Destination, Source);
        }
    }

    struct FSelectedComponentRecord
    {
        UStaticMeshComponent* Template = nullptr;
        USCS_Node* Node = nullptr;
    };

    static bool GatherSelection(
        USubobjectEditorMenuContext* Context,
        TArray<FSelectedComponentRecord>& OutRecords,
        UBlueprint*& OutBlueprint,
        UStaticMeshComponent*& OutLastSelectedTemplate,
        USCS_Node*& OutLastSelectedNode,
        FText& OutError)
    {
        OutRecords.Reset();
        OutBlueprint = nullptr;
        OutLastSelectedTemplate = nullptr;
        OutLastSelectedNode = nullptr;

        if (!Context)
        {
            OutError = LOCTEXT("InvalidContext", "Invalid Blueprint Components context.");
            return false;
        }

        const TArray<UObject*> SelectedObjects = Context->GetSelectedObjects();
        if (SelectedObjects.Num() < 2)
        {
            OutError = LOCTEXT("NeedTwo", "Select at least two Static Mesh Components.");
            return false;
        }

        for (UObject* Object : SelectedObjects)
        {
            UStaticMeshComponent* Component = Cast<UStaticMeshComponent>(Object);
            if (!Component || !Component->GetStaticMesh())
            {
                continue;
            }

            UBlueprint* Blueprint = FindOwningBlueprint(Component);
            if (!Blueprint)
            {
                OutError = LOCTEXT("NoBlueprint", "Could not resolve the owning Blueprint for a selected component.");
                return false;
            }

            if (!OutBlueprint)
            {
                OutBlueprint = Blueprint;
            }
            else if (OutBlueprint != Blueprint)
            {
                OutError = LOCTEXT("DifferentBlueprints", "All selected components must belong to the same Blueprint.");
                return false;
            }

            USCS_Node* Node = FindNodeForTemplate(Blueprint, Component);
            if (!Node)
            {
                OutError = FText::Format(
                    LOCTEXT("NoSCSNodeFmt", "'{0}' is not a directly-owned SCS component. This version supports Blueprint-owned StaticMeshComponents only."),
                    FText::FromName(Component->GetFName()));
                return false;
            }

            FSelectedComponentRecord& Record = OutRecords.AddDefaulted_GetRef();
            Record.Template = Component;
            Record.Node = Node;
        }

        if (!OutBlueprint || OutRecords.Num() < 2)
        {
            OutError = LOCTEXT("NeedTwoValid", "Select at least two valid Static Mesh Components with assigned meshes.");
            return false;
        }

        // UE 5.6.1 does not expose SSubobjectEditor::GetSelectionSet() publicly.
        // USubobjectEditorMenuContext::GetSelectedObjects() is the supported menu-context API.
        // Preserve that returned order and use the final valid StaticMeshComponent as the
        // output-component template candidate. This matches the Components-panel selection
        // order in the target workflow and is verified at runtime by the log below.
        OutLastSelectedTemplate = OutRecords.Last().Template;
        OutLastSelectedNode = OutRecords.Last().Node;

        if (!OutLastSelectedTemplate || !OutLastSelectedNode || !OutLastSelectedTemplate->GetStaticMesh())
        {
            OutError = LOCTEXT("NoLastSelection", "Could not resolve the last-selected Static Mesh Component.");
            return false;
        }

        FString SelectionOrder;
        for (int32 Index = 0; Index < OutRecords.Num(); ++Index)
        {
            if (Index > 0)
            {
                SelectionOrder += TEXT(" -> ");
            }
            SelectionOrder += OutRecords[Index].Node->GetVariableName().ToString();
        }
        UE_LOG(LogTemp, Display, TEXT("[SelectedComponentMerger] Selection context order: %s | Duplicate source: %s"),
            *SelectionOrder,
            *OutLastSelectedNode->GetVariableName().ToString());

        return true;
    }
}

void FSelectedComponentMergerModule::StartupModule()
{
    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FSelectedComponentMergerModule::RegisterMenus));
}

void FSelectedComponentMergerModule::ShutdownModule()
{
    if (UToolMenus::IsToolMenuUIEnabled())
    {
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
    }
}

void FSelectedComponentMergerModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("Kismet.SubobjectEditorContextMenu");
    FToolMenuSection& Section = Menu->FindOrAddSection("SelectedComponentMerger");
    Section.AddDynamicEntry(
        "SelectedComponentMerger.Merge",
        FNewToolMenuSectionDelegate::CreateStatic(&FSelectedComponentMergerModule::AddMergeMenuEntry));
}

void FSelectedComponentMergerModule::AddMergeMenuEntry(FToolMenuSection& Section)
{
    USubobjectEditorMenuContext* Context = Section.FindContext<USubobjectEditorMenuContext>();
    if (!Context)
    {
        return;
    }

    int32 ValidCount = 0;
    for (UObject* Object : Context->GetSelectedObjects())
    {
        if (const UStaticMeshComponent* Component = Cast<UStaticMeshComponent>(Object))
        {
            if (Component->GetStaticMesh())
            {
                ++ValidCount;
            }
        }
    }

    if (ValidCount < 2)
    {
        return;
    }

    const TWeakObjectPtr<USubobjectEditorMenuContext> WeakContext(Context);
    Section.AddMenuEntry(
        "SelectedComponentMerger.MergeSelected",
        LOCTEXT("MergeSelectedLabel", "Merge Selected Static Mesh Components"),
        LOCTEXT("MergeSelectedTooltip", "Merge selected Blueprint StaticMeshComponents. The last-selected component is used as the template for a new merged component; selected source components are deleted. Pivot position is the parent origin, while parent rotation/scale are countered on the merged component."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateStatic(&FSelectedComponentMergerModule::ExecuteMerge, WeakContext)));
}

void FSelectedComponentMergerModule::ExecuteMerge(TWeakObjectPtr<USubobjectEditorMenuContext> WeakContext)
{
    USubobjectEditorMenuContext* Context = WeakContext.Get();
    TArray<SelectedComponentMerger::FSelectedComponentRecord> Records;
    UBlueprint* Blueprint = nullptr;
    UStaticMeshComponent* LastSelectedTemplate = nullptr;
    USCS_Node* LastSelectedNode = nullptr;
    FText Error;

    if (!SelectedComponentMerger::GatherSelection(
        Context,
        Records,
        Blueprint,
        LastSelectedTemplate,
        LastSelectedNode,
        Error))
    {
        SelectedComponentMerger::Notify(Error, SNotificationItem::CS_Fail);
        return;
    }

    if (!Blueprint->GeneratedClass || !Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass()))
    {
        SelectedComponentMerger::Notify(LOCTEXT("NotActorBP", "The owning Blueprint is not an Actor Blueprint."), SNotificationItem::CS_Fail);
        return;
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        SelectedComponentMerger::Notify(LOCTEXT("NoEditorWorld", "No Editor World is available."), SNotificationItem::CS_Fail);
        return;
    }

    UStaticMesh* NamingSourceMesh = LastSelectedTemplate->GetStaticMesh();
    if (!NamingSourceMesh)
    {
        SelectedComponentMerger::Notify(LOCTEXT("NoNamingSource", "The last-selected component has no Static Mesh."), SNotificationItem::CS_Fail);
        return;
    }

    // Parent Origin is only unambiguous when all selected source components are siblings.
    // This also makes deletion/replacement deterministic in the Components panel.
    USCS_Node* SharedParentNode = SelectedComponentMerger::FindParentNode(Blueprint, LastSelectedNode);
    if (!SharedParentNode)
    {
        SelectedComponentMerger::Notify(
            LOCTEXT("NoSharedParent", "The selected components must be children of the same Blueprint parent component."),
            SNotificationItem::CS_Fail);
        return;
    }

    for (const SelectedComponentMerger::FSelectedComponentRecord& Record : Records)
    {
        if (SelectedComponentMerger::FindParentNode(Blueprint, Record.Node) != SharedParentNode)
        {
            SelectedComponentMerger::Notify(
                LOCTEXT("DifferentParents", "All selected Static Mesh Components must share the same direct parent."),
                SNotificationItem::CS_Fail);
            return;
        }

        // v0.3 keeps the destructive operation intentionally narrow: selected source
        // mesh components must be leaf nodes. This avoids silently changing transforms
        // of unrelated child components while removing their parent.
        if (Record.Node->GetChildNodes().Num() > 0)
        {
            SelectedComponentMerger::Notify(
                FText::Format(
                    LOCTEXT("SourceHasChildrenFmt", "Selected component '{0}' has child components. Destructive merge currently supports leaf Static Mesh Components only."),
                    FText::FromName(Record.Node->GetVariableName())),
                SNotificationItem::CS_Fail);
            return;
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.ObjectFlags |= RF_Transient;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.bHideFromSceneOutliner = true;

    AActor* TempActor = World->SpawnActor<AActor>(Blueprint->GeneratedClass, FTransform::Identity, SpawnParams);
    if (!TempActor)
    {
        SelectedComponentMerger::Notify(LOCTEXT("SpawnFailed", "Failed to create a temporary Blueprint instance for merging."), SNotificationItem::CS_Fail);
        return;
    }

    TArray<UPrimitiveComponent*> ComponentsToMerge;
    ComponentsToMerge.Reserve(Records.Num());

    UStaticMeshComponent* LastSelectedInstance = nullptr;
    TMap<USCS_Node*, UStaticMeshComponent*> InstanceByNode;

    for (const SelectedComponentMerger::FSelectedComponentRecord& Record : Records)
    {
        UStaticMeshComponent* InstanceComponent = SelectedComponentMerger::ResolveSpawnedComponent(
            TempActor,
            Record.Node->GetVariableName());

        if (!InstanceComponent || !InstanceComponent->GetStaticMesh())
        {
            TempActor->Destroy();
            SelectedComponentMerger::Notify(
                FText::Format(
                    LOCTEXT("ResolveFailedFmt", "Could not resolve spawned component '{0}'. No Blueprint changes were made."),
                    FText::FromName(Record.Node->GetVariableName())),
                SNotificationItem::CS_Fail);
            return;
        }

        InstanceByNode.Add(Record.Node, InstanceComponent);
        if (Record.Node == LastSelectedNode)
        {
            LastSelectedInstance = InstanceComponent;
        }
    }

    if (!LastSelectedInstance)
    {
        TempActor->Destroy();
        SelectedComponentMerger::Notify(LOCTEXT("LastInstanceMissing", "Could not resolve the last-selected component instance."), SNotificationItem::CS_Fail);
        return;
    }

    const USceneComponent* ParentInstance = LastSelectedInstance->GetAttachParent();
    if (!ParentInstance)
    {
        TempActor->Destroy();
        SelectedComponentMerger::Notify(
            LOCTEXT("ParentInstanceMissing", "Could not resolve the shared parent component instance."),
            SNotificationItem::CS_Fail);
        return;
    }

    const FTransform ParentWorldTransform = ParentInstance->GetComponentTransform();

    // Pivot policy:
    //   - Position: the direct parent's origin.
    //   - Axes/scale: Blueprint-root/world aligned (identity rotation, unit scale).
    //
    // Example: parent world X rotation +90 -> merged component relative X rotation -90,
    // so the resulting component frame cancels the parent rotation while staying at the
    // parent's origin. Parent scale is similarly countered.
    const FTransform ParentOriginFrame(
        FQuat::Identity,
        ParentWorldTransform.GetLocation(),
        FVector::OneVector);

    const FTransform MergedRelativeTransform = ParentOriginFrame.GetRelativeTransform(ParentWorldTransform);

    // Rebase source geometry into the ParentOriginFrame before merging. This bakes the
    // parent's rotation/scale into the generated mesh, while the new component applies
    // the inverse relative offset above so the final result preserves the original look.
    for (const SelectedComponentMerger::FSelectedComponentRecord& Record : Records)
    {
        UStaticMeshComponent* InstanceComponent = InstanceByNode.FindRef(Record.Node);
        if (!InstanceComponent)
        {
            continue;
        }

        const FTransform OriginFrameLocalTransform =
            InstanceComponent->GetComponentTransform().GetRelativeTransform(ParentOriginFrame);

        InstanceComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
        InstanceComponent->SetWorldTransform(OriginFrameLocalTransform, false, nullptr, ETeleportType::TeleportPhysics);
        InstanceComponent->UpdateComponentToWorld();
        ComponentsToMerge.Add(InstanceComponent);
    }

    const FString SourceFolder = FPackageName::GetLongPackagePath(NamingSourceMesh->GetOutermost()->GetName());
    const FString MergedFolder = SourceFolder / TEXT("Merged");

    FString SafeVariableName = LastSelectedNode->GetVariableName().ToString();
    SafeVariableName.ReplaceInline(TEXT(" "), TEXT("_"));

    const FString DesiredPackageName = MergedFolder / FString::Printf(TEXT("SM_%s_Merged"), *SafeVariableName);
    FString UniquePackageName;
    FString UniqueAssetName;
    FAssetToolsModule::GetModule().Get().CreateUniqueAssetName(
        DesiredPackageName,
        TEXT(""),
        UniquePackageName,
        UniqueAssetName);

    FMeshMergingSettings MergeSettings;
    MergeSettings.LODSelectionType = EMeshLODSelectionType::AllLODs;
    MergeSettings.bPivotPointAtZero = true;   // Parent Origin after rebasing above.
    MergeSettings.bMergePhysicsData = true;
    MergeSettings.bMergeMaterials = false;    // Preserve effective material references/sections.
    MergeSettings.bBakeVertexDataToMesh = true;
    MergeSettings.bGenerateLightMapUV = false;

    const IMeshMergeUtilities& MeshUtilities =
        FModuleManager::LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();

    TArray<UObject*> CreatedAssets;
    FVector MergedActorLocation = FVector::ZeroVector;
    const float ScreenAreaSize = TNumericLimits<float>::Max();

    MeshUtilities.MergeComponentsToStaticMesh(
        ComponentsToMerge,
        World,
        MergeSettings,
        nullptr,
        nullptr,
        UniquePackageName,
        CreatedAssets,
        MergedActorLocation,
        ScreenAreaSize,
        true);

    TempActor->Destroy();

    UStaticMesh* MergedMesh = nullptr;
    for (UObject* CreatedAsset : CreatedAssets)
    {
        if (!MergedMesh)
        {
            MergedMesh = Cast<UStaticMesh>(CreatedAsset);
        }
    }

    if (!MergedMesh)
    {
        SelectedComponentMerger::Notify(LOCTEXT("MergeFailed", "Mesh merge failed; no Static Mesh asset was generated."), SNotificationItem::CS_Fail);
        return;
    }

    FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    for (UObject* CreatedAsset : CreatedAssets)
    {
        if (!CreatedAsset)
        {
            continue;
        }

        CreatedAsset->SetFlags(RF_Public | RF_Standalone);
        CreatedAsset->MarkPackageDirty();
        AssetRegistry.AssetCreated(CreatedAsset);
    }

    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    USCS_Node* ParentNode = SharedParentNode;
    const FString LastSelectedVariableName = LastSelectedNode->GetVariableName().ToString();

    {
        const FScopedTransaction Transaction(LOCTEXT("MergeTransaction", "Merge Selected Blueprint Static Mesh Components"));
        Blueprint->Modify();
        SCS->Modify();
        ParentNode->Modify();

        const FString DesiredVariableString = LastSelectedVariableName + TEXT("_Merged");
        const FName NewVariableName = SCS->GenerateNewComponentName(LastSelectedTemplate->GetClass(), FName(*DesiredVariableString));

        USCS_Node* NewNode = SCS->CreateNode(LastSelectedTemplate->GetClass(), NewVariableName);
        if (!NewNode)
        {
            SelectedComponentMerger::Notify(LOCTEXT("CreateNodeFailed", "Merged mesh was created, but the duplicate Blueprint component could not be created."), SNotificationItem::CS_Fail);
            return;
        }

        UStaticMeshComponent* NewTemplate = Cast<UStaticMeshComponent>(NewNode->ComponentTemplate);
        if (!NewTemplate)
        {
            SelectedComponentMerger::Notify(LOCTEXT("CreateTemplateFailed", "Merged mesh was created, but the duplicate StaticMeshComponent template could not be created."), SNotificationItem::CS_Fail);
            return;
        }

        // Duplicate the last-selected component's editable template properties, then
        // replace mesh/material overrides and apply the parent-countering transform.
        SelectedComponentMerger::CopyComponentTemplateProperties(LastSelectedTemplate, NewTemplate);

        NewTemplate->Modify();
        NewTemplate->SetStaticMesh(MergedMesh);
        NewTemplate->EmptyOverrideMaterials();
        NewTemplate->SetRelativeTransform(MergedRelativeTransform);
        NewTemplate->PostEditChange();

        NewNode->AttachToName = LastSelectedNode->AttachToName;
        NewNode->CategoryName = LastSelectedNode->CategoryName;

        // Add under the same parent, then force the merged node to the top of that
        // parent's child list in the Components panel. ChildNodes is public on USCS_Node.
        ParentNode->AddChildNode(NewNode, true);
        ParentNode->ChildNodes.Remove(NewNode);
        ParentNode->ChildNodes.Insert(NewNode, 0);

        // Delete the selected source components. Leaf-only validation above makes this
        // deterministic and prevents unrelated child components from being moved/deleted.
        for (const SelectedComponentMerger::FSelectedComponentRecord& Record : Records)
        {
            USCS_Node* SourceNode = Record.Node;
            if (!SourceNode)
            {
                continue;
            }

            SourceNode->Modify();
            SCS->RemoveNode(SourceNode, false);
        }

        SCS->ValidateSceneRootNodes();
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        GEditor->RedrawAllViewports();
    }

    if (!IsRunningCommandlet())
    {
        FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
        ContentBrowser.Get().SyncBrowserToAssets(CreatedAssets, true);
    }

    const FText SuccessText = FText::Format(
        LOCTEXT("MergeSuccessFmt", "Merged {0} components into '{1}'. Source components deleted. New component: {2}. Pivot: parent origin with parent transform counter-offset."),
        FText::AsNumber(Records.Num()),
        FText::FromString(MergedMesh->GetName()),
        FText::FromString(LastSelectedVariableName + TEXT("_Merged")));

    SelectedComponentMerger::Notify(SuccessText, SNotificationItem::CS_Success);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSelectedComponentMergerModule, SelectedComponentMerger)
