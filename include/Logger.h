#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>

class Logger {
public:
    Logger() {
        createLogDirectory();
    }

    void logDayAction(int day, const std::string& action) {
        std::ofstream file(logDir + "/day_" + std::to_string(day) + ".txt", std::ios::app);
        file << action << std::endl;
    }

    void logNightAction(int day, const std::string& action) {
        std::ofstream file(logDir + "/night_" + std::to_string(day) + ".txt", std::ios::app);
        file << action << std::endl;
    }

    void logResult(const std::string& result) {
        std::ofstream file(logDir + "/results.txt", std::ios::app);
        file << result << std::endl;
    }

private:
    const std::string logDir = "../logs"; // так как запускаем игру из build

    void createLogDirectory() {
        std::filesystem::path dirPath(logDir);
        if (!std::filesystem::exists(dirPath)) {
            std::filesystem::create_directory(dirPath);
        }
    }
};
