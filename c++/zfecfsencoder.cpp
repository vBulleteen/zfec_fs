#include "zfecfsencoder.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

#include "utils.h"
#include "decodedpath.h"
#include "encodedfile.h"

namespace ZFecFS {

ZFecFSEncoder::ZFecFSEncoder(unsigned int sharesRequired,
                             unsigned int numShares,
                             const std::string& source)
    : ZFecFS(sharesRequired, numShares, source)
{
}

int ZFecFSEncoder::Getattr(const char* path, struct stat* stbuf)
{
    try {
        DecodedPath decodedPath = DecodedPath::DecodePath(path, GetSource());
        if (decodedPath.indexGiven) {
            if (lstat(decodedPath.path.c_str(), stbuf) == -1)
                return -errno;
            if ((stbuf->st_mode & S_IFMT) == S_IFREG)
                stbuf->st_size = EncodedSize(stbuf->st_size);
        } else {
            memset(stbuf, 0, sizeof(struct stat));
            stbuf->st_mode = S_IFDIR | 0755;
            stbuf->st_nlink = numShares + 2;
        }
    } catch (const std::exception& exc) {
        return -ENOENT;
    }

    return 0;
}

int ZFecFSEncoder::Opendir(const char* path, fuse_file_info* fileInfo)
{
    fileInfo->keep_cache = 1;
    fileInfo->fh = 0;
    try {
        DecodedPath decodedPath = DecodedPath::DecodePath(path, GetSource());
        if (decodedPath.indexGiven) {
            fileInfo->fh = reinterpret_cast<uint64_t>(opendir(decodedPath.path.c_str()));
            if (fileInfo->fh == 0)
                return -errno;
        }
    } catch (const std::exception& exc) {
        return -ENOENT;
    }
    return 0;
}

int ZFecFSEncoder::Readdir(const char*, void* buffer, fuse_fill_dir_t filler,
                           off_t offset, fuse_file_info* fileInfo)
{

    try {
        struct stat st;
        memset(&st, 0, sizeof(st));

        if (fileInfo->fh == 0) {
            filler(buffer, ".", NULL, 0);
            filler(buffer, "..", NULL, 0);
            for (DecodedPath::ShareIndex shareIndex = 0; shareIndex < numShares; ++shareIndex) {
                char name[3];
                DecodedPath::EncodeShareIndex(shareIndex, &(name[0]));
                // TODO provide stat
                filler(buffer, name, NULL, 0);
            }
        } else {
            DIR* d = reinterpret_cast<DIR*>(fileInfo->fh);
            if (d == NULL) return -errno;

            if (offset != 0)
                seekdir(d, offset);
            for (struct dirent* entry = readdir(d); entry != NULL; entry = readdir(d))
            {
                st.st_ino = entry->d_ino;
                st.st_mode = entry->d_type << 12;
                st.st_size = EncodedSize(st.st_size);

                if (filler(buffer, entry->d_name, &st, telldir(d)) == 1)
                    break;
            }
        }
    } catch (const std::exception& exc) {
        return -ENOENT;
    }
    return 0;
}

int ZFecFSEncoder::Releasedir(const char*, fuse_file_info *fileInfo)
{
    if (fileInfo->fh != 0) {
        DIR* d = reinterpret_cast<DIR*>(fileInfo->fh);
        closedir(d);
    }
    return 0;
}

int ZFecFSEncoder::Open(const char* path, fuse_file_info* fileInfo)
{
    fileInfo->keep_cache = 1;

    try {
        DecodedPath decodedPath = DecodedPath::DecodePath(path, GetSource());

        if ((fileInfo->flags & O_ACCMODE) != O_RDONLY)
            return -EACCES;

        try {
            fileInfo->fh = EncodedFile::Open(sharesRequired, decodedPath);
        } catch (const std::exception& exc) {
            return -errno;
        }
    } catch (const std::exception& exc) {
        return -ENOENT;
    }
    return 0;
}

} // namespace ZFecFS
