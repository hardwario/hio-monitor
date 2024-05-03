#include "flash.h"

class DllManager {
public:
    Flash* flashInstance;

    DllManager(Flash* instance) : flashInstance(instance) {}

    ~DllManager() {
        if (flashInstance) {
            flashInstance->freeDll();
        }
    }
};

Flash::Flash(QObject *parent) : DeviceInterface(parent) {
    _name = "flash";
};

QVariant Flash::getCommandHistory() {
    return QVariant();
}

void Flash::defaultFlash() {
    setReady(false);
    _isRunning = true;
    flash(_hexPath);
}

void Flash::sendCommand(const QString &command) {
    if (command.contains("help")) {
        emit sendCommandSucceeded(command);
        emit deviceMessageReceived(_helpMessage);
        return;
    }

    if (_isRunning) {
        emit deviceMessageReceived(makeMessage("wrn", "please wait for the flashing process to complete!"));
        return;
    }

    tryDownload(command);
}

void Flash::setHexPath(const QString &path) {
    _hexPath = path;
    static QRegularExpression re(".*?(?=[A-Z]:)");
    _hexPath.remove(re);
    _isFileDownloaded = false;
    emit deviceMessageReceived(makeMessage("inf", "file path: " + path));
    setReady(true);
}

bool Flash::tryDownload(const QString &str) {
    QUrl url(QString("https://firmware.hardwario.com/chester/%1/hex").arg(str));
    _downloader = new FileDownloader(url, this);
    emit deviceMessageReceived(makeMessage("inf", "downloading..."));

    connect(_downloader, &FileDownloader::downloaded,
            [this, str] {
                qDebug() << "flash program file downloaded!";
                _hexPath = _downloader->save(str + ".hex");
                emit deviceMessageReceived(makeMessage("inf", "flash program file saved to: " + _hexPath));
                setReady(true);
                _isFileDownloaded = true;
            });

    connect(_downloader, &FileDownloader::errorOccured,
            [this, str](QString error) {
                qDebug() << "flash program file error while downloading: " << error;
                emit sendCommandFailed(str);
                emit deviceMessageReceived(makeMessage("err", error));
                setReady(false);
                return false;
            });

    return true;
}

QString Flash::makeMessage(QString tag, QString msg) {
    auto currentDateTime = QDateTime::currentDateTime().toString("hh:mm:ss.zzz,zzz");
    qDebug() << "flash msg " << "[" + currentDateTime + "] " + "<" + tag + "> " + msg;
    return "[" + currentDateTime + "] " + "<" + tag + "> " + msg;
}

bool Flash::checkErr(nrfjprogdll_err_t err, const QString& context) {
    bool isSuccess = err == nrfjprogdll_err_t::SUCCESS;
    if (!isSuccess) {
        switch(err) {
        case INVALID_SESSION:
            emit deviceMessageReceived(makeMessage("err", context + " invalid session"));
            break;
        case INVALID_OPERATION:
            emit deviceMessageReceived(makeMessage("err", context + " invalid operation"));
            break;
        case JLINKARM_DLL_ERROR:
            emit deviceMessageReceived(makeMessage("err", context + " jlink dll error"));
            break;
        case JLINKARM_DLL_TIME_OUT_ERROR:
            emit deviceMessageReceived(makeMessage("err", context + " jlink dll time out"));
            break;
        case NOT_AVAILABLE_BECAUSE_PROTECTION:
            emit deviceMessageReceived(makeMessage("err", context + " operation is not available due to readback protection"));
            break;
        case CANNOT_CONNECT:
            emit deviceMessageReceived(makeMessage("err", context + " could not connect to any nRF device"));
            break;
        case WRONG_FAMILY_FOR_DEVICE:
            emit deviceMessageReceived(makeMessage("err", context + "  connected device does not match the configured family"));
            break;
        case NO_EMULATOR_CONNECTED:
            emit deviceMessageReceived(makeMessage("err", context + " there is no emulator connected to the PC"));
            break;
        case INVALID_PARAMETER:
            emit deviceMessageReceived(makeMessage("err", context + " the clock_speed_in_khz parameter is not within limits"));
            break;
        case INTERNAL_ERROR:
            emit deviceMessageReceived(makeMessage("err", context + " internal error has been occured"));
            break;
        case UNKNOWN_MEMORY_ERROR:
            emit deviceMessageReceived(makeMessage("err", context + " illegal or unknown memory addresses"));
            break;
        case FILE_OPERATION_FAILED:
            emit deviceMessageReceived(makeMessage("err", context + " unable to open file"));
            break;
        case FILE_INVALID_ERROR:
            emit deviceMessageReceived(makeMessage("err", context + " file has overlapping segments of data"));
            break;
        case FILE_PARSING_ERROR:
            emit deviceMessageReceived(makeMessage("err", context + " failed to parse file contents"));
            break;
        case FILE_UNKNOWN_FORMAT_ERROR:
            emit deviceMessageReceived(makeMessage("err", context + " unsupported file ending"));
            break;
        case OUT_OF_MEMORY:
            emit deviceMessageReceived(makeMessage("err", context + " could not allocate hex file buffer"));
            break;
        case VERIFY_ERROR:
            emit deviceMessageReceived(makeMessage("err", context + " verify failed"));
            break;
        case JLINKARM_DLL_TOO_OLD:
            emit deviceMessageReceived(makeMessage("err", context + " the version of JLinkARM is lower than the minimum version required"));
            break;
        case JLINKARM_DLL_COULD_NOT_BE_OPENED:
            emit deviceMessageReceived(makeMessage("err", context + "could not open jlink dll"));
            break;
        case JLINKARM_DLL_NOT_FOUND:
            emit deviceMessageReceived(makeMessage("err", context + " the jlink_path did not yield a usable DLL, or the automatic search failed"));
            break;
        default:
            qDebug() << "Flash unknown error " << err;
            emit deviceMessageReceived(makeMessage("err", context + " unknown Error"));
            break;
        }
        _isRunning = false;
        emit errorOccured();
    }
    return isSuccess;
}

