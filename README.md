# GSE

중세 암흑시대를 배경으로 하는 2.5D 오픈월드 RPG 프로토타입입니다.

창·입력 프레임워크는 GLFW 3.5.1입니다. 소스·헤더·라이선스와 정적 빌드 프로젝트를 저장소에 포함하므로 GLFW를 따로 설치하거나 내려받을 필요가 없습니다.

Windows 10/11에서 Visual Studio 2022 이상의 ‘C++를 사용한 데스크톱 개발’, MSVC v143, Windows SDK를 설치하고 `BaseProject_SE/SimpleGame.sln`을 빌드합니다. x64/x86, Debug/Release를 지원합니다. 필요한 GLEW DLL도 자동 복사됩니다.

```powershell
msbuild BaseProject_SE\SimpleGame.sln /m /p:Configuration=Release /p:Platform=x64
```

- [이미지 지도·포탈 활성화·상인 상호작용](BaseProject_SE/SimpleGame/MAP_PORTALS_KR.md)
- [상점·무기·물약·인벤토리와 조작](BaseProject_SE/SimpleGame/SHOP_INVENTORY_KR.md)
- [화면 선명도·플레이어 공격 조정](BaseProject_SE/SimpleGame/CLARITY_AND_COMBAT_KR.md)
- [프레임 끊김 원인·개선·재현 방법](BaseProject_SE/SimpleGame/STUTTER_ANALYSIS_KR.md)
- [GLFW 전환·입력·성능 검증](BaseProject_SE/SimpleGame/GLFW_MIGRATION_KR.md)
- [포함한 GLFW 라이브러리와 빌드 구성](BaseProject_SE/SimpleGame/Dependencies/GLFW/README_KR.md)
- [월드·NPC·자산 연동](BaseProject_SE/SimpleGame/WORLD_COMPLETION_KR.md)

별도 제작 중인 외부 GLB 자산 폴더는 `BaseProject_SE/SimpleGame/Assets/모델.txt`의 root로 지정합니다. 해당 폴더가 없으면 저장소에 포함된 기본 모델을 사용합니다.


- [오픈월드 탐험·동적 지도 개선](BaseProject_SE/SimpleGame/OPEN_WORLD_KR.md)
