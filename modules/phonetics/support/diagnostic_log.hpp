#pragma once
#ifndef CORAL_MODULES_PHONETICS_SUPPORT_DIAGNOSTIC_LOG_HPP_INCLUDED
#define CORAL_MODULES_PHONETICS_SUPPORT_DIAGNOSTIC_LOG_HPP_INCLUDED

#include <chrono>
#if defined(_WIN32) || defined(_MSC_VER)
#include <string>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <iostream>

class Logger {
public:
    static Logger& instance() {
        static Logger instance;
        return instance;
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // 写日志到文件
    void write(const std::string& message) {
        check_and_create_file(); // 检查是否需要创建新文件
        if (file.is_open()) {
            file << current_date_time() << " - " << message << "\n";
            file.flush();
        } else {
            std::cerr << "Failed to open file: " << outputFile << std::endl;
        }
    }

    void write(const char* message) {
        write(std::string(message));
    }

    // 设置日志文件路径
    void set_output_file(const std::string& directoryPath) {
        std::string logDir = directoryPath + "/log";
        std::filesystem::create_directories(logDir); // 确保日志目录存在

        outputFile = logDir + "/enginelog" + current_hour_string() + "h.log";
        if (file.is_open()) file.close();
        file.open(outputFile, std::ios::out | std::ios::app);
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << outputFile << std::endl;
        }
    }

private:
    Logger() {
        coralInputMethodDir = std::filesystem::temp_directory_path().string() + "/CORAL Input Method";
        set_output_file(coralInputMethodDir); // 设置默认存储路径
    }
    ~Logger() {
        if (file.is_open()) {
            file.close();
        }
    }

    void check_and_create_file() {
        std::string currentHourLogFile = "enginelog" + current_hour_string() + "h.log";
        if (outputFile.empty() || outputFile.find(currentHourLogFile) == std::string::npos) {
            set_output_file(coralInputMethodDir);
        }
    }

    std::string current_date_time() {
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
#ifdef _MSC_VER
        localtime_s(&tm_buf, &now_c);
#else
        localtime_r(&now_c, &tm_buf);
#endif
        std::ostringstream ss;
        ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    std::string current_hour_string() {
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
#ifdef _MSC_VER
        localtime_s(&tm_buf, &now_c);
#else
        localtime_r(&now_c, &tm_buf);
#endif
        std::ostringstream ss;
        ss << std::put_time(&tm_buf, "%Y%m%d_%H");
        return ss.str();
    }

    std::string coralInputMethodDir;// 临时目录
    std::string outputFile; // 输出日志文件路径
    std::ofstream file; // 文件句柄
};
#elif defined(__linux__) || defined(__ANDROID__)
//todo
class Logger {
public:
    static Logger& instance() {
        static Logger instance;
        return instance;
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // 写日志到文件
    void write(const std::string& message) {
        /*checkAndCreateFile(); // 检查是否需要创建新文件
        QTextStream out(&file);
        if (file.isOpen()) {
            out << QDateTime::currentDateTime().toStringEx("yyyy-MM-dd HH:mm:ss") << " - " << message << "\n";
        }
        else {
            //qDebug() << "Failed to open file: " << outputFile;
        }*/
    }

    void write(const char* message) {
        /*write(QString::fromUtf8(message)); */
    }

    // 设置日志文件路径
    void set_output_file(const std::string& directoryPath) {
        /*std::string logDir = directoryPath + QDir::separator() + "log";
        QDir().mkpath(logDir); // 确保日志目录存在

        outputFile = logDir + QDir::separator() + "enginelog" + QDateTime::currentDateTime().toStringEx("yyyyMMdd_HH") + "h.log";
        file.setFileName(outputFile);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            //qDebug() << "Failed to open file: " << outputFile;
        }*/
    }

private:
    Logger() {
        /*coralInputMethodDir = QDir::tempPath() + QDir::separator() + "CORAL Input Method";
        setOutputFile(coralInputMethodDir); // 设置默认存储路径*/
    }
    ~Logger() {
        /*if (file.isOpen()) {
            file.close();
        }*/
    }

    void check_and_create_file() {
        /*std::string currentHourLogFile = "enginelog" + QDateTime::currentDateTime().toStringEx("yyyyMMdd_HH") + "h.log";
        if (currentHourLogFile != this->outputFile.section(QDir::separator(), -1)) {
            setOutputFile(coralInputMethodDir);
        }*/
    }

    std::string coralInputMethodDir;// 临时目录
    std::string outputFile; // 输出日志文件路径
    //QFile file; // 文件句柄
};
#endif

#endif // LOGGER_HPP（日志头文件）
