# BPComponentMerger

Unreal Engine 5.6.1용 **Blueprint StaticMeshComponent 선택 병합 Editor Plugin**입니다.

FBX Scene Import로 생성된 Blueprint 내부에서 여러 `StaticMeshComponent`를 직접 선택해 Unreal 내부에서 하나의 Static Mesh로 병합합니다. 외부 DCC로 FBX를 내보냈다가 다시 가져오는 과정에서 발생하던 **Material Asset Reference 손실 / WorldGridMaterial 대체 문제**를 피하는 것이 이 플러그인의 출발점입니다.

## Status

**v0.3.0 — 기능 완료**

실제 UE 5.6.1 프로젝트에서 다음 항목을 확인했습니다.

- Components 패널 선택 기반 Merge
- 기존 Material Reference 유지
- Parent Origin 기준 Pivot
- Parent Transform 상쇄 Offset
- Merge 후 시각적 위치 / 회전 / 크기 유지
- 선택한 원본 Component 삭제
- `Merged` 하위 폴더에 결과 Static Mesh 생성
- 결과용 새 StaticMeshComponent 생성

> Known non-blocking issue: 결과 Component를 Parent의 Children 중 Components 패널 최상단에 배치하려는 처리는 UE 5.6.1 Editor UI에서 기대대로 표시되지 않을 수 있습니다. Merge 기능에는 영향이 없으며 현재 기능 계약에는 포함하지 않습니다.

## Problem this solves

기존 작업 흐름은 다음과 같았습니다.

```text
FBX Scene Import
→ Blueprint 내부에 파츠별 StaticMeshComponent 생성
→ UE에서 FBX Export
→ 외부 DCC에서 Mesh Merge
→ FBX Reimport
→ Material 경로/참조 손실
→ WorldGridMaterial 또는 수동 Material 복원
```

이 플러그인은 해당 과정을 다음처럼 줄입니다.

```text
Blueprint 열기
→ 병합할 Component 선택
→ Merge Selected Static Mesh Components
→ 완료
```

Material 이름이 같더라도 Unreal 내부의 실제 Material Asset Reference를 기준으로 병합하므로 FBX 왕복에서 발생하는 경로 손실을 피할 수 있습니다.

## Requirements

- Unreal Engine **5.6.1**
- Windows
- C++ 프로젝트 빌드가 가능한 Visual Studio 2022 환경
- Blueprint 기반 프로젝트도 사용 가능
  - 프로젝트에 이 Source Plugin을 추가하면 UnrealBuildTool이 필요한 Editor 모듈을 빌드합니다.

## Installation

저장소를 프로젝트의 `Plugins` 아래에 배치합니다.

```text
YourProject/
└─ Plugins/
   └─ SelectedComponentMerger/
      ├─ SelectedComponentMerger.uplugin
      └─ Source/
```

기존 버전에서 교체하는 경우:

1. Unreal Editor 종료
2. 기존 `Plugins/SelectedComponentMerger/` 교체
3. 플러그인 내부의 `Binaries/`, `Intermediate/`가 있다면 삭제
4. `.uproject` 실행
5. Rebuild 요청 승인
6. `Edit > Plugins`에서 **Selected Component Merger** 활성 상태 확인

## Usage

권장 사용 루틴:

1. 대상 Blueprint를 엽니다.
2. Blueprint **Viewport**에서 `Ctrl`을 누른 채 Merge할 Component들을 모두 선택합니다.
3. **Details > Visible**을 이용해 의도한 Component들이 전부 선택되어 있는지 시각적으로 확인합니다.
   - 이 단계는 선택 범위 검증용입니다.
4. **Components** 패널에서 현재 선택된 Component 중 하나를 우클릭합니다.
5. **Merge Selected Static Mesh Components**를 클릭합니다.
6. 결과 Static Mesh와 Component를 확인하고 Blueprint / 생성 Asset을 저장합니다.

요약:

```text
BP Viewport에서 Ctrl + 다중선택
→ Details > Visible로 선택 범위 검증
→ Components에서 선택 항목 우클릭
→ Merge Selected Static Mesh Components
→ 완료
```

## Merge behavior

v0.3.0의 동작 규칙입니다.

### Selection constraints

- 2개 이상의 `StaticMeshComponent` 필요
- 선택 Component들은 **같은 Direct Parent** 아래의 sibling이어야 함
- 선택 Component들은 안전한 삭제를 위해 **leaf node**여야 함
- child가 달린 선택 Component가 있으면 작업을 중단

### Output asset

결과 Static Mesh는 마지막 선택 Component의 원본 Mesh 경로를 기준으로:

```text
<SourceFolder>/Merged/
```

아래에 생성됩니다.

예:

```text
/Game/Metro/Meshes/SM_A
→ /Game/Metro/Meshes/Merged/SM_<LastSelectedVariable>_Merged
```

### Material

- `Merge Materials = false`
- Material atlas/bake를 수행하지 않음
- 원본 Component에 적용된 Unreal Material reference/section을 유지

### Pivot / Transform

