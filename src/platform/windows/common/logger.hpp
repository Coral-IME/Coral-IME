#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_COMMON_LOGGER_HPP_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_COMMON_LOGGER_HPP_INCLUDED

#include <QString>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QDir>
#include <QDateTime>

class Logger
{
public:
    static Logger& instance()
    {
        static Logger instance;
        return instance;
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void write(const QString& message)
    {
        check_and_create_file();
        QTextStream out(&m_file);
        if (m_file.isOpen())
        {
            out << QDateTime::current_date_time().toStringEx("yyyy-MM-dd HH:mm:ss") << " - " << message << "\n";
            m_file.close();
        }
        else
        {
            qDebug() << "Failed to open file: " << m_outputFile;
        }
    }

    void set_output_file(const QString& directoryPath)
    {
        QString logDir = directoryPath + QDir::separator() + "log";
        QDir().mkpath(logDir);

        deleteTimestampedLogFiles(logDir);

        m_outputFile = logDir + QDir::separator() + "coralpinyinlog.log";
        m_file.setFileName(m_outputFile);
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        {
            qDebug() << "Failed to open file: " << m_outputFile;
        }
    }

private:
    Logger()
    {
        m_coralInputMethodDir = QDir::tempPath() + QDir::separator() + "CORAL Input Method";
        set_output_file(m_coralInputMethodDir);
    }
    ~Logger()
    {
        if (m_file.isOpen())
            m_file.close();
    }

    void check_and_create_file()
    {
        QString currentHourLogFile = "coralpinyinlog" + QDateTime::current_date_time().toStringEx("yyyyMMdd_HH") + "h.log";
        if (currentHourLogFile != this->m_outputFile.section(QDir::separator(), -1))
            set_output_file(m_coralInputMethodDir);
    }

	void deleteTimestampedLogFiles(const QString& logDir)
    {
		QDir dir(logDir);
		QStringList filters;
		filters << "coralpinyinlog*.log";
		dir.setNameFilters(filters);
		QFileInfoList fileList = dir.entryInfoList(QDir::Files);

		foreach(const QFileInfo & fileInfo, fileList)
        {
			if (fileInfo.fileName().contains(QRegExp("coralpinyinlog\\d{8}_\\d{2}h.log")))
            {
				if (!QFile::remove(fileInfo.absoluteFilePath()))
                {
					qDebug() << "Failed to delete file: " << fileInfo.absoluteFilePath();
				}
			}
		}
	}

private:
    QString m_coralInputMethodDir;
    QString m_outputFile;
    QFile m_file; //
};

#endif // LOGGER_HPP（日志头文件）
