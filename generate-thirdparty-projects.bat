@echo on
setlocal

set "LAB_POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "LAB_PSMODULEPATH=%SystemRoot%\system32\WindowsPowerShell\v1.0\Modules"

echo === Ensure lab package signing certificate ===
if not exist cert.pfx (
  set "PSModulePath=%LAB_PSMODULEPATH%"
  "%LAB_POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; Import-Module Microsoft.PowerShell.Security -ErrorAction Stop; Import-Module PKI -ErrorAction Stop; $password = New-Object System.Security.SecureString; 'moonlight'.ToCharArray() | ForEach-Object { $password.AppendChar($_) }; $password.MakeReadOnly(); $cert = New-SelfSignedCertificate -Type Custom -Subject 'CN=CE07B73A-712E-4E05-932B-D08CE2C8A87C' -KeyUsage DigitalSignature -FriendlyName 'Moonlight UWP Pacing Lab' -CertStoreLocation 'Cert:\CurrentUser\My' -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}'); Export-PfxCertificate -Cert ('Cert:\CurrentUser\My\' + $cert.Thumbprint) -FilePath cert.pfx -Password $password | Out-Host"
  if errorlevel 1 exit /b 1
)

echo === Configure moonlight-common-c ===
cd third_party\moonlight-common-c || exit /b 1
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=..\..\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-uwp -DVCPKG_INSTALLED_DIR=..\..\vcpkg_installed -DVCPKG_MANIFEST_MODE=off -G "Visual Studio 17 2022" -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION="10.0" -DTARGET_UWP=ON || exit /b 1

echo === Configure libgamestream ===
cd ..\..\libgamestream || exit /b 1
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=..\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-uwp -DVCPKG_INSTALLED_DIR=..\vcpkg_installed -DVCPKG_MANIFEST_MODE=off -G "Visual Studio 17 2022" -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION="10.0" -DTARGET_UWP=ON -DBUILD_SHARED_LIBS=off || exit /b 1

cd ..
echo === Third party project generation complete ===
