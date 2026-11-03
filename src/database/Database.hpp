#pragma once
#include <sqlite3.h>
#include <string>
namespace streamgallery {
// Confine each connection and its repositories to one worker thread.
class Database {
public:
    explicit Database(const std::string& path);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    void execute(const std::string& sql);
    sqlite3* handle() const { return connection; }
private:
    sqlite3* connection = nullptr;
};
}
