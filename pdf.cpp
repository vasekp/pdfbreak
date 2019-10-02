#include <cstdlib>
#include <optional>
#include <cstring>
#include <cstdio>
#include <type_traits>
#include <stdexcept>

#include "pdf.h"

namespace pdf {

/***** Helper classes and functions *****/

enum class CharType {
  ws,
  delim,
  regular
};

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

void skipToNL(std::streambuf& stream) {
  for(auto cInt = stream.sbumpc(); cInt != std::streambuf::traits_type::eof(); cInt = stream.sbumpc())
    if(char c = std::streambuf::traits_type::to_char_type(cInt); c == '\n' || c == '\r')
      break;
}

std::string readToNL(std::streambuf& stream) {
  std::string s{};
  for(auto cInt = stream.sbumpc(); cInt != std::streambuf::traits_type::eof(); cInt = stream.sbumpc()) {
    char c = std::streambuf::traits_type::to_char_type(cInt);
    s.push_back(c);
    if(c == '\n' || c == '\r')
      break;
  }
  return s;
}

std::string fPos(const TokenStream& ts, bool exact = false) {
  /*if(ts.stream().in_avail() == 0)
    return " at end of input";*/
  auto pos = ts.stream().pubseekoff(0, std::ios_base::cur);
  assert(pos != std::streambuf::pos_type(-1));
  char buf[100];
  std::snprintf(buf, 100, exact ? " at %zu" : " (near %zu)", (std::size_t)pos);
  return {buf};
}

void print_offset(std::ostream& os, unsigned off, const std::string& text) {
  os << std::string(2*off, ' ') << text;
}


/***** Implementation of TokenStream *****/

std::string TokenStream::underflow() {
  char c;
  for(auto cInt = _stream.sgetc(); ; cInt = _stream.snextc()) {
    if(cInt == std::streambuf::traits_type::eof())
      return "";
    c = std::streambuf::traits_type::to_char_type(cInt);
    if(charType(c) != CharType::ws)
      break;
  }
  switch(charType(c)) {
    case CharType::delim:
      if(c == '%') {
        skipToNL(_stream);
        return underflow();
      } else if(c == '<' || c == '>') {
        if(auto cInt = _stream.snextc(); std::streambuf::traits_type::to_char_type(cInt) == c) {
          _stream.sbumpc();
          return {c, c};
        }
      }
      // All other cases
      _stream.sbumpc();
      return {c};
    case CharType::regular:
      {
        std::string s{c};
        for(auto cInt = _stream.snextc(); cInt != std::streambuf::traits_type::eof(); cInt = _stream.snextc()) {
          c = std::streambuf::traits_type::to_char_type(cInt);
          if(charType(c) == CharType::regular)
            s.push_back(c);
          else
            break;
        }
        return s;
      }
    case CharType::ws:
    default:
      throw std::logic_error("Unexpected CharType");
  }
}


/***** Implementation of PDF object classes *****/

void Null::dump(std::ostream& os, unsigned off) const {
  print_offset(os, off, "null");
}

Invalid::Invalid(const TokenStream& ts, std::string&& error_)
  : error(std::move(error_))
{
  error.append(fPos(ts));
}

void Invalid::dump(std::ostream& os, unsigned off) const {
  print_offset(os, off, "null");
  os << '\n';
  print_offset(os, off, "% !!! " + error);
}

void Boolean::dump(std::ostream& os, unsigned off) const {
  print_offset(os, off, val ? "true" : "false");
}

Numeric::Numeric(std::string str) {
  if(str.empty()) {
    dp = -1; //fail
    return;
  }
  auto off = str.find('.');
  char *last;
  int dp_;
  if(off != std::string::npos) {
    str.erase(off, 1);
    dp_ = str.length() - off;
  } else
    dp_ = 0;
  int res = (int)strtol(str.data(), &last, 10);
  if(last != str.data() + str.length()) {
    dp = -1; //fail
    return;
  } else {
    val_s = res;
    dp = dp_;
  }
}

long Numeric::val_long() const {
  assert(integral());
  long ret = val_s;
  int d = dp;
  while(d > 0) {
    ret /= 10;
    d -= 1;
  }
  return ret;
}

unsigned long Numeric::val_ulong() const {
  assert(uintegral());
  unsigned long ret = val_s;
  int d = dp;
  while(d > 0) {
    ret /= 10;
    d -= 1;
  }
  return ret;
}

void Numeric::dump(std::ostream& os, unsigned off) const {
  assert(!failed());
  char buf[20];
  std::snprintf(buf, 20, "%0*li", dp + (val_s < 0 ? 1 : 0) + 1, val_s);
  std::string str{buf};
  if(dp > 0)
    str.insert(str.length() - dp, 1, '.');
  print_offset(os, off, str);
}

void String::dump(std::ostream& os, unsigned off) const {
  if(hex) {
    print_offset(os, off, "<");
    for(char c : val) {
      char buf[3];
      sprintf(buf, "%02X", (unsigned char)c);
      os << buf;
    }
    os << '>';
  } else {
    print_offset(os, off, "(");
    for(char c : val) {
      if(c >= 32 && (unsigned char)c <= 127 && c != '(' && c != ')' && c != '\\')
        os << c;
      else {
        char buf[5];
        sprintf(buf, "\\%03o", (unsigned char)c);
        os << buf;
      }
    }
    os << ')';
  }
  if(!error.empty()) {
    os << '\n';
    print_offset(os, off, "% !!! " + error);
  }
}

void Name::dump(std::ostream& os, unsigned off) const {
  print_offset(os, off, "/" + val);
}

void Array::dump(std::ostream& os, unsigned off) const {
  print_offset(os, off, "[\n");
  for(const auto& o : val) {
    o.dump(os, off+1);
    os << '\n';
  }
  if(!error.empty())
    print_offset(os, off + 1, "% !!! " + error + "\n");
  print_offset(os, off, "]");
}

void Dictionary::dump(std::ostream& os, unsigned off) const {
  print_offset(os, off, "<<\n");
  for(const auto& [k, v] : val) {
    print_offset(os, off+1, "/" + k + "\n");
    v.dump(os, off+2);
    os << '\n';
  }
  if(!error.empty())
    print_offset(os, off + 1, "% !!! " + error + "\n");
  print_offset(os, off, ">>");
}

void Stream::dump(std::ostream& os, unsigned off) const {
  dict.dump(os, off);
  os << '\n';
  print_offset(os, off, "stream\n");
  os.write(data.data(), data.length());
  os << '\n';
  print_offset(os, off, "endstream");
  if(!error.empty()) {
    os << '\n';
    print_offset(os, off, "% !!! " + error);
  }
}

void Indirect::dump(std::ostream& os, unsigned off) const {
  print_offset(os, off, "");
  os << num << ' ' << gen << " R";
}

/***** Implementation of PDF top level object classes *****/

void NamedObject::dump(std::ostream& os, unsigned off) const {
  print_offset(os, off, "");
  os << num << ' ' << gen << " obj\n";
  contents.dump(os, off+1);
  os << '\n';
  if(!error.empty()) {
    print_offset(os, off, "% !!! " + error + "\n");
  }
  print_offset(os, off, "endobj");
}

void XRefTable::dump(std::ostream& os, unsigned off) const {
  print_offset(os, off, "xref\n");
  for(const auto& section : table)
    os << section.start << ' ' << section.count << '\n'
      << section.data /* << '\n' */;
  print_offset(os, off, "trailer\n");
  trailer.dump(os, off+1);
}

void StartXRef::dump(std::ostream& os, unsigned) const {
  os << "startxref\n" << val << "\n%%EOF";
}

/***** Parse functions *****/

Object parseName(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "/");
  s = ts.read();
  if(charType(s[0]) == CharType::regular)
    return {Name{std::move(s)}};
  else
    return {Invalid{ts, "/ not followed by a proper name"}};
}

