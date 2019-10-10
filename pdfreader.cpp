#include <cstdio>

#include "pdfreader.h"
#include "pdfparser.h"

namespace pdf {

std::optional<std::pair<unsigned, unsigned>> Reader::readVersion() {
  if(std::streambuf::traits_type::to_char_type(_stream.sgetc()) != '%')
    return {};
  std::string line = parser::readToNL(_stream);
  unsigned v1, v2;
  int len;
  if(sscanf(line.data(), "%%PDF-%u.%u%n", &v1, &v2, &len) != 2)
    return {};
  if(len != 8)
    return {};
  return {{v1, v2}};
}

TopLevelObject Reader::readTopLevelObject() {
  return parser::readTopLevelObject(_stream);
}

} // namespace pdf
