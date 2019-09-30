#include <cstdlib>
#include <optional>
#include <cstring>
#include <cstdio>
#include <type_traits>

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

void skipToNL(std::istream& is) {
  char c;
  while(is.get(c))
    if(c == '\n' || c == '\r')
      break;
}

std::string readToNL(std::istream& is) {
  std::string s{};
  char c;
  while(is.get(c)) {
    s.push_back(c);
    if(c == '\n' || c == '\r')
      break;
  }
  return s;
}

std::string fPos(TokenStream& ts) {
  // Does not give nonsense on errors (like tellg goes)
  ts.reset();
  std::size_t pos = ts.istream().rdbuf()
      ->pubseekoff(0, std::ios_base::cur, std::ios_base::in);
  char buf[100];
  std::snprintf(buf, 100, " (near %zu)", pos);
  return {buf};
}

void print_offset(std::ostream& os, unsigned off, const std::string& text) {
  os << std::string(2*off, ' ') << text;
}


/***** Implementation of TokenStream *****/

std::string TokenStream::underflow() {
  is >> std::ws;
  char c = is.get();
  if(!is)
    return "";
  switch(charType(c)) {
    case CharType::delim:
      if(c == '%') {
        skipToNL(is);
        return underflow();
      } else if(c == '<' || c == '>') {
        if(is.peek() == c) {
          is.get();
          return {c, c};
        }
      }
      // All other cases
      return {c};
    case CharType::regular:
      {
        std::string s{c};
        while(charType(c = is.get()) == CharType::regular)
          s.push_back(c);
        is.unget();
        return s;
      }
    case CharType::ws:
    default:
      return "";
  }
}


/***** Implementation of PDF object classes *****/

