function Component()
{
    if (systemInfo.kernelType !== "winnt") {
        var comps = installer.components("\bj(?:\d+)?(?:\.\d+)?link\b");
        if (comps.length > 0) {
            console.log("Set this to installed state: ", comps[0].name); 
            comps[0].setInstalled();
        }
    } 
}

Component.prototype.createOperationsForArchive = function(archive)
{
    component.addElevatedOperation("Extract", archive, "@TargetDir@/");
}

Component.prototype.createOperations = function()
{
    component.createOperations();
    if (systemInfo.kernelType === "winnt") {
        handleWindows();
    } else if (systemInfo.kernelType === "darwin") {
        handleMacOs();
    } else if (systemInfo.kernelType === "linux") {
        handleLinux();
    } else {
        QMessageBox.critical("os.error", "Installer",
                               "Unable to detect your system",
                               QMessageBox.Ok);
    }
}

handleWindows = function()
{
    var userProfile = installer.environmentVariable("USERPROFILE");
    installer.setValue("UserProfile", userProfile);
    component.addOperation("CreateShortcut", "@TargetDir@\\HARDWARIO Monitor.exe", "@UserProfile@\\Desktop\\HARDWARIO Monitor.lnk");
    component.addOperation("CreateShortcut", "@TargetDir@\\HARDWARIO Monitor.exe", "@StartMenuDir@\\HARDWARIO Monitor.lnk", "iconPath=@TargetDir@/HARDWARIO Monitor.exe", "description=Start HARDWARIO Monitor");
}

handleMacOs = function()
{
    component.addOperation("Move", "@TargetDir@/HARDWARIO Monitor.app", "/Applications/HARDWARIO Monitor.app");
}