Pivot 정책은 **Parent Origin**입니다.

Merge frame:

```text
Location = Direct Parent Origin
Rotation = Blueprint Root / World-aligned Identity
Scale    = 1,1,1
```

Source geometry를 이 frame으로 Bake하고, 새 Merged Component는 원래 Parent 아래에 유지됩니다. 따라서 Parent의 Rotation / Scale을 상쇄하는 Relative Transform을 자동 계산합니다.

예:

```text
Parent Rotation X = +90
→ Merged Component Relative Rotation X ≈ -90
→ 합성 결과는 Root-aligned
```

고정 `-90`, `0.01` 같은 보정값을 하드코딩하지 않습니다.

### Source components

Merge 성공 후:

- 선택한 원본 Component들은 **삭제**
- 결과용 새 Component 생성
- 마지막 선택 Component를 property/name template으로 사용
- 결과 Component에 Merged Static Mesh 할당
- 기존 Material Override는 제거하여 Merged Mesh의 Material slot을 사용

## Safety / limitations

### Destructive component replacement

Merge가 성공하면 선택한 원본 Component를 Blueprint SCS에서 삭제합니다.

따라서 해당 Component Variable을 Blueprint Graph에서 직접 참조하고 있다면 참조가 깨질 수 있습니다.

주 대상은:

- FBX Scene Import Blueprint
- 개별 StaticMeshComponent에 별도 Blueprint 로직 참조가 없는 구조

입니다.

중요 Blueprint에서는 복제본 또는 Source Control 상태에서 먼저 테스트하는 것을 권장합니다.

### Reimport

FBX Scene Import Blueprint를 다시 `Reimport Hierarchy`하면 Merge 후 수정한 Component 구조와 충돌할 수 있습니다.

장기 운용 시에는 다음 분리를 권장합니다.

```text
Source FBX Blueprint
→ 복제
→ Optimization Blueprint
→ BPComponentMerger 적용
```

### Cases worth further validation

현재 핵심 요구사항은 완료됐지만, 다음 케이스는 실제 사용량이 늘면 추가 검증 대상입니다.

- Negative Scale / Mirrored Mesh
- Non-uniform Scale
- Collision
- Nanite
- 다중 LOD
- 매우 많은 Component
- Undo 시 생성 Asset까지 포함한 완전 복구
- 실제 사용자 클릭 순서와 Context selection 배열 순서의 장기적 일치 여부

## Known issue

### Components panel ordering

Merged Component를 Direct Parent의 첫 번째 Child로 배치하려는 처리는 현재 Components 패널에서 안정적으로 반영되지 않습니다.

실제 Merge 결과, Transform, Material, 원본 삭제에는 영향이 없으므로 **non-blocking**으로 분류했습니다.

## Project structure

```text
SelectedComponentMerger/
├─ SelectedComponentMerger.uplugin
├─ Source/
│  └─ SelectedComponentMerger/
│     ├─ SelectedComponentMerger.Build.cs
│     ├─ Public/
│     │  └─ SelectedComponentMergerModule.h
│     └─ Private/
│        └─ SelectedComponentMergerModule.cpp
└─ README.md
```

## Implementation outline

Editor Plugin이 Blueprint Components context menu를 확장하고 선택된 Component 정보를 가져온 뒤 Unreal의 mesh merge 계층을 사용합니다.

핵심 처리 흐름:

```text
Selected Blueprint StaticMeshComponents
→ selection validation
→ shared direct parent 확인
→ live/transient merge representation 구성
→ Parent Origin frame으로 Transform bake
→ IMeshMergeUtilities::MergeComponentsToStaticMesh
→ <SourceFolder>/Merged/에 Static Mesh 생성
→ 결과 Component 생성 및 counter-transform 적용
→ 선택 원본 Component 삭제
→ Blueprint compile / refresh
```

## Version history

### v0.3.0

- Parent Origin Pivot 확정
- Parent Rotation / Scale 상쇄 Offset 적용
- Merge 후 원본 Component 삭제
- 시각적 위치 / 크기 / 방향 유지 검증
- 결과 Component 생성 방식 확정
- 기능 완료 판정

### v0.2.x

- 결과 Mesh를 별도 Component에 할당하는 구조로 변경
- `Merged/` 출력 경로 추가
- UE 5.6.1 Components selection API 호환 수정

### v0.1.x

- Components 패널 선택 기반 Merge 최초 구현
- Material Reference 보존
- BP-only 프로젝트에서 Source Plugin 빌드 검증
- 초기 Transform / Pivot 문제 확인 및 재설계

## Scope

이 프로젝트는 범용 Mesh Editor가 아니라 다음 작업을 빠르게 해결하기 위한 작은 Editor Tool입니다.

> **FBX Scene Blueprint 내부의 여러 StaticMeshComponent를 선택적으로, Material Reference와 시각적 배치를 유지한 채 Unreal 내부에서 병합한다.**

Pivot 재배치 기능 등은 별도 툴의 역할로 두며 이 플러그인의 범위를 확장하지 않습니다.
