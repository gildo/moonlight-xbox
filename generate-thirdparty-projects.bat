@echo on
setlocal

echo === Configure moonlight-common-c ===
cd third_party\moonlight-common-c || exit /b 1
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=..\..\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-uwp -DVCPKG_INSTALLED_DIR=..\..\vcpkg_installed -DVCPKG_MANIFEST_MODE=off -G "Visual Studio 17 2022" -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION="10.0" -DTARGET_UWP=ON || exit /b 1

echo === Configure libgamestream ===
cd ..\..\libgamestream || exit /b 1
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=..\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-uwp -DVCPKG_INSTALLED_DIR=..\vcpkg_installed -DVCPKG_MANIFEST_MODE=off -G "Visual Studio 17 2022" -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION="10.0" -DTARGET_UWP=ON -DBUILD_SHARED_LIBS=off || exit /b 1

cd ..
echo === Third party project generation complete ===
