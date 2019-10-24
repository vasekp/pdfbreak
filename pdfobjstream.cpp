#include "pdfobjstream.h"

namespace pdf::parser {

ObjStream::ObjStream(const Stream& stm) :
    stm{stm}, dd{stm}, ts{dd.rdbuf()}, nums{},
    first{0}, ix{0}, fail{false} {
  if(!dd.complete())
    throw objstm_error{"Couldn't unpack object stream"};
  const auto& oN = stm.dict().lookup("N");
  const auto& oFirst = stm.dict().lookup("First");
  if(!oN.is<Numeric>() || !oN.get<Numeric>().uintegral()
      || !oFirst.is<Numeric>() || !oFirst.get<Numeric>().uintegral())
    throw objstm_error{"Object stream lacks required fields"};
  unsigned long count = oN.get<Numeric>().val_ulong();
  first = oFirst.get<Numeric>().val_ulong();
  for(auto i = 0ul; i < count; i++) {
    Numeric num{ts.read()};
    if(!num.uintegral())
      throw objstm_error{"Broken object stream header"};
    nums.push_back(num.val_ulong());
    Numeric offset{ts.read()};
    if(!offset.uintegral())
      throw objstm_error{"Broken object stream header"};
    // offset ignored for now
  }
}

void ObjStream::rewind() {
  // We need to do this the stupid way because a basic_streambuf can't seek
  // and sgetn needs an array to write to
  dd = DecoderChain{stm};
  ts.newstream(dd.rdbuf());
  for(unsigned i = 0ul; i < first; i++)
    dd.rdbuf()->sbumpc();
  ix = 0;
  fail = false;
}

TopLevelObject ObjStream::read() {
  if(fail)
    return {Invalid{"Read on a failed ObjStream"}};
  if(ix == nums.size()) {
    fail = true;
    return {}; // EOF
  }
  Object o = readObject(ts);
  unsigned long num = nums[ix];
  fail = o.failed();
  if(!fail)
    ++ix;
  return {NamedObject{num, 0, std::move(o), ""}};
}

} // namespace pdf::parser
