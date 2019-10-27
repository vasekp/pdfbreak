#include <cstdlib>
#include <cassert>

#include "pdfobjects.h"
#include "pdfparser.h"

namespace pdf {

namespace parser {

/***** Helper classes and functions *****/

CharType charType(char c) {
  switch(c) {
    case '\0': case '\t': case '\r': case '\n': case '\x0c': case ' ':
      return CharType::ws;
    case '(': case ')': case '<': case '>': case '[': case ']': case '{': case '}': case '/': case '%':
      return CharType::delim;
    default:
      return CharType::regular;
  }
}

namespace {

void skipToLF(std::streambuf& stream) {
  for(auto cInt = stream.sbumpc(); cInt != std::streambuf::traits_type::eof(); cInt = stream.sbumpc())
    if(char c = std::streambuf::traits_type::to_char_type(cInt); c == '\n')
      break;
}

void skipToNL(std::streambuf& stream) {
  for(auto cInt = stream.sgetc(); cInt != std::streambuf::traits_type::eof(); cInt = stream.snextc())
    if(char c = std::streambuf::traits_type::to_char_type(cInt); c == '\n' || c == '\r')
      break;
  if(std::streambuf::traits_type::to_char_type(stream.sbumpc()) == '\r')
    if(std::streambuf::traits_type::to_char_type(stream.sgetc()) == '\n')
      stream.sbumpc();
}

std::string readToNL(std::streambuf& stream) {
  std::string s{};
  for(auto cInt = stream.sbumpc(); cInt != std::streambuf::traits_type::eof(); cInt = stream.sbumpc()) {
    char c = std::streambuf::traits_type::to_char_type(cInt);
    s.push_back(c);
    if(c == '\n')
      break;
    if(c == '\r')
      if(std::streambuf::traits_type::to_char_type(stream.sgetc()) == '\n') {
        s.push_back('\n');
        stream.sbumpc();
        break;
      }
  }
  return s;
}

std::string chopNL(std::string&& in) {
  if(in.empty())
    return in;
  if(in.back() == '\r')
    in.resize(in.length() - 1);
  else if(in.back() == '\n') {
    in.resize(in.length() - 1);
    if(!in.empty() && in.back() == '\r')
      in.resize(in.length() - 1);
  }
  return in;
}

std::string format_position(std::streamoff offset) {
  char buf[20];
  std::snprintf(buf, 50, "%zu", offset);
  return {buf};
}

std::string report_position(const TokenParser& ts) {
  return " at " + format_position(ts.lastpos());
}

} // anonymous namespace

std::string readLine(std::streambuf& stream) {
  return chopNL(readToNL(stream));
}

/***** Implementation of TokenParser *****/

std::string TokenParser::underflow() {
  char c;
  _lastLen = 0;
  for(auto cInt = _stream->sgetc(); ; cInt = _stream->snextc()) {
    if(cInt == std::streambuf::traits_type::eof())
      return "";
    c = std::streambuf::traits_type::to_char_type(cInt);
    if(charType(c) != CharType::ws)
      break;
  }
  switch(charType(c)) {
    case CharType::delim:
      if(c == '%') {
        skipToNL(*_stream);
        return underflow();
      } else if(c == '<' || c == '>') {
        if(auto cInt = _stream->snextc(); std::streambuf::traits_type::to_char_type(cInt) == c) {
          _stream->sbumpc();
          _lastLen = 2;
          return {c, c};
        }
      } else
        _stream->sbumpc(); // For < and > this was already done by snexts()
      _lastLen = 1;
      return {c};
    case CharType::regular:
      {
        std::string s{c};
        for(auto cInt = _stream->snextc(); cInt != std::streambuf::traits_type::eof(); cInt = _stream->snextc()) {
          c = std::streambuf::traits_type::to_char_type(cInt);
          if(charType(c) == CharType::regular)
            s.push_back(c);
          else
            break;
        }
        _lastLen = s.length();
        return s;
      }
    case CharType::ws:
    default:
      throw std::logic_error("Unexpected CharType");
  }
}

/***** Parse functions *****/

Object parseName(TokenParser& ts) {
  std::string s = ts.read();
  assert(s == "/");
  s = ts.read();
  if(charType(s[0]) == CharType::regular)
    return {Name{std::move(s)}};
  else
    return {Invalid{"/ not followed by a proper name" + report_position(ts)}};
}

Object parseNumberIndir(TokenParser& ts, Numeric&& n1) {
  std::string t2 = ts.read();
  Numeric n2{t2};
  if(n1.uintegral() && n2.uintegral()) {
    auto t3 = ts.read();
    if(t3 == "R")
      return {Indirect{n1.val_ulong(), n2.val_ulong()}};
    else
      ts.unread(t3);
  }
  ts.unread(t2);
  return {std::move(n1)};
}

Object parseStringLiteral(TokenParser& ts) {
  std::string s = ts.read();
  assert(s == "(");
  assert(ts.empty());
  std::streambuf& stream = *ts.stream();
  std::string ret{};
  std::string error{};
  unsigned parens = 0;

  for(auto cInt = stream.sbumpc(); ; cInt = stream.sbumpc()) {
    if(cInt == std::streambuf::traits_type::eof()) {
      error = "End of input while reading string";
      break;
    }
    char c = std::streambuf::traits_type::to_char_type(cInt);
    if(c == ')') {
      if(parens > 0) {
        ret.push_back(c);
        parens--;
      } else // end of string literal
        break;
    } else if(c == '(') {
      ret.push_back(c);
      parens++;
    } else if(c == '\\') {
      cInt = stream.sbumpc();
      if(cInt == std::streambuf::traits_type::eof()) {
        error = "End of input while reading string";
        break;
      }
      c = std::streambuf::traits_type::to_char_type(cInt);
      switch(c) {
        case 'n':
          ret.push_back('\n');
          break;
        case 'r':
          ret.push_back('\r');
          break;
        case 't':
          ret.push_back('\t');
          break;
        case 'b':
          ret.push_back('\b');
          break;
        case 'f':
          ret.push_back('\f');
          break;
        case '(':
        case ')':
        case '\\':
          ret.push_back(c);
          break;
        case '\r':
        case '\n':
          /* ignore */
          break;
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
          {
            int d = c - '0';
            cInt = stream.sgetc();
            if(cInt == std::streambuf::traits_type::eof()) {
              error = "End of input while reading string";
              goto end;
            }
            c = std::streambuf::traits_type::to_char_type(cInt);
            if(c >= '0' && c <= '7') {
              d = d*8 + (c - '0');
              cInt = stream.snextc();
              if(cInt == std::streambuf::traits_type::eof()) {
                error = "End of input while reading string";
                goto end;
              }
              c = std::streambuf::traits_type::to_char_type(cInt);
              if(c >= '0' && c <= '7') {
                stream.sbumpc();
                d = d*8 + (c - '0');
              }
            }
            if(d > 255) {
              error = "Invalid octal value at " + format_position(ts.pos() - 4);
              goto end;
            }
            ret.push_back(std::streambuf::traits_type::to_char_type(d));
          }
          break;
        default:
          error = "Invalid character in string at " + format_position(ts.pos() - 1);
          goto end;
      }
    } else
      ret.push_back(c);
  }
end:
  return {String{std::move(ret), false, std::move(error)}};
}

Object parseStringHex(TokenParser& ts) {
  std::string s = ts.read();
  assert(s == "<");
  assert(ts.empty());
  std::streambuf& stream = *ts.stream();
  std::string ret{};
  std::string error{};
  unsigned odd = 0;
  char d = 0;

  for(auto cInt = stream.sbumpc(); ; cInt = stream.sbumpc()) {
    if(cInt == std::streambuf::traits_type::eof()) {
      error = "End of input while reading string";
      break;
    }
    char c = std::streambuf::traits_type::to_char_type(cInt);
    if(c == '>') {
      if(odd)
        ret.push_back(16*d);
      break;
    } else if(std::isxdigit(c)) {
      if(c >= '0' && c <= '9')
        c -= '0';
      else if(c >= 'A' && c <= 'F')
        c = c - 'A' + 10;
      else if(c >= 'a' && c <= 'f')
        c = c - 'a' + 10;
      d = d*16 + c;
      if(odd) {
        ret.push_back(d);
        d = 0;
      }
      odd ^= 1;
    } else if(c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f')
      /* ignore */;
    else {
      error = "Invalid character in string at " + format_position(ts.pos() - 1);
      break;
    }
  }
  return {String{std::move(ret), true, std::move(error)}};
}

Object parseArray(TokenParser& ts) {
  std::string s = ts.read();
  assert(s == "[");
  std::vector<Object> array{};
  std::string error{};
  while(ts.peek() != "]") {
    Object o = readObject(ts);
    bool failed = o.failed();
    array.push_back(std::move(o));
    if(failed) {
      error = "Error reading array element" + report_position(ts);
      break;
    }
  }
  if(ts.peek() == "]")
    ts.consume();
  return {Array{std::move(array), std::move(error)}};
}

Object parseDict(TokenParser& ts) {
  std::string s = ts.read();
  assert(s == "<<");
  std::map<std::string, Object> dict{};
  std::string error{};
  while(ts.peek() != ">>") {
    Object oKey = readObject(ts);
    if(oKey.failed()) {
      error = "Error reading key" + report_position(ts);
      break;
    }
    if(!oKey.is<Name>()) {
      error = "Key not a name" + report_position(ts);
      break;
    }
    std::string key = oKey.get<Name>();
    if(dict.find(key) != dict.end()) {
      error = "Duplicite key /" + key + report_position(ts);
      break;
    }
    Object oVal = (ts.peek() == ">>")
      ? Object{Invalid{"Value not present" + report_position(ts)}}
      : readObject(ts);
    bool failed = oVal.failed();
    dict.emplace(std::move(key), std::move(oVal));
    // Yes, we want to store the value even if parsing failed
    if(failed) {
      error = "Error reading value" + report_position(ts);
      break;
    }
  }
  if(ts.peek() == ">>")
    ts.consume();
  return {Dictionary{std::move(dict), std::move(error)}};
}

Object parseStream(TokenParser& ts, Dictionary&& dict) {
  std::string s = ts.read();
  assert(s == "stream");
  assert(ts.empty());
  std::streambuf& stream = *ts.stream();
  skipToLF(stream);
  std::string contents{};
  std::string error{};
  if(auto oLen = dict.lookup("Length");
      oLen.is<Numeric>() && oLen.get<Numeric>().uintegral()) {
    auto len = oLen.get<Numeric>().val_ulong();
    contents.resize(len);
    if(std::size_t lenRead = stream.sgetn(contents.data(), len); lenRead < len)
      error = "End of input during reading stream data, read " + format_position(lenRead) + " bytes";
    else if(ts.read() != "endstream")
      error = "endstream not found" + report_position(ts);
  } else {
    const std::string sep = "endstream";
    for(s = readToNL(stream); !s.empty(); s = readToNL(stream)) {
      /* We can't rely on this being the only thing on a line, especially
         if the file is possibly broken anyway. */
      if(auto off = s.find(sep); off != std::string::npos) {
        contents.append(s.data(), off);
        if(off + sep.length() == s.length()) // separator at end of line: OK
          break;
        if(auto r = stream.pubseekoff(- s.length() + off + sep.length(), std::ios_base::cur);
            r == (decltype(r))(-1))
          throw std::logic_error("Can't seek in provided stream");
        ts.reset();
        if(char after = std::streambuf::traits_type::to_char_type(stream.sgetc());
            charType(after) != CharType::regular)
          break;
        else // False alarm, try again
          contents.append(sep);
      } else
        contents.append(s);
    }
    if(s.empty())
      error = "End of input during reading stream data";
    chopNL(std::move(contents));
  }
  return {Stream{std::move(dict), std::move(contents), std::move(error)}};
}

/***** Top level object parsing *****/

bool skipToEndobj(std::streambuf& stream) {
  const std::string sep = "endobj";
  for(std::string s = readToNL(stream); !s.empty(); s = readToNL(stream)) {
    if(auto off = s.find(sep); off != std::string::npos) {
      if(off + sep.length() == s.length()) // separator at end of line: OK
        return true;
      stream.pubseekoff(- s.length() + off + sep.length(), std::ios_base::cur);
      if(char after = std::streambuf::traits_type::to_char_type(stream.sgetc());
          charType(after) != CharType::regular)
        return true;
    }
  }
  return false;
}

TopLevelObject parseNamedObject(TokenParser& ts) {
  Numeric num{ts.read()};
  if(!num.uintegral())
    return {Invalid{"Misshaped named object header (gen)" + report_position(ts)}};
  Numeric gen{ts.read()};
  if(!gen.uintegral())
    return {Invalid{"Misshaped named object header (gen)" + report_position(ts)}};
  if(ts.read() != "obj")
    return {Invalid{"Misshaped named object header (obj)" + report_position(ts)}};
  Object contents = readObject(ts);
  if(contents.is<Dictionary>() && ts.peek() == "stream")
    contents = parseStream(ts, std::move(contents).get<Dictionary>());
  std::string error{};
  if(std::string s = ts.read(); s != "endobj") {
    if(s.empty())
      error = "End of input where endobj expected";
    else
      error = "endobj not found" + report_position(ts);
  }
  return {NamedObject{num.val_ulong(), gen.val_ulong(), std::move(contents),
    std::move(error)}};
}

TopLevelObject parseXRefTable(TokenParser& ts) {
  std::string s = ts.read();
  assert(s == "xref");
  assert(ts.empty());
  std::streambuf& stream = *ts.stream();
  skipToNL(stream);
  std::vector<XRefTable::Section> sections{};
  while(true) {
    s = ts.peek();
    if(s.empty()) // EOF
      return {Invalid{"End of input while reading xref table"}};
    else if(s == "trailer")
      break;
    ts.consume();
    Numeric start{s};
    if(!start.uintegral())
      return {Invalid{"Broken xref subsection header (start)" + report_position(ts)}};
    Numeric count{ts.read()};
    if(!count.uintegral())
      return {Invalid{"Broken xref subsection header (count)" + report_position(ts)}};
    skipToNL(stream);
    unsigned len = 20 * count.val_ulong();
    s.resize(len);
    if(stream.sgetn(s.data(), len) < len)
      return {Invalid{"End of input while reading xref table"}};
    sections.push_back({start.val_ulong(), count.val_ulong(), std::move(s)});
  }
  return {XRefTable{std::move(sections)}};
}

TopLevelObject parseTrailer(TokenParser& ts) {
  std::string s = ts.read();
  assert(s == "trailer");
  auto start = ts.lastpos();
  Object trailer = readObject(ts);
  return {Trailer{std::move(trailer), start}};
}

TopLevelObject parseStartXRef(TokenParser& ts) {
  std::string s = ts.read();
  assert(s == "startxref");
  Numeric num{ts.read()};
  if(!num.uintegral())
    return {Invalid{"Broken startxref" + report_position(ts)}};
  return {StartXRef{static_cast<std::streamoff>(num.val_ulong())}};
}


Object readObject(TokenParser& ts) {
  std::string t = ts.peek();
  if(t == "")
    return {Invalid{"End of input"}};
  else if(t == "/")
    return parseName(ts);
  else if(t == "(")
    return parseStringLiteral(ts);
  else if(t == "<")
    return parseStringHex(ts);
  else if(t == "<<")
    return parseDict(ts);
  else if(t == "[")
    return parseArray(ts);
  else if(t == "null") {
    ts.consume();
    return {Null{}};
  } else if(t == "true" || t == "false") {
    ts.consume();
    return {Boolean{t == "true"}};
  } else if(Numeric n1{t}; n1.valid()) {
    ts.consume();
    return parseNumberIndir(ts, std::move(n1));
  } else {
    return {Invalid{"Garbage or unexpected token" + report_position(ts)}};
  }
}

TopLevelObject readTopLevelObject(TokenParser& ts) {
  std::string t = ts.peek();
  if(t == "") // EOF
    return {Null{}};
  else if(Numeric{t}.uintegral())
    return parseNamedObject(ts);
  else if(t == "xref")
    return parseXRefTable(ts);
  else if(t == "trailer")
    return parseTrailer(ts);
  else if(t == "startxref")
    return parseStartXRef(ts);
  else
    return {Invalid{"Garbage or unexpected token" + report_position(ts)}};
}

} // namespace pdf::parser

/***** std::istream interface *****/

std::istream& operator>> (std::istream& is, Version& version) {
  std::istream::sentry s(is);
  if(s) {
    std::streambuf& stream = *is.rdbuf();
    if(std::streambuf::traits_type::to_char_type(stream.sgetc()) != '%') {
      is.setstate(std::ios::failbit);
      return is;
    }
    std::string line = parser::readLine(stream);
    unsigned v1, v2;
    int len;
    if(sscanf(line.data(), "%%PDF-%u.%u%n", &v1, &v2, &len) != 2 || len != 8) {
      is.setstate(std::ios::failbit);
      return is;
    }
    version.major = v1;
    version.minor = v2;
  }
  return is;
}

std::istream& operator>> (std::istream& is, TopLevelObject& tlo) {
  std::istream::sentry s(is);
  if(s) {
    parser::TokenParser ts{is.rdbuf()};
    tlo = parser::readTopLevelObject(ts);
    // !tlo: Null or Invalid
    if(!tlo) {
      if(tlo.failed()) // Invalid (nothing salvaged)
        is.setstate(std::ios::failbit);
      else // Null (EOF)
        is.setstate(std::ios::eofbit);
    } else if(tlo.failed())
      // Object usable but partially parsed
      is.setstate(std::ios::badbit);
  }
  return is;
}

std::istream& skipToEndObj(std::istream& is) {
  std::istream::sentry s(is);
  if(s) {
    bool success = parser::skipToEndobj(*is.rdbuf());
    if(!success)
      is.setstate(std::ios::failbit);
  }
  return is;
}

} // namespace pdf
