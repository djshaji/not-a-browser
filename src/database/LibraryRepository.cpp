#include "database/LibraryRepository.hpp"
#include <glib.h>
#include <algorithm>
#include <memory>
#include <stdexcept>
namespace streamgallery {
namespace {
std::string trimTag(const std::string& name) {
    auto* trimmed = g_strdup(name.c_str());
    g_strstrip(trimmed);
    std::string result(trimmed);
    g_free(trimmed);
    return result;
}
class Statement {
public:
    Statement(Database& db, const char* sql) : connection(db.handle()) {
        if (sqlite3_prepare_v2(connection, sql, -1, &statement, nullptr) != SQLITE_OK) fail();
    }
    ~Statement() { sqlite3_finalize(statement); }
    void text(int index, const std::string& value) {
        if (sqlite3_bind_text(statement,index,value.c_str(),-1,SQLITE_TRANSIENT) != SQLITE_OK) fail();
    }
    void integer(int index, std::int64_t value) {
        if (sqlite3_bind_int64(statement,index,value) != SQLITE_OK) fail();
    }
    bool step() {
        const int code=sqlite3_step(statement);
        if (code != SQLITE_ROW && code != SQLITE_DONE) fail();
        return code == SQLITE_ROW;
    }
    std::int64_t id(int index=0) { return sqlite3_column_int64(statement,index); }
    std::string textColumn(int index) { return reinterpret_cast<const char*>(sqlite3_column_text(statement,index)); }
private:
    [[noreturn]] void fail() { throw std::runtime_error(sqlite3_errmsg(connection)); }
    sqlite3* connection;
    sqlite3_stmt* statement=nullptr;
};
}
std::string canonicalizeUrl(const std::string& url) {
    if (url.empty() || url.find_first_of("\r\n\t ") != std::string::npos || url.find('\0') != std::string::npos)
        throw std::invalid_argument("Enter an absolute HTTP or HTTPS URL without whitespace");
    std::unique_ptr<GUri, decltype(&g_uri_unref)> uri(g_uri_parse(url.c_str(), G_URI_FLAGS_ENCODED, nullptr), g_uri_unref);
    if (!uri || !g_uri_get_host(uri.get()) || !*g_uri_get_host(uri.get())) throw std::invalid_argument("URL needs a host");
    std::string scheme=g_uri_get_scheme(uri.get()) ? g_uri_get_scheme(uri.get()) : "";
    std::transform(scheme.begin(),scheme.end(),scheme.begin(),[](unsigned char c) { return g_ascii_tolower(c); });
    if ((scheme != "http" && scheme != "https") || g_uri_get_userinfo(uri.get()))
        throw std::invalid_argument("Only HTTP(S) URLs without credentials are supported");
    std::string host=g_uri_get_host(uri.get());
    std::transform(host.begin(),host.end(),host.begin(),[](unsigned char c) { return g_ascii_tolower(c); });
    int port=g_uri_get_port(uri.get());
    if ((scheme=="https" && port==443)||(scheme=="http" && port==80)) port=-1;
    const char* path=g_uri_get_path(uri.get());
    gchar* normalized=g_uri_join(G_URI_FLAGS_ENCODED,scheme.c_str(),nullptr,host.c_str(),port,
        path && *path ? path : "/",g_uri_get_query(uri.get()),nullptr);
    std::string result(normalized); g_free(normalized); return result;
}
Video LibraryRepository::bookmark(const std::string& url, const std::string& title, const std::string& thumbnailUrl) {
    return bookmarkWithStatus(url, title, thumbnailUrl).video;
}
BookmarkResult LibraryRepository::bookmarkWithStatus(const std::string& url, const std::string& title, const std::string& thumbnailUrl) {
    const auto canonical=canonicalizeUrl(url);
    if (!thumbnailUrl.empty() && !g_str_has_prefix(thumbnailUrl.c_str(), "file://"))
        (void)canonicalizeUrl(thumbnailUrl);
    Statement insert(db,"INSERT INTO videos(canonical_url,title,thumbnail_url) VALUES(?,?,?) ON CONFLICT(canonical_url) DO NOTHING");
    insert.text(1,canonical); insert.text(2,title); insert.text(3,thumbnailUrl); insert.step();
    const bool created = sqlite3_changes(db.handle()) != 0;
    Statement select(db,"SELECT id,canonical_url,title,thumbnail_url,description FROM videos WHERE canonical_url=?");
    select.text(1,canonical); if (!select.step()) throw std::runtime_error("Bookmark was not saved");
    return {{select.id(),select.textColumn(1),select.textColumn(2),select.textColumn(3),select.textColumn(4),videoTags(select.id())}, created};
}
void LibraryRepository::updateMetadata(std::int64_t videoId, const std::string& title, const std::string& description) {
    Statement update(db,"UPDATE videos SET title=?,description=? WHERE id=?");
    update.text(1,title); update.text(2,description); update.integer(3,videoId); update.step();
    if (sqlite3_changes(db.handle()) != 1) throw std::runtime_error("Bookmark no longer exists");
}
std::vector<std::string> LibraryRepository::videoTags(std::int64_t videoId) {
    Statement select(db,"SELECT name FROM tags JOIN video_tags ON tags.id=video_tags.tag_id WHERE video_id=? ORDER BY name COLLATE NOCASE,name");
    select.integer(1,videoId);
    std::vector<std::string> result;
    while (select.step()) result.push_back(select.textColumn(0));
    return result;
}
void LibraryRepository::updateBookmark(std::int64_t videoId, const std::string& title, const std::string& description,
                                      const std::vector<std::string>& tags) {
    db.execute("BEGIN IMMEDIATE");
    try {
        updateMetadata(videoId,title,description);
        Statement clear(db,"DELETE FROM video_tags WHERE video_id=?");
        clear.integer(1,videoId); clear.step();
        for (const auto& name : tags) tagVideo(videoId,createTag(name).id);
        db.execute("COMMIT");
    } catch (...) { db.execute("ROLLBACK"); throw; }
}
RemovedBookmark LibraryRepository::removeBookmark(std::int64_t videoId) {
    db.execute("BEGIN IMMEDIATE");
    try {
        RemovedBookmark removed;
        Statement select(db,"SELECT id,canonical_url,title,thumbnail_url,description,added_at FROM videos WHERE id=?");
        select.integer(1,videoId);
        if (!select.step()) throw std::runtime_error("Bookmark no longer exists");
        removed.video = {select.id(),select.textColumn(1),select.textColumn(2),select.textColumn(3),select.textColumn(4),videoTags(select.id())};
        removed.addedAt = select.textColumn(5);
        Statement folders(db,"SELECT folder_id FROM video_folders WHERE video_id=?");
        folders.integer(1,videoId);
        while (folders.step()) removed.folderIds.push_back(folders.id());
        Statement tags(db,"SELECT tag_id FROM video_tags WHERE video_id=?");
        tags.integer(1,videoId);
        while (tags.step()) removed.tagIds.push_back(tags.id());
        Statement remove(db,"DELETE FROM videos WHERE id=?");
        remove.integer(1,videoId); remove.step();
        db.execute("COMMIT");
        return removed;
    } catch (...) { db.execute("ROLLBACK"); throw; }
}
void LibraryRepository::restoreBookmark(const RemovedBookmark& removed) {
    db.execute("BEGIN IMMEDIATE");
    try {
        Statement duplicate(db,"SELECT id FROM videos WHERE canonical_url=?");
        duplicate.text(1,removed.video.canonicalUrl);
        if (duplicate.step()) throw std::runtime_error("Cannot undo: this URL is already in Library. The existing bookmark was kept; Undo remains available.");
        Statement occupied(db,"SELECT id FROM videos WHERE id=?");
        occupied.integer(1,removed.video.id);
        const auto id = occupied.step() ? 0 : removed.video.id;
        Statement insert(db,"INSERT INTO videos(id,canonical_url,title,thumbnail_url,description,added_at) VALUES(NULLIF(?,0),?,?,?,?,?)");
        insert.integer(1,id); insert.text(2,removed.video.canonicalUrl); insert.text(3,removed.video.title);
        insert.text(4,removed.video.thumbnailUrl); insert.text(5,removed.video.description); insert.text(6,removed.addedAt);
        insert.step();
        const auto restoredId = sqlite3_last_insert_rowid(db.handle());
        for (auto folder : removed.folderIds) addToFolder(restoredId,folder);
        for (auto tag : removed.tagIds) tagVideo(restoredId,tag);
        db.execute("COMMIT");
    } catch (...) { db.execute("ROLLBACK"); throw; }
}
std::vector<Video> LibraryRepository::videos() {
    Statement select(db,"SELECT id,canonical_url,title,thumbnail_url,description FROM videos ORDER BY id");
    std::vector<Video> result;
    while(select.step()) result.push_back({select.id(),select.textColumn(1),select.textColumn(2),select.textColumn(3),select.textColumn(4),videoTags(select.id())});
    return result;
}
VideoPage LibraryRepository::page(const std::string& search, VideoSort sort, int offset, int limit, const std::string& tag) {
    if (offset < 0 || limit < 1 || limit > 100) throw std::invalid_argument("Invalid page bounds");
    // Treat input as literal prefix terms, never as executable FTS query syntax.
    std::string expression;
    gchar** terms = g_strsplit_set(search.c_str(), " \t\r\n", -1);
    for (int i = 0; terms[i]; ++i) {
        if (!*terms[i]) continue;
        if (!expression.empty()) expression += " AND ";
        expression += '\"';
        for (const char* c = terms[i]; *c; ++c) {
            if (*c == '\"') expression += '\"';
            expression += *c;
        }
        expression += "\"*";
    }
    g_strfreev(terms);
    std::string where = expression.empty() ? "" :
        " WHERE id IN (SELECT rowid FROM videos_fts WHERE videos_fts MATCH ?)";
    const auto tagName = trimTag(tag);
    if (!tagName.empty()) where += (where.empty() ? " WHERE " : " AND ") +
        std::string("id IN (SELECT video_id FROM video_tags JOIN tags ON tags.id=video_tags.tag_id WHERE tags.name=?)");
    const char* order = sort == VideoSort::Title ? "title COLLATE NOCASE,id" :
        sort == VideoSort::Oldest ? "id ASC" : "id DESC";
    VideoPage result;
    const std::string countSql = "SELECT count(*) FROM videos" + where;
    Statement count(db, countSql.c_str());
    int countIndex = 1;
    if (!expression.empty()) count.text(countIndex++,expression);
    if (!tagName.empty()) count.text(countIndex,tagName);
    if (!count.step()) throw std::runtime_error("Cannot count library records");
    result.total = count.id();
    const std::string selectSql = "SELECT id,canonical_url,title,thumbnail_url,description FROM videos" + where +
        " ORDER BY " + order + " LIMIT ? OFFSET ?";
    Statement select(db, selectSql.c_str());
    int index = 1;
    if (!expression.empty()) select.text(index++,expression);
    if (!tagName.empty()) select.text(index++,tagName);
    select.integer(index++,limit); select.integer(index,offset);
    while (select.step()) result.items.push_back({select.id(),select.textColumn(1),select.textColumn(2),select.textColumn(3),select.textColumn(4),videoTags(select.id())});
    return result;
}
Folder LibraryRepository::createFolder(const std::string& name, std::int64_t parentId) {
    Statement insert(db,"INSERT INTO folders(name,parent_id) VALUES(?,NULLIF(?,0))");
    insert.text(1,name); insert.integer(2,parentId); insert.step();
    return {sqlite3_last_insert_rowid(db.handle()),name,parentId};
}
Tag LibraryRepository::createTag(const std::string& name) {
    if (name.find('\0') != std::string::npos || !g_utf8_validate(name.c_str(), -1, nullptr) ||
        name.find_first_of("\r\n") != std::string::npos)
        throw std::invalid_argument("Tag names must be valid text on a single line");
    const auto normalized = trimTag(name);
    if (normalized.empty()) throw std::invalid_argument("Tag names cannot be empty");
    Statement insert(db,"INSERT INTO tags(name) VALUES(?) ON CONFLICT(name) DO NOTHING");
    insert.text(1,normalized); insert.step();
    Statement select(db,"SELECT id FROM tags WHERE name=?"); select.text(1,normalized); select.step();
    return {select.id(),normalized};
}
void LibraryRepository::addToFolder(std::int64_t videoId,std::int64_t folderId) {
    Statement insert(db,"INSERT INTO video_folders VALUES(?,?) ON CONFLICT DO NOTHING");
    insert.integer(1,videoId); insert.integer(2,folderId); insert.step();
}
void LibraryRepository::tagVideo(std::int64_t videoId,std::int64_t tagId) {
    Statement insert(db,"INSERT INTO video_tags VALUES(?,?) ON CONFLICT DO NOTHING");
    insert.integer(1,videoId); insert.integer(2,tagId); insert.step();
}
}
