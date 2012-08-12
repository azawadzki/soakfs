/*******************************************************************************
 * Copyright (c) 2012, Andrzej Zawadzki (azawadzki@gmail.com)
 *  
 * soakfs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *  
 * soakfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with soakfs; if not, see <http ://www.gnu.org/licenses/>.
 ******************************************************************************/
#include "soakfs_api.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fuse.h>

#include <boost/filesystem.hpp>

class SoakFS {

    typedef soak::Storage<soak::DownloadPolicyCppLib> Storage;
    typedef std::pair<Storage::Dirnames, Storage::Files> LsData;

public:

    SoakFS(const std::string &user, const std::string &password)
    : m_storage { new Storage { user, password } } {

    }
    
    int getattr(const std::string &path_string, struct stat *stbuf) {
        memset(stbuf, 0, sizeof(struct stat));
        // we don't need to load any data for root dir, so let's treat this
        // special case separately
        if (path_string == "/") { stbuf->st_mode = S_IFDIR | 0755;
            // finding proper link count is not needed in our case and it
            // imposes a performance tax due to high network loading times. We
            // fake the count with 1 and hope that fuse will not care too much.
            stbuf->st_nlink = 1;
            return 0;
        }
        boost::filesystem::path path { path_string };
        std::string parent { path.parent_path().string() };
        std::string filename = { path.filename().string() };
        // we must load parent's resources, as the api doesn't permit for
        // fetching the file/dir info directly.
        const LsData &data { get_dir_data(parent) };
        const Storage::Dirnames &dirs = data.first;

        auto filename_end = filename.end();
        if (!filename.empty() && *filename.rbegin() == '/') {
            --filename_end;
        }
        auto dir_iter = std::find_if(dirs.begin(), dirs.end(),
            [&filename, &filename_end] (const std::string &curr_name) -> bool {
                return std::equal(filename.begin(), filename_end, curr_name.begin());
            });
        if (dir_iter != dirs.end()) {
            stbuf->st_mode = S_IFDIR | 0755;
            stbuf->st_nlink = 1;
            return 0;
        }

        // check if it's a file
        const Storage::Files &files = data.second;
        auto file_iter = std::find_if(files.begin(), files.end(),
            [&filename] (soak::File f) -> bool {
                return filename == f.name;
            });
        if (file_iter != files.end()) {
            stbuf->st_mode = S_IFREG | 0444;
            stbuf->st_nlink = 1;
            stbuf->st_size = file_iter->size;
            stbuf->st_ctime = file_iter->ctime;
            stbuf->st_mtime = file_iter->mtime;
            return 0;
        }
        return -ENOENT;
    }

    int readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t, fuse_file_info*) {
        filler(buf, ".", NULL, 0);
        filler(buf, "..", NULL, 0);
        const LsData &data { get_dir_data(path) };
        for (std::string dir : data.first) {
            // convert to path relative to mounted fs root
            dir = boost::regex_replace(dir, boost::regex("/$"), "");
            filler(buf, dir.c_str(), NULL, 0);
        }
        for (auto file : data.second) {
            filler(buf, file.name.c_str(), NULL, 0);
        }
        return 0;
    }

    int open(const char *path, struct fuse_file_info *fi) {
        if ((fi->flags & 3) != O_RDONLY) {
            return -EACCES;
        }
        return 0;
    }

    int read(const char *path, char *buf, size_t requested_size, off_t offset, struct fuse_file_info *) {
        try {
            const soak::File &f = get_file_data(std::string(path));
            const long read_size = std::min(requested_size, f.size - offset); // last block?
            std::stringstream out;
            out.rdbuf()->pubsetbuf(buf, requested_size);
            m_storage->download(path, out, std::make_pair(offset, offset + read_size - 1));
            return read_size;
        } catch (const std::exception &e) {
            return -1;
        }
    }

