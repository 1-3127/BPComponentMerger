# Selected Component Merger v0.4.0 (test)

UE 5.6.1 Editor Plugin.

## Modes

### Merge Selected Static Mesh Components
Existing v0.3.0 path. Geometry is merged into one Static Mesh. Behavior intentionally unchanged.

### Pack Selected Static Mesh Components to Blueprint
New v0.4.0 path. Geometry is not merged.

- Selected StaticMeshComponents must be leaf siblings under one direct parent.
- A new Blueprint asset is created in the same `<SourceMeshPath>/Merged/` folder used by Static Mesh merge output.
- The new Blueprint keeps the selected Static Mesh assets/material overrides as separate StaticMeshComponents.
- Internal component transforms are rebased to the same Parent Origin frame used by Static Mesh mode.
- The original selected components are deleted from the source Blueprint.
- They are replaced by one ChildActorComponent that references the generated Blueprint.
- Parent rotation/scale are countered using the same Parent Origin transform policy used by Static Mesh mode.

Example output:

```text
/Game/.../Meshes/Merged/SM_object5214_Merged   // Static Mesh mode
/Game/.../Meshes/Merged/BP_object5214_Packed  // Blueprint mode
```

## Usage

1. Open the source Blueprint.
2. In the BP Viewport, Ctrl-select the target components.
3. Use Details > Visible to verify the selection set if needed.
4. Right-click one selected item in Components.
5. Choose either:
   - `Merge Selected Static Mesh Components`
   - `Pack Selected Static Mesh Components to Blueprint`

## Status

- Static Mesh path: previously validated v0.3.0 behavior, unchanged.
- Blueprint Pack path: newly added in v0.4.0 and requires UE 5.6.1 compile/runtime validation.
