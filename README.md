# BPComponentMerger

Unreal Engine 5.6.1용 Blueprint `StaticMeshComponent` 정리/병합 Editor Plugin입니다.

## Project status

**개발 종료 / 보존 상태 — 2026-09-30**

이 프로젝트는 FBX Scene Import Blueprint의 Component들을 외부 DCC에서 병합 후 재import할 때 Unreal의 Material Asset Reference가 보존되지 않아 일부 슬롯이 `WorldGridMaterial`로 대체되는 문제를 해결하기 위해 시작되었습니다.

이후 **최초 제작 동기였던 문제 상황 자체가 별도 방식으로 해결**되어 추가 개발 필요성이 사라졌습니다.

현재 보존 정책:

- **v0.3.0** — 실제 UE 5.6.1 프로젝트에서 핵심 동작 검증을 마친 최종 안정 스냅샷
- **v0.4.0-test** — Blueprint Pack 선택지를 추가한 실험 버전. `feature/bp-pack-v0.4.0-test` branch에 보존
- v0.4.0-test는 추가 개발/검증을 중단했으며 `main`에 병합하지 않음
- `main`은 검증된 v0.3.0 상태를 유지

## Release packages

| Version | Package | Status |
| --- | --- | --- |
| v0.3.0 | `SelectedComponentMerger.zip` | Validated / final stable snapshot |
| v0.4.0-test | `BPComponentMerger-feature-bp-pack-v0.4.0-test.zip` | Experimental / discontinued |

SHA-256:

```text
v0.3.0
be4bd7915a56924ea6fb38bd3224de3c172520931748c451f141e39bbb49919b

v0.4.0-test
4504bbb20154f9dca973aa975959b3c6885076db1bd6a066f325df1c84009be4
```

## v0.3.0 — validated behavior

From a Blueprint Components panel, select 2+ sibling `StaticMeshComponent`s and run:

```text
Right Click
→ Merge Selected Static Mesh Components
```

Confirmed behavior:

- selected Component subset만 Merge
- 기존 Unreal Material Asset Reference 유지
- Parent Origin 기준 Pivot
- Parent Rotation / Scale 상쇄 Offset
- Merge 후 시각적 위치/회전/크기 유지
- 결과 Static Mesh를 `<SourceFolder>/Merged/` 아래 생성
- 결과용 새 StaticMeshComponent 생성
- 선택한 원본 Component 삭제
- Blueprint compile / refresh

Known non-blocking issue:

- 결과 Component를 Parent의 Children 최상단에 배치하려는 UI 순서 변경은 안정적으로 반영되지 않음
- Merge 결과에는 영향 없음

## Usage

권장 루틴:

1. 대상 Blueprint를 연다.
2. BP **Viewport**에서 `Ctrl`을 누른 채 대상 Component들을 모두 선택한다.
3. **Details > Visible**을 이용해 의도한 Component들이 전부 선택되었는지 확인한다.
4. **Components** 패널에서 선택된 Component 중 하나를 우클릭한다.
5. `Merge Selected Static Mesh Components`를 실행한다.
6. 결과 Static Mesh / Component를 확인하고 저장한다.

요약:

```text
BP Viewport Ctrl + 다중선택
→ Details > Visible로 선택 범위 확인
→ Components에서 선택 항목 우클릭
→ Merge Selected Static Mesh Components
```

## v0.3.0 transform policy

Merge frame:

```text
Location = Direct Parent Origin
Rotation = Blueprint Root / World-aligned Identity
Scale    = 1,1,1
```

Source geometry를 이 frame으로 Bake하고, 새 Component는 원래 Parent 아래에 유지합니다. 따라서 Parent Rotation/Scale을 상쇄하는 Relative Transform을 적용합니다.

예:

```text
Parent Rotation X = +90
→ Merged Component Relative Rotation X ≈ -90
```

고정 `-90`, `0.01` 등의 보정값은 하드코딩하지 않습니다.

## v0.4.0-test branch

Branch:

```text
feature/bp-pack-v0.4.0-test
```

기존 Static Mesh Merge 경로는 유지하면서 아래 선택지를 추가했습니다.

```text
Pack Selected Static Mesh Components to Blueprint
```

목표 동작:

- Geometry를 하나의 Static Mesh로 합치지 않음
- 선택 Mesh들을 새 Blueprint 내부의 개별 StaticMeshComponent로 유지
- 새 Blueprint Asset도 기존과 동일하게 `<SourceMeshPath>/Merged/`에 생성
- Parent Origin + Parent Transform 상쇄 정책 재사용
- 원본 선택 Component 삭제
- 원본 BP에는 생성된 BP를 참조하는 ChildActorComponent 배치

v0.4.0-test는 컴파일 오류 수정까지 반영된 실험 스냅샷으로 보존하며, 프로젝트 종료로 인해 추가 runtime validation과 `main` 병합은 진행하지 않습니다.

## Safety / limitations

- 선택 Component들은 동일한 Direct Parent 아래의 sibling이어야 함
- destructive replacement 대상은 leaf `StaticMeshComponent`로 제한
- 삭제되는 Component Variable을 Blueprint Graph에서 참조하고 있다면 참조가 깨질 수 있음
- Negative Scale, Non-uniform Scale, Collision, Nanite, 복수 LOD, 대량 Component, 완전한 Undo/asset cleanup 등은 범용 검증하지 않음

## Original problem

기존 흐름:

```text
FBX Scene Import
→ Blueprint 내부에 파츠별 StaticMeshComponent
→ FBX Export
→ 외부 DCC Merge
→ FBX Reimport
→ Material Reference 손실 / WorldGridMaterial
```

v0.3.0은 이를 UE 내부 Merge로 대체했습니다.

```text
Blueprint
→ Component 선택
→ UE 내부 Merge
→ Material Reference 유지
```

현재는 이 **원래 문제 상황 자체가 별도 방식으로 해결되어** 본 프로젝트의 추가 개발을 종료했습니다.

## Version history

### v0.4.0-test
- 기존 v0.3.0 Static Mesh Merge 경로 유지
- `Pack Selected Static Mesh Components to Blueprint` 실험 경로 추가
- 같은 `Merged/` 폴더에 Blueprint Asset 생성
- Parent Origin transform policy 재사용
- ChildActorComponent replacement 방식 구현
- 실험 branch로 보존, 추가 개발 중단

### v0.3.0
- Parent Origin Pivot 확정
- Parent Rotation / Scale 상쇄 Offset
- Material Reference 유지
- Merge 후 원본 Component 삭제
- 시각적 위치/크기/방향 유지 검증
- UE 5.6.1 실제 프로젝트에서 완료 판정

### v0.2.x
- 결과 Mesh를 새 Component에 할당
- `Merged/` 출력 경로 추가
- UE 5.6.1 Components selection API 호환 수정

### v0.1.x
- Components 패널 선택 기반 UE 내부 Merge 최초 구현
- Material Reference 보존 확인
- 초기 Transform / Pivot 문제 발견 및 재설계
