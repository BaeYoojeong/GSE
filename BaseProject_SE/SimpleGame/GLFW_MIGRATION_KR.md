# FreeGLUT → GLFW 전환

2026-11-10. 기존 dev의 FreeGLUT 프레임워크를 GLFW 3.5.1로 교체한다.

## 실행 경로

`SimpleGame.cpp`가 GLFW 초기화·창/컨텍스트·콜백·명시적 프레임 루프와 종료를 소유한다.
렌더러의 배칭·메시 캐시·셰이더·HDR/Bloom은 같은 경로를 사용한다.

- OpenGL 3.3 compatibility 이상, 더블 버퍼, RGB 8비트, depth 24비트·stencil 8비트·MSAA 0. 기존 Windows FreeGLUT에서 실제 선택된 픽셀 형식을 유지한다. 실제 버전과 픽셀 형식은 측정 보고서에 기록한다.
- 전경에서는 glfwPollEvents → Update → Render → glfwSwapBuffers 순서로 진행한다.
- 기존의 제한 없는 전경 루프를 유지하도록 기본 swap interval은 0. `ASHEN_VSYNC=1`이면 수직동기화를 켠다. 드라이버 강제 설정이 있으면 측정 시 실제 값을 함께 확인한다.
- 시간은 glfwGetTime의 단조 증가 double 값을 사용한다. 기존 정수 밀리초 시간의 장기 오버플로를 피한다.
- 포커스 해제·최소화에서 이동·공격·달리기·점프 입력을 비운다. 이때 glfwWaitEventsTimeout(0.02)로 대기하면서 약 50Hz로 월드를 계속 갱신한다.
- 최소화 또는 0×0 프레임버퍼에서는 렌더/스왑을 생략한다. 복원하면 렌더를 재개한다.
- 화면 픽셀 크기는 framebuffer-size 콜백으로 받고, 연속 리사이즈 요청은 마지막 크기를 다음 프레임에 한 번 적용한다.
- GPU 자원과 저장소는 OpenGL 컨텍스트가 살아 있을 때 정리하고, 그 뒤 창과 GLFW를 종료한다.

## 입력

`WindowInput.h`의 SpecialKey는 게임 입력을 프레임워크 키 번호와 분리한다.
GLFW 방향키는 GLUT의 연속 번호와 다르므로 명시적으로 Left/Up/Right/Down에 매핑한다.
F1~F10, Esc, Tab, 대괄호, 숫자, 알파벳, Space의 기존 기능을 유지한다.
GLFW_REPEAT는 게임 명령으로 처리하지 않으며, 눌림/해제 상태로 이동과 공격을 유지한다.
Ctrl/Alt/Super 조합의 새 게임 동작은 무시하고, 해제는 항상 받아 키가 붙지 않도록 한다.
자유 입력창이 없는 현재 게임은 문자 콜백 대신 물리 키를 사용한다. 이후 채팅/이름 입력에는 Unicode 문자 콜백을 별도로 연결한다.

## 포함한 라이브러리와 다른 PC 빌드

`Dependencies/GLFW/`에 원본 전체·헤더·라이선스·출처/해시·정적 빌드 프로젝트를 포함했다.
게임 ProjectReference와 솔루션 구성이 GLFW를 자동으로 빌드한다. 외부 다운로드나 절대 라이브러리 경로가 필요 없다.
`Dependencies/GLEW/bin/{Win32,x64}/glew32.dll`도 포함하고 빌드 시 자동 복사한다.
현재 게임의 지원 환경은 Windows 10/11 + Visual Studio 2022 이상 + v143/Windows SDK다.
자세한 방법은 `Dependencies/GLFW/README_KR.md`를 참조한다.

FreeGLUT 헤더·import library·배포 DLL은 제거했다. 과거 기획/설명 문서의 FreeGLUT 언급은 변경 이력으로 남을 수 있다.

## 재현 가능한 점검

