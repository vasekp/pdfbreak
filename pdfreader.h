#ifndef PDFREADER_H
#define PDFREADER_H

#include <streambuf>
#include <utility>
#include <optional>
#include "pdf.h"

namespace pdf {

class Reader {
  std::streambuf& _stream;

  public:
  Reader(std::streambuf& stream_) : _stream(stream_) { }

  std::optional<std::pair<unsigned, unsigned>> readVersion();
  TopLevelObject readTopLevelObject();
};

} // namespace pdf

#endif
