#include "SelectedComponentMergerModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/ChildActorComponent.h"
#include "Components/SceneComponent.h"
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
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "SubobjectEditorMenuContext.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "SelectedComponentMergerBlueprintPack"

namespace SelectedComponentMergerBlueprintPack
{
    struct FSelectedComponentRecord
    {
        UStaticMeshComponent* Template = nullptr;
        USCS_Node* Node = nullptr;
    };

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

    static void CopyComponentTemplateProperties(
        const UStaticMeshComponent* Source,
        UStaticMeshComponent* Destination)
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

            if (Property->HasAnyPropertyFlags(
                CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient))
            {
                continue;
            }

            Property->CopyCompleteValue_InContainer(Destination, Source);
        }
    }

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
                OutError = LOCTEXT("NoBlueprint", "Could not resolve the owning Blueprint.");
                return false;
            }

            if (!OutBlueprint)
            {
                OutBlueprint = Blueprint;
            }
            else if (OutBlueprint != Blueprint)
            {
                OutError = LOCTEXT(
                    "DifferentBlueprints",
                    "All selected components must belong to the same Blueprint.");
                return false;
            }

            USCS_Node* Node = FindNodeForTemplate(Blueprint, Component);
            if (!Node)
            {
                OutError = FText::Format(
                    LOCTEXT(
                        "NoSCSNodeFmt",
                        "'{0}' is not a directly-owned SCS component."),
                    FText::FromName(Component->GetFName()));
                return false;
            }

            FSelectedComponentRecord& Record = OutRecords.AddDefaulted_GetRef();
            Record.Template = Component;
            Record.Node = Node;
        }

        if (!OutBlueprint || OutRecords.Num() < 2)
        {
            OutError = LOCTEXT(
                "NeedTwoValid",
                "Select at least two valid Static Mesh Components with assigned meshes.");
            return false;
        }

        OutLastSelectedTemplate = OutRecords.Last().Template;
        OutLastSelectedNode = OutRecords.Last().Node;

        return OutLastSelectedTemplate && OutLastSelectedNode;
    }
}

