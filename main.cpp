#include <iostream>
#include <boost/crc.hpp> // библиотека для вычисления CRC32 (циклический избыточный код)
#include <string>
#include <vector>
#include <filesystem>
#include <map> // для хэш-таблиц
#include <algorithm>
#include <fstream>
#include <regex> // регулярные выражения для обработки строк
#include <set>

namespace fs = std::filesystem;

// Функция для вычисления CRC32
// boost::crc_32_type - тип, используемый для вычисления CRC32, является специальной реализацией класса CRC, оптимизированной для 32-битных CRC
// process_bytes - метод класса boost::crc_32_type, который обрабатывает массив байтов для вычисления CRC
// checksum - метод класса boost::crc_32_type, который возвращает вычисленный CRC (хэш)
uint32_t calculateCRC32(const std::string& data) {
    boost::crc_32_type crc;
    crc.process_bytes(data.data(), data.size()); // обработка байтов строки
    return crc.checksum(); // хэш
}

// Функция для чтения файла и получения последовательности хэшей
// std::filesystem::path - класс для работы с путями файловой системы, используемый для представления путей к файлам и директориям
// std::regex - класс для работы с регулярными выражениями, используемый для фильтрации имен файлов по заданной маске
// gcount - возвращает количество прочитанных байтов (std::ifstream)
std::vector<uint32_t> readFile(const fs::path& filePath, size_t blockSize) {
    std::vector<uint32_t> hashSequence; // вектор последовательности хэшей
    std::ifstream file(filePath, std::ios::binary); // открытие файла в бинарном режиме
    if (!file) { // если файл не открывается
        throw std::runtime_error("Cannot open file: " + filePath.string());
    }
    std::string buffer(blockSize, '\0'); // буфер для чтения блоков
    while (file.read(&buffer[0], blockSize) || file.gcount() > 0) { // чтение данных из файла блоками
        size_t bytesRead = static_cast<size_t>(file.gcount()); // количество прочитанных байтов
        buffer.resize(bytesRead); // обновление размера буфера до количества прочитанных байтов
        if (bytesRead < blockSize) { // если прочитано меньше байтов, чем размер блока
            buffer.resize(blockSize, '\0'); // дополнение буфера нулями до полного размера блока
        }
        uint32_t hash = calculateCRC32(buffer); // вычисление CRC32-хэша блока
        hashSequence.push_back(hash); // добавление хэша в вектор хэшей
    }
    return hashSequence; // возвращение вектора хэшей после завершения чтения файла
}

// Функция для сравнения двух файлов по их хэшам
bool compareHashes(const std::vector<uint32_t>& hashes1, const std::vector<uint32_t>& hashes2) {
    if (hashes1.size() != hashes2.size()) return false; // размеры векторов разные => файлы разные
    for (size_t i = 0; i < hashes1.size(); ++i) { // перебор элементов векторов
        if (hashes1[i] != hashes2[i]) return false; // хотя бы один хэш не совпадает => файлы разные
    }
    return true; // файлы идентичны
}

// Функция для обработки файла
// std::filesystem::directory_entry - класс, который представляет собой элемент каталога файловой системы, используется для работы с файлами и директориями, предоставляя информацию о них
// is_regular_file - проверяет, является ли элемент обычным файлом (std::filesystem::directory_entry)
// regex_match - проверяет, соответствует ли строка заданному регулярному выражению (std::regex)
void processFile(const fs::directory_entry& entry, const std::vector<fs::path>& exclusions, size_t minSize, const std::regex& maskRegex, size_t blockSize, std::map<fs::path, std::vector<uint32_t>>& allFiles) {
    if (entry.is_regular_file()) { // является ли элемент обычным файлом
        if (std::find(exclusions.begin(), exclusions.end(), entry.path().parent_path()) != exclusions.end()) { // если родительская директория файла в списке исключений
            return;
        }
        if (entry.file_size() < minSize) { // если размер файла меньше минимального размера
            return;
        }
        if (!std::regex_match(entry.path().filename().string(), maskRegex)) { // если имя файла не подходит к маске
            return;
        }
        auto hashes = readFile(entry.path(), blockSize); // вычисление хэшей файла
        allFiles[entry.path()] = hashes;
    }
}

