import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    function headline() {
        return findChild(window.contentItem, "headline");
    }

    function initTestCase() {
        AppViewModel.setLanguage("en");
        const component = Qt.createComponent("HardwarioMonitor", "Main");
        verify(component.status === Component.Ready, component.errorString());
        window = component.createObject(null);
        verify(window !== null);
    }

    function cleanupTestCase() {
        AppViewModel.setLanguage("en");
        window.destroy();
    }

    function test_title() {
        compare(window.title, "HARDWARIO Monitor");
    }

    function test_showsVersion() {
        verify(headline().text.indexOf(AppViewModel.version) >= 0, headline().text);
        verify(headline().text.indexOf("nothing to see yet") >= 0, headline().text);
        verify(findChild(window.contentItem, "buildInfo").text.indexOf(AppViewModel.version) >= 0);
    }

    function test_languages() {
        const codes = AppViewModel.availableLanguages.map(language => language.code);
        compare(codes, ["system", "en", "cs"]);
    }

    function test_switchToCzechAndBack() {
        AppViewModel.setLanguage("cs");
        compare(AppViewModel.language, "cs");
        tryVerify(() => headline().text.indexOf("zatím tu nic není") >= 0, 5000, headline().text);
        compare(findChild(window.contentItem, "languageSelector").currentIndex, 2);

        AppViewModel.setLanguage("en");
        compare(AppViewModel.language, "en");
        tryVerify(() => headline().text.indexOf("nothing to see yet") >= 0, 5000, headline().text);
        compare(findChild(window.contentItem, "languageSelector").currentIndex, 1);
    }

    function test_unknownLanguageIsIgnored() {
        AppViewModel.setLanguage("xx");
        compare(AppViewModel.language, "en");
    }

    name: "Main"
    when: windowShown
}