static void cb(const char *msg_str) {
    qDebug() << msg_str;
}

bool Flash::loadDll() {
    return checkErr(NRFJPROG_open_dll(NULL, (msg_callback*)cb, NRF52_FAMILY), "open dll");
}

void Flash::freeDll() {
    qDebug() << "close flash dll";

    if (_isFileDownloaded && _downloader->remove(_hexPath)) {
        emit deviceMessageReceived(makeMessage("inf", "remove file: " + _hexPath + " successful"));
        _hexPath.clear();
    }

    NRFJPROG_close_dll();
    _isRunning = false;
}

void Flash::flash(QString path) {
    flashThread = QThread::create([this, path]{
        if (!loadDll()) {
            return;
        }

        DllManager defer(this);

        auto bytes = path.toUtf8(); // so it's not temporary
        auto filepath = bytes.constData();

        nrfjprogdll_err_t err;
        err = NRFJPROG_connect_to_emu_without_snr(4000);
        if (!checkErr(err, "connect to emu with clock speed 4000")) {
            return;
        }
        emit deviceMessageReceived(makeMessage("inf", "connect successful"));

        err = NRFJPROG_sys_reset();
        if (!checkErr(err, "reset")) {
            return;
        }
        emit deviceMessageReceived(makeMessage("inf", "reset successful"));

        err = NRFJPROG_halt();
        if (!checkErr(err, "halt")) {
            return;
        }
        emit deviceMessageReceived(makeMessage("inf", "halt successful"));

        err = NRFJPROG_erase_file(filepath, ERASE_PAGES, ERASE_NONE);
        if (!checkErr(err, "erase_file")) {
            return;
        }
        emit deviceMessageReceived(makeMessage("inf", "sector erase successful"));

        err = NRFJPROG_program_file(filepath);
        if (!checkErr(err, "program_file")) {
            return;
        }
        emit deviceMessageReceived(makeMessage("inf", "flash successful"));

        err = NRFJPROG_verify_file(filepath, VERIFY_HASH);
        if (!checkErr(err, "verify")) {
            return;
        }
        emit deviceMessageReceived(makeMessage("inf", "verify successful"));

        err = NRFJPROG_sys_reset();
        if (!checkErr(err, "reset")) {
            return;
        }
        emit deviceMessageReceived(makeMessage("inf", "reset successful"));

        err = NRFJPROG_go();
        if (!checkErr(err, "go")) {
            return;
        }

        err = NRFJPROG_disconnect_from_emu();
        if (!checkErr(err, "disconnect from emu")) {
            return;
        }

        emit finished();
        emit deviceMessageReceived(makeMessage("inf", "flashing completed successfully!"));
    });

    flashThread->start();
}
