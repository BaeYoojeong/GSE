# 게임과 Blender 자산의 연결 규칙

이 문서의 `ashen-assets` JSON 블록은 게임 실행 중 약 1초마다 다시 읽습니다.
일반 설명 문장은 실행되지 않습니다. 잘못된 JSON, 알 수 없는 모델 ID 또는 지원하지 않는 기능이 발견되면 마지막 정상 규칙을 유지하며 로그에 원인을 남깁니다.
외부 제작 MD에도 같은 형식의 블록을 추가할 수 있습니다. `모델.txt`의 watch_md 순서대로 읽으며 뒤쪽 규칙이 우선합니다.

```ashen-assets
{
  "schema": "ashen_asset_contract_1",
  "required_features": ["glb", "skeletal", "animations", "modular", "flat_color", "emissive"],
  "bindings": {
    "player": "NPC_Modular_Library",
    "citizen": "NPC_Modular_Library",
    "residence": "ENV_Cottage",
    "house": "ENV_Cottage",
    "chapel": "ENV_Tavern"
  },
  "clips": {
    "idle": "Idle", "walk": "Walk", "run": "Run", "jump": "Jump",
    "turn_left": "TurnLeft", "turn_right": "TurnRight"
  },
  "root_bone": "root",
  "pose_fps": 12,
  "units_per_meter": 30,
  "limits": {"nodes": 4096, "joints": 256, "vertices": 250000, "animations": 128, "keys": 1000000}
}
```

- `enemy`를 생략하면 manifest의 animals 목록에서 자동 선택합니다. 새 동물을 추가하면 다시 컴파일할 필요가 없습니다.
- tree, rubble, herb 역할도 모델 ID 또는 ID 배열로 지정할 수 있습니다. 배열은 NPC/오브젝트 ID에 따른 안정적인 분산 선택을 사용합니다.
- durations는 idle/walk/run/jump/turn_left/turn_right의 재생 길이를 초 단위로 덮어씁니다. 생략하면 manifest.clips의 duration_seconds를 사용합니다.
- 모듈형 부품은 GLB의 **노드 이름**과 manifest.npc_parts.mesh가 같아야 합니다.
- 방향 전환 클립의 root 회전은 제거하고 게임의 방향 보간이 처리합니다. 점프의 루트 높이 이동은 스키닝 결과로 표현합니다.
- 텍스처, 모프 타깃, Draco, FBX 직접 읽기 등 새 데이터 형식은 별도 로더 구현이 필요합니다. MD가 임의의 프로그램 코드를 생성하거나 실행하지 않습니다.