실제 게임 저장소를 사용하지 않도록, 아래 모드는 **존재하지 않는 새 폴더의 절대 경로**만 받는다.
그래픽 창은 숨기며, 평소 프로필·모니터 서버·Blender 내보내기를 실행하지 않는다.

```powershell
.\SimpleGame.exe --window-smoke C:\Temp\Ashen_Glfw_Input_New
.\SimpleGame.exe --benchmark C:\Temp\Ashen_Glfw_Frozen_New
.\SimpleGame.exe --benchmark-live C:\Temp\Ashen_Glfw_Moving_New
# 또는 Tools/RunFrameworkChecks.ps1 -Executable <실행파일 절대 경로>
```

- window-smoke: 방향키, 입력 해제/반복, 달리기·공격·점프 요청, 포커스/최소화 처리, 백그라운드 월드 진행과 렌더 재개, 실제 프레임버퍼 리사이즈, OpenGL 오류, 종료 요청을 검사한다.
- benchmark: 시드 20261110, 동일 초기 장면, 120프레임 준비 + 600프레임 측정.
- benchmark-live: 같은 시드와 1/60초 고정 갱신, 180프레임마다 이동 방향을 바꾼다. 비교 재현성을 위해 원거리 작업자만 동기 방식으로 돌린다.
- CPU 시간에는 이벤트 처리·갱신(live)·렌더·swap·기존 로그 기록을 포함한다.
- GPU 시간은 timer query로 측정하고 결과는 비동기로 읽는다. 마지막 결과 대기는 측정 루프 밖에서 수행한다.
- 화면 비교용 추가 프레임의 읽기와 파일 저장도 측정 루프 밖에서 수행한다.
- framework_frames.csv, framework_benchmark.json, frame.ppm, 기존 Profiling CSV를 기록한다.

이 결과는 동일 PC·숨김 창·해당 두 장면의 비교다. 모든 드라이버/해상도에서 동일한 성능을 보장하는 수치는 아니다.
측정 결과는 `GLFW_VALIDATION.json`에 보관한다.

## 이번 전환 검증 결과

x64/Win32 × Debug/Release의 네 구성을 모두 빌드했고 오류·경고는 0개였다. 각 구성의 창·입력·백그라운드 갱신 점검 28개도 모두 통과했다. 실행 파일 import table에서 FreeGLUT/GLFW DLL 의존성이 없음을 확인했다.

NVIDIA GeForce RTX 5060, 1120×784, 동일 OpenGL/프레임버퍼·시드·swap interval 0 조건이다. 고정 장면은 각 2회, 이동 장면은 각 4회 실행했고, 각 실행은 120프레임 준비 후 600프레임을 측정했다. 표는 독립 실행별 중앙값의 중앙값이며 낮을수록 좋다.

| 장면 | CPU FreeGLUT → GLFW | GPU FreeGLUT → GLFW |
|---|---|---|
| 고정 | 0.774 → 0.739 ms (-4.52%) | 0.116 → 0.115 ms (-0.77%) |
| 이동 | 0.981 → 0.979 ms (-0.20%) | 0.144 → 0.144 ms (-0.44%) |

고정/이동 모두 캡처 픽셀이 완전히 같았고, 측정한 모든 프레임의 draw call·mesh build·upload byte 수가 기존 버전과 같았다. OpenGL 오류도 없었다. 이 측정 범위에서 성능 저하는 관찰되지 않았다. GPU 시간의 미세한 차이는 실행 환경 변동을 포함한다.

별도 경로의 소스 전용 사본에서 .sln 없이 SimpleGame.vcxproj를 직접 빌드했다. 기존 glfw3.lib·obj·실행 파일을 복사하지 않았고, 오류·경고 0개 및 창 점검 28개 통과를 확인했다.

실제 dev 저장소에 반영한 뒤에도 네 구성의 빌드(각 오류·경고 0개)와 창 점검(각 28개)을 다시 통과했다. 실제 x64 Release의 최종 렌더 캡처도 기준 버전과 완전히 같았고 draw call은 106회였다. 생성된 Debug/Release 배포 복사본은 Git에서 제외한다.
