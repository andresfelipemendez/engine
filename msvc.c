#include "platform.h"

void load_env() {
 	// Hardcoded from: vcvars64.bat && set  (VS 18.9.3, MSVC 14.51.36231, WinSDK 10.0.26100.0)
    _putenv("INCLUDE=C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.51.36231\\include;C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.51.36231\\ATLMFC\\include;C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\VS\\include;C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.26100.0\\shared;C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.26100.0\\winrt;C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.26100.0\\cppwinrt;C:\\Program Files (x86)\\Windows Kits\\NETFXSDK\\4.8\\include\\um");
    _putenv("LIB=C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.51.36231\\ATLMFC\\lib\\x64;C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.51.36231\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\NETFXSDK\\4.8\\lib\\um\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\lib\\10.0.26100.0\\ucrt\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\lib\\10.0.26100.0\\um\\x64");
    _putenv("LIBPATH=C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.51.36231\\ATLMFC\\lib\\x64;C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.51.36231\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\UnionMetadata\\10.0.26100.0;C:\\Program Files (x86)\\Windows Kits\\10\\References\\10.0.26100.0");
    _putenv("PATH=C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.51.36231\\bin\\HostX64\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.26100.0\\x64;C:\\WINDOWS\\system32;C:\\WINDOWS");
    _putenv("WindowsSdkDir=C:\\Program Files (x86)\\Windows Kits\\10\\");
    _putenv("WindowsSDKVersion=10.0.26100.0\\");
    _putenv("WindowsSDKLibVersion=10.0.26100.0\\");
    _putenv("NETFXSDKDir=C:\\Program Files (x86)\\Windows Kits\\NETFXSDK\\4.8\\");
    _putenv("UCRTVersion=10.0.26100.0");
    _putenv("VCToolsInstallDir=C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.51.36231\\");
    _putenv("VCToolsVersion=14.51.36231");
    _putenv("VisualStudioVersion=18.0");
    _putenv("Platform=x64");
    _putenv("FrameworkDir=C:\\Windows\\Microsoft.NET\\Framework64\\");
    _putenv("FrameworkDir64=C:\\Windows\\Microsoft.NET\\Framework64\\");
    _putenv("FrameworkVersion=v4.0.30319");
    _putenv("FrameworkVersion64=v4.0.30319");
    _putenv("CommonProgramW6432=C:\\Program Files\\Common Files");
    _putenv("CommonProgramFiles(x86)=C:\\Program Files (x86)\\Common Files");
    _putenv("VSINSTALLDIR=C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\");
    _putenv("VCINSTALLDIR=C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\");
    _putenv("VSCMD_ARG_app_plat=Desktop");
    _putenv("VSCMD_ARG_HOST_ARCH=x64");
    _putenv("VSCMD_ARG_TGT_ARCH=x64");
    _putenv("VSCMD_VER=18.9.3");
    _putenv("WindowsSdkBinPath=C:\\Program Files (x86)\\Windows Kits\\10\\bin\\");
    _putenv("WindowsSdkVerBinPath=C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.26100.0\\");
    _putenv("WindowsSDK_ExecutablePath_x64=C:\\Program Files (x86)\\Microsoft SDKs\\Windows\\v10.0A\\bin\\NETFX 4.8 Tools\\x64\\");
    _putenv("WindowsSDK_ExecutablePath_x86=C:\\Program Files (x86)\\Microsoft SDKs\\Windows\\v10.0A\\bin\\NETFX 4.8 Tools\\");
    _putenv("WindowsLibPath=C:\\Program Files (x86)\\Windows Kits\\10\\UnionMetadata\\10.0.26100.0;C:\\Program Files (x86)\\Windows Kits\\10\\References\\10.0.26100.0");
    _putenv("ExtensionSdkDir=C:\\Program Files (x86)\\Microsoft SDKs\\Windows Kits\\10\\ExtensionSDKs");
    _putenv("VCToolsRedistDir=C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Redist\\MSVC\\14.51.36231\\");
    _putenv("VS180COMNTOOLS=C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\Common7\\Tools\\");
    _putenv("use_x64_llvm=true");
}


void ErrorExit() 
{ 
    // Retrieve the system error message for the last-error code

    LPVOID lpMsgBuf;
    DWORD dw = GetLastError(); 

    if (FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR) &lpMsgBuf,
        0, NULL) == 0) {
    	printf("FormatMessage failed");

        ExitProcess(dw);
    }
	printf("error %s\n",(LPCTSTR)lpMsgBuf);

    LocalFree(lpMsgBuf);
    ExitProcess(dw); 
}
