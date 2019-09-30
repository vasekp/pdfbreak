#ifndef PDF_H
#define PDF_H

#include <stack>
#include <string>
#include <istream>
#include <ostream>
#include <cstdlib>
#include <optional>
#include <variant>
#include <vector>
#include <map>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <type_traits>

namespace {

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


void skipToNL(std::istream& is);

class TokenStream {
  std::istream& is;
  std::stack<std::string> stack;

  public:
  TokenStream(std::istream& stream_) : is(stream_), stack() { }

  std::string read() {
    if(!stack.empty()) {
      auto ret = stack.top();
      stack.pop();
      return ret;
    } else
      return underflow();
  }

  void consume() {
    read();
  }

  void unread(std::string t) {
    stack.push(std::move(t));
  }

  std::string peek() {
    if(stack.empty())
      stack.push(underflow());
    return stack.top();
  }

  bool empty() const {
    return stack.empty();
  }

  operator bool() const {
    return is.good();
  }

  void clear() {
    is.clear();
    stack = {};
  }

  std::istream& istream() {
    return is;
  }

  std::istream::pos_type tellg() const {
    return is.rdbuf()->pubseekoff(0, std::ios_base::cur, std::ios_base::in);
  }

  private:
  std::string underflow() {
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
        } else
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
};

std::string fPos(const TokenStream& ts) {
  char buf[100];
  std::snprintf(buf, 100, " (near %zu)", (std::size_t)ts.tellg());
  return {buf};
}


struct Object;

void print_offset(std::ostream& os, unsigned off, const std::string& text) {
  os << std::string(2*off, ' ') << text;
}

struct Null {
  void dump(std::ostream& os, unsigned off) const {
    print_offset(os, off, "null");
  }
  bool failed() const { return false; }
};

struct Boolean {
  bool val;
  void dump(std::ostream& os, unsigned off) const {
    print_offset(os, off, val ? "true" : "false");
  }
  bool failed() const { return false; }
};

class Numeric {
  long val_s;
  int dp;

public:
  Numeric(long val) : val_s(val), dp(0) { }

  Numeric(std::string str) : val_s(0), dp(-1) /* default = fail state */ {
    if(str.empty())
      return;
    auto off = str.find('.');
    char *last;
    int dp_;
    if(off != std::string::npos) {
      str.erase(off, 1);
      dp_ = str.length() - off;
    } else
      dp_ = 0;
    int res = (int)strtol(str.data(), &last, 10);
    if(last != str.data() + str.length())
      return;
    else {
      val_s = res;
      dp = dp_;
    }
  }

  bool integral() const { return dp == 0; }
  
  bool uintegral() const { return integral() && val_s >= 0; }

  bool failed() const { return dp < 0; }

  bool valid() const { return !failed(); }

  long val_long() const {
    assert(integral());
    long ret = val_s;
    int d = dp;
    while(d > 0) {
      ret /= 10;
      d -= 1;
    }
    return ret;
  }

  unsigned long val_ulong() const {
    assert(uintegral());
    unsigned long ret = val_s;
    int d = dp;
    while(d > 0) {
      ret /= 10;
      d -= 1;
    }
    return ret;
  }

  void dump(std::ostream& os, unsigned off) const {
    assert(!failed());
    char buf[20];
    std::snprintf(buf, 20, "%0*li", dp + (val_s < 0 ? 1 : 0) + 1, val_s);
    std::string str{buf};
    if(dp > 0)
      str.insert(str.length() - dp, 1, '.');
    print_offset(os, off, str);
  }
};

struct String {
  std::string val;
  bool hex;
  std::string error;

