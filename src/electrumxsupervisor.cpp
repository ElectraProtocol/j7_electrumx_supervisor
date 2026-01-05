#include "electrumxsupervisor.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>

ElectrumxSupervisor::ElectrumxSupervisor(const QString wdir, const QString start, const QString history, QObject* parent)
    : QObject(parent)
    , m_work_dir(wdir)
    , m_start_electrumx(start)
    , m_compact_history(history)
{
    m_history.setProcessChannelMode(QProcess::MergedChannels);
    m_electrumx.setProcessChannelMode(QProcess::MergedChannels);

    connect(&m_history, &QProcess::readyRead, this, &ElectrumxSupervisor::onHistoryOutput);
    connect(&m_electrumx, &QProcess::readyRead, this, &ElectrumxSupervisor::onElectrumxOutput);

    connect(&m_history, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &ElectrumxSupervisor::onHistoryFinished);

    connect(&m_electrumx, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &ElectrumxSupervisor::onElectrumxFinished);

    m_backoff.setSingleShot(true);
    connect(&m_backoff, &QTimer::timeout, this, &ElectrumxSupervisor::startHistory);
}

bool ElectrumxSupervisor::start()
{
    QDir dir(m_work_dir);
    const QString startPath = dir.filePath(m_start_electrumx);
    const QString historyPath = dir.filePath(m_compact_history);

    const QFileInfo startInfo(startPath);
    if(!startInfo.exists() || !startInfo.isFile() || !startInfo.isExecutable()) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).critical().noquote() << "ABORT - Electrumx starting script missing or not executable:\n" << startPath;
        return false;
    }

    const QFileInfo histInfo(historyPath);
    if(!histInfo.exists() || !histInfo.isFile() || !histInfo.isExecutable()) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).critical().noquote() << "ABORT - Electrumx history script missing or not executable:\n" << historyPath;
        return false;
    }

    startHistory();
    return true;
}

void ElectrumxSupervisor::requestStop()
{
    if(m_shutdownRequested) {
        return;
    }
    m_shutdownRequested = true;

    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info().noquote() << "Shutdown requested. Stopping timers and child processes...";

    m_backoff.stop();

    // Stop history first (it may be compacting)
    stopProcess(m_history, "history", 15000, 5000);

    // Stop electrumx
    stopProcess(m_electrumx, "electrumx", 30000, 10000);
}

void ElectrumxSupervisor::startScript(QProcess& proc, const QString& scriptName)
{
    if(proc.state() != QProcess::NotRunning) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning().noquote() << "Refusing to start (already running): " << scriptName;
        return;
    }

    QDir dir(m_work_dir);
    const QString scriptPath = dir.filePath(scriptName);

    proc.setWorkingDirectory(m_work_dir);
    proc.setProgram("/bin/bash");
    proc.setArguments({"-lc", QString("exec \"%1\"").arg(scriptPath)});

    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info().noquote() << "Starting script:" << scriptName;
    proc.start();

    if(!proc.waitForStarted(15000)) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning().noquote() << "Failed to start" << scriptPath << ":" << proc.errorString();
    }
}

void ElectrumxSupervisor::stopProcess(QProcess& proc, const QString& name, int termMs, int killMs)
{
    if(proc.state() == QProcess::NotRunning) {
        return;
    }

    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info().noquote() << "Stopping" << name << "...";

    proc.terminate();
    if(!proc.waitForFinished(termMs)) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning().noquote() << name << "did not terminate in time; killing...";
        proc.kill();
        proc.waitForFinished(killMs);
    }
}

void ElectrumxSupervisor::startHistory()
{
    if(m_shutdownRequested) {
        return;
    }

    m_backoff.stop();

    if(m_electrumx.state() != QProcess::NotRunning) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info().noquote() << "Stopping electrumx before running history...";
        m_electrumx.terminate();
        if(!m_electrumx.waitForFinished(30000)) {
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info().noquote() << "electrumx did not terminate; killing...";
            m_electrumx.kill();
            m_electrumx.waitForFinished(10000);
        }
    }

    startScript(m_history, m_compact_history);
}

void ElectrumxSupervisor::startElectrumx()
{
    if(m_shutdownRequested) {
        return;
    }

    startScript(m_electrumx, m_start_electrumx);
}

void ElectrumxSupervisor::onHistoryFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info().noquote() << "compact history finished, exitCode:" << exitCode << "- status:"
                                                                     << (exitStatus == QProcess::NormalExit ? "NormalExit" : "CrashExit");

    if(m_shutdownRequested) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info().noquote() << "Shutdown requested; not starting electrumx.";
        return;
    }

    if(exitStatus != QProcess::NormalExit || exitCode != 0) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info().noquote() << "compacting history failed; backing off and retrying...";
        m_backoffMs = qMin(m_backoffMs * 2, 300000);
        m_backoff.start(m_backoffMs);
        return;
    }

    m_backoffMs = 2000;
    startElectrumx();
}

void ElectrumxSupervisor::onElectrumxFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info().noquote() << "electrumx finished, exitCode:" << exitCode << "- status:"
                                                                     << (exitStatus == QProcess::NormalExit ? "NormalExit" : "CrashExit");

    if(m_shutdownRequested) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info().noquote() << "Shutdown requested; not restarting.";
        return;
    }

    // whenever it stops, run history then restart.
    m_backoff.start(2000);
}

void ElectrumxSupervisor::onHistoryOutput()
{
    const QString t = QString::fromUtf8(m_history.readAll());
    for(const QString& line : t.split('\n', Qt::SkipEmptyParts)) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug().noquote() << "[history]" << line;
    }
}

void ElectrumxSupervisor::onElectrumxOutput()
{
    const QString t = QString::fromUtf8(m_electrumx.readAll());
    for(const QString& line : t.split('\n', Qt::SkipEmptyParts)) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug().noquote() << "[electrumx]" << line;
    }
}
