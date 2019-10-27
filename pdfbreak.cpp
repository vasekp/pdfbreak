#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>

#include "pdfobjects.h"
#include "pdfparser.h"
#include "pdffilter.h"
#include "pdfobjstream.h"

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

void unpack_objstm(const pdf::Stream& stm, const std::string& basename) {
  std::clog << "Entering ObjStream\n";
  try {
    pdf::parser::ObjStream objstm{stm};
    pdf::TopLevelObject tlo;
    while(tlo = objstm.read()) {
      assert(tlo.is<pdf::NamedObject>());
      auto [num, gen] = tlo.get<pdf::NamedObject>().numgen();
      std::string filename = [&basename, num] {
        std::ostringstream oss{};
        oss << basename << '-' << num << ".obj";
        return oss.str();
      }();
      std::ofstream ofs{filename};
      ofs << tlo << '\n';
      std::clog << "Saved: " << filename << (tlo.failed() ? " (errors)\n" : "\n");
    }
    if(tlo.failed()) {
      std::cerr << "!!! Error reading from ObjStream\n";
      return;
    }
    std::clog << "Reading ObjStream successful\n";
  } catch(pdf::codec::decode_error& e) {
    std::cerr << "!!! " << e.what() << '\n';
  } catch(pdf::parser::objstm_error& e) {
    std::cerr << "!!! " << e.what() << '\n';
    auto [filename, errors] = save_data(stm, basename);
    std::clog << "Saved data: " << filename << (errors ? " (errors)\n" : "\n");
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
        if(const auto& val = obj.get<pdf::Stream>().dict().lookup("Type");
            val.is<pdf::Name>() && val.get<pdf::Name>() == "ObjStm") {
          unpack_objstm(obj.get<pdf::Stream>(), basename);
        }
        else if(decompress) {
          auto [filename, errors] = save_data(obj.get<pdf::Stream>(), basename);
          std::clog << "Saved data: " << filename << (errors ? " (errors)\n" : "\n");
        }
      }
      ifs.clear();
    } else if(tlo.is<pdf::XRefTable>()) {
      std::clog << "Skipping xref table\n";
    } else if(tlo.is<pdf::Trailer>()) {
      const auto& trailer = tlo.get<pdf::Trailer>();
      std::ostringstream oss{};
      oss << argv[1] << "-trailer-" << trailer.start() << ".obj";
      std::ofstream ofs{oss.str()};
      ofs << trailer << '\n';
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
