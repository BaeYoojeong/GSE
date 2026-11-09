> 2026-10-27 최신 구현: [월드·자산 연동 보완 기록](WORLD_COMPLETION_KR.md)을 우선한다. 중심 간 5청크(약 20.21초), SQLite/V6 저장, 원거리 C++ 작업자, MD 계약 재적용, 별도 Blender 내보내기 및 J/C 애니메이션을 추가했다. 게임 빌드·실행 검수는 사용자가 진행한다.

> 2026-10-27 확장: 현재 외부 제작 자산은 [GLB·스켈레탈 로더](ASSET_LOADER_KR.md)와 `Assets/모델.txt`를 사용한다. 아래 내용은 기존 `.ashmesh` 자산과 폴백 경로의 제작 안내다.

# Blender 백그라운드 자산 파이프라인

## 생성한 자산

Blender 5.2.2 LTS의 백그라운드 모드에서 `Tools/build_blender_assets.py`를 실행했다.
이미지 렌더나 게임 실행 대신 저폴리 입체 모델과 게임용 메시 데이터를 생성했다.

- `Assets/Blender/medieval_library.blend`: 편집 가능한 원본. 집 4종, 종탑 2상태, 종지기 2상태를 개별 컬렉션으로 보관한다.
- `cottage_0..3.ashmesh`: 주민 집. 지붕·벽·창문에 변형을 주고 창문에 발광 값을 설정한다.
- `tower_dark/lit.ashmesh`: 복구 전후 종탑.
- `keeper_dark/enraged.ashmesh`: 보스 기본/격노 상태.
- `manifest.json`: 생성기 버전과 자산별 정점 수. 최초 출력은 8개 자산, 합계 1,854개 정점이다.

## 제작과 실행의 분리

```text
Blender 백그라운드 프로세스
  → .blend 원본 + 고정 시점으로 투영한 .ashmesh
  → Visual Studio 빌드 시 실행 폴더 Assets/Blender로 복사
  → BlenderAssets CPU 캐시
  → MeshCache / DiskMeshCache
  → RenderBatch
  → 기존 HDR / Bloom / 비넷 / 가장자리 블러
```

게임 실행 시 Blender 설치나 Blender 프로세스가 필요하지 않다.
NPC의 판단·이동·사회 시뮬레이션은 게임에서 처리하며 Blender로 보내지 않는다.
현재 게임의 45도 고정 2.5D 카메라에 맞춰 투영/면 정렬/단순 명암을 베이크한다. 카메라 회전을 자유롭게 지원하는 3D 모델 로더는 아니다.
발광은 메시의 `emission` 값으로 기존 HDR/Bloom 단계에 전달한다.

## 다시 생성

프로젝트 `SimpleGame` 폴더에서 PowerShell 예시:

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background --factory-startup --python 'Tools\build_blender_assets.py' -- --output 'Assets\Blender'
```

이 명령은 생성 파일을 덮어쓴다. `.blend`를 직접 편집했다면 별도 이름으로 저장하거나 생성기에도 변경을 반영한 뒤 실행한다.
현재 생성기는 절차적으로 원본을 새로 만든다. 기존 `.blend`의 수동 수정을 읽어 재수출하는 명령은 아니다.

## 런타임 로딩과 캐시

- 실행 파일 위치를 기준으로 `Assets/Blender`를 찾는다. 작업 디렉터리에는 의존하지 않는다.
- 파일 이름·헤더·정점 수·유한 숫자·발광 범위를 검사한다.
- 파일은 자산 이름별로 한 번만 읽고 CPU 캐시에 둔다. 정점 배열의 내용 해시를 GPU/디스크 메시 키에 포함한다.
- 같은 종류의 집은 위치만 다른 Actor/렌더 인스턴스가 동일한 메시를 공유한다. 프레임마다 Blender 모델을 만들거나 다시 읽지 않는다.
- 파일이 없거나 잘못되면 기존 절차형 모델로 폴백하고 `blender_asset_fallback`을 기록한다.
- 새 자산을 다시 생성한 뒤 게임을 재시작하면 내용 해시가 바뀌어 이전 디스크 캐시와 구분된다. 실행 중 자동 파일 감시는 없다.
- 보스 공격 예고는 공유 사각형의 인스턴스로 그린다. 매 프레임 원형 메시를 생성하지 않는다.

## 확인 범위와 한계

Blender 자산 생성은 성공했다. 게임 빌드와 실제 렌더 화면 확인은 사용자에게 맡긴다.
현재 자산은 프로토타입 수준이며 최종 아트, 텍스처 UV 작업, 스켈레탈 애니메이션, 동적 그림자, PBR 재질 전체를 제공하지 않는다.
기존 렌더러의 500×350 내부 렌더 해상도와 픽셀 표현을 유지한다. 모델을 입체적으로 개선해도 픽셀 스타일은 남는다.
