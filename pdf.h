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
        if(c == '<' || c == '>') {
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

struct Numeric {
  double val;
  void dump(std::ostream& os, unsigned off) const {
    char buf[100];
    std::snprintf(buf, 100, val == (long)val ? "%.0f" : "%f", val);
    print_offset(os, off, buf);
  }
  bool failed() const { return false; }
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
  int num;
  int gen;
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
    unsigned len = std::get<Numeric>(o.contents).val;
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
    o = {Numeric{contents.length()}};
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

void skipComment(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "%");
  assert(ts.empty());
  skipToNL(ts.istream());
}

std::optional<double> toFloat(const std::string& s) {
  char *last;
  if(s.empty())
    return {};
  double res = (double)strtod(s.data(), &last);
  if(last != s.data() + s.length())
    return {};
  else
    return {res};
}

std::optional<int> toInt(const std::string& s) {
  char *last;
  if(s.empty())
    return {};
  int res = (int)strtol(s.data(), &last, 10);
  if(last != s.data() + s.length())
    return {};
  else
    return {res};
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
  else if(t == "%") {
    skipComment(ts);
    return ts >> obj;
  } else if(t == "true" || t == "false") {
    obj = {Boolean{t == "true"}};
    ts.consume();
  } else if(toFloat(t)) {
    ts.consume();
    auto t2 = ts.read();
    if(toInt(t) && toInt(t2)) {
      auto t3 = ts.read();
      if(t3 == "R") {
        obj = {Indirect{*toInt(t), *toInt(t2)}};
        return true;
      }
      ts.unread(t3);
    }
    ts.unread(t2);
    obj = {Numeric{*toFloat(t)}};
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
  int num;
  int gen;
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
  int start;
  int count;
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
  int val;
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
  std::string t = ts.read();
  if(!toInt(t))
    return {Invalid{"Misshaped named object header (gen)" + fPos(ts)}};
  int num = *toInt(t);
  t = ts.read();
  if(!toInt(t))
    return {Invalid{"Misshaped named object header (gen)" + fPos(ts)}};
  int gen = *toInt(t);
  if(ts.read() != "obj")
    return {Invalid{"Misshaped named object header (obj)" + fPos(ts)}};
  Object contents;
  if(!(ts >> contents))
    return {Invalid{"Unable to read contents" + fPos(ts)}};
  if(ts.read() != "endobj")
    return {NamedObject{num, gen, std::move(contents), "endobj not found" + fPos(ts)}};
  return {NamedObject{num, gen, std::move(contents)}};
}

TopLevelObject readXRefTable(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "xref");
  std::istream& is = ts.istream();
  skipToNL(is);
  std::vector<XRefTableSection> sections{};
  while(is) {
    int start, count;
    s = ts.read();
    if(s == "trailer")
      break;
    if(!toInt(s))
      return {Invalid{"Broken xref subsection header (start)" + fPos(ts)}};
    start = *toInt(s);
    s = ts.read();
    if(!toInt(s))
      return {Invalid{"Broken xref subsection header (count)" + fPos(ts)}};
    count = *toInt(s);
    skipToNL(is);
    s.resize(20*count);
    is.read(s.data(), 20*count);
    sections.push_back({start, count, std::move(s)});
  }
  Object trailer;
  ts >> trailer;
  return {XRefTable{std::move(sections), trailer}};
}

TopLevelObject readStartXRef(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "startxref");
  s = ts.read();
  if(!toInt(s))
    return {Invalid{"Broken startxref" + fPos(ts)}};
  return {StartXRef{*toInt(s)}};
}

bool operator>> (TokenStream& ts, TopLevelObject& obj) {
  auto t = ts.peek();
  if(t == "")
    return false;
  else if(t == "%") {
    skipComment(ts);
    return ts >> obj;
  } else if(toInt(t))
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