private:

    const soak::File& get_file_data(const std::string &file) {
        boost::filesystem::path path { file };
        // the api permits for accessing the file data only by querying for parent dir info
        std::string parent { path.parent_path().string() };
        const LsData& data { get_dir_data(parent) };
        const Storage::Files &files = data.second;

        std::string filename = { path.filename().string() };
        auto file_iter = std::find_if(files.begin(), files.end(), [&filename] (soak::File f) -> bool {
            return filename == f.name;
        });
        if (file_iter == files.end()) {
            // file path was invalid
            throw std::invalid_argument(file + " not found");
        }
        return *file_iter;
    }

    const LsData& get_dir_data(const std::string &dir) {
        std::string d { dir };
        if (!d.empty() && d[0] == '/') {
            // convert to path relative to mounted fs root
            d = d.substr(1);
        }
        // m_data can be accessed and modified concurrently if fuse is run in
        // multithreaded mode
        std::lock_guard<std::mutex> lock(m_data_mutex);
        if (m_data.find(d) == m_data.end()) {
            // not accessed previously, so we need to load it
            m_data[d] = m_storage->ls(d);
        }
        return m_data[d];
    }

    std::unique_ptr<Storage> m_storage;

    std::map<std::string, LsData> m_data;
    std::mutex m_data_mutex;

};

static SoakFS* soakfs_get_fs() {
    assert(fuse_get_context());
    return reinterpret_cast<SoakFS*>(fuse_get_context()->private_data);
}

static int soakfs_getattr(const char *path, struct stat *stbuf) {
    assert(soakfs_get_fs() != nullptr);
    try {
        return soakfs_get_fs()->getattr(path, stbuf);
    } catch (...) {
        return -1;
    }
}

static int soakfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, fuse_file_info *fi) {
    assert(soakfs_get_fs() != nullptr);
    try {
        return soakfs_get_fs()->readdir(path, buf, filler, offset, fi);
    } catch (...) {
        return -1;
    }
}

static int soakfs_open(const char *path, struct fuse_file_info *fi) {
    assert(soakfs_get_fs() != nullptr);
    try {
        return soakfs_get_fs()->open(path, fi);
    } catch (...) {
        return -1;
    }
}

static int soakfs_read(const char *path, char *buf, size_t size, off_t offset, fuse_file_info *fi) {
    assert(soakfs_get_fs() != nullptr);
    try {
        return soakfs_get_fs()->read(path, buf, size, offset, fi);
    } catch (...) {
        return -1;
    }
}

static void soakfs_destroy(void*) {
    assert(soakfs_get_fs() != nullptr);
    delete soakfs_get_fs();
}

typedef std::pair<std::string, std::string> creds_pair;

static void* soakfs_init(fuse_conn_info*) {
    assert(fuse_get_context());
    std::unique_ptr<creds_pair> p { reinterpret_cast<creds_pair*>(fuse_get_context()->private_data) };
    try {
        return new SoakFS { p->first, p->second };
    } catch (const soak::AuthException &e) {
        exit(1);
    }
}

// HACK ALERT:
// When fuse is run in background mode/daemonized, crossing the threshold of
// fuse_main blocks all active threads. This requires that any new threads are
// created only after fuse_ops's init function is called. In our case, this is
// an unfortunate design decision, as cpp-netlib maintains an internal thread
// pool which is being used from the start when creating SoakFS objects. Due to
// all that, SoakFS objects are created twice:
// 1) in main, to check user credentials after application start;
// 2) in soakfs_init, to provice the actual fs implementation.
// Proper solution would be to either dump cpp-netlib or add explicit
// credentials checking functionality outside current SoakFS object.
int main(int argc, char *argv[]) {
    std::string username;
    std::cout << "Username: ";
    std::cin >> username;
    std::string password { getpass("Password: ") };
    try {
        std::unique_ptr<SoakFS> test_creds_fs { new SoakFS { username, password } };
    } catch (const soak::AuthException &e) {
        std::cout << "Unable to login" << std::endl;
        return EXIT_FAILURE;
    }
    fuse_operations soakfs_ops { nullptr };
    soakfs_ops.getattr    = soakfs_getattr;
    soakfs_ops.readdir    = soakfs_readdir;
    soakfs_ops.open       = soakfs_open;
    soakfs_ops.read       = soakfs_read;
    soakfs_ops.destroy    = soakfs_destroy;
    soakfs_ops.init       = soakfs_init;

    return fuse_main(argc, argv, &soakfs_ops, new creds_pair(username, password));
}