void Null::dump(std::ostream& os, unsigned off) const {
  print_offset(os, off, "null");
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
    return {Invalid{"/ not followed by a proper name" + fPos(ts)}};
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
  std::istream& is = ts.istream();
  std::string ret{};
  std::string error{};
  unsigned parens = 0;
  while(true) {
    char c;
    if(!is.get(c)) {
      error = "EOF while reading string" + fPos(ts);
      break;
    }
    if(c == ')') {
      if(parens > 0) {
        ret.push_back(c);
        parens--;
      } else
        break;
    } else if(c == '(') {
      ret.push_back(c);
      parens++;
    } else if(c == '\\') {
      if(!is.get(c)) {
        error = "EOF while reading string" + fPos(ts);
        break;
      }
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
            char d = c - '0';
            c = is.peek();
            if(c >= '0' && c <= '7') {
              is.get();
              d = d*8 + (c - '0');
              c = is.peek();
              if(c >= '0' && c <= '7') {
                is.get();
                d = d*8 + (c - '0');
              }
            }
            ret.push_back(d);
          }
          break;
        default:
          error = "Invalid characters in string" + fPos(ts);
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
  std::istream& is = ts.istream();
  std::string ret{};
  std::string error{};
  unsigned odd = 0;
  char d = 0;
  while(is) {
    char c;
    is.get(c);
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
      error = "Invalid character in hex string" + fPos(ts);
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
      error = "Error reading array element" + fPos(ts);
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
      error = "Error reading key" + fPos(ts);
      break;
    }
    if(!std::holds_alternative<Name>(oKey.contents)) {
      error = "Key not a name" + fPos(ts);
      break;
    }
    std::string name = std::get<Name>(oKey.contents).val;
    Object oVal = readObject(ts);
    bool failed = oVal.failed();
    tmp[name] = std::move(oVal);
    if(failed) {
      error = "Error reading value" + fPos(ts);
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
  std::istream& is = ts.istream();
  skipToNL(is);
  Object& o = dict.val["Length"];
  std::string contents{};
  std::string error{};
  if(std::holds_alternative<Numeric>(o.contents)) {
    unsigned len = std::get<Numeric>(o.contents).val_long();
    contents.resize(len);
    is.read(contents.data(), len);
    if(ts.read() != "endstream")
      error = "endstream not found where expected" + fPos(ts);
  } else {
    while(is) {
      s = readToNL(is);
      /* We can't rely on this being the only thing on a line, especially
         if the file is possibly broken anyway. */
      char sep[] = "endstream";
      if(auto off = s.find(sep); off != std::string::npos) {
        contents.append(s.data(), off);
        auto pos = (std::size_t)is.tellg() - s.length() + off;
        is.seekg(pos);
        ts.reset();
        if(ts.read() == sep)
          break;
        else {
          contents.append(sep);
          is.seekg(pos + sizeof(sep) - 1);
          ts.reset();
        }
      } else
        contents.append(s);
    }
    /* TODO: PDF spec says newline before endstream should be ignored
       (but real world examples show it may be omitted). Do we care?
       + our output routine inserts more whitespace before endstream,
       may need fixing if so. */
    if(!is)
      error = "Reached end of file" + fPos(ts);
  }
  if(std::holds_alternative<Null>(o.contents))
    o = {Numeric{(long)contents.length()}};
  return {Stream{std::move(dict), std::move(contents), std::move(error)}};
}

TopLevelObject parseNamedObject(TokenStream& ts) {
  Numeric num{ts.read()};
  if(!num.uintegral())
    return {Invalid{"Misshaped named object header (gen)" + fPos(ts)}};
  Numeric gen{ts.read()};
  if(!gen.uintegral())
    return {Invalid{"Misshaped named object header (gen)" + fPos(ts)}};
  if(ts.read() != "obj")
    return {Invalid{"Misshaped named object header (obj)" + fPos(ts)}};
  Object contents = readObject(ts);
  if(ts.read() != "endobj")
    return {NamedObject{num.val_ulong(), gen.val_ulong(), std::move(contents), "endobj not found" + fPos(ts)}};
  return {NamedObject{num.val_ulong(), gen.val_ulong(), std::move(contents)}};
}

TopLevelObject parseXRefTable(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "xref");
  assert(ts.empty());
  std::istream& is = ts.istream();
  skipToNL(is);
  std::vector<XRefTable::Section> sections{};
  while(is) {
    s = ts.read();
    if(s == "trailer")
      break;
    Numeric start{s};
    if(!start.uintegral())
      return {Invalid{"Broken xref subsection header (start)" + fPos(ts)}};
    Numeric count{ts.read()};
    if(!count.uintegral())
      return {Invalid{"Broken xref subsection header (count)" + fPos(ts)}};
    skipToNL(is);
    s.resize(20*count.val_ulong());
    is.read(s.data(), 20*count.val_ulong());
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
    return {Invalid{"Broken startxref" + fPos(ts)}};
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
  else if(t == "true" || t == "false") {
    ts.consume();
    return {Boolean{t == "true"}};
  } else if(Numeric n1{t}; n1.valid()) {
    ts.consume();
    return parseNumberIndir(ts, std::move(n1));
  } else {
    ts.consume();
    return {Invalid{"Unexpected keyword: " + t + fPos(ts)}};
  }
}

TopLevelObject readTopLevelObject(TokenStream& ts) {
  std::string t = ts.peek();
  if(t == "")
    return {Invalid{}};
  else if(Numeric{t}.uintegral())
    return parseNamedObject(ts);
  else if(t == "xref")
    return parseXRefTable(ts);
  else if(t == "startxref")
    return parseStartXRef(ts);
  else {
    ts.consume();
    return {Invalid{"Unexpected token: " + t + fPos(ts)}};
  }
}


std::istream::pos_type skipBrokenObject(TokenStream& ts) {
  ts.reset();
  std::istream& is = ts.istream();
  while(is) {
    std::string s = pdf::readToNL(is);
    /* We can't rely on this being the only thing on a line, especially
       if the file is possibly broken anyway. */
    const std::string sep = "endobj";
    if(auto off = s.find(sep); off != std::string::npos) {
      if(off + sep.length() == s.length()) // separator at end of line: OK
        return is.tellg();
      auto pos = (std::size_t)is.tellg() - s.length() + off + sep.length();
      is.seekg(pos);
      ts.reset();
      if(char after = is.peek(); charType(after) != CharType::regular)
        return is.tellg();
    }
  }
  // End of file
  return std::istream::pos_type(-1);
}

} //namespace pdf