handleLinux = function()
{
    try
    {
        component.addOperation("Move", "@TargetDir@/libjlinkarm.so.7", "/usr/lib/libjlinkarm.so.7");
        component.addOperation("Move", "@TargetDir@/libQt6Quick.so.6", "/usr/lib/libQt6Quick.so.6");
        component.addOperation("Move", "@TargetDir@/libQt6Bluetooth.so.6", "/usr/lib/libQt6Bluetooth.so.6");
        component.addOperation("Move", "@TargetDir@/libQt6Gui.so.6", "/usr/lib/libQt6Gui.so.6");
        component.addOperation("Move", "@TargetDir@/libGLX.so.0", "/usr/lib/libGLX.so.0");
        component.addOperation("Move", "@TargetDir@/libOpenGL.so.0", "/usr/lib/libOpenGL.so.0");
        component.addOperation("Move", "@TargetDir@/libQt6Qml.so.6", "/usr/lib/libQt6Qml.so.6");
        component.addOperation("Move", "@TargetDir@/libQt6Core.so.6", "/usr/lib/libQt6Core.so.6");
        component.addOperation("Move", "@TargetDir@/libstdc++.so.6", "/usr/lib/libstdc++.so.6");
        component.addOperation("Move", "@TargetDir@/libgcc_s.so.1", "/usr/lib/libgcc_s.so.1");
        component.addOperation("Move", "@TargetDir@/libc.so.6", "/usr/lib/libc.so.6");
        component.addOperation("Move", "@TargetDir@/libdl.so.2", "/usr/lib/libdl.so.2");
        component.addOperation("Move", "@TargetDir@/libQt6QmlModels.so.6", "/usr/lib/libQt6QmlModels.so.6");
        component.addOperation("Move", "@TargetDir@/libQt6Network.so.6", "/usr/lib/libQt6Network.so.6");
        component.addOperation("Move", "@TargetDir@/libxkbcommon.so.0", "/usr/lib/libxkbcommon.so.0");
        component.addOperation("Move", "@TargetDir@/libQt6OpenGL.so.6", "/usr/lib/libQt6OpenGL.so.6");
        component.addOperation("Move", "@TargetDir@/libGL.so.1", "/usr/lib/libGL.so.1");
        component.addOperation("Move", "@TargetDir@/libm.so.6", "/usr/lib/libm.so.6");
        component.addOperation("Move", "@TargetDir@/libpthread.so.0", "/usr/lib/libpthread.so.0");
        component.addOperation("Move", "@TargetDir@/libQt6DBus.so.6", "/usr/lib/libQt6DBus.so.6");
        component.addOperation("Move", "@TargetDir@/libEGL.so.1", "/usr/lib/libEGL.so.1");
        component.addOperation("Move", "@TargetDir@/libfontconfig.so.1", "/usr/lib/libfontconfig.so.1");
        component.addOperation("Move", "@TargetDir@/libX11.so.6", "/usr/lib/libX11.so.6");
        component.addOperation("Move", "@TargetDir@/libglib-2.0.so.0", "/usr/lib/libglib-2.0.so.0");
        component.addOperation("Move", "@TargetDir@/libz.so.1", "/usr/lib/libz.so.1");
        component.addOperation("Move", "@TargetDir@/libfreetype.so.6", "/usr/lib/libfreetype.so.6");
        component.addOperation("Move", "@TargetDir@/libgthread-2.0.so.0", "/usr/lib/libgthread-2.0.so.0");
        component.addOperation("Move", "@TargetDir@/libGLdispatch.so.0", "/usr/lib/libGLdispatch.so.0");
        component.addOperation("Move", "@TargetDir@/libicui18n.so.56", "/usr/lib/libicui18n.so.56");
        component.addOperation("Move", "@TargetDir@/libicuuc.so.56", "/usr/lib/libicuuc.so.56");
        component.addOperation("Move", "@TargetDir@/libicudata.so.56", "/usr/lib/libicudata.so.56");
        component.addOperation("Move", "@TargetDir@/librt.so.1", "/usr/lib/librt.so.1");
        component.addOperation("Move", "@TargetDir@/libgssapi_krb5.so.2", "/usr/lib/libgssapi_krb5.so.2");
        component.addOperation("Move", "@TargetDir@/libdbus-1.so.3", "/usr/lib/libdbus-1.so.3");
        component.addOperation("Move", "@TargetDir@/libexpat.so.1", "/usr/lib/libexpat.so.1");
        component.addOperation("Move", "@TargetDir@/libuuid.so.1", "/usr/lib/libuuid.so.1");
        component.addOperation("Move", "@TargetDir@/libxcb.so.1", "/usr/lib/libxcb.so.1");
        component.addOperation("Move", "@TargetDir@/libpcre.so.3", "/usr/lib/libpcre.so.3");
        component.addOperation("Move", "@TargetDir@/libpng16.so.16", "/usr/lib/libpng16.so.16");
        component.addOperation("Move", "@TargetDir@/libbrotlidec.so.1", "/usr/lib/libbrotlidec.so.1");
        component.addOperation("Move", "@TargetDir@/libkrb5.so.3", "/usr/lib/libkrb5.so.3");
        component.addOperation("Move", "@TargetDir@/libk5crypto.so.3", "/usr/lib/libk5crypto.so.3");
        component.addOperation("Move", "@TargetDir@/libcom_err.so.2", "/usr/lib/libcom_err.so.2");
        component.addOperation("Move", "@TargetDir@/libkrb5support.so.0", "/usr/lib/libkrb5support.so.0");
        component.addOperation("Move", "@TargetDir@/libsystemd.so.0", "/usr/lib/libsystemd.so.0");
        component.addOperation("Move", "@TargetDir@/libXau.so.6", "/usr/lib/libXau.so.6");
        component.addOperation("Move", "@TargetDir@/libXdmcp.so.6", "/usr/lib/libXdmcp.so.6");
        component.addOperation("Move", "@TargetDir@/libbrotlicommon.so.1", "/usr/lib/libbrotlicommon.so.1");
        component.addOperation("Move", "@TargetDir@/libkeyutils.so.1", "/usr/lib/libkeyutils.so.1");
        component.addOperation("Move", "@TargetDir@/libresolv.so.2", "/usr/lib/libresolv.so.2");
        component.addOperation("Move", "@TargetDir@/liblzma.so.5", "/usr/lib/liblzma.so.5");
        component.addOperation("Move", "@TargetDir@/libzstd.so.1", "/usr/lib/libzstd.so.1");
        component.addOperation("Move", "@TargetDir@/liblz4.so.1", "/usr/lib/liblz4.so.1");
        component.addOperation("Move", "@TargetDir@/libcap.so.2", "/usr/lib/libcap.so.2");
        component.addOperation("Move", "@TargetDir@/libgcrypt.so.20", "/usr/lib/libgcrypt.so.20");
        component.addOperation("Move", "@TargetDir@/libbsd.so.0", "/usr/lib/libbsd.so.0");
        component.addOperation("Move", "@TargetDir@/libgpg-error.so.0", "/usr/lib/libgpg-error.so.0");
        component.addOperation("Move", "@TargetDir@/libmd.so.0", "/usr/lib/libmd.so.0");
    }
    catch (e) {
        console.log("Error moving libraries");
    }
}


