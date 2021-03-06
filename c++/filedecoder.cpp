#include "filedecoder.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include "utils.h"
#include "unistd.h"

#include "metadata.h"


namespace ZFecFS {

inline
Metadata ReadMetadata(const AbstractFile& file)
{
    char buffer[Metadata::size];
    size_t sizeRead = file.Read(buffer, Metadata::size, 0);
    if (sizeRead != Metadata::size)
        throw SimpleException("Unable to read metadata.");

    return Metadata(buffer);
}

FileDecoder* FileDecoder::Open(const std::vector<boost::shared_ptr<AbstractFile> >& encodedFiles, const FecWrapper& fecWrapper)
{
    if (encodedFiles.empty())
        throw SimpleException("Too few encoded files.");

    off_t encodedSize = encodedFiles.front()->Size();
    std::vector<unsigned char> fileIndices(encodedFiles.size());
    Metadata firstMeta = ReadMetadata(*encodedFiles[0]);
    fileIndices[0] = firstMeta.index;
    for (unsigned int i = 1; i < encodedFiles.size(); ++i) {
        Metadata meta = ReadMetadata(*encodedFiles[i]);
        if (meta.required != firstMeta.required)
            throw SimpleException("Inconsistent metadata (required).");
        if (meta.excessBytes != firstMeta.excessBytes)
            throw SimpleException("Inconsistent metadata (excessBytes).");
        if (encodedFiles[i]->Size() != encodedSize)
            throw SimpleException("Inconsistent file sizes.");
        fileIndices[i] = meta.index;
    }
    if (firstMeta.required != fecWrapper.GetSharesRequired())
        throw SimpleException("'required'-value not consistent with filesystem.");
    if (firstMeta.excessBytes >= firstMeta.required || encodedSize < off_t(Metadata::size))
        throw SimpleException("Invalid 'excessBytes'-value");

    return new FileDecoder(encodedFiles, fileIndices, firstMeta, encodedSize, fecWrapper);
}

int FileDecoder::Read(char *outBuffer, size_t size, off_t offset)
{
    if (offset >= Size())
        return 0;

    unsigned int sharesRequired = fecWrapper.GetSharesRequired();
    // read some more in case size is not a multiple of required
    int bytesToRead = (size + sharesRequired - 1) / sharesRequired + 1;
    int minBytesRead = bytesToRead;

    // TODO better to have only one vector? - avoid re-allocating the vectors
    std::vector<std::vector<char> >& readBuffers(threadLocalData.Get().readBuffers);
    readBuffers.resize(sharesRequired);
    for (unsigned int i = 0; i < sharesRequired; ++i) {
        readBuffers[i].resize(bytesToRead);
        int bytesRead = encodedFiles[i]->Read(readBuffers[i].data(), bytesToRead,
                                             offset / sharesRequired + Metadata::size);
        minBytesRead = std::min(minBytesRead, bytesRead);
    }
    if (minBytesRead == 0)
        return 0;

    std::vector<const char*> fecInputPtrs(sharesRequired);
    std::vector<unsigned int> fecInputIndices(fileIndices.begin(), fileIndices.end());
    for (unsigned int i = 0; i < sharesRequired; ++i)
        fecInputPtrs[i] = readBuffers[i].data();

    NormalizeIndices(fecInputPtrs, fecInputIndices);

    std::vector<char>& workBuffer(threadLocalData.Get().workBuffer);
    workBuffer.resize(minBytesRead * sharesRequired);
    std::vector<char*> fecOutputPtrs(sharesRequired);
    for (unsigned int i = 0; i < sharesRequired; ++i)
        fecOutputPtrs[i] = workBuffer.data() + i * minBytesRead;

    fecWrapper.Decode(fecOutputPtrs.data(), fecInputPtrs.data(),
                      fecInputIndices.data(), minBytesRead);

    unsigned int offsetCorrection = offset % sharesRequired;

    size = std::min<size_t>(std::min<size_t>(size, minBytesRead * sharesRequired - offsetCorrection), Size() - offset);
    for (unsigned int i = 0; i < sharesRequired; ++i) {
        const char* decoded = fecInputIndices[i] < sharesRequired ? fecInputPtrs[i] : fecOutputPtrs[i];
        char* out = outBuffer + i - offsetCorrection;
        if (i < offsetCorrection) {
            out += sharesRequired;
            ++decoded;
        }
        CopyToNthElement(out, outBuffer + size, decoded, sharesRequired);
    }
    return size;
}

off_t FileDecoder::Size(const std::string& encodedFilePath)
{
    File file(encodedFilePath);

    char buffer[Metadata::size];
    ssize_t sizeRead = file.Read(buffer, Metadata::size, 0);
    if (sizeRead != ssize_t(Metadata::size))
        throw SimpleException("Size cannot be read from file.");

    return Size(Metadata(buffer), file.Size());
}

void FileDecoder::NormalizeIndices(std::vector<const char*>& fecInputPtrs,
                                   std::vector<unsigned int>& indices)
{
    unsigned int sharesRequired = fecWrapper.GetSharesRequired();
    for (unsigned int i = 0; i < sharesRequired;) {
        unsigned char index = indices[i];
        if (index < sharesRequired && index != i) {
            if (indices[index] != index) { // otherwise we would get an infinite loop, but we are probably screwed anyway...
                std::swap(indices[i], indices[index]);
                std::swap(fecInputPtrs[i], fecInputPtrs[index]);
            }
        } else {
            ++i;
        }
    }
}

template <class TOutIter, class TInIter>
TOutIter FileDecoder::CopyToNthElement(TOutIter out, TOutIter outEnd, TInIter in,
                                       unsigned int stride) const
{
    while (out < outEnd) {
        *out = *in;
        ++in;
        out += stride;
    }
    return out;
}

} // namespace ZFecFS
