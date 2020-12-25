set VmxPath=C:\Program Files (x86)\VMware\VMware Workstation\x64

del "%VmxPath%\dsound.dll"
mklink "%VmxPath%\dsound.dll" "%~dp0VmxHijack\build\dsound.dll"