Object parseNumberIndir(TokenStream& ts, Numeric&& n1) {
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

Object parseStringLiteral(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "(");
  assert(ts.empty());
  std::streambuf& stream = ts.stream();
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
              break;
            }
            c = std::streambuf::traits_type::to_char_type(cInt);
            if(c >= '0' && c <= '7') {
              d = d*8 + (c - '0');
              cInt = stream.snextc();
              if(cInt == std::streambuf::traits_type::eof()) {
                error = "End of input while reading string";
                break;
              }
              c = std::streambuf::traits_type::to_char_type(cInt);
              if(c >= '0' && c <= '7') {
                stream.sbumpc();
                d = d*8 + (c - '0');
              }
            }
            if(d > 255) {
              error = "Invalid octal value";
              break;
            }
            ret.push_back(std::streambuf::traits_type::to_char_type(d));
          }
          break;
        default:
          error = "Invalid character in string" + fPos(ts);
          goto end;
      }
    } else
      ret.push_back(c);
  }
end:
  return {String{std::move(ret), false, std::move(error)}};
}

Object parseStringHex(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "<");
  assert(ts.empty());
  std::streambuf& stream = ts.stream();
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
      error = "Invalid character in string" + fPos(ts);
      break;
    }
  }
  return {String{std::move(ret), true, std::move(error)}};
}

