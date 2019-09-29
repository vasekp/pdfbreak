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
    if(infs.get() == '%') {
      std::getline(infs, line);
      if(std::strncmp(line.data(), "%PDF-1.", 6))
        std::clog << "Warning: PDF header missing\n";
    } else {
      std::clog << "Warning: PDF header missing\n";
      infs.unget();
    }

    TokenStream ints{infs};
    TopLevelObject obj;
    unsigned trailercnt = 0;
    while(ints && ints >> obj) {
#if 1
      if(std::holds_alternative<NamedObject>(obj.contents)) {
        const NamedObject& nmo = std::get<NamedObject>(obj.contents);
        std::ostringstream oss{};
        oss << argv[1] << '-' << nmo.num << '.' << nmo.gen << ".obj";
        std::ofstream ofs{oss.str()};
        ofs << obj << '\n';
        std::clog << "Saving: " << oss.str() << '\n';
      } else if(std::holds_alternative<XRefTable>(obj.contents)) {
        const Object& trailer = std::get<XRefTable>(obj.contents).trailer;
        std::clog << "Skipping xref table\n";
        std::ostringstream oss{};
        oss << argv[1] << "-trailer" << ++trailercnt << ".obj";
        std::ofstream ofs{oss.str()};
        ofs << "trailer\n" << trailer << '\n';
        std::clog << "Saving: " << oss.str() << '\n';
      } else if(std::holds_alternative<StartXRef>(obj.contents)) {
        std::clog << "Skipping startxref marker\n";
      } else {
        std::clog << obj << '\n';
        std::clog << "Skipping till next endobj...\n";
        std::string s;
        while(infs) {
          s = readToNL(infs);
          if(s.substr(std::max((int)s.length() - 6, 0)) == "endobj")
            break;
        }
      }
#else
      std::cout << obj << '\n';
#endif
    }
  } catch(const std::ios_base::failure& err) {
    std::cerr << err.what() << '\n';
    return 1;
  }
}
