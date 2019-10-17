#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>

#include "pdfobjects.h"
#include "pdfreader.h"
#include "pdffilter.h"

std::tuple<bool, std::string, bool> try_decompress(const pdf::Stream& str, const std::string& basename) {
  if(auto& val = str.dict().lookup("Filter"); val.is<pdf::Name>()) { // TODO array
    const std::string& filter = val.get<pdf::Name>();
    if(filter == "FlateDecode") {
      bool errors = false;
      std::string filename = basename + ".data";
      std::ofstream ofs{filename};
      std::stringbuf strb{str.data()};
      pdf::codec::DeflateDecoder dd{&strb};
      ofs.exceptions(std::ios::failbit);
      try {
        ofs << &dd;
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
      return {true, filename, errors};
    } else if(filter == "DCTDecode" || filter == "JPXDecode" || filter == "JBIG2Decode") {
      std::string ext = "";
      if(filter == "DCTDecode")
        ext = "jpg";
      else if(filter == "JBIG2Decode")
        ext = "jbig2";
      else if(filter == "JPXDecode")
        ext = "jpx";
      else
        throw std::logic_error("mismatch in handled types");
      std::string filename = basename + "." + ext;
      std::ofstream ofs{filename};
      ofs << str.data();
      return {true, filename, false};
    }
  }
  return {false, {}, false};
}

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

  bool decompress = true; // TODO

  pdf::Reader reader{filebuf};
  if(!reader.readVersion())
    std::clog << "Warning: PDF header missing\n";

  unsigned trailercnt = 0;
  while(true) {
    pdf::TopLevelObject tlo = reader.readTopLevelObject();
    if(tlo.is<pdf::NamedObject>()) {
      auto& nmo = tlo.get<pdf::NamedObject>();
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
      auto& obj = nmo.object();
      if(obj.is<pdf::Stream>() && decompress) {
        auto [success, filename, errors] = try_decompress(obj.get<pdf::Stream>(), basename);
        if(success)
          std::clog << "Saved aux: " << filename << (errors ? " (errors)\n" : "\n");
      }
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
      if(error.empty()) // end of input
        break;
      std::clog << "!!! " << error << '\n';
    }
  }
}