Object parseArray(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "[");
  std::vector<Object> array{};
  std::string error{};
  while(ts.peek() != "]") {
    Object o = readObject(ts);
    bool failed = o.failed();
    array.push_back(std::move(o));
    if(failed) {
      error = "Error reading array element";
      break;
    }
  }
  ts.consume();
  return {Array{std::move(array), std::move(error)}};
}

Object parseStream(TokenStream& ts, Dictionary&& dict);

Object parseDict(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "<<");
  std::map<std::string, Object> tmp{};
  std::string error{};
  while(ts.peek() != ">>") {
    Object oKey = readObject(ts);
    if(oKey.failed()) {
      error = "Error reading key";
      break;
    }
    if(!std::holds_alternative<Name>(oKey.contents)) {
      error = "Key not a name";
      break;
    }
    std::string name = std::get<Name>(oKey.contents).val;
    Object oVal;
    if(ts.peek() == ">>")
      oVal = {Invalid{ts, "Value not present"}};
    else
      oVal = readObject(ts);
    bool failed = oVal.failed();
    tmp[name] = std::move(oVal);
    if(failed) {
      error = "Error reading value";
      break;
    }
  }
  ts.consume();
  Dictionary dict{std::move(tmp), std::move(error)};
  if(ts.peek() == "stream")
    return parseStream(ts, std::move(dict));
  else
    return {std::move(dict)};
}

Object parseStream(TokenStream& ts, Dictionary&& dict) {
  std::string s = ts.read();
  assert(s == "stream");
  assert(ts.empty());
  std::streambuf& stream = ts.stream();
  skipToNL(stream);
  Object& o = dict.val["Length"];
  std::string contents{};
  std::string error{};
  if(std::holds_alternative<Numeric>(o.contents)) {
    unsigned len = std::get<Numeric>(o.contents).val_long();
    contents.resize(len);
    if(stream.sgetn(contents.data(), len) < len)
      error = "End of input during reading stream data";
    if(ts.read() != "endstream")
      error = "endstream not found" + fPos(ts);
  } else {
    const std::string sep = "endstream";
    for(s = readToNL(stream); !s.empty(); s = readToNL(stream)) {
      /* We can't rely on this being the only thing on a line, especially
         if the file is possibly broken anyway. */
      if(auto off = s.find(sep); off != std::string::npos) {
        contents.append(s.data(), off);
        if(off + sep.length() == s.length()) // separator at end of line: OK
          break;
        stream.pubseekoff(- s.length() + off + sep.length(), std::ios_base::cur);
        ts.reset();
        if(char after = std::streambuf::traits_type::to_char_type(stream.sgetc());
            charType(after) != CharType::regular)
          break;
        else // False alarm, try again
          contents.append(sep);
      } else
        contents.append(s);
    }
    /* TODO: PDF spec says newline before endstream should be ignored
       (but real world examples show it may be omitted). Do we care?
       + our output routine inserts more whitespace before endstream,
       may need fixing if so. */
    if(s.empty())
      error = "End of input during reading stream data";
  }
  if(std::holds_alternative<Null>(o.contents))
    o = {Numeric{(long)contents.length()}};
  return {Stream{std::move(dict), std::move(contents), std::move(error)}};
}

