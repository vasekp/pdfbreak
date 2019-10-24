#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>

#include "pdfobjects.h"
#include "pdfparser.h"
#include "pdffilter.h"

std::tuple<std::string, bool> save_data(const pdf::Stream& stm, const std::string& basename) {
  try {
    pdf::DecoderChain dd{stm};
    std::string ext;
    if(dd.complete())
      ext = "data.d";
    else {
      const auto& inner = dd.last();
      if(inner == "DCTDecode")
        ext = "jpg";
      else if(inner == "JBIG2Decode")
        ext = "jbig2";
      else if(inner == "JPXDecode")
        ext = "jpx";
      else
        ext = "data";
    }
    std::string filename = basename + "." + ext;
    std::ofstream ofs{filename};
    bool errors = false;
    ofs.exceptions(std::ios::failbit);
    try {
      ofs << dd.rdbuf();
    }
    catch(pdf::codec::decode_error& e) {
      ofs.clear();
      ofs << "\n% !!! " << e.what();
      errors = true;
    }
    catch(std::ios::failure& e) {
      ofs.clear();
      ofs << "% (empty stream)";
    }
    return {filename, errors};
  } catch(pdf::codec::decode_error& e) {
    std::cerr << "!!! " << e.what() << '\n';
    std::string filename = basename + ".data";
    std::ofstream ofs{filename};
    ofs << stm.data();
    return {filename, true};
  }
}

int main(int argc, char* argv[]) {
  if(argc != 2) {
    std::cerr << "Usage: " << argv[0] << " filename.pdf\n";
    return 1;
  }

  std::ifstream ifs{argv[1], std::ios_base::in | std::ios_base::binary};
  if(!ifs) {
    std::cerr << "Can't open " << argv[1] << " for reading.\n";
    return 1;
  }

  bool decompress = true; // TODO

  if(pdf::Version v{}; !(ifs >> v)) {
    std::clog << "Warning: PDF header missing\n";
    ifs.clear();
  }

  unsigned trailercnt = 0;
  pdf::TopLevelObject tlo{};
  while(true) {
    ifs >> tlo;
    if(ifs.eof())
      break;
    if(tlo.is<pdf::NamedObject>()) {
      const auto& nmo = tlo.get<pdf::NamedObject>();
      std::string basename = [&nmo, &argv]() {
        std::ostringstream oss{};
        auto [num, gen] = nmo.numgen();
        oss << argv[1] << '-' << num << '.' << gen;
        return oss.str();
      }();
      std::string filename = basename + ".obj";
      std::ofstream ofs{filename};
      ofs << tlo << '\n';
      std::clog << "Saved: " << filename << (tlo.failed() ? " (errors)\n" : "\n");
      const auto& obj = nmo.object();
      if(obj.is<pdf::Stream>()) {
        if(decompress) {
          auto [filename, errors] = save_data(obj.get<pdf::Stream>(), basename);
          std::clog << "Saved data: " << filename << (errors ? " (errors)\n" : "\n");
        }
      }
      ifs.clear();
    } else if(tlo.is<pdf::XRefTable>()) {
      const pdf::Object& trailer = tlo.get<pdf::XRefTable>().trailer();
      std::clog << "Skipping xref table\n";
      std::ostringstream oss{};
      oss << argv[1] << "-trailer" << ++trailercnt << ".obj";
      std::ofstream ofs{oss.str()};
      ofs << "trailer\n" << trailer << '\n';
      std::clog << "Saving: " << oss.str() << '\n';
    } else if(tlo.is<pdf::StartXRef>()) {
      std::clog << "Skipping startxref marker\n";
    } else if(tlo.is<pdf::Invalid>()) {
      std::string error = tlo.get<pdf::Invalid>().get_error();
      assert(!error.empty());
      std::cerr << "!!! " << error << '\n';
      ifs.clear();
      if(ifs >> pdf::skipToEndObj)
        std::clog << "Skipping past endobj at " << ifs.tellg() << '\n';
      else {
        std::clog << "End of file reached seeking enobj\n";
        break;
      }
    }
  }
}
