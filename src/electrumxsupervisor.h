#ifndef ELECTRUMXSUPERVISOR_H
#define ELECTRUMXSUPERVISOR_H

#include <QObject>
#include <QProcess>
#include <QTimer>

class ElectrumxSupervisor : public QObject
{
    Q_OBJECT

public:
    explicit ElectrumxSupervisor(const QString wdir,
                                 const QString start,
                                 const QString history,
                                 QObject* parent = nullptr);
    bool start();
    void requestStop();

private Q_SLOTS:
    void startHistory();
    void startElectrumx();

    void onHistoryFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onElectrumxFinished(int exitCode, QProcess::ExitStatus exitStatus);

    void onHistoryOutput();
    void onElectrumxOutput();

private:
    void startScript(QProcess& proc, const QString& scriptName);
    void stopProcess(QProcess& proc, const QString& name, int termMs, int killMs);

private:
    QString m_work_dir;
    QString m_start_electrumx;
    QString m_compact_history;

    QProcess m_history;
    QProcess m_electrumx;

    QTimer m_backoff;
    int m_backoffMs = 2000;

    bool m_shutdownRequested = false;
};

#endif // ELECTRUMXSUPERVISOR_H
