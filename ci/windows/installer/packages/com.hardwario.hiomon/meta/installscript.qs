var targetDirectoryPage = null;

function Component()
{
    component.loaded.connect(this, this.installerLoaded);
}

Component.prototype.createOperationsForArchive = function(archive)
{
    component.addElevatedOperation("Extract", archive, "@TargetDir@/");
    component.addElevatedOperation("License");
}

Component.prototype.createOperations = function()
{
    component.createOperations();
    if (systemInfo.kernelType === "winnt") {
        handleWindows();
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

Component.prototype.installerLoaded = function()
{
    installer.setDefaultPageVisible(QInstaller.TargetDirectory, false);
    installer.addWizardPage(component, "TargetWidget", QInstaller.TargetDirectory);

    targetDirectoryPage = gui.pageWidgetByObjectName("DynamicTargetWidget");
    targetDirectoryPage.windowTitle = "Choose Installation Directory";
    targetDirectoryPage.description.setText("Please select where the HARDWARIO Monitor will be installed:");
    targetDirectoryPage.targetDirectory.textChanged.connect(this, this.targetDirectoryChanged);
    targetDirectoryPage.targetDirectory.setText(installer.value("TargetDir"));
    targetDirectoryPage.targetChooser.released.connect(this, this.targetChooserClicked);

    gui.pageById(QInstaller.ComponentSelection).entered.connect(this, this.componentSelectionPageEntered);
}

Component.prototype.targetChooserClicked = function()
{
    var dir = QFileDialog.getExistingDirectory("", targetDirectoryPage.targetDirectory.text);
    targetDirectoryPage.targetDirectory.setText(dir);
}

Component.prototype.targetDirectoryChanged = function()
{
    var dir = targetDirectoryPage.targetDirectory.text;
    if (installer.fileExists(dir) && installer.fileExists(dir + "/Uninstall.exe")) {
        targetDirectoryPage.warning.setText("<p style=\"color: red\">Existing installation detected and will be overwritten.</p>");
    }
    else if (installer.fileExists(dir)) {
        targetDirectoryPage.warning.setText("<p style=\"color: red\">Installing in existing directory. It will be wiped on uninstallation.</p>");
    }
    else {
        targetDirectoryPage.warning.setText("");
    }
    installer.setValue("TargetDir", dir);
}

Component.prototype.componentSelectionPageEntered = function()
{
    var dir = installer.value("TargetDir");
    if (installer.fileExists(dir) && installer.fileExists(dir + "/Uninstall.exe")) {
        installer.execute(dir + "/Uninstall.exe", ["purge", "-c"]);
    }
}
