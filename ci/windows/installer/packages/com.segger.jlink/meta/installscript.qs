function Component()
{

}

Component.prototype.createOperations = function()
{
    if (!component.installationRequested()) {
        return;
    }
    component.createOperations();
    if (systemInfo.kernelType === "winnt") {
        component.addOperation("Execute", "{0,3010}", "@TargetDir@\\JLink_Windows_driver.exe");
        component.addOperation("Delete", "@TargetDir@\\JLink_Windows_driver.exe");
    } else {
        QMessageBox.information("os.error", "Installer",
                               "This driver is needed only for Windows",
                               QMessageBox.Ok);
    }
}
