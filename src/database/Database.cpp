#include "database/Database.hpp"
#include <stdexcept>
namespace streamgallery {
Database::Database(const std::string& path) {
    if (sqlite3_open_v2(path.c_str(), &connection, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        const std::string message = connection ? sqlite3_errmsg(connection) : "Cannot allocate SQLite connection";
        if (connection) sqlite3_close(connection);
        throw std::runtime_error(message);
    }
    try {
        sqlite3_busy_timeout(connection, 5000);
        execute("PRAGMA foreign_keys=ON");
        execute("BEGIN IMMEDIATE");
        sqlite3_stmt* version = nullptr;
        if (sqlite3_prepare_v2(connection, "PRAGMA user_version", -1, &version, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(connection));
        const int result = sqlite3_step(version);
        const int current = result == SQLITE_ROW ? sqlite3_column_int(version, 0) : -1;
        sqlite3_finalize(version);
        if (current < 0 || current > 3) throw std::runtime_error("Unsupported library schema version");
        if (current == 0) {
            execute(R"SQL(
CREATE TABLE videos(id INTEGER PRIMARY KEY, canonical_url TEXT NOT NULL UNIQUE,
 title TEXT NOT NULL DEFAULT '', description TEXT NOT NULL DEFAULT '',
 added_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
CREATE TABLE folders(id INTEGER PRIMARY KEY, name TEXT NOT NULL CHECK(length(trim(name))>0),
 parent_id INTEGER REFERENCES folders(id) ON DELETE CASCADE);
CREATE TABLE tags(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE CHECK(length(trim(name))>0));
CREATE TABLE video_folders(video_id INTEGER REFERENCES videos(id) ON DELETE CASCADE NOT NULL,
 folder_id INTEGER REFERENCES folders(id) ON DELETE CASCADE NOT NULL, PRIMARY KEY(video_id,folder_id));
CREATE TABLE video_tags(video_id INTEGER REFERENCES videos(id) ON DELETE CASCADE NOT NULL,
 tag_id INTEGER REFERENCES tags(id) ON DELETE CASCADE NOT NULL, PRIMARY KEY(video_id,tag_id));
CREATE VIRTUAL TABLE videos_fts USING fts5(title,description,content='videos',content_rowid='id');
CREATE TRIGGER videos_insert AFTER INSERT ON videos BEGIN
 INSERT INTO videos_fts(rowid,title,description) VALUES(new.id,new.title,new.description); END;
CREATE TRIGGER videos_delete AFTER DELETE ON videos BEGIN
 INSERT INTO videos_fts(videos_fts,rowid,title,description) VALUES('delete',old.id,old.title,old.description); END;
CREATE TRIGGER videos_update AFTER UPDATE ON videos BEGIN
 INSERT INTO videos_fts(videos_fts,rowid,title,description) VALUES('delete',old.id,old.title,old.description);
 INSERT INTO videos_fts(rowid,title,description) VALUES(new.id,new.title,new.description); END;
PRAGMA user_version=1;
)SQL");
        }
        if (current < 2) {
            execute("ALTER TABLE videos ADD COLUMN thumbnail_url TEXT NOT NULL DEFAULT ''");
            execute("CREATE INDEX IF NOT EXISTS idx_videos_title ON videos(title COLLATE NOCASE, id)");
            execute("PRAGMA user_version=2");
        }
        if (current < 3) {
            execute("ALTER TABLE videos ADD COLUMN last_played_at INTEGER NOT NULL DEFAULT 0");
            execute("CREATE INDEX idx_videos_recent ON videos(last_played_at DESC, id DESC)");
            execute("PRAGMA user_version=3");
        }
        execute("COMMIT");
    } catch (...) { sqlite3_close(connection); connection = nullptr; throw; }
}
Database::~Database() { if (connection) sqlite3_close(connection); }
void Database::execute(const std::string& sql) {
    char* error = nullptr;
    if (sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error ? error : sqlite3_errmsg(connection);
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}
}
