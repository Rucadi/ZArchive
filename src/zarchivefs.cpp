#include <iostream>
#include <cstring>
#include <unordered_map>
#include <boost/unordered_map.hpp>
#include <fuse.h>
#include <memory>
#include "zarchivereader.h"

#define FUSE_USE_VERSION 26
#define _FILE_OFFSET_BITS 64

using namespace std;

ZArchiveReader* reader;

struct dir {
    dir() = default;
    dir(const dir&) = delete;
    dir& operator=(dir&&) = default;
    boost::unordered_map<string, dir> dirs; // STL sucks
    std::unordered_map<string, ZArchiveNodeHandle> files;
    dir& create_dir(const std::string& name) {
        return dirs[name] = dir();
    }
    ZArchiveNodeHandle create_file(const std::string& name, const ZArchiveNodeHandle nodeIdx) {
        return files[name] = nodeIdx;
    }

    ZArchiveNodeHandle handle;
};

dir nodir;

bool operator!(const dir& d) {
    return &d == &nodir;
}
/*
bool operator!(const file& d) {
    return &d == &nofile;
}*/
dir all_dirs;

tuple<dir&, ZArchiveNodeHandle> find_dir(const char* path) {
    unique_ptr<char> p(strdup(path));
    char* ptr;
    char* pc = strtok_r(p.get(), "/", &ptr);
    auto cur = &all_dirs;
    while (pc != NULL) {
        auto found = cur->dirs.find(pc);
        if (found == cur->dirs.end()) {
            auto found_file = cur->files.find(pc);
            if (found_file == cur->files.end()) {
                return {nodir, -1};
            } else {
                return {*cur, found_file->second};
            }
        }
        cur = &found->second;
        pc = strtok_r(NULL, "/", &ptr);
    }
    return {*cur, -1};
}

int readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t off, struct fuse_file_info *fi) {
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    auto cur = find_dir(path);
    auto& cur_dir = get<0>(cur);
    if (!cur_dir) {
        return -ENOENT;
    }
    for (auto& f : cur_dir.files) {
        filler(buf, f.first.c_str(), NULL, 0);
    }
    for (auto& f : cur_dir.dirs) {
        filler(buf, f.first.c_str(), NULL, 0);
    }
    return 0;
}

int getattr(const char *path, struct stat *s) {
    string p(path);
    if (p == "." || p == ".." || p == "/") {
        s->st_mode = 0755 | S_IFDIR;
        return 0;
    }
    auto cur = find_dir(path);
    auto& cur_dir = get<0>(cur);
    auto& cur_file = get<1>(cur);
    if (!cur_dir) {
        return -ENOENT;
    }
    if (!cur_file) {
        s->st_mode = 0755 | S_IFDIR;
        return 0;
    }
    s->st_mode = 0644 | S_IFREG;
    s->st_size = reader->GetFileSize(cur_file);
    return 0;
}

int fileread(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    auto cur = find_dir(path);
    auto& cur_dir = get<0>(cur);
    auto& cur_file = get<1>(cur);
    if (!cur_dir || !cur_file) {
        return -ENOENT;
    }
    auto s = min(reader->GetFileSize(cur_file) - offset, size);
    reader->ReadFromFile(cur_file, offset, s, buf);
    return s;
}


void populate(ZArchiveReader* reader, dir& current_dir, ZArchiveNodeHandle current_dir_node)
{
        for(int i = 0; i < reader->GetDirEntryCount(current_dir_node); ++i)
        {
            ZArchiveReader::DirEntry e; 
            reader->GetDirEntry(current_dir_node, i, e);
            if(e.isDirectory)
            {
                auto& new_dir = current_dir.create_dir(std::string(e.name));
                populate(reader, new_dir, reader->GetNodeEntryIndex(current_dir_node, i));
            }
            else {
                current_dir.create_file(std::string(e.name), reader->GetNodeEntryIndex(current_dir_node, i));//name + node index
            }
        }
}


int main(int argc, char* argv[]) {

    
    std::vector<char*> args;
    for(int i = 0; i < argc; ++i)
    {
        args.push_back(argv[i]);
    }

    if(argc < 2)
    {
        std::cout << "Usage: zarchivefs <archive> <mountpoint>\n";
        return 1;
    }


    reader = ZArchiveReader::OpenFromFile(args[1]);
    std::swap(args[1], args[2]);
    args.pop_back();
    
    if (!reader)
            return -1;

    populate(reader, all_dirs, reader->LookUp("/"));

    fuse_operations ops;
    memset(&ops, 0, sizeof(ops));
    ops.getattr = getattr;
    ops.readdir = readdir;
    ops.read = fileread;
    fuse_main(args.size(), &args[0], &ops, NULL);
}