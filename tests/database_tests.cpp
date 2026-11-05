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
            check(scalar(db,"PRAGMA user_version")==3);
        }
        {
            streamgallery::Database db((path.parent_path() / "suggestions.db").string());
            streamgallery::LibraryRepository library(db);
            library.createTag("Music"); library.createTag("music");
            library.createTag("Live, acoustic"); library.createTag("100%_literal");
            library.createTag("100xxliteral"); library.createTag("Quoted ' name");
            library.createTag(" संगीत ");
            auto root = library.createFolder("Music");
            library.createFolder("Music"); library.createFolder("Nested only",root.id);
            library.createFolder("music");
            check(library.assignmentNames(false,"Mus") == std::vector<std::string>{"Music"});
            check(library.assignmentNames(false," mus ") == std::vector<std::string>{"music"});
            check(library.assignmentNames(false,"100%_") == std::vector<std::string>{"100%_literal"});
            check(library.assignmentNames(false,"Quoted '") == std::vector<std::string>{"Quoted ' name"});
            check(library.assignmentNames(false,"सं") == std::vector<std::string>{"संगीत"});
            check(library.assignmentNames(true) == (std::vector<std::string>{"Music","music"}));
            check(library.assignmentNames(true,"Nested").empty());
            for (int i = 0; i < 125; ++i) library.createTag("Many " + std::to_string(i));
            check(library.assignmentNames(false,"Many").size() == 50);
            check(library.assignmentNames(false,"Many",100).size() == 100);
            bool invalid = false;
            try { library.assignmentNames(false,"",101); } catch (const std::invalid_argument&) { invalid = true; }
            check(invalid);
        }
        const auto historyPath = path.parent_path() / "history.db";
        {
            streamgallery::Database db(historyPath.string());
            streamgallery::LibraryRepository library(db);
            using streamgallery::VideoSort;
            auto first = library.bookmark("https://example.com/first", "First concert");
            auto second = library.bookmark("https://example.com/second", "Second concert");
            library.bookmark("https://example.com/unplayed", "Unplayed");
            check(library.page("",VideoSort::Recent,0).total == 0);
            check(!library.recordPlayback("https://example.com/unsaved",100));
            check(library.recordPlayback("HTTPS://EXAMPLE.COM:443/first#player",100));
            check(library.recordPlayback(second.canonicalUrl,200));
            check(library.recordPlayback(first.canonicalUrl,300));
            check(library.recordPlayback(first.canonicalUrl,50)); // Late worker cannot move history backward.
            auto recent = library.page("",VideoSort::Recent,0,1);
            check(recent.total == 2 && recent.items[0].id == first.id && recent.items[0].lastPlayedAt == 300);
            check(library.page("",VideoSort::Recent,1,1).items[0].id == second.id);
            library.updateBookmark(first.id,"Edited concert","Description",{"Live"},std::vector<std::string>{"Music"});
            check(library.page("Edited",VideoSort::Recent,0,24,"Live","Music").total == 1);
            check(library.page("Edited",VideoSort::Recent,0,24,"Missing","Music").total == 0);
            check(library.bookmark(first.canonicalUrl,"Duplicate").lastPlayedAt == 300);
            auto removed = library.removeBookmark(first.id);
            check(library.page("",VideoSort::Recent,0).total == 1);
            library.restoreBookmark(removed);
            check(library.page("",VideoSort::Recent,0).items[0].lastPlayedAt == 300);
            // Stable tie ordering and bounded pagination.
            library.recordPlayback(second.canonicalUrl,300);
            check(library.page("",VideoSort::Recent,0).items[0].id == second.id);
            bool invalid = false;
            try { library.recordPlayback(first.canonicalUrl,0); } catch (const std::invalid_argument&) { invalid = true; }
            check(invalid);
        }
        {
            streamgallery::Database db(historyPath.string());
            streamgallery::LibraryRepository library(db);
            check(library.page("",streamgallery::VideoSort::Recent,0).total == 2);
            check(library.page("",streamgallery::VideoSort::Recent,0).items[0].lastPlayedAt == 300);
        }
        // A version-2 library upgrades without inventing history or losing metadata.
        {
            streamgallery::Database db(historyPath.string());
            db.execute("DROP INDEX idx_videos_recent");
            db.execute("ALTER TABLE videos DROP COLUMN last_played_at");
            db.execute("PRAGMA user_version=2");
        }
        {
            streamgallery::Database db(historyPath.string());
            streamgallery::LibraryRepository library(db);
            check(scalar(db,"PRAGMA user_version") == 3);
            check(library.videos().size() == 3);
            check(library.page("",streamgallery::VideoSort::Recent,0).total == 0);
            check(library.page("Edited",streamgallery::VideoSort::Newest,0,24,"Live","Music").total == 1);
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
        const auto tagsPath = path.parent_path() / "tags.db";
        {
            streamgallery::Database db(tagsPath.string());
            streamgallery::LibraryRepository library(db);
            std::vector<std::int64_t> ids;
            for (int index = 0; index < 30; ++index) {
                auto video = library.bookmark("https://example.com/tagged/" + std::to_string(index),
                    "Tagged " + std::to_string(index));
                ids.push_back(video.id);
                library.updateBookmark(video.id, video.title, "Findable description", {" Music ", "Music", "Live, acoustic", "संगीत"});
            }
            auto untagged = library.bookmark("https://example.com/untagged", "No assignment");
            auto tags = library.videoTags(ids.front());
            check(tags.size() == 3);
            check(library.createTag(" Music ").id == library.createTag("Music").id);
            check(library.page("", streamgallery::VideoSort::Newest, 0, 24, "Music").total == 30);
            auto last = library.page("Findable", streamgallery::VideoSort::Oldest, 24, 24, " Music ");
            check(last.total == 30 && last.items.size() == 6 && last.items.front().id == ids[24]);
            check(library.page("", streamgallery::VideoSort::Newest, 0, 1, "Music").items[0].id == ids.back());
            check(library.page("", streamgallery::VideoSort::Title, 0, 1, "Music").items[0].title == "Tagged 0");
            check(library.page("missing", streamgallery::VideoSort::Newest, 0, 24, "Music").total == 0);
            check(library.page("", streamgallery::VideoSort::Newest, 0, 24, "music").total == 0);
            check(library.page("", streamgallery::VideoSort::Newest, 0, 24, "संगीत").total == 30);
            check(library.page("", streamgallery::VideoSort::Newest, 0, 24, "Live, acoustic").total == 30);
            check(library.page("", streamgallery::VideoSort::Newest, 0, 24, "' OR 1=1 --").total == 0);
            check(library.page("", streamgallery::VideoSort::Newest, 0).total == 31);
            library.updateBookmark(ids.front(), "Edited tags", "Updated description", {"Music", "Other"});
            check(library.videoTags(ids.front()).size() == 2);
            check(library.page("Edited", streamgallery::VideoSort::Title, 0, 24, "Live, acoustic").total == 0);
            check(library.page("Updated", streamgallery::VideoSort::Title, 0, 24, "Other").total == 1);
            bool rejected = false;
            try { library.updateBookmark(ids.front(), "Must roll back", "", {"Rolled back tag", "   "}); }
            catch (const std::invalid_argument&) { rejected = true; }
            check(rejected && scalar(db,"SELECT count(*) FROM tags WHERE name='Rolled back tag'") == 0);
            check(library.page("Updated", streamgallery::VideoSort::Newest, 0, 24, "Other").total == 1);
            check(library.videoTags(ids.front()).size() == 2);
            rejected = false;
            try { library.updateBookmark(99999, "Missing", "", {"Missing tag"}); }
            catch (const std::exception&) { rejected = true; }
            check(rejected && scalar(db,"SELECT count(*) FROM tags WHERE name='Missing tag'") == 0);
            auto removed = library.removeBookmark(ids.front());
            check(library.page("", streamgallery::VideoSort::Newest, 0, 24, "Other").total == 0);
            library.restoreBookmark(removed);
            check(library.page("Updated", streamgallery::VideoSort::Newest, 0, 24, "Other").total == 1);
            library.updateBookmark(untagged.id, "No assignment", "", {});
        }
        {
            streamgallery::Database db(tagsPath.string());
            streamgallery::LibraryRepository library(db);
            auto page = library.page("Updated", streamgallery::VideoSort::Newest, 0, 24, "Other");
            check(page.total == 1 && page.items[0].tags.size() == 2);
            auto duplicate = library.bookmarkWithStatus(page.items[0].canonicalUrl, "Replacement");
            check(!duplicate.created && duplicate.video.tags == page.items[0].tags);
            library.updateBookmark(page.items[0].id, "Edited tags", "Updated description", {});
            check(library.page("", streamgallery::VideoSort::Newest, 0, 24, "Other").total == 0);
            check(library.videoTags(page.items[0].id).empty());
            check(library.videos().size() == 31);
        }
        const auto foldersPath = path.parent_path() / "folders.db";
        {
            streamgallery::Database db(foldersPath.string());
            streamgallery::LibraryRepository library(db);
            std::vector<std::int64_t> ids;
            for (int index = 0; index < 30; ++index) {
                auto video = library.bookmark("https://example.com/folder/" + std::to_string(index), "Folder video " + std::to_string(index));
                ids.push_back(video.id);
                library.updateBookmark(video.id, video.title, "Folder description", {"Shared tag"},
                    std::vector<std::string>{" Collection ", "Collection", "संग्रह"});
            }
            check(scalar(db,"SELECT count(*) FROM folders") == 2);
            check(library.videoRootFolders(ids[0]).size() == 2);
            auto last = library.page("description", streamgallery::VideoSort::Oldest, 24, 24, "Shared tag", " Collection ");
            check(last.total == 30 && last.items.size() == 6 && last.items.front().id == ids[24]);
            check(library.page("", streamgallery::VideoSort::Newest, 0, 1, "", "Collection").items[0].id == ids.back());
            check(library.page("", streamgallery::VideoSort::Title, 0, 1, "", "Collection").items[0].title == "Folder video 0");
            check(library.page("", streamgallery::VideoSort::Newest, 0, 24, "Missing", "Collection").total == 0);
            check(library.page("", streamgallery::VideoSort::Newest, 0, 24, "", "collection").total == 0);
            check(library.page("", streamgallery::VideoSort::Newest, 0, 24, "", "' OR 1=1 --").total == 0);
            auto parent = library.createFolder("Parent");
            auto nested = library.createFolder("Collection", parent.id);
            auto duplicate = library.createFolder("Collection");
            library.addToFolder(ids[0], nested.id);
            library.addToFolder(ids[0], duplicate.id);
            library.updateBookmark(ids[0], "Edited folder", "Folder description", {"Shared tag"}, std::vector<std::string>{"Collection"});
            check(scalar(db,"SELECT count(*) FROM video_folders WHERE video_id=1") == 3);
            check(library.videoRootFolders(ids[0]).size() == 1);
            // Metadata/tag-only callers preserve all existing folder links.
            library.updateBookmark(ids[0], "Edited folder", "Folder description", {"Shared tag"});
            check(scalar(db,"SELECT count(*) FROM video_folders WHERE video_id=1") == 3);
            bool rejected = false;
            try { library.updateBookmark(ids[0], "Must roll back", "", {"New rollback tag"},
                std::vector<std::string>{"New rollback folder", "  "}); }
            catch (const std::invalid_argument&) { rejected = true; }
            check(rejected && scalar(db,"SELECT count(*) FROM folders WHERE name='New rollback folder'") == 0);
            check(scalar(db,"SELECT count(*) FROM tags WHERE name='New rollback tag'") == 0);
            check(library.page("Edited", streamgallery::VideoSort::Newest, 0, 24, "Shared tag", "Collection").total == 1);
            library.updateBookmark(ids[0], "Edited folder", "Folder description", {"Shared tag"}, std::vector<std::string>{});
            check(library.videoRootFolders(ids[0]).empty());
            check(scalar(db,"SELECT count(*) FROM video_folders WHERE video_id=1") == 1);
            check(library.page("Edited", streamgallery::VideoSort::Newest, 0, 24, "", "Collection").total == 0);
            library.updateBookmark(ids[0], "Edited folder", "Folder description", {"Shared tag"}, std::vector<std::string>{"Collection"});
            auto removed = library.removeBookmark(ids[0]);
            library.restoreBookmark(removed);
            check(library.page("Edited", streamgallery::VideoSort::Newest, 0, 24, "Shared tag", "Collection").total == 1);
        }
        {
            streamgallery::Database db(foldersPath.string());
            streamgallery::LibraryRepository library(db);
            auto page = library.page("Edited", streamgallery::VideoSort::Newest, 0, 24, "Shared tag", "Collection");
            check(page.total == 1 && page.items[0].rootFolders == std::vector<std::string>{"Collection"});
            auto duplicate = library.bookmarkWithStatus(page.items[0].canonicalUrl, "Replacement");
            check(!duplicate.created && duplicate.video.rootFolders == page.items[0].rootFolders);
            check(scalar(db,"SELECT count(*) FROM video_folders WHERE video_id=1") == 2);
        }
        const auto removalPath = path.parent_path() / "removal.db";
        streamgallery::RemovedBookmark snapshot;
        {
            streamgallery::Database db(removalPath.string());
            streamgallery::LibraryRepository library(db);
            auto video = library.bookmark("https://example.com/remove", "Original", "https://example.com/poster.jpg");
            library.updateMetadata(video.id, "Original", "Searchable description");
            auto folder = library.createFolder("Keep folder");
            auto tag = library.createTag("Keep tag");
            library.addToFolder(video.id, folder.id); library.tagVideo(video.id, tag.id);
            snapshot = library.removeBookmark(video.id);
            check(snapshot.video.description == "Searchable description");
            check(snapshot.folderIds.size() == 1 && snapshot.tagIds.size() == 1);
            check(library.videos().empty());
            check(library.page("Searchable", streamgallery::VideoSort::Newest, 0).total == 0);
            check(scalar(db,"SELECT count(*) FROM video_tags") == 0);
            bool missing = false;
            try { library.removeBookmark(video.id); } catch (const std::exception&) { missing = true; }
            check(missing);
            // An unrelated insert may reuse the deleted ID; it must survive Undo.
            auto unrelated = library.bookmark("https://example.com/unrelated", "Unrelated");
            check(unrelated.id == video.id);
            library.restoreBookmark(snapshot);
            auto restored = library.page("Searchable", streamgallery::VideoSort::Newest, 0);
            check(restored.total == 1 && restored.items[0].id != unrelated.id);
            check(restored.items[0].thumbnailUrl == snapshot.video.thumbnailUrl);
            check(restored.items[0].canonicalUrl == snapshot.video.canonicalUrl);
            check(scalar(db,"SELECT count(*) FROM video_tags") == 1);
            check(scalar(db,"SELECT count(*) FROM video_folders") == 1);
            snapshot = library.removeBookmark(restored.items[0].id);
            auto replacement = library.bookmark(snapshot.video.canonicalUrl, "New metadata");
            bool conflict = false;
            try { library.restoreBookmark(snapshot); } catch (const std::exception&) { conflict = true; }
            check(conflict);
            check(library.page("New", streamgallery::VideoSort::Newest, 0).total == 1);
            library.removeBookmark(replacement.id);
            // A missing relationship must roll back the entire restoration.
            auto invalid = snapshot;
            invalid.tagIds.push_back(99999);
            bool rolledBack = false;
            try { library.restoreBookmark(invalid); } catch (const std::exception&) { rolledBack = true; }
            check(rolledBack && library.page("Searchable", streamgallery::VideoSort::Newest, 0).total == 0);
            check(scalar(db,"SELECT count(*) FROM video_folders") == 0);
            library.restoreBookmark(snapshot);
        }
        {
            streamgallery::Database db(removalPath.string());
            streamgallery::LibraryRepository library(db);
            check(library.videos().size() == 2);
            auto restored = library.page("Searchable", streamgallery::VideoSort::Newest, 0);
            check(restored.total == 1 && restored.items[0].title == "Original");
            check(scalar(db,"SELECT count(*) FROM videos WHERE added_at IS NOT NULL") == 2);
            check(scalar(db,"SELECT count(*) FROM video_tags") == 1);
            check(scalar(db,"SELECT count(*) FROM video_folders") == 1);
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
            check(scalar(migrated,"PRAGMA user_version") == 3);
            check(scalar(migrated,"SELECT count(*) FROM video_folders WHERE video_id=1 AND folder_id=1") == 1);
            check(scalar(migrated,"SELECT count(*) FROM video_tags WHERE video_id=1 AND tag_id=1") == 1);
        }
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; status=1; }
    std::filesystem::remove_all(path.parent_path());
    return status;
}
