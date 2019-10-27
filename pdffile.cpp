#include <random>

#include "pdffile.h"
#include "pdfparser.h"

namespace pdf {

void NamedObject::dump(std::ostream& os, unsigned) const {
  os << num << ' ' << gen << " obj\n";
  contents.dump(os, 1);
  os << '\n';
  if(!error.empty())
    os << "% !!! " + error + '\n';
  os << "endobj\n";
}

void XRefTable::dump(std::ostream& os, unsigned) const {
  os << "xref\n";
  for(const auto& section : _table)
    os << section.start << ' ' << section.count << '\n'
      << section.data /* << '\n' already in data */;
}

void Trailer::dump(std::ostream& os, unsigned) const {
  os << "trailer\n";
  _dict.dump(os, 1);
  os << '\n';
}

void StartXRef::dump(std::ostream& os, unsigned) const {
  os << "startxref\n" << val << "\n%%EOF\n";
}

/***** iostream interface *****/

std::ostream& operator<< (std::ostream& os, Version version) {
  os << "%PDF-" << version.major << '.' << version.minor << '\n';
  std::random_device rd{};
  std::uniform_int_distribution<unsigned char> dist{128, 255};
  os << '%';
  for(int i = 0; i < 4; i++)
    os << dist(rd);
  os << '\n';
  return os;
}

std::istream& operator>> (std::istream& is, Version& version) {
  std::istream::sentry s(is);
  if(s) {
    std::streambuf& stream = *is.rdbuf();
    if(std::streambuf::traits_type::to_char_type(stream.sgetc()) != '%') {
      is.setstate(std::ios::failbit);
      return is;
    }
    std::string line = parser::readLine(stream);
    unsigned v1, v2;
    int len;
    if(sscanf(line.data(), "%%PDF-%u.%u%n", &v1, &v2, &len) != 2 || len != 8) {
      is.setstate(std::ios::failbit);
      return is;
    }
    version.major = v1;
    version.minor = v2;
  }
  return is;
}

} //namespace pdf
