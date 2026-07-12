// Logger.h
#pragma once
#include <QObject>
#include <QString>

class Logger : public QObject {
    Q_OBJECT
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    void logMessage(const QString& msg) { emit newMessage(msg); }

signals:
    void newMessage(const QString& msg);

private:
    Logger() = default;
    Q_DISABLE_COPY(Logger)
};