void FSelectedComponentMergerModule::ExecutePackToBlueprint(
    TWeakObjectPtr<USubobjectEditorMenuContext> WeakContext)
{
    using namespace SelectedComponentMergerBlueprintPack;

    USubobjectEditorMenuContext* Context = WeakContext.Get();
    TArray<FSelectedComponentRecord> Records;
    UBlueprint* SourceBlueprint = nullptr;
    UStaticMeshComponent* LastSelectedTemplate = nullptr;
    USCS_Node* LastSelectedNode = nullptr;
    FText Error;

    if (!GatherSelection(
        Context,
        Records,
        SourceBlueprint,
        LastSelectedTemplate,
        LastSelectedNode,
        Error))
    {
        Notify(Error, SNotificationItem::CS_Fail);
        return;
    }

    if (!SourceBlueprint->GeneratedClass ||
        !SourceBlueprint->GeneratedClass->IsChildOf(AActor::StaticClass()))
    {
        Notify(
            LOCTEXT("NotActorBlueprint", "The owning Blueprint is not an Actor Blueprint."),
            SNotificationItem::CS_Fail);
        return;
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Notify(
            LOCTEXT("NoEditorWorld", "No Editor World is available."),
            SNotificationItem::CS_Fail);
        return;
    }

    UStaticMesh* NamingSourceMesh = LastSelectedTemplate->GetStaticMesh();
    if (!NamingSourceMesh)
    {
        Notify(
            LOCTEXT("NoNamingSource", "The last-selected component has no Static Mesh."),
            SNotificationItem::CS_Fail);
        return;
    }

    USCS_Node* SharedParentNode = FindParentNode(SourceBlueprint, LastSelectedNode);
    if (!SharedParentNode)
    {
        Notify(
            LOCTEXT(
                "NoSharedParent",
                "The selected components must be children of the same Blueprint parent component."),
            SNotificationItem::CS_Fail);
        return;
    }

    for (const FSelectedComponentRecord& Record : Records)
    {
        if (FindParentNode(SourceBlueprint, Record.Node) != SharedParentNode)
        {
            Notify(
                LOCTEXT(
                    "DifferentParents",
                    "All selected Static Mesh Components must share the same direct parent."),
                SNotificationItem::CS_Fail);
            return;
        }

        if (Record.Node->GetChildNodes().Num() > 0)
        {
            Notify(
                FText::Format(
                    LOCTEXT(
                        "SourceHasChildrenFmt",
                        "Selected component '{0}' has child components. Blueprint packing supports leaf Static Mesh Components only."),
                    FText::FromName(Record.Node->GetVariableName())),
                SNotificationItem::CS_Fail);
            return;
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.ObjectFlags |= RF_Transient;
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.bHideFromSceneOutliner = true;

    AActor* TempActor = World->SpawnActor<AActor>(
        SourceBlueprint->GeneratedClass,
        FTransform::Identity,
        SpawnParams);

    if (!TempActor)
    {
        Notify(
            LOCTEXT(
                "SpawnFailed",
                "Failed to create a temporary Blueprint instance for packing."),
            SNotificationItem::CS_Fail);
        return;
    }

    UStaticMeshComponent* LastSelectedInstance = nullptr;
    TMap<USCS_Node*, UStaticMeshComponent*> InstanceByNode;

    for (const FSelectedComponentRecord& Record : Records)
    {
        UStaticMeshComponent* InstanceComponent =
            ResolveSpawnedComponent(TempActor, Record.Node->GetVariableName());

        if (!InstanceComponent || !InstanceComponent->GetStaticMesh())
        {
            TempActor->Destroy();
            Notify(
                FText::Format(
                    LOCTEXT(
                        "ResolveFailedFmt",
                        "Could not resolve spawned component '{0}'. No Blueprint changes were made."),
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
        Notify(
            LOCTEXT(
                "LastInstanceMissing",
                "Could not resolve the last-selected component instance."),
            SNotificationItem::CS_Fail);
        return;
    }

    const USceneComponent* ParentInstance = LastSelectedInstance->GetAttachParent();
    if (!ParentInstance)
    {
        TempActor->Destroy();
        Notify(
            LOCTEXT(
                "ParentInstanceMissing",
                "Could not resolve the shared parent component instance."),
            SNotificationItem::CS_Fail);
        return;
    }

    const FTransform ParentWorldTransform = ParentInstance->GetComponentTransform();

    // Same policy as Static Mesh mode:
    // position = direct parent origin, axes/scale = root/world aligned.
    const FTransform ParentOriginFrame(
        FQuat::Identity,
        ParentWorldTransform.GetLocation(),
        FVector::OneVector);

    const FTransform PackedActorRelativeTransform =
        ParentOriginFrame.GetRelativeTransform(ParentWorldTransform);

    TMap<USCS_Node*, FTransform> PackedRelativeTransforms;

    for (const FSelectedComponentRecord& Record : Records)
    {
        UStaticMeshComponent* InstanceComponent = InstanceByNode.FindRef(Record.Node);
        if (!InstanceComponent)
        {
            TempActor->Destroy();
            Notify(
                LOCTEXT(
                    "MissingInstance",
                    "A selected component instance disappeared during packing."),
                SNotificationItem::CS_Fail);
            return;
        }

        PackedRelativeTransforms.Add(
            Record.Node,
            InstanceComponent->GetComponentTransform().GetRelativeTransform(
                ParentOriginFrame));
    }

    TempActor->Destroy();

    const FString SourceFolder = FPackageName::GetLongPackagePath(
        NamingSourceMesh->GetOutermost()->GetName());

    const FString MergedFolder = SourceFolder / TEXT("Merged");

    FString SafeVariableName = LastSelectedNode->GetVariableName().ToString();
    SafeVariableName.ReplaceInline(TEXT(" "), TEXT("_"));

    const FString DesiredPackageName =
        MergedFolder / FString::Printf(TEXT("BP_%s_Packed"), *SafeVariableName);

    FString UniquePackageName;
    FString UniqueAssetName;

    FAssetToolsModule::GetModule().Get().CreateUniqueAssetName(
        DesiredPackageName,
        TEXT(""),
        UniquePackageName,
        UniqueAssetName);

    UPackage* PackedPackage = CreatePackage(*UniquePackageName);
    if (!PackedPackage)
    {
        Notify(
            LOCTEXT(
                "PackageFailed",
                "Could not create a package for the packed Blueprint."),
            SNotificationItem::CS_Fail);
        return;
    }

    UBlueprint* PackedBlueprint = FKismetEditorUtilities::CreateBlueprint(
        AActor::StaticClass(),
        PackedPackage,
        FName(*UniqueAssetName),
        BPTYPE_Normal,
        FName(TEXT("SelectedComponentMerger")));

    if (!PackedBlueprint || !PackedBlueprint->SimpleConstructionScript)
    {
        Notify(
            LOCTEXT(
                "BlueprintFailed",
                "Could not create the packed Blueprint asset."),
            SNotificationItem::CS_Fail);
        return;
    }

    USimpleConstructionScript* PackedSCS =
        PackedBlueprint->SimpleConstructionScript;

    PackedBlueprint->Modify();
    PackedSCS->Modify();

    USCS_Node* PackedRootNode = PackedSCS->GetDefaultSceneRootNode();
    if (!PackedRootNode)
    {
        PackedRootNode = PackedSCS->CreateNode(
            USceneComponent::StaticClass(),
            FName(TEXT("DefaultSceneRoot")));

        if (!PackedRootNode)
        {
            Notify(
                LOCTEXT(
                    "RootFailed",
                    "Could not create a root component for the packed Blueprint."),
                SNotificationItem::CS_Fail);
            return;
        }

        PackedSCS->AddNode(PackedRootNode);
    }

    for (const FSelectedComponentRecord& Record : Records)
    {
        const FName NewVariableName = PackedSCS->GenerateNewComponentName(
            Record.Template->GetClass(),
            Record.Node->GetVariableName());

        USCS_Node* NewPackedNode =
            PackedSCS->CreateNode(Record.Template->GetClass(), NewVariableName);

        if (!NewPackedNode)
        {
            Notify(
                FText::Format(
                    LOCTEXT(
                        "CreatePackedNodeFmt",
                        "Could not create packed component '{0}'."),
                    FText::FromName(Record.Node->GetVariableName())),
                SNotificationItem::CS_Fail);
            return;
        }

        UStaticMeshComponent* NewPackedTemplate =
            Cast<UStaticMeshComponent>(NewPackedNode->ComponentTemplate);

        if (!NewPackedTemplate)
        {
            Notify(
                LOCTEXT(
                    "CreatePackedTemplateFailed",
                    "Could not create a packed StaticMeshComponent template."),
                SNotificationItem::CS_Fail);
            return;
        }

        CopyComponentTemplateProperties(
            Record.Template,
            NewPackedTemplate);

        NewPackedTemplate->Modify();
        NewPackedTemplate->SetRelativeTransform(
            PackedRelativeTransforms.FindRef(Record.Node));
        NewPackedTemplate->PostEditChange();

        NewPackedNode->CategoryName = Record.Node->CategoryName;
        PackedRootNode->AddChildNode(NewPackedNode, true);
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(PackedBlueprint);
    FKismetEditorUtilities::CompileBlueprint(PackedBlueprint);

    if (!PackedBlueprint->GeneratedClass)
    {
        Notify(
            LOCTEXT(
                "CompileFailed",
                "The packed Blueprint was created but did not produce a generated class."),
            SNotificationItem::CS_Fail);
        return;
    }

    PackedBlueprint->SetFlags(RF_Public | RF_Standalone);
    PackedBlueprint->MarkPackageDirty();

    FAssetRegistryModule& AssetRegistry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    AssetRegistry.AssetCreated(PackedBlueprint);

    USimpleConstructionScript* SourceSCS =
        SourceBlueprint->SimpleConstructionScript;

    const FString LastSelectedVariableName =
        LastSelectedNode->GetVariableName().ToString();

    {
        const FScopedTransaction Transaction(
            LOCTEXT(
                "PackTransaction",
                "Pack Selected Blueprint Static Mesh Components"));

        SourceBlueprint->Modify();
        SourceSCS->Modify();
        SharedParentNode->Modify();

        const FString DesiredVariableString =
            LastSelectedVariableName + TEXT("_Packed");

        const FName NewVariableName =
            SourceSCS->GenerateNewComponentName(
                UChildActorComponent::StaticClass(),
                FName(*DesiredVariableString));

        USCS_Node* ChildActorNode =
            SourceSCS->CreateNode(
                UChildActorComponent::StaticClass(),
                NewVariableName);

        if (!ChildActorNode)
        {
            Notify(
                LOCTEXT(
                    "ChildNodeFailed",
                    "Packed Blueprint was created, but the replacement ChildActorComponent could not be created."),
                SNotificationItem::CS_Fail);
            return;
        }

        UChildActorComponent* ChildActorTemplate =
            Cast<UChildActorComponent>(ChildActorNode->ComponentTemplate);

        if (!ChildActorTemplate)
        {
            Notify(
                LOCTEXT(
                    "ChildTemplateFailed",
                    "Packed Blueprint was created, but the replacement ChildActorComponent template could not be created."),
                SNotificationItem::CS_Fail);
            return;
        }

        ChildActorTemplate->Modify();

        UClass* PackedGeneratedClass = PackedBlueprint->GeneratedClass.Get();
        if (!PackedGeneratedClass || !PackedGeneratedClass->IsChildOf(AActor::StaticClass()))
        {
            Notify(
                LOCTEXT(
                    "InvalidPackedGeneratedClass",
                    "The packed Blueprint did not produce a valid Actor generated class."),
                SNotificationItem::CS_Fail);
            return;
        }

        ChildActorTemplate->SetChildActorClass(PackedGeneratedClass);
        ChildActorTemplate->SetRelativeTransform(PackedActorRelativeTransform);
        ChildActorTemplate->PostEditChange();

        ChildActorNode->AttachToName = LastSelectedNode->AttachToName;
        ChildActorNode->CategoryName = LastSelectedNode->CategoryName;
        SharedParentNode->AddChildNode(ChildActorNode, true);

        for (const FSelectedComponentRecord& Record : Records)
        {
            if (!Record.Node)
            {
                continue;
            }

            Record.Node->Modify();
            SourceSCS->RemoveNode(Record.Node, false);
        }

        SourceSCS->ValidateSceneRootNodes();
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(SourceBlueprint);
        FKismetEditorUtilities::CompileBlueprint(SourceBlueprint);
        GEditor->RedrawAllViewports();
    }

    if (!IsRunningCommandlet())
    {
        TArray<UObject*> AssetsToSync;
        AssetsToSync.Add(PackedBlueprint);

        FContentBrowserModule& ContentBrowser =
            FModuleManager::LoadModuleChecked<FContentBrowserModule>(
                "ContentBrowser");

        ContentBrowser.Get().SyncBrowserToAssets(AssetsToSync, true);
    }

    Notify(
        FText::Format(
            LOCTEXT(
                "PackSuccessFmt",
                "Packed {0} components into Blueprint '{1}'. Source components deleted and replaced with ChildActorComponent '{2}'."),
            FText::AsNumber(Records.Num()),
            FText::FromString(PackedBlueprint->GetName()),
            FText::FromString(LastSelectedVariableName + TEXT("_Packed"))),
        SNotificationItem::CS_Success);
}

#undef LOCTEXT_NAMESPACE
