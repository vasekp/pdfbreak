#include <sstream>
#include <stdexcept>
#include <cassert>
#include <zlib.h>

#include "pdffilter.h"

namespace pdf {

namespace codec {

std::string decode_error::format(std::string_view component, std::string_view error, std::streamoff where) {
  std::ostringstream oss{};
  if(!component.empty())
    oss << component << ": ";
  oss << error;
  if(where != -1)
   oss << " at position " << where;
  return oss.str();
}

namespace internal {

// Inspired by https://github.com/mateidavid/zstr

template<direction dir>
class ZStream {
  z_stream _stream;

  public:
  ZStream() {
    _stream.zalloc = Z_NULL;
    _stream.zfree = Z_NULL;
    _stream.opaque = NULL;
    int ret;
    if constexpr(dir == direction::compress)
      ret = ::deflateInit(&_stream, Z_DEFAULT_COMPRESSION);
    else
      ret = ::inflateInit(&_stream);
    if(ret != Z_OK) {
      assert(_stream.msg != NULL);
      throw decode_error("zlib", _stream.msg, -1);
    }
  }

  ~ZStream() {
    if constexpr(dir == direction::compress)
      ::deflateEnd(&_stream);
    else
      ::inflateEnd(&_stream);
  }

  z_stream& get() {
    return _stream;
  }
};

} // namespace pdf::codec::internal

DeflateDecoder::DeflateDecoder(std::streambuf* in_sbuf_)
  : in_sbuf(in_sbuf_),
    stream(std::make_unique<decltype(stream)::element_type>())
{
  stream->get().avail_in = 0;
  setg(&outBuffer[0], &outBuffer[0], &outBuffer[0]);
}

DeflateDecoder::~DeflateDecoder() { }

std::streambuf::int_type DeflateDecoder::underflow() {
  auto& zstr = stream->get();
  zstr.next_out = reinterpret_cast<Bytef*>(&outBuffer[0]);
  zstr.avail_out = bufSize;
  std::streamsize readBytes;
  while(true) {
    if(zstr.avail_in == 0) {
      zstr.next_in = reinterpret_cast<Bytef*>(&inBuffer[0]);
      zstr.avail_in = in_sbuf->sgetn(&inBuffer[0], bufSize);
      if(zstr.avail_in == 0)
        return traits_type::eof();
    }
    int ret = ::inflate(&zstr, Z_NO_FLUSH);
    if(ret != Z_OK && ret != Z_STREAM_END)
      ret = ::inflate(&zstr, Z_SYNC_FLUSH);
    readBytes = bufSize - zstr.avail_out;
    if(readBytes != 0)
      break;
    else {
      if(ret == Z_STREAM_END)
        return traits_type::eof();
      else if(ret == Z_OK)
        continue;
      else {
        assert(zstr.msg != NULL);
        std::streamoff pos = static_cast<std::streamoff>(
            in_sbuf->pubseekoff(0, std::ios::cur, std::ios::in))
          - zstr.avail_in;
        throw decode_error("zlib", zstr.msg, pos);
      }
    }
  }
  setg(&outBuffer[0], &outBuffer[0], &outBuffer[readBytes]);
  return traits_type::to_int_type(outBuffer[0]);
}

} // namespace pdf::codec

DecoderChain::DecoderChain(const Stream& stm) : chain{}, inner{} {
  chain.emplace_back(std::make_unique<std::stringbuf>(stm.data()));
  const auto& filters = stm.dict().lookup("Filter");
  if(!filters)
    return;
  else if(filters.is<Name>()) {
    const auto& filter = filters.get<Name>();
    bool parsed = chain_append(filter);
    if(!parsed)
      inner = filter;
  } else if(filters.is<Array>()) {
    for(const auto& entry : filters.get<pdf::Array>().items()) {
      if(!entry.is<pdf::Name>())
        throw codec::decode_error("", "Invalid /Filter", -1);
      const auto& filter = entry.get<pdf::Name>();
      if(!chain_append(filter)) {
        inner = filter;
        break;
      }
    }
  } else
    throw codec::decode_error("", "Invalid /Filter", -1);
}

bool DecoderChain::chain_append(const std::string& filter) {
  if(filter == "FlateDecode") {
    chain.emplace_back(std::make_unique<pdf::codec::DeflateDecoder>(chain.back().get()));
    return true;
  } else // TODO other decoders
    return false;
}

} // namespace pdf
