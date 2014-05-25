#ifndef FECWRAPPER_H
#define FECWRAPPER_H

#include <vector>

extern "C" {

#include "fec.h"

}

namespace ZFecFS {


class FecWrapper
{
    const unsigned int sharesRequired;
    fec_t* fecData;
public:
    FecWrapper(unsigned int sharesRequired, unsigned int numShares)
        : sharesRequired(sharesRequired)
        , fecData(fec_new(sharesRequired, numShares))
    { }

    ~FecWrapper()
    {
        fec_free(fecData);
    }

    unsigned int GetSharesRequired() const { return sharesRequired; }

    void Encode(char* outBuffer, char** fecInput, unsigned int index, unsigned int length) const
    {
        // note that encoding is done in 8192 byte blocks, so length should be at most 8192
        fec_encode(fecData,
                   reinterpret_cast<gf* const*>(fecInput),
                   reinterpret_cast<gf* const*>(&outBuffer),
                   &index, 1,
                   length);
    }
};

}

#endif // FECWRAPPER_H
