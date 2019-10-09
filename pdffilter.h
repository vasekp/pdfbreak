#ifndef PDF_CODEC_H
#define PDF_CODEC_H

#include <memory>
#include <streambuf>

namespace pdf::codec {

class decode_error : public std::runtime_error {
  public:
  decode_error(std::string_view component, std::string_view error, std::streamoff where)
    : std::runtime_error(format(component, error, where))
  { }

  private:
  std::string format(std::string_view component, std::string_view error, std::streamoff where);
};

namespace internal {
  enum class direction {
    compress,
    decompress
  };
  template<direction> class ZStream;
}

class DeflateDecoder : public std::streambuf {
  public:
  DeflateDecoder(std::streambuf& in_sbuf_);
  virtual ~DeflateDecoder();

  virtual int_type underflow() override;

  private:
  static constexpr std::size_t bufSize = 1024;

  std::streambuf& in_sbuf;
  std::unique_ptr<internal::ZStream<internal::direction::decompress>> stream;
  std::array<char_type, bufSize> inBuffer, outBuffer;
};

} // namespace pdf::codec

#endif