// Функция для поиска дубликатов
void findDuplicates(const std::vector<fs::path>& directories, const std::vector<fs::path>& exclusions, size_t blockSize, size_t minSize, std::regex& maskRegex, int scanLevel) {
    std::map<std::string, std::set<fs::path>> duplicates; // словарь для хранения путей к дубликатам по их хэшам
    std::map<fs::path, std::vector<uint32_t>> allFiles; // вектор пар для хранения путей к файлам и их хэшей
    for (const auto& dir : directories) { // перебор директорий
        if (!fs::exists(dir) || !fs::is_directory(dir)) { // если директории не существует или не является директорий
            std::cerr << "Directory doesn't exist or isn't a directory: " << dir << std::endl;
            continue;
        }
        if (scanLevel == 0) { // только указанная директория без вложенных
            for (const auto& entry : fs::directory_iterator(dir)) { // итератор, который перебирает только файлы в указанной директории без рекурсии
                processFile(entry, exclusions, minSize, maskRegex, blockSize, allFiles); // обработка файла
            }
        } else { // сканирование с вложенными
            for (const auto& entry : fs::recursive_directory_iterator(dir)) { // итератор, который перебирает все файлы и поддиректории рекурсивно
                processFile(entry, exclusions, minSize, maskRegex, blockSize, allFiles); // обработка файла
            }
        }
    }
    // Сравнение хешей и добавление дубликатов в duplicates
    for (auto it1 = allFiles.begin(); it1 != allFiles.end(); ++it1) {
        for (auto it2 = std::next(it1); it2 != allFiles.end(); ++it2) {
            if (compareHashes(it1->second, it2->second)) { // если хэши файлов равны
                std::string hashKey(reinterpret_cast<const char*>(it1->second.data()), it1->second.size() * sizeof(uint32_t)); // последовательность байтов из вектора хэшей
                // Добавление путей к дубликатам
                duplicates[hashKey].insert(it1->first); // путь к файлу из первого итератора
                duplicates[hashKey].insert(it2->first); // путь к файлу из второго итератора
            }
        }
    }
    // Вывод результатов
    for (const auto& duplicate : duplicates) {
        std::cout << "Duplicates:\n";
        for (const auto& file : duplicate.second) {
            std::cout << file << std::endl;
        }
    }
}

// std::regex_replace - заменяет все вхождения подстроки, соответствующей регулярному выражению, на другую строку
// std::regex_constants::icase - указывает на то, что регулярное выражение должно игнорировать регистр
int main() {
    std::vector<fs::path> directories;
    std::vector<fs::path> exclusions;
    size_t blockSize; // размер блока 
    size_t minSize = 1; // минимальный размер файла
    int scanLevel; // уровень сканирования (1 - рекурсивное)
    int numDirs; // количество директорий для сканирования
    std::cout << "Enter the number of directories to scan: ";
    std::cin >> numDirs;
    for (int i = 0; i < numDirs; ++i) {
        fs::path dir;
        std::cout << "Enter the path to the directory " << (i + 1) << ": ";
        std::cin >> dir;
        directories.push_back(dir);
    }
    int numExclusions; // количество директорий для исключения
    std::cout << "Enter the number of directories to exclude: ";
    std::cin >> numExclusions;
    for (int i = 0; i < numExclusions; ++i) {
        fs::path exclDir;
        std::cout << "Enter the path to the directory " << (i + 1) << " to exclude: ";
        std::cin >> exclDir;
        exclusions.push_back(exclDir);
    }
    std::cout << "Enter the scan level (0 - only the specified directory without nested ones, 1 - with attachments): ";
    std::cin >> scanLevel;
    std::string maskString; // маска имен файлов
    std::cout << "Enter a file name mask for comparison (for example, *.txt or file?.txt): ";
    std::cin >> maskString;
    // Преобразование маски в регулярное выражение
    std::regex star_regex("\\*");
    std::regex question_regex("\\?");
    maskString = "^" + std::regex_replace(maskString, star_regex, ".*"); // замена "*" на ".*"
    maskString = std::regex_replace(maskString, question_regex, "."); // замена "?" на "."
    maskString += "$"; // добавление конца строки
    std::cout << "Enter the block size (recommended value is 4096): ";
    std::cin >> blockSize;
    std::regex maskRegex(maskString, std::regex_constants::icase); // игнорируем регистр
    findDuplicates(directories, exclusions, blockSize, minSize, maskRegex, scanLevel);
    return 0;
}