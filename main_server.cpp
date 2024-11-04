#include <iostream>
#include <cstring>
#include <string>
#include <sstream>
#include <openssl/sha.h>
#include <fstream>
#include <netinet/in.h>
#include <unistd.h>
#include <iomanip>
#include <chrono>
#include <vector>
#include <filesystem>
#include <pstl/glue_algorithm_defs.h>

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

std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;

    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }

    return tokens;
}

std::vector<std::vector<std::string>> splitStringVector(const std::vector<std::string>& original, size_t part_size) {
    if (original.size() % part_size != 0) {
        throw std::out_of_range("Data from client is not a multiple of " + std::to_string(part_size) + ", please check again.");
    }

    std::vector<std::vector<std::string>> parts;
    for (size_t i = 0; i < original.size(); i += part_size) {
        std::vector<std::string> part(original.begin() + i, original.begin() + i + part_size);
        parts.push_back(part);
    }

    return parts;
}

void processStringPart(const std::vector<std::string>& part) {
    // Пример обработки части
    std::cout << "Processing part: ";
    for (const std::string& str : part) {
        std::cout << str << "\n";
    }
    std::cout << std::endl;

    std::string filename = part[0]; 
    std::string text = part[1];
    int name_byte_size = std::stoi(part[2]);
    std::string filehasg = part[3];
    std::string strig_hasg = part[4];

    std::cout << filename << std::endl;
    std::cout << text << std::endl;
    std::cout << name_byte_size << std::endl;
    std::cout << filehasg << std::endl;
    std::cout << strig_hasg << std::endl;

    //checks

    if (filename.size() != name_byte_size){
        throw std::runtime_error("received filename is not correct, check again");   
    }

    std::string full_string = hashString(filename) + hashString(text) + hashString(std::to_string(name_byte_size)) + hashString(filehasg);

    std::cout << "Length of strig_hasg: " << strig_hasg.size() << std::endl;
    std::cout << "Length of full_string: " << full_string.size() << std::endl;

    if (strig_hasg != full_string) {
        throw std::runtime_error("received data is not correct, check again");
    }
    std::filesystem::path dir ("qwe");
    std::filesystem::path full_path = dir / filename;

    std::cout << "File Path - " << full_path << std::endl;

    // Создание файла и запись текста
    std::ofstream outFile(full_path);
    if (!outFile.is_open()) {
        std::cerr << "Unable to create file: " << filename << std::endl;
        return;
    }
    
    outFile << text;
    outFile.close();
    std::cout << "File " << filename << " created successfully and verified." << std::endl;

}

std::string receiveInChunks(int clientSocket) {
    char buffer[1024]; // Буфер для приема данных
    std::vector<char> receivedData; // Вектор для хранения полученных данных

    while (true) {
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived < 0) {
            perror("Error receiving data");
        }
        if (bytesReceived == 0) {
            // Соединение закрыто
            break;
        }

        // Сохраняем данные в векторе
        receivedData.insert(receivedData.end(), buffer, buffer + bytesReceived);
    }

    // Завершаем строку
    receivedData.push_back('\0'); // Добавляем нулевой терминатор
    std::string receivedString(receivedData.begin(), receivedData.end() - 1); // Преобразуем в строку без последнего нулевого символа
    std::cout << "Received string: " << receivedString << std::endl;
    return receivedString;
}

void handleClient(int clientSocket) {
    
    std::string data = receiveInChunks(clientSocket);
    std::size_t part_size = 5;
    char delimiter = ';';

    std::vector<std::string> result = split(data, delimiter);
    std::cout << result.size() << "\n";

    try {
        // Разделяем вектор
        std::vector<std::vector<std::string>> parts = splitStringVector(result, part_size);
        
        // Обрабатываем каждую часть
        for (const auto& part : parts) {
            processStringPart(part);
        }

    } catch (const std::out_of_range& e) {
        std::cerr << e.what() << std::endl;
    }

}

int main() {
    const int port = 8080;
    int serverSocket, clientSocket;

    struct sockaddr_in serverAddr, clientAddr;
    socklen_t addr_size = sizeof(clientAddr);

    // Создание сокета
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        perror("Socket creation failed");
        return -1;
    }

    // Настройка адреса и порта
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    // Привязка сокета
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Bind failed");
        return -1;
    }

    // Начало прослушивания
    if (listen(serverSocket, 5) < 0) {
        perror("Listen failed");
        return -1;
    }

    std::cout << "Server listening on port " << port << std::endl;

    while (true) {
        // Прием клиента
        clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addr_size);
        if (clientSocket < 0) {
            perror("Accept failed");
            continue;
        }

        handleClient(clientSocket); // Обработка клиента
        close(clientSocket); // Закрытие сокета клиента
    }

    close(serverSocket); // Закрытие серверного сокета
    return 0;
}
