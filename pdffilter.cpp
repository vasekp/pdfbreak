#include <zlib.h>
#include "pdffilter.h"

// Inspired by https://github.com/mateidavid/zstr

namespace pdf::codec {

namespace internal {

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
    if(ret != Z_OK) { /*TODO throw*/ }
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

  z_stream* getp() {
    return &_stream;
  }
};

} // namespace pdf::codec::internal

DeflateDecoder::DeflateDecoder(std::streambuf& in_sbuf_)
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
  do {
    if(zstr.avail_in == 0) {
      zstr.next_in = reinterpret_cast<Bytef*>(&inBuffer[0]);
      zstr.avail_in = in_sbuf.sgetn(&inBuffer[0], bufSize);
      if(zstr.avail_in == 0)
        return traits_type::eof();
    }
    int ret = ::inflate(&zstr, Z_NO_FLUSH);
    if(ret != Z_OK)
      ret = ::inflate(&zstr, Z_SYNC_FLUSH);
    readBytes = bufSize - zstr.avail_out;
    if(ret != Z_OK && readBytes == 0) { /*TODO throw*/ }
  } while (readBytes == 0);
  setg(&outBuffer[0], &outBuffer[0], &outBuffer[readBytes]);
  return outBuffer[0];
}

} // namespace pdf::codec
