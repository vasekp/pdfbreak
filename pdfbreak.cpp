#include <iostream>
#include <fstream>
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
    std::getline(infs, line);
    if(std::strncmp(line.data(), "%PDF-1.", 6)) {
      std::cerr << "Not a PDF 1.x file.\n";
      return 1;
    }

    TokenStream ints{infs};
    Object obj;
    while(ints && ints >> obj);
  } catch(const std::ios_base::failure& err) {
    std::cerr << err.what() << '\n';
    return 1;
  }
}
