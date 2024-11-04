#include <iostream>
#include <fstream>
#include <sstream>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iomanip>
#include <stdexcept>
#include <filesystem>
#include <chrono>
#include <vector>

namespace fs = std::filesystem;

void handleOpenSSLErrors() {
    ERR_print_errors_fp(stderr);
    abort();
}

std::string hashFile(const std::string& filePath) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Unable to open file: " + filePath);
    }

    char buffer[1024];
    while (file.read(buffer, sizeof(buffer))) {
        SHA256_Update(&sha256, buffer, sizeof(buffer));
    }
    SHA256_Update(&sha256, buffer, file.gcount());

    SHA256_Final(hash, &sha256);

    std::ostringstream oss;
    for (const auto& byte : hash) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str();
}

std::string hashString(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data.c_str(), data.size());
    SHA256_Final(hash, &sha256);

    std::ostringstream oss;
    for (const auto& byte : hash) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str();
}

std::vector<fs::path> get_recent_files(const fs::path& directory) {
    std::vector<fs::path> recent_files;
    auto now = std::chrono::system_clock::now();
    
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (fs::is_regular_file(entry)) {
            auto last_write_time = fs::last_write_time(entry);
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(last_write_time - fs::file_time_type::clock::now() + now);

            auto duration_since_last_write = std::chrono::duration_cast<std::chrono::hours>(now - sctp).count();

            if (duration_since_last_write < 24) {
                recent_files.push_back(entry.path());
            }
        }
    }

    return recent_files;
}

void sendInChunks(int sock, const std::string &final_string, size_t chunkSize) {
    size_t totalSent = 0;
    size_t stringSize = final_string.size();

    while (totalSent < stringSize) {
        size_t remaining = stringSize - totalSent;
        size_t toSend = std::min(chunkSize, remaining);
        
        int bytesSent = send(sock, final_string.c_str() + totalSent, toSend, 0);
        if (bytesSent < 0) {
            perror("Error sending data");
            return;
        }
        totalSent += bytesSent;
    }
}


void sendFileInfo(int sock, const fs::path& path) {
    std::string filename = path.filename().string();
    std::size_t name_byte_size = filename.size();
    std::size_t chunkSize = 1024;

    // Чтение текста файла
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Unable to open file: " + path.string());
    }

    std::ostringstream text_stream;
    text_stream << file.rdbuf();
    std::string text = text_stream.str();

    // Вычисление хеша текста
    std::string text_hash = hashFile(path.string());

    // Создание итоговой строки
    std::string final_string = filename + ';' + text + ';' + std::to_string(name_byte_size) + ';' + text_hash + ';';
    
    // Хеш всей строки
    std::string filename_hash = hashString(filename);
    std::string final_text_hash = hashString(text);
    std::string name_byte_size_hash = hashString(std::to_string(name_byte_size));
    std::string final_text_hash_hash = hashString(text_hash);

    final_string += filename_hash; // Добавляем хеш всей строки в конец
    final_string += final_text_hash;
    final_string += name_byte_size_hash;
    final_string += final_text_hash_hash + ';';

    std::cout << final_string << std::endl; 

    // Отправка строки на сервер
    sendInChunks(sock, final_string, chunkSize);
}

int main(int argc, char* argv[]) {

    std::string directory = argv[1];
    const std::string serverAddress = "127.0.0.1"; // Адрес сервера
    const int serverPort = 8080; // Порт сервера
    //const std::string directory = "ewq";

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);
    inet_pton(AF_INET, serverAddress.c_str(), &serverAddr.sin_addr);

    // Создание сокета
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return -1;
    }

    // Подключение к серверу
    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Connection failed");
        close(sock);
        return -1;
    }

    auto files = get_recent_files(directory);
    
    try {
        for (const auto& path : files) {
            sendFileInfo(sock, path); // Отправляем информацию о каждом файле
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }

    close(sock); // Закрытие сокета
    return 0;
}
