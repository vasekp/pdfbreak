#include <iostream>
#include <fstream>
#include <utility>
#include <map>
#include <vector>
#include <tuple>
#include <cstdio>

#include "pdfobjects.h"
#include "pdfparser.h"

int main(int argc, char* argv[]) {
  if(argc < 2) {
    std::cerr << "Usage: " << argv[0] << " [in1.pdf|in1.obj] ...\n";
    return 1;
  }

  std::string ofname = "out.pdf"; // TODO
  std::ofstream ofs{ofname};
  ofs << pdf::Version{1, 7};

  std::vector<std::string> fnames{&argv[1], &argv[argc]};
  std::map<pdf::ObjRef, std::streamoff> map{};
  pdf::TopLevelObject trailer{};
  for(const auto& fname : fnames) {
    std::ifstream ifs{fname, std::ios_base::in | std::ios_base::binary};
    if(!ifs) {
      std::cerr << "Can't open " << fname << " for reading.\n";
      continue;
    }
    pdf::TopLevelObject tlo{};
    while(ifs >> tlo) {
      if(tlo.is<pdf::NamedObject>()) {
        auto& nmo = tlo.get<pdf::NamedObject>();
        auto [num, gen] = nmo.numgen();
        auto offset = ofs.tellp();
        map[{num, gen}] = offset;
        ofs << tlo;
      } else if(tlo.is<pdf::XRefTable>())
        std::clog << "Skipping xref table\n";
      else if(tlo.is<pdf::Trailer>())
        trailer = tlo;
      else if(tlo.is<pdf::StartXRef>())
        std::clog << "Skipping startxref marker\n";
    }
    if(tlo.failed()) {
      ifs.clear();
      std::clog << "Error reading " << fname << " at " << ifs.tellg() << '\n';
    }
  }
  unsigned long max = 0;
  for(const auto [key, val] : map) {
    if(key.num > max)
      max = key.num;
  }
  std::vector<std::tuple<unsigned long, unsigned long/*TODO*/, bool>> xrefs(max+1);
  for(const auto [key, val] : map) {
    xrefs[key.num] = {val, key.gen, true};
  }
  {
    unsigned long last_free = 0;
    for(auto it = xrefs.rbegin(); it != xrefs.rend(); it++) {
      if(std::get<2>(*it) == false) {
        std::get<0>(*it) = last_free;
        std::get<1>(*it) = 65535;
        last_free = std::distance(it, xrefs.rend()) - 1;
      }
    }
  }
  auto xrefstart = ofs.tellp();
  ofs << "xref\n"
    << "0 " << xrefs.size() << '\n';
  for(const auto [off, gen, used] : xrefs) {
    char buf[21];
    std::snprintf(buf, sizeof(buf), "%010lu %05lu %c \n", off, gen, used?'n':'f');
    ofs << buf << std::flush;
  }
  if(!trailer)
    std::cerr << "!!! No trailer found; expect invalid PDF\n";
  pdf::TopLevelObject startxref{pdf::StartXRef{xrefstart}};
  ofs << trailer << startxref;
}
