#ifndef PDF_READER_H
#define PDF_READER_H

#include <streambuf>
#include <utility>
#include <optional>

#include "pdfobjects.h"

namespace pdf {

using Version = std::pair<unsigned, unsigned>;

class Reader {
  std::streambuf& _stream;

  public:
  Reader(std::streambuf& stream_) : _stream(stream_) { }

  std::optional<Version> readVersion();
  TopLevelObject readTopLevelObject();
};

} // namespace pdf

#endif
