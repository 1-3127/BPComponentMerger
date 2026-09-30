# Selected Component Merger v0.4.0-test

UE 5.6.1 Editor Plugin experimental branch.

## Status

**Discontinued experimental snapshot — 2026-09-30**

최초 프로젝트 제작 동기였던 FBX Merge/Reimport Material Reference 문제 상황이 별도 방식으로 해결되어, 이 branch의 추가 개발/검증을 중단했습니다.

- branch: `feature/bp-pack-v0.4.0-test`
- `main`: 검증된 v0.3.0 유지
- 이 branch는 `main`에 병합하지 않고 실험 기록으로 보존
- v0.4.0-test 배포 ZIP: `BPComponentMerger-feature-bp-pack-v0.4.0-test.zip`
- SHA-256: `4504bbb20154f9dca973aa975959b3c6885076db1bd6a066f325df1c84009be4`

## Modes

### Merge Selected Static Mesh Components

기존 v0.3.0 경로입니다. Geometry를 하나의 Static Mesh로 병합합니다. 기존 경로는 의도적으로 유지했습니다.

### Pack Selected Static Mesh Components to Blueprint

v0.4.0-test에서 추가한 실험 경로입니다. Geometry를 병합하지 않습니다.

- 선택 StaticMeshComponent들은 하나의 Direct Parent 아래 leaf sibling이어야 함
- 새 Blueprint Asset을 `<SourceMeshPath>/Merged/` 아래 생성
- 기존 Static Mesh Asset / Material Override를 개별 StaticMeshComponent로 유지
- 내부 Transform을 Parent Origin frame 기준으로 재구성
- 원본 선택 Component 삭제
- 원본 BP에는 생성 BP를 참조하는 ChildActorComponent 생성
- Parent Rotation / Scale은 v0.3.0과 같은 상쇄 정책 사용

Example:

```text
/Game/.../Meshes/Merged/SM_object5214_Merged
/Game/.../Meshes/Merged/BP_object5214_Packed
```

## Usage

1. BP Viewport에서 `Ctrl` + 다중선택
2. Details > Visible로 선택 범위 확인
3. Components에서 선택 Component 중 하나 우클릭
4. 아래 중 선택
   - `Merge Selected Static Mesh Components`
   - `Pack Selected Static Mesh Components to Blueprint`

## Validation state

- Static Mesh mode: v0.3.0에서 실제 UE 5.6.1 동작 검증 완료
- Blueprint Pack mode: 컴파일 오류 수정까지 반영된 실험 구현
- 프로젝트 종료로 인해 추가 runtime validation은 진행하지 않음