  void dump(std::ostream& os, unsigned off) const {
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

  bool failed() const { return !error.empty(); }
};

struct Name {
  std::string val;
  void dump(std::ostream& os, unsigned off) const {
    print_offset(os, off, "/" + val);
  }
  bool failed() const { return false; }
};

struct Array {
  std::vector<Object> val;
  std::string error = "";
  void dump(std::ostream& os, unsigned off) const;
  bool failed() const { return !error.empty(); }
};

struct Dictionary {
  std::map<std::string, Object> val;
  std::string error = "";
  void dump(std::ostream& os, unsigned off) const;
  bool failed() const { return !error.empty(); }
};

struct Stream {
  Dictionary dict;
  std::string data;
  std::string error = "";
  void dump(std::ostream& os, unsigned off) const {
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
  bool failed() const { return dict.failed() || !error.empty(); }
};

struct Indirect {
  unsigned long num;
  unsigned long gen;
  void dump(std::ostream& os, unsigned off) const {
    print_offset(os, off, "");
    os << num << ' ' << gen << " R";
  }
  bool failed() const { return false; }
};

struct Invalid {
  std::string error;
  void dump(std::ostream& os, unsigned off) const {
    print_offset(os, off, "null");
    os << '\n';
    print_offset(os, off, "% !!! " + error);
  }
  const std::string& get_error() const {
    return error;
  }
  bool failed() const { return true; }
};

struct Object {
  std::variant<
    Null,
    Boolean,
    Numeric,
    String,
    Name,
    Array,
    Dictionary,
    Stream,
    Indirect,
    Invalid> contents;
  
  void dump(std::ostream& os, unsigned off) const {
    std::visit([&os, off](auto&& arg) { arg.dump(os, off); }, contents);
  }

