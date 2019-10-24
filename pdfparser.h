#ifndef PDF_PARSER_H
#define PDF_PARSER_H

#include <stack>
#include <string>
#include <stdexcept>
#include <istream>
#include <cassert>

#include "pdfobjects.h"

namespace pdf {

namespace parser {

class TokenParser {
  std::streambuf* _stream;
  std::stack<std::string> _stack;
  std::size_t _lastLen;

  public:
  TokenParser(std::streambuf* stream_) : _stream{stream_}, _stack{}, _lastLen{0} { }

  ~TokenParser() {
    assert(_stack.size() <= 1);
    if(_stack.size() > 0)
      _stream->pubseekoff(-_lastLen, std::ios_base::cur);
  }

  std::string read() {
    if(!_stack.empty()) {
      auto ret = _stack.top();
      _stack.pop();
      return ret;
    } else
      return underflow();
  }

  void consume() {
    read();
  }

  void unread(std::string t) {
    _stack.push(std::move(t));
  }

  std::string peek() {
    if(_stack.empty())
      _stack.push(underflow());
    return _stack.top();
  }

  bool empty() const {
    return _stack.empty();
  }

  std::streambuf* stream() {
    assert(empty());
    reset();
    return _stream;
  }

  void newstream(std::streambuf* stream_) {
    _stream = stream_;
    reset();
  }

  // Always call after manipulating the underlying stream.
  void reset() {
    _stack = {};
    _lastLen = 0;
  }

  std::size_t pos() const {
    auto offset = _stream->pubseekoff(0, std::ios_base::cur);
    if(offset == (decltype(offset))(-1))
      throw std::logic_error("Can't determine position in provided stream");
    return std::size_t(offset);
  }

  std::size_t lastpos() const {
    return pos() - _lastLen;
  }

  private:
  std::string underflow();
};

enum class CharType {
  ws,
  delim,
  regular
};

CharType charType(char c);
std::string readLine(std::streambuf& stream);

Object parseName(TokenParser& ts);
Object parseNumberIndir(TokenParser& ts, Numeric&& n1);
Object parseStringLiteral(TokenParser& ts);
Object parseStringHex(TokenParser& ts);
Object parseArray(TokenParser& ts);
Object parseStream(TokenParser& ts, Dictionary&& dict);
Object parseDict(TokenParser& ts);

TopLevelObject parseNamedObject(TokenParser& ts);
TopLevelObject parseXRefTable(TokenParser& ts);
TopLevelObject parseStartXRef(TokenParser& ts);

Object readObject(TokenParser&);
TopLevelObject readTopLevelObject(TokenParser&);

} // namespace pdf::parser

std::istream& operator>> (std::istream& is, Version& version);
std::istream& operator>> (std::istream& is, TopLevelObject& tlo);
std::istream& skipToEndObj(std::istream&);

} // namespace pdf

#endif
