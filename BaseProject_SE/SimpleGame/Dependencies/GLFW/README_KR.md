# GLFW 의존성

- 고정 버전: GLFW 3.5.1, 공식 배포 원본을 `upstream/`에 그대로 포함했다.
- 출처·원본 압축 파일 SHA-256: `UPSTREAM.json`.
- 라이선스: `upstream/LICENSE.md` (zlib/libpng).
- 공개 헤더, 전체 플랫폼 소스, 공식 CMake 파일, 문서·예제·테스트가 함께 들어 있다. Git submodule이나 빌드 시 다운로드를 사용하지 않는다.

`GLFW.vcxproj`는 공식 `upstream/src/CMakeLists.txt`의 공통·null·Win32·WGL/EGL/OSMesa 소스 목록을 사용한다.
원본 소스에는 패치를 적용하지 않았다. 게임 솔루션의 ProjectReference로 자동 빌드하는 정적 라이브러리다.
Debug/Release 및 Win32/x64를 구분하고, 게임과 같은 `/MDd` 또는 `/MD` 런타임을 사용한다.
출력은 `build/<플랫폼>/<구성>/glfw3.lib`이며 생성 결과는 Git에서 제외한다. 다른 PC에서는 포함된 소스로 해당 PC의 컴파일러에 맞게 다시 생성한다.
GLFW DLL은 실행에 필요 없다.

## Windows 빌드

Visual Studio 2022 이상에서 ‘C++를 사용한 데스크톱 개발’, MSVC v143, Windows 10/11 SDK를 설치한다.
`BaseProject_SE/SimpleGame.sln`을 열고 x64 또는 x86, Debug 또는 Release를 선택한다.
프로젝트만 직접 빌드해도 ProjectReference로 GLFW가 먼저 빌드된다. CMake·vcpkg·NuGet·사용자별 라이브러리 경로 설정은 필요 없다.

```powershell
msbuild BaseProject_SE\SimpleGame.sln /m /p:Configuration=Release /p:Platform=x64
msbuild BaseProject_SE\SimpleGame.sln /m /p:Configuration=Debug /p:Platform=x86
```

기존 GLEW 로더의 import library는 상위 Dependencies 폴더에 유지한다.
런타임 DLL은 `../GLEW/bin/Win32`와 `../GLEW/bin/x64`에 포함했으며 빌드가 실행 폴더로 자동 복사한다.
프로젝트의 라이브러리 검색 경로는 `$(ProjectDir)` 기준이므로 실행한 콘솔의 현재 폴더와 무관하다.

게임은 GDI 글꼴·Windows SQLite 등 Windows API를 사용하므로 현재 게임 빌드 지원 범위는 Windows다.
GLFW 원본 자체의 다른 플랫폼/CMake 빌드 파일은 포함했지만 게임 전체를 Linux/macOS로 이식한 것은 아니다.

공식 자료: https://www.glfw.org/download.html / https://www.glfw.org/docs/latest/compile.html
