#pragma once
#include <atomic>
#include <set>
#include <shlobj.h>
#include <thread>

struct VRFilePicker
{
    struct Entry
    {
        fs::path path;
        bool directory;
        fs::file_time_type modified;
    };
    struct Scan
    {
        std::atomic<bool> done{false}, cancel{false};
        std::vector<Entry> entries;
        std::wstring error;
    };
    bool open = false, multiple = false, busy = false, loading = false;
    size_t first = 0;
    fs::path folder;
    std::wstring message;
    std::vector<Entry> entries;
    std::set<fs::path> selected;
    std::shared_ptr<Scan> scan;
    static fs::path known(REFKNOWNFOLDERID id)
    {
        PWSTR value = nullptr;
        fs::path result;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &value)))
            result = value;
        CoTaskMemFree(value);
        return result;
    }
    static fs::path journals()
    {
        return known(FOLDERID_SavedGames) / L"Frontier Developments" / L"Elite Dangerous";
    }
    void close()
    {
        if (scan)
            scan->cancel = true;
        scan.reset();
        open = busy = loading = false;
        selected.clear();
        entries.clear();
    }
    ~VRFilePicker()
    {
        close();
    }
    void load(const fs::path &path)
    {
        if (scan)
            scan->cancel = true;
        folder = path;
        entries.clear();
        selected.clear();
        first = 0;
        message = L"Loading folder...";
        loading = true;
        auto job = scan = std::make_shared<Scan>();
        std::thread([job, path] {
            try
            {
                std::error_code error;
                fs::directory_iterator it(path, error), end;
                while (!error && it != end && !job->cancel)
                {
                    std::error_code metadataError;
                    bool directory = it->is_directory(metadataError);
                    if (!metadataError && (directory || it->is_regular_file(metadataError)))
                    {
                        auto modified = it->last_write_time(metadataError);
                        job->entries.push_back(
                            {it->path(), directory, metadataError ? fs::file_time_type::min() : modified});
                    }
                    if (job->entries.size() >= 10000)
                    {
                        job->error = L"Showing the first 10,000 entries.";
                        break;
                    }
                    it.increment(error);
                }
                if (error)
                    job->error = L"Cannot read this folder. Choose another folder or Cancel.";
                std::sort(job->entries.begin(), job->entries.end(), [](const Entry &a, const Entry &b) {
                    if (a.directory != b.directory)
                        return a.directory;
                    if (!a.directory && a.modified != b.modified)
                        return a.modified > b.modified;
                    return _wcsicmp(a.path.filename().c_str(), b.path.filename().c_str()) < 0;
                });
            }
            catch (...)
            {
                job->error = L"Cannot read this folder.";
            }
            job->done.store(true, std::memory_order_release);
        }).detach();
    }
    void begin(bool multi)
    {
        close();
        open = true;
        multiple = multi;
        load(journals());
    }
    bool poll()
    {
        if (!scan || !scan->done.load(std::memory_order_acquire))
            return false;
        entries = std::move(scan->entries);
        message = scan->error;
        scan.reset();
        loading = false;
        return true;
    }
    void pick(size_t index)
    {
        if (busy || loading || index >= entries.size())
            return;
        auto entry = entries[index];
        if (entry.directory)
        {
            load(entry.path);
            return;
        }
        if (selected.erase(entry.path))
            return;
        if (!multiple)
            selected.clear();
        selected.insert(entry.path);
    }
    void scroll(int delta)
    {
        first = static_cast<size_t>(std::clamp(static_cast<long long>(first) + delta, 0LL,
                                               std::max(0LL, static_cast<long long>(entries.size()) - 8)));
    }
};
