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
#ifndef SOAKFS_API_HPP
#define SOAKFS_API_HPP

#include <algorithm>
#include <cassert>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

#include <boost/network.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <libjson.h>
#include <JSONNode.h>

#include <basen.hpp>

using namespace boost::network;
using namespace boost::network::http;

namespace soak {

const int UNINITIALIZED = -1;

struct File {

    std::string name;
    std::string url_component;
    unsigned long size;
    unsigned long ctime;
    unsigned long mtime;
};

class AuthException : public std::exception {

};


class DownloadPolicyCppLib {

public:

    DownloadPolicyCppLib(const std::string &username, const std::string &password)
    : m_user_creds(http_auth_creds(username, password)) {

    }

protected:

    void load(const std::string &url, std::ostream &out, std::pair<long, long> range) {
        client::request req { url };
        req << header("Authorization", m_user_creds);
        if (range.first != UNINITIALIZED || range.second != UNINITIALIZED) {
            std::ostringstream r;
            r <<  "bytes=";
            if (range.first != UNINITIALIZED) {
                r << range.first;
            }
            r << "-";
            if (range.second != UNINITIALIZED) {
                r << range.second;
            }
            req << header("Range", r.str());
        }
        client::response resp { m_client.get(req) };
        if (resp.status() == 401) {
            throw AuthException();
        }
        out << body(resp);
    }

    void load(const std::string &url, std::ostream &out) {
        load(url, out, std::make_pair(UNINITIALIZED, UNINITIALIZED));
    }

private:

    std::string http_auth_creds(const std::string &id, const std::string &pwd) const noexcept {
        std::ostringstream auth_builder { "Basic ", std::ios::ate };
        std::string b64;
        std::string to_encode { id + ":" + pwd };
        bn::encode_b64(to_encode.begin(), to_encode.end(), std::back_inserter(b64));
        auth_builder << b64;
        switch(b64.size() % 4) {
            case 2: auth_builder << "==";
            case 3: auth_builder << "=";
        }
        return auth_builder.str();
    }

    std::string m_user_creds;
    http::client m_client;
};

template<typename DownloadPolicy>
class Storage : public DownloadPolicy {

public:

    typedef std::vector<std::string> Dirnames;
    typedef std::vector<File> Files;
    typedef std::pair<std::string, std::string> NameUrlTuple;
    typedef std::map<std::string, std::vector<NameUrlTuple>> root_paths_t;

    Storage(const std::string &id, const std::string &pwd)
    : DownloadPolicy(id, pwd) {
        init_storage_root(id);
        init_root_paths();
    }

    void download(const std::string &path, std::ostream &out) {
        std::string sanitized { sanitize_file_path(path) };
        load(build_url_for_path(sanitized), out);
    }

    void download(const std::string &path, std::ostream &out, std::pair<long, long> range) {
        std::string sanitized { sanitize_file_path(path) };
        load(build_url_for_path(sanitized), out, range);
    }

    std::pair<Dirnames, Files> ls(const std::string &path) {
        std::string sanitized { sanitize_dir_path(path) };
        std::string data { load(build_url_for_path(sanitized)) };
        JSONNode n { libjson::parse(data) };
        Dirnames dirs;
        Files files;
        for (auto items : n) {    
            if (items.name() == "files") {
                for (auto i : items) {
                    assert(i.type() == JSON_NODE);
                    File f;
                    f.name = i["name"].as_string();
                    f.size = i["size"].as_int();
                    f.ctime = i["ctime"].as_int();
                    f.mtime = i["mtime"].as_int();
                    files.push_back(f);
                }
            } else if (items.name() == "dirs" || items.name() == "devices") {
                for (auto i : items) {
                    assert(i.type() == JSON_ARRAY);
                    dirs.push_back(i.at(0).as_string());
                }
            }
        }
        return std::make_pair(std::move(dirs), std::move(files));
    }

private:

    void init_storage_root(const std::string &id) noexcept {
        std::ostringstream url_builder { "https://spideroak.com/storage/", std::ios::ate };
        bn::encode_b32(id.begin(), id.end(), std::ostream_iterator<char>(url_builder, ""));
        url_builder << "/";
        m_storage_root = url_builder.str();
    }

    void init_root_paths() {
        std::string data { load(storage_root()) };
        JSONNode n { libjson::parse(data) };
        for (auto metadata : n) {    
            if (metadata.name() != "devices") {
                continue;
            }
            assert(metadata.type() == JSON_ARRAY);
            for (auto dev : metadata) {
                assert(dev.type() == JSON_ARRAY);
                if (dev.type() == JSON_ARRAY) {
                    assert(dev.size() == 2);
                    const std::string &device_name = dev.at(0).as_string() + '/';
                    std::lock_guard<std::mutex> lock(m_root_paths_mutex);
                    assert(m_root_paths.find(device_name) == m_root_paths.end());
                    m_root_paths[device_name];
                }
            }
        }
    }

