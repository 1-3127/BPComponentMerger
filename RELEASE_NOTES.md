# Release Notes

Project development concluded on 2026-09-30 because the original production issue that motivated this plugin was resolved through a separate workflow.

## v0.3.0 — Final validated snapshot

**Package:** `SelectedComponentMerger.zip`

**SHA-256:** `be4bd7915a56924ea6fb38bd3224de3c172520931748c451f141e39bbb49919b`

Status: validated in the target Unreal Engine 5.6.1 project.

Highlights:

- Merge selected sibling Blueprint StaticMeshComponents inside Unreal
- Preserve Unreal Material Asset references
- Parent Origin pivot policy
- Parent rotation/scale counter-offset
- Preserve final visual placement
- Write merged Static Mesh under `<SourceFolder>/Merged/`
- Delete successfully merged source components
- Keep v0.3.0 as the repository `main` baseline

Known non-blocking issue:

- Components panel child ordering does not reliably place the generated component at the visual top of the list.

## v0.4.0-test — Experimental Blueprint Pack snapshot

**Package:** `BPComponentMerger-feature-bp-pack-v0.4.0-test.zip`

**SHA-256:** `4504bbb20154f9dca973aa975959b3c6885076db1bd6a066f325df1c84009be4`

**Branch:** `feature/bp-pack-v0.4.0-test`

Status: experimental/discontinued. Not merged to `main`.

Adds a second action while keeping the v0.3.0 Static Mesh merge path:

```text
Pack Selected Static Mesh Components to Blueprint
```

Intended behavior:

- Do not merge geometry
- Create a new Blueprint under the same `Merged/` output folder
- Preserve selected meshes as separate StaticMeshComponents
- Rebase them to the Parent Origin frame
- Replace selected source components with one ChildActorComponent referencing the generated Blueprint
- Reuse the Parent rotation/scale counter-offset policy

The UE 5.6.1 `SetChildActorClass` compile mismatch found during development was corrected in this snapshot. Further runtime validation was stopped because the original production problem no longer required this extension.

## Project closure

The plugin remains available as a completed utility and experiment record, but no further feature development is planned for the original problem scope.
