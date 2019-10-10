#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
//#include "zstr.hpp"

#include "pdf.h"
#include "pdfreader.h"

int main(int argc, char* argv[]) {
  if(argc != 2) {
    std::cerr << "Usage: " << argv[0] << " filename.pdf\n";
    return 1;
  }

  std::filebuf filebuf{};
  if(!filebuf.open(argv[1], std::ios_base::in | std::ios_base::binary)) {
    std::cerr << "Can't open " << argv[1] << " for reading.\n";
    return 1;
  }

  pdf::Reader reader{filebuf};
  if(!reader.readVersion())
    std::clog << "Warning: PDF header missing\n";

  unsigned trailercnt = 0;
  while(true) {
    pdf::TopLevelObject obj = reader.readTopLevelObject();
    if(obj.is<pdf::NamedObject>()) {
      const pdf::NamedObject& nmo = obj.get<pdf::NamedObject>();
      std::ostringstream oss{};
      auto [num, gen] = nmo.numgen();
      oss << argv[1] << '-' << num << '.' << gen << ".obj";
      std::ofstream ofs{oss.str()};
      ofs << obj << '\n';
      std::clog << "Saving: " << oss.str()
        << (obj.failed() ? " (errors)\n" : "\n");
    } else if(obj.is<pdf::XRefTable>()) {
      const pdf::Object& trailer = obj.get<pdf::XRefTable>().trailer();
      std::clog << "Skipping xref table\n";
      std::ostringstream oss{};
      oss << argv[1] << "-trailer" << ++trailercnt << ".obj";
      std::ofstream ofs{oss.str()};
      ofs << "trailer\n" << trailer << '\n';
      std::clog << "Saving: " << oss.str() << '\n';
    } else if(obj.is<pdf::StartXRef>()) {
      std::clog << "Skipping startxref marker\n";
    } else if(obj.is<pdf::Invalid>()) {
      std::string error = obj.get<pdf::Invalid>().get_error();
      if(error.empty()) // end of input
        break;
      std::clog << "!!! " << error << '\n';
    }
  }
}