/***** Top level object parsing *****/

bool skipBrokenObject(TokenStream& ts) {
  ts.reset();
  std::streambuf& stream = ts.stream();
  const std::string sep = "endobj";
  for(std::string s = readToNL(stream); !s.empty(); s = readToNL(stream)) {
    if(auto off = s.find(sep); off != std::string::npos) {
      if(off + sep.length() == s.length()) // separator at end of line: OK
        return true;
      stream.pubseekoff(- s.length() + off + sep.length(), std::ios_base::cur);
      ts.reset();
      if(char after = std::streambuf::traits_type::to_char_type(stream.sgetc());
          charType(after) != CharType::regular)
        return true;
    }
  }
  return false;
}

TopLevelObject parseNamedObject(TokenStream& ts) {
  Numeric num{ts.read()};
  if(!num.uintegral())
    return {Invalid{ts, "Misshaped named object header (gen)"}};
  Numeric gen{ts.read()};
  if(!gen.uintegral())
    return {Invalid{ts, "Misshaped named object header (gen)"}};
  if(ts.read() != "obj")
    return {Invalid{ts, "Misshaped named object header (obj)"}};
  Object contents = readObject(ts);
  std::string error{};
  if(std::string s = ts.read(); s != "endobj") {
    if(s.empty())
      error = "End of input where endobj expected";
    else {
      error = "endobj not found" + fPos(ts);
      bool success = skipBrokenObject(ts);
      if(success)
        error.append(", continuing" + fPos(ts, true));
      else
        error.append(", not found until end of input");
    }
  }
  return {NamedObject{num.val_ulong(), gen.val_ulong(), std::move(contents),
    std::move(error)}};
}

TopLevelObject parseXRefTable(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "xref");
  assert(ts.empty());
  std::streambuf& stream = ts.stream();
  skipToNL(stream);
  std::vector<XRefTable::Section> sections{};
  while(true) {
    s = ts.read();
    if(s.empty()) // EOF
      return {Invalid{"End of input while reading xref table"}};
    else if(s == "trailer")
      break;
    Numeric start{s};
    if(!start.uintegral())
      return {Invalid{ts, "Broken xref subsection header (start)"}};
    Numeric count{ts.read()};
    if(!count.uintegral())
      return {Invalid{ts, "Broken xref subsection header (count)"}};
    skipToNL(stream);
    unsigned len = 20 * count.val_ulong();
    s.resize(len);
    if(stream.sgetn(s.data(), len) < len)
      return {Invalid{"End of input while reading xref table"}};
    sections.push_back({start.val_ulong(), count.val_ulong(), std::move(s)});
  }
  Object trailer = readObject(ts);
  //ts >> trailer;
  return {XRefTable{std::move(sections), std::move(trailer)}};
}

TopLevelObject parseStartXRef(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "startxref");
  Numeric num{ts.read()};
  if(!num.uintegral())
    return {Invalid{ts, "Broken startxref"}};
  return {StartXRef{num.val_ulong()}};
}


Object readObject(TokenStream& ts) {
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
    ts.consume();
    return {Invalid{ts, "Garbage or unexpected token"}};
  }
}

TopLevelObject readTopLevelObject(TokenStream& ts) {
  std::string t = ts.peek();
  if(t == "") // EOF
    return {Invalid{}};
  else if(Numeric{t}.uintegral())
    return parseNamedObject(ts);
  else if(t == "xref")
    return parseXRefTable(ts);
  else if(t == "startxref")
    return parseStartXRef(ts);
  else {
    std::string error = "Garbage or unexpected token" + fPos(ts);
    bool success = skipBrokenObject(ts);
    if(success)
      error.append(", continuing" + fPos(ts, true));
    else
      error.append(", no recovery until end of input");
    return {Invalid{std::move(error)}};
  }
}

} //namespace pdf
