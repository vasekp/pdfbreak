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
    unsigned trailercnt = 0;
    while(ints) {
      pdf::TopLevelObject obj = readTopLevelObject(ints);
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
      } else if(std::holds_alternative<pdf::Invalid>(obj.contents)) {
        std::string error = std::get<pdf::Invalid>(obj.contents).get_error();
        if(error.empty()) // end of input
          break;
        std::clog << "!!! " << error << '\n';
      }
      if(obj.failed()) {
        std::clog << "Skipping till next endobj... ";
        auto offset = pdf::skipBrokenObject(ints);
        if(ints)
          std::clog << offset << '\n';
        else
          std::clog << "(end of file)\n";
      }
    }
  } catch(const std::ios_base::failure& err) {
    std::cerr << err.what() << '\n';
    return 1;
  }
}
