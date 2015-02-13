#include "loghandler.h"
#include <QDateTime>
#include <QDate>
#include <QApplication>
#include <QSettings>

LogHandler::LogHandler(QObject *parent) :
    QObject(parent)
{
    qRegisterMetaType<LogHandler::MessageCode>("LogHandler::MessageCode");
    d_currentMonth = QDate::currentDate().month();
    d_logFile.setFileName(makeLogFileName());
    d_logFile.open(QIODevice::Append);
}

LogHandler::~LogHandler()
{
    if(d_logFile.isOpen())
        d_logFile.close();
}

void LogHandler::logMessage(const QString text, const MessageCode type)
{
	QDateTime time;
    QString timeStamp = time.currentDateTime().toString();
    writeToFile(text, type, timeStamp);

    if(type == LogHandler::Debug)
        return;

	QString out;
    out.append(QString("<span style=\"font-size:7pt\">%1</span> ").arg(timeStamp));

	switch(type)
	{
	case Warning:
		out.append(QString("<span style=\"font-weight:bold\">Warning: %1</span>").arg(text));
		break;
	case Error:
		out.append(QString("<span style=\"font-weight:bold;color:red\">Error: %1</span>").arg(text));
		break;
	case Highlight:
		out.append(QString("<span style=\"font-weight:bold;color:green\">%1</span>").arg(text));
		break;
	case Normal:
	default:
		out.append(text);
		break;
	}

	//emit signal containing formatted message
    emit sendLogMessage(out);
}

void LogHandler::writeToFile(const QString text, const LogHandler::MessageCode type, const QString timeStamp)
{
    QDate now = QDate::currentDate();
    if(now.month() != d_currentMonth)
    {
        d_currentMonth = now.month();
        QString newLogFile = makeLogFileName();

        if(d_logFile.isOpen())
            d_logFile.close();

        d_logFile.setFileName(newLogFile);

        d_logFile.open(QIODevice::Append);
    }

    if(d_logFile.isOpen())
    {
        QString msg = QString("%1: ").arg(timeStamp);
        switch (type)
        {
        case LogHandler::Warning:
            msg.append(QString("[WARNING] "));
            break;
        case LogHandler::Error:
            msg.append(QString("[ERROR] "));
            break;
        case LogHandler::Debug:
            msg.append(QString("[DEBUG] "));
            break;
        default:
            break;
        }

        msg.append(text).append(QString("\n"));

        d_logFile.write(msg.toLatin1());
        d_logFile.flush();
    }
}

QString LogHandler::makeLogFileName()
{
    QString month;
    if(d_currentMonth < 10)
        month = QString("0%1").arg(d_currentMonth);
    else
        month = QString::number(d_currentMonth);

    QSettings s(QSettings::SystemScope,QApplication::organizationName(),QApplication::applicationName());
    return QString("%1/log/%2%3.log").arg(s.value(QString("savePath")).toString()).arg(QDate::currentDate().year()).arg(month);

}
