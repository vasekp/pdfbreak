#ifndef PDF_OBJSTREAM_H
#define PDF_OBJSTREAM_H

#include <stdexcept>
#include <vector>

#include "pdfbase.h"
#include "pdfparser.h"
#include "pdffilter.h"

namespace pdf::parser {

struct objstm_error : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

class ObjStream {
  const Stream& stm;
  DecoderChain dd;
  TokenParser ts;
  std::vector<unsigned long> nums;
  unsigned long first;
  unsigned long ix;
  bool fail;

public:
  ObjStream(const pdf::Stream& stm);

  void rewind();
  TopLevelObject read();
};

} // namespace pdf::parser

#endif
