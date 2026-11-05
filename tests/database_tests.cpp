#include "database/LibraryRepository.hpp"
#include <glib.h>
#include <filesystem>
#include <stdexcept>
#include <iostream>
namespace {
void check(bool condition) { if (!condition) throw std::runtime_error("Database test failed"); }
int scalar(streamgallery::Database& db,const char* sql) {
    sqlite3_stmt* statement=nullptr;
    check(sqlite3_prepare_v2(db.handle(),sql,-1,&statement,nullptr)==SQLITE_OK);
    const int code=sqlite3_step(statement);
    const int result=code==SQLITE_ROW ? sqlite3_column_int(statement,0) : -1;
    sqlite3_finalize(statement); check(code==SQLITE_ROW); return result;
}
}
int main() {
    GError* error=nullptr;
    gchar* directory=g_dir_make_tmp("stream-gallery-test-XXXXXX",&error);
    if (!directory) { std::cerr << error->message; g_error_free(error); return 1; }
    const std::filesystem::path path=std::filesystem::path(directory)/"library.db";
    g_free(directory);
    int status=0;
    try {
        {
            streamgallery::Database db(path.string());
            streamgallery::LibraryRepository library(db);
            auto saved=library.bookmarkWithStatus("HTTPS://Example.COM:443/watch?v=1#position","A concert", "https://example.com/poster.jpg");
            check(saved.created);
            auto video=saved.video;
            auto duplicate=library.bookmarkWithStatus("https://example.com/watch?v=1", "Changed title", "https://example.com/changed.jpg");
            check(!duplicate.created && duplicate.video.id == video.id);
            check(duplicate.video.title == "A concert" && duplicate.video.thumbnailUrl == "https://example.com/poster.jpg");
            check(library.bookmark("https://example.com/watch?v=1","Duplicate").id==video.id);
            check(library.videos().size()==1);
            check(library.bookmark("https://example.com/watch?v=2","Other").id!=video.id);
            const auto paged = library.page("concert",streamgallery::VideoSort::Newest,0,1);
            check(paged.total == 1 && paged.items.size() == 1 && paged.items[0].id == video.id);
            check(library.page("concert OR other",streamgallery::VideoSort::Title,0).total == 0);
            check(library.page("\"",streamgallery::VideoSort::Title,0).total == 0);
            check(library.page("",streamgallery::VideoSort::Oldest,1,1).items[0].title == "Other");
            check(library.page("",streamgallery::VideoSort::Title,0,1).items[0].title == "A concert");
            check(library.page("absent",streamgallery::VideoSort::Newest,0).items.empty());
            bool badPage = false;
            try { library.page("",streamgallery::VideoSort::Newest,-1); }
            catch (const std::invalid_argument&) { badPage = true; }
            check(badPage);
            auto folder=library.createFolder("Music");
            auto nested=library.createFolder("Live",folder.id);
            auto tag=library.createTag("concert");
            check(library.createTag("concert").id==tag.id);
            library.addToFolder(video.id,nested.id); library.addToFolder(video.id,nested.id);
            library.tagVideo(video.id,tag.id); library.tagVideo(video.id,tag.id);
            check(scalar(db,"SELECT count(*) FROM video_tags")==1);
            bool rejected=false;
            try { library.addToFolder(9999,folder.id); } catch(const std::exception&) { rejected=true; }
            check(rejected);
            check(scalar(db,"SELECT count(*) FROM videos_fts WHERE videos_fts MATCH 'concert'")==1);
            db.execute("UPDATE videos SET title='Changed' WHERE id=1");
            check(scalar(db,"SELECT count(*) FROM videos_fts WHERE videos_fts MATCH 'concert'")==0);
            db.execute("DELETE FROM folders WHERE parent_id IS NULL");
            check(scalar(db,"SELECT count(*) FROM video_folders")==0);
            db.execute("DELETE FROM videos WHERE id=1");
            check(scalar(db,"SELECT count(*) FROM video_tags")==0);
            bool invalid=false;
            try { library.bookmark("file:///etc/passwd","Invalid"); } catch(const std::invalid_argument&) { invalid=true; }
            check(invalid);
            check(streamgallery::canonicalizeUrl("https://example.com")=="https://example.com/");
        }
        {
            streamgallery::Database db(path.string());
            streamgallery::LibraryRepository library(db);
            check(library.videos().size()==1);
            check(library.videos()[0].title=="Other");
            check(scalar(db,"PRAGMA user_version")==2);
        }
        const auto savedPath = path.parent_path() / "saved.db";
        {
            streamgallery::Database db(savedPath.string());
            streamgallery::LibraryRepository library(db);
            check(library.bookmarkWithStatus("https://example.com/watch#player", "Saved video", "https://example.com/poster.jpg").created);
        }
        {
            streamgallery::Database reopened(savedPath.string());
            streamgallery::LibraryRepository library(reopened);
            auto duplicate = library.bookmarkWithStatus("https://example.com/watch", "Replacement", "");
            check(!duplicate.created && library.videos().size() == 1);
            check(duplicate.video.canonicalUrl == "https://example.com/watch");
            check(duplicate.video.title == "Saved video" && duplicate.video.thumbnailUrl == "https://example.com/poster.jpg");
            check(library.page("Saved", streamgallery::VideoSort::Newest, 0).total == 1);
            library.updateMetadata(duplicate.video.id, "Edited title", "Unique description phrase");
            check(library.page("Saved", streamgallery::VideoSort::Newest, 0).total == 0);
            auto edited = library.page("description", streamgallery::VideoSort::Title, 0);
            check(edited.total == 1 && edited.items[0].title == "Edited title");
            check(edited.items[0].description == "Unique description phrase");
            check(edited.items[0].canonicalUrl == duplicate.video.canonicalUrl);
            check(edited.items[0].thumbnailUrl == duplicate.video.thumbnailUrl);
            bool missing = false;
            try { library.updateMetadata(9999, "Missing", ""); }
            catch (const std::exception&) { missing = true; }
            check(missing);
        }
        {
            streamgallery::Database reopened(savedPath.string());
            streamgallery::LibraryRepository library(reopened);
            auto video = library.videos().at(0);
            check(video.title == "Edited title" && video.description == "Unique description phrase");
            auto duplicate = library.bookmarkWithStatus(video.canonicalUrl, "Replacement");
            check(!duplicate.created && duplicate.video.description == video.description);
        }
        {
            streamgallery::Database large(":memory:");
            large.execute("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<10000) "
                "INSERT INTO videos(canonical_url,title) SELECT 'https://example.com/' || x, 'Video ' || x FROM n");
            streamgallery::LibraryRepository library(large);
            auto page = library.page("",streamgallery::VideoSort::Newest,0);
            check(page.total == 10000 && page.items.size() == 24 && page.items[0].id == 10000);
            auto lastPage = library.page("",streamgallery::VideoSort::Newest,9984);
            check(lastPage.total == 10000 && lastPage.items.size() == 16);
            check(library.page("Video",streamgallery::VideoSort::Oldest,0).total == 10000);
        }
        // Simulate a persisted version-1 database from the previous release.
        const auto legacyPath = path.parent_path() / "legacy.db";
        sqlite3* legacy = nullptr;
        check(sqlite3_open(legacyPath.c_str(), &legacy) == SQLITE_OK);
        const char* legacySql = "CREATE TABLE videos(id INTEGER PRIMARY KEY, canonical_url TEXT UNIQUE, title TEXT, description TEXT);"
            "INSERT INTO videos VALUES(1,'https://example.com/legacy','Legacy',''); "
            "CREATE TABLE folders(id INTEGER PRIMARY KEY, name TEXT, parent_id INTEGER REFERENCES folders(id));"
            "CREATE TABLE tags(id INTEGER PRIMARY KEY, name TEXT UNIQUE);"
            "CREATE TABLE video_folders(video_id INTEGER REFERENCES videos(id), folder_id INTEGER REFERENCES folders(id));"
            "CREATE TABLE video_tags(video_id INTEGER REFERENCES videos(id), tag_id INTEGER REFERENCES tags(id));"
            "INSERT INTO folders VALUES(1,'Music',NULL); INSERT INTO tags VALUES(1,'Live');"
            "INSERT INTO video_folders VALUES(1,1); INSERT INTO video_tags VALUES(1,1); PRAGMA user_version=1;";
        const int migrationSetup = sqlite3_exec(legacy,legacySql,nullptr,nullptr,nullptr);
        sqlite3_close(legacy);
        check(migrationSetup == SQLITE_OK);
        {
            streamgallery::Database migrated(legacyPath.string());
            streamgallery::LibraryRepository library(migrated);
            check(library.videos()[0].title == "Legacy");
            check(library.videos()[0].thumbnailUrl.empty());
            check(scalar(migrated,"PRAGMA user_version") == 2);
            check(scalar(migrated,"SELECT count(*) FROM video_folders WHERE video_id=1 AND folder_id=1") == 1);
            check(scalar(migrated,"SELECT count(*) FROM video_tags WHERE video_id=1 AND tag_id=1") == 1);
        }
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; status=1; }
    std::filesystem::remove_all(path.parent_path());
    return status;
}