  bool failed() const {
    return std::visit([](auto&& arg) { return arg.failed(); }, contents);
  }
};

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

bool operator>> (TokenStream& ts, Object&);

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

Object readName(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "/");
  s = ts.read();
  if(charType(s[0]) == CharType::regular)
    return {Name{std::move(s)}};
  else
    return {Invalid{"/ not followed by a proper name" + fPos(ts)}};
}

Object readStream(TokenStream& ts, Dictionary&& dict) {
  std::istream& is = ts.istream();
  std::string s = ts.read();
  assert(s == "stream");
  assert(ts.empty());
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
        ts.clear();
        if(ts.read() == sep)
          break;
        else {
          contents.append(sep);
          is.seekg(pos + sizeof(sep) - 1);
          ts.clear();
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

Object readArray(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "[");
  std::vector<Object> array{};
  std::string error{};
  while(ts.peek() != "]") {
    Object o;
    if(!(ts >> o)) {
      error = "Unable to read object" + fPos(ts);
      break;
    }
    array.push_back(std::move(o));
  }
  ts.consume();
  return {Array{std::move(array), std::move(error)}};
}

Object readDict(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "<<");
  std::map<std::string, Object> tmp{};
  std::string error{};
  while(ts.peek() != ">>") {
    Object o1, o2;
    if(!(ts >> o1) || !(ts >> o2)) {
      error = "Unable to read object" + fPos(ts);
      break;
    }
    if(!std::holds_alternative<Name>(o1.contents)) {
      error = "Key not a name" + fPos(ts);
      break;
    }
    std::string name = std::get<Name>(o1.contents).val;
    tmp[name] = std::move(o2);
  }
  ts.consume();
  Dictionary dict{std::move(tmp), std::move(error)};
  if(ts.peek() == "stream")
    return readStream(ts, std::move(dict));
  else
    return {std::move(dict)};
}

Object readStringLiteral(TokenStream& ts) {
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

Object readStringHex(TokenStream& ts) {
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

bool operator>> (TokenStream& ts, Object& obj) {
  auto t = ts.peek();
  if(t == "")
    return false;
  else if(t == "/")
    obj = readName(ts);
  else if(t == "(")
    obj = readStringLiteral(ts);
  else if(t == "<")
    obj = readStringHex(ts);
  else if(t == "<<")
    obj = readDict(ts);
  else if(t == "[")
    obj = readArray(ts);
  else if(t == "true" || t == "false") {
    obj = {Boolean{t == "true"}};
    ts.consume();
  } else if(Numeric n1{t}; n1.valid()) {
    ts.consume();
    auto t2 = ts.read();
    Numeric n2{t2};
    if(n1.uintegral() && n2.uintegral()) {
      auto t3 = ts.read();
      if(t3 == "R") {
        obj = {Indirect{n1.val_ulong(), n2.val_ulong()}};
        return true;
      }
      ts.unread(t3);
    }
    ts.unread(t2);
    obj = {std::move(n1)};
  } else {
    ts.consume();
    obj = {Invalid{"Unexpected keyword: " + t + fPos(ts)}};
  }
  return true;
}

[[maybe_unused]] std::ostream& operator<< (std::ostream& os, const Object& obj) {
  obj.dump(os, 0);
  return os;
}


struct NamedObject {
  unsigned long num;
  unsigned long gen;
  Object contents;
  std::string error = "";
  void dump(std::ostream& os, unsigned off) const {
    print_offset(os, off, "");
    os << num << ' ' << gen << " obj\n";
    contents.dump(os, off+1);
    os << '\n';
    if(!error.empty()) {
      print_offset(os, off, "% !!! " + error + "\n");
    }
    print_offset(os, off, "endobj");
  }
  bool failed() const { return !error.empty(); }
};

struct XRefTableSection {
  unsigned long start;
  unsigned long count;
  std::string data;
};

struct XRefTable {
  std::vector<XRefTableSection> table;
  Object trailer;
  void dump(std::ostream& os, unsigned off) const {
    print_offset(os, off, "xref\n");
    for(const auto& section : table)
      os << section.start << ' ' << section.count << '\n'
        << section.data /* << '\n' */;
    print_offset(os, off, "trailer\n");
    trailer.dump(os, off+1);
  }
  bool failed() const { return false; }
};

struct StartXRef {
  unsigned long val;
  void dump(std::ostream& os, unsigned) const {
    os << "startxref\n" << val << "\n%%EOF";
  }
  bool failed() const { return false; }
};

struct TopLevelObject {
  std::variant<
    NamedObject,
    XRefTable,
    StartXRef,
    Invalid> contents;

  void dump(std::ostream& os, unsigned off) const {
    std::visit([&os, off](auto&& arg) { arg.dump(os, off); }, contents);
  }

  bool failed() const {
    return std::visit([](auto&& arg) { return arg.failed(); }, contents);
  }
};

TopLevelObject readNamedObject(TokenStream& ts) {
  Numeric num{ts.read()};
  if(!num.uintegral())
    return {Invalid{"Misshaped named object header (gen)" + fPos(ts)}};
  Numeric gen{ts.read()};
  if(!gen.uintegral())
    return {Invalid{"Misshaped named object header (gen)" + fPos(ts)}};
  if(ts.read() != "obj")
    return {Invalid{"Misshaped named object header (obj)" + fPos(ts)}};
  Object contents;
  if(!(ts >> contents))
    return {Invalid{"Unable to read contents" + fPos(ts)}};
  if(ts.read() != "endobj")
    return {NamedObject{num.val_ulong(), gen.val_ulong(), std::move(contents), "endobj not found" + fPos(ts)}};
  return {NamedObject{num.val_ulong(), gen.val_ulong(), std::move(contents)}};
}

TopLevelObject readXRefTable(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "xref");
  std::istream& is = ts.istream();
  skipToNL(is);
  std::vector<XRefTableSection> sections{};
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
  Object trailer;
  ts >> trailer;
  return {XRefTable{std::move(sections), trailer}};
}

TopLevelObject readStartXRef(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "startxref");
  Numeric num{ts.read()};
  if(!num.uintegral())
    return {Invalid{"Broken startxref" + fPos(ts)}};
  return {StartXRef{num.val_ulong()}};
}

bool operator>> (TokenStream& ts, TopLevelObject& obj) {
  auto t = ts.peek();
  if(t == "")
    return false;
  else if(Numeric{t}.uintegral())
    obj = readNamedObject(ts);
  else if(t == "xref")
    obj = readXRefTable(ts);
  else if(t == "startxref")
    obj = readStartXRef(ts);
  else {
    ts.consume();
    obj = {Invalid{"Unexpected token: " + t + fPos(ts)}};
  }
  return true;
}

[[maybe_unused]] std::ostream& operator<< (std::ostream& os, const TopLevelObject& obj) {
  obj.dump(os, 0);
  return os;
}

}

#endif
