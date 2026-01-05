#include "electrumxsupervisor.h"

#include <iostream>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <signal.h>
#include <unistd.h>
#include <QSocketNotifier>

static int sigPipeFd[2];

static void signalHandler(int)
{
    const char c = 1;
    ssize_t r = ::write(sigPipeFd[1], &c, sizeof(c));
    (void)r;
}

QFile debugLogFile;

QMutex logMutex;
bool consoleDebug = false;

void myMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QMutexLocker locker(&logMutex);

    QString timeStamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    QString logMessage = QString("[%1]%2 %3")
                             .arg(timeStamp)
                             .arg([&]{
                                 switch(type) {
                                 case QtDebugMsg: return ""; //debug type is reserved to electrumx process
                                 case QtInfoMsg:
                                 case QtWarningMsg:
                                 case QtCriticalMsg:
                                 case QtFatalMsg: return " [SUPERVISOR]";
                                 default: return "UNK";
                                 }
                             }())
                             .arg(msg);

    if(type != QtDebugMsg && type < QtInfoMsg) {
        logMessage += QString(" (%1:%2)").arg(context.file ? context.file : "").arg(context.line);
    }

    // Always log everything to debug log
    if(debugLogFile.isOpen()) {
        QTextStream out(&debugLogFile);
        out << logMessage << Qt::endl;
        out.flush();
    }

    // Print to console
    if(type != QtDebugMsg || consoleDebug) {
        std::cout << logMessage.toLocal8Bit().constData() << std::endl;
    }

    if(type == QtFatalMsg) {
        abort();
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QCoreApplication::setApplicationName(SUPERVISOR_APP);
    QCoreApplication::setApplicationVersion(SUPERVISOR_VERSION);

    QString wdir = "/home/ubuntu/electrumx";
    QString startElectrumx = "start.sh";
    QString history = "history.sh";

    const QStringList& args = QCoreApplication::arguments();
    for(const QString &arg : args) {
        if(arg == "-version" || arg == "-v") {
            std::cout << SUPERVISOR_APP << " - " << SUPERVISOR_VERSION << std::endl;
            return 0;
        }
        else if(arg == "-debug" || arg == "-d") {
            consoleDebug = true;
        }
        else if(arg.startsWith("-dir=") && arg.split("=").size() == 2) {
            wdir = arg.section("=", 1);
        }
        else if(arg.startsWith("-start=") && arg.split("=").size() == 2) {
            startElectrumx = arg.section("=", 1);
        }
        else if(arg.startsWith("-history=") && arg.split("=").size() == 2) {
            history = arg.section("=", 1);
        }
    }

    // ensure the dir exists before to continue and create log file
    if(!QDir(wdir).exists()) {
        std::cerr << "ABORT - Working directory does not exist:" << wdir.toStdString() << std::endl;
        return -1;
    }

    const QString logPath = QDir(wdir).filePath("supervisor.log");
    debugLogFile.setFileName(logPath);
    if(!debugLogFile.open(QIODevice::Append | QIODevice::Text)) {
        std::cerr << "Failed to open log file: " << logPath.toStdString() << std::endl;
        return -1;
    }

    qInstallMessageHandler(myMessageHandler);

    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info().noquote() << "Starting Electrumx Supervisor, version:" << SUPERVISOR_VERSION;

    ElectrumxSupervisor sup(wdir, startElectrumx, history);

    if(::pipe(sigPipeFd) != 0) {
        std::cerr << "pipe() failed\n";
        return -1;
    }

    QSocketNotifier sn(sigPipeFd[0], QSocketNotifier::Read);
    QObject::connect(&sn, &QSocketNotifier::activated, [&](){
        sn.setEnabled(false);
        char tmp;
        ssize_t n = ::read(sigPipeFd[0], &tmp, sizeof(tmp));
        (void)n; // silence warning

        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info().noquote() << "Termination signal received; requesting supervisor stop.";

        sup.requestStop();
        a.quit();

        sn.setEnabled(true);
    });

    // register specific signals to catch
    ::signal(SIGTERM, signalHandler);
    ::signal(SIGINT, signalHandler);

    if(!sup.start()) {
        return -1;
    }

    int result = a.exec();

    qInstallMessageHandler(nullptr);

    debugLogFile.close();

    ::close(sigPipeFd[0]);
    ::close(sigPipeFd[1]);

    return result;
}
