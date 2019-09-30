#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
//#include "zstr.hpp"

#include "pdf.h"

int main(int argc, char* argv[]) {
  if(argc != 2) {
    std::cerr << "Usage: " << argv[0] << " filename.pdf\n";
    return 1;
  }

  std::ifstream infs{argv[1]};
  if(!infs) {
    std::cerr << "Can't open " << argv[1] << " for reading.\n";
    return 1;
  }

  try {
    std::string line;
    if(infs.peek() == '%') {
      std::getline(infs, line);
      if(std::strncmp(line.data(), "%PDF-1.", 6))
        std::clog << "Warning: PDF header missing\n";
    } else {
      std::clog << "Warning: PDF header missing\n";
    }

    pdf::TokenStream ints{infs};
    pdf::TopLevelObject obj;
    unsigned trailercnt = 0;
    while(ints && ints >> obj) {
      if(std::holds_alternative<pdf::NamedObject>(obj.contents)) {
        const pdf::NamedObject& nmo = std::get<pdf::NamedObject>(obj.contents);
        std::ostringstream oss{};
        oss << argv[1] << '-' << nmo.num << '.' << nmo.gen << ".obj";
        std::ofstream ofs{oss.str()};
        ofs << obj << '\n';
        std::clog << "Saving: " << oss.str()
          << (obj.failed() ? " (errors)\n" : "\n");
      } else if(std::holds_alternative<pdf::XRefTable>(obj.contents)) {
        const pdf::Object& trailer = std::get<pdf::XRefTable>(obj.contents).trailer;
        std::clog << "Skipping xref table\n";
        std::ostringstream oss{};
        oss << argv[1] << "-trailer" << ++trailercnt << ".obj";
        std::ofstream ofs{oss.str()};
        ofs << "trailer\n" << trailer << '\n';
        std::clog << "Saving: " << oss.str() << '\n';
      } else if(std::holds_alternative<pdf::StartXRef>(obj.contents)) {
        std::clog << "Skipping startxref marker\n";
      } else {
        std::clog << "!!! " << std::get<pdf::Invalid>(obj.contents).get_error() << '\n';
      }
      if(obj.failed()) {
        std::clog << "Skipping till next endobj... ";
        while(infs) {
          std::string s = pdf::readToNL(infs);
          /* We can't rely on this being the only thing on a line, especially
             if the file is possibly broken anyway. */
          char sep[] = "endobj";
          if(auto off = s.find(sep); off != std::string::npos) {
            auto pos = (std::size_t)infs.tellg() - s.length() + off;
            infs.seekg(pos);
            ints.clear();
            if(ints.read() == sep) {
              std::clog << pos;
              break;
            } else {
              infs.seekg(pos + sizeof(sep) - 1);
              ints.clear();
            }
          }
        }
        std::clog << '\n';
      }
    }
  } catch(const std::ios_base::failure& err) {
    std::cerr << err.what() << '\n';
    return 1;
  }
}