    void init_device(const std::string &dev) {
        std::string device { dev };
        if (!device.empty() && *device.rbegin() != '/') {
            device += '/';
        }
        std::string url { storage_root() + pseudo_url_encode(device) };
        std::string data { load(url) };
        JSONNode n { libjson::parse(data) };
        for (auto metadata : n) {    
            if (metadata.name() != "dirs") {
                continue;
            }
            assert(metadata.type() == JSON_ARRAY);
            for (auto dev : metadata) {
                assert(dev.type() == JSON_ARRAY);
                if (dev.type() == JSON_ARRAY) {
                    assert(dev.size() == 2);
                    std::lock_guard<std::mutex> lock(m_root_paths_mutex);
                    m_root_paths[device].push_back(
                        std::make_pair(
                            dev.at(0).as_string(),
                            dev.at(1).as_string()));
                }
            }
        }
    }

    std::string build_url_for_path(const std::string &path) {
        if (path.empty()) {
            return storage_root();
        }
        root_paths_t::const_iterator device_iter { find_device_from_path(path) };
        const std::string &device_name { device_iter->first };
        const std::vector<NameUrlTuple> &device_dirs { device_iter->second };
        if (device_dirs.empty()) {
            init_device(device_name);
        }
        const std::string encoded_device { pseudo_url_encode(device_name) };
        if (path == device_name) {
            // full url already found so we bail out
            return storage_root() + encoded_device;
        } else {
            // let's fetch the actual intra-device path
            std::string subdir_path;
            // first let's find the root directory on the current device. We
            // have it cached in NameUrlTuple, where we keep the mapping from
            // name to url component.
            auto dir_iter = std::find_if(device_dirs.begin(), device_dirs.end(),
                [&path, &device_name] (NameUrlTuple dir) -> bool {
                    return path.find(dir.first, device_name.length()) == device_name.length();
                });
            if (dir_iter == device_dirs.end() && path == device_name) {
                throw std::invalid_argument("malformed path");
            }
            const std::string &base_dir_name { dir_iter->first };
            const std::string &base_dir_url { dir_iter->second };
            // root directory url component was taken from api in already
            // encoded form. The rest of the path we need to encode on our own.
            subdir_path = path.substr(device_name.length() + base_dir_name.length() + 1);
            return storage_root() + encoded_device + base_dir_url + pseudo_url_encode(subdir_path);
        }
    }

    root_paths_t::const_iterator find_device_from_path(const std::string &path) const {
        std::lock_guard<std::mutex> lock(m_root_paths_mutex);
        root_paths_t::const_iterator dev_iter = std::find_if(m_root_paths.begin(), m_root_paths.end(),
            [&path] (const root_paths_t::value_type &device) -> bool {
                return std::equal(device.first.begin(), device.first.end(), path.begin());
            });
        if (dev_iter == m_root_paths.end()) {
            throw std::invalid_argument("incorrect device");
        }
        return dev_iter;
    }

    std::string pseudo_url_encode(const std::string &url) const noexcept {
        std::ostringstream buf;
        for (char c : url) {
            if (isalnum(c) 
                    || c == '/'
                    || c == '.'
                    || c == '_'
                    || c == '-') {
                buf << c;
            } else {
                buf << '%' << std::setw(2) << std::setfill('0') << std::hex << std::uppercase << int(c);
            }
        };
        return buf.str();
    }

    void load(const std::string &url, std::ostream &out, std::pair<long, long> range = std::pair<long, long>(UNINITIALIZED, UNINITIALIZED)) {
        return DownloadPolicy::load(url, out, range);
    } 

    std::string load(const std::string &url, std::pair<long, long> range = std::pair<long, long>(UNINITIALIZED, UNINITIALIZED)) {
        std::ostringstream obuf;
        load(url, obuf, range);
        return obuf.str();
    } 

    std::string sanitize_file_path(const std::string &path) const {
        auto s = boost::regex_replace(path, boost::regex("/+"), "/");
        return boost::regex_replace(s, boost::regex("^/"), "");
    }

    std::string sanitize_dir_path(const std::string &path) const {
        std::string sanitized = boost::regex_replace(path, boost::regex("/+"), "/");
        if (sanitized.length() > 0 && *sanitized.rbegin() != '/') {
            sanitized.append(1, '/');
        }
        return sanitized;
    }

    const std::string& storage_root() const noexcept {
        return m_storage_root;
    }

    std::string m_storage_root;
    root_paths_t m_root_paths;
    // m_root_paths can be accessed and modified concurrently if fuse is run in
    // multithreaded mode
    mutable std::mutex m_root_paths_mutex;
};

}

#endif // SOAKFS_API_HPP

