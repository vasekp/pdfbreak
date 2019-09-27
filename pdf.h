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

  bool empty() {
    return stack.empty();
  }

  operator bool() {
    return is.good();
  }

  std::istream& istream() { return is; }

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


struct Object;

void print_offset(std::ostream& os, unsigned off, const std::string& text) {
  os << std::string(2*off, ' ') << text;
}

struct Null {
  void dump(std::ostream& os, unsigned off) const {
    print_offset(os, off, "null");
  }
};

struct Boolean {
  bool val;
  void dump(std::ostream& os, unsigned off) const {
    print_offset(os, off, val?"true":"false");
  }
};

struct Numeric {
  double val;
  void dump(std::ostream& os, unsigned off) const {
    char buf[100];
    std::snprintf(buf, 100, val == (long)val ? "%.0f" : "%f", val);
    print_offset(os, off, buf);
  }
};

struct String {
  std::string val;
  bool hex;
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
        if(c >= 32 /*&& c <= 127 always true*/ && c != '(' && c != ')' && c != '\\')
          os << c;
        else {
          char buf[5];
          sprintf(buf, "\\%03o", (unsigned char)c);
          os << buf;
        }
      }
      os << ')';
    }
  }
};

struct Name {
  std::string val;
  void dump(std::ostream& os, unsigned off) const {
    print_offset(os, off, "/" + val);
  }
};

struct Array {
  std::vector<Object> val;
  void dump(std::ostream& os, unsigned off) const;
};

struct Dictionary {
  std::map<std::string, Object> val;
  void dump(std::ostream& os, unsigned off) const;
};

struct Stream {
  Dictionary dict;
  std::string data;
  void dump(std::ostream& os, unsigned off) const {
    dict.dump(os, off);
    os << '\n';
    print_offset(os, off, "stream\n");
    os.write(data.data(), data.length());
    os << '\n';
    print_offset(os, off, "endstream");
  }
};

struct Indirect {
  int num;
  int gen;
  void dump(std::ostream& os, unsigned off) const {
    print_offset(os, off, "");
    os << num << ' ' << gen << " R";
  }
};

struct Invalid {
  std::string error;
  void dump(std::ostream& os, unsigned off) const {
    print_offset(os, off, "!!! " + error);
  }
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
};

void Array::dump(std::ostream& os, unsigned off) const {
  print_offset(os, off, "[\n");
  for(const auto& o : val) {
    o.dump(os, off+1);
    os << '\n';
  }
  print_offset(os, off, "]");
}

void Dictionary::dump(std::ostream& os, unsigned off) const {
  print_offset(os, off, "<<\n");
  for(const auto& [k, v] : val) {
    print_offset(os, off+1, "/" + k + "\n");
    v.dump(os, off+2);
    os << '\n';
  }
  print_offset(os, off, ">>");
}

bool operator>> (TokenStream& ts, Object&);

Object readName(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "/");
  s = ts.read();
  if(charType(s[0]) == CharType::regular)
    return {Name{std::move(s)}};
  else
    return {Invalid{"/ not followed by a proper name"}};
}

Object readStream(TokenStream& ts, Dictionary&& dict) {
  std::istream& is = ts.istream();
  std::string s = ts.read();
  assert(s == "stream");
  assert(ts.empty());
  std::getline(is, s);
  Object o = dict.val["Length"];
  std::string contents{};
  if(std::holds_alternative<Numeric>(o.contents)) {
    unsigned len = std::get<Numeric>(o.contents).val;
    contents.resize(len);
    is.read(contents.data(), len);
    std::getline(is, s);
    if(ts.read() != "endstream")
      return {Invalid{"Malformed stream (endstream)"}};
  } else {
    while(std::getline(is, s))
      if(std::strncmp(s.data() + s.length() - 9, "endstream", 9)) {
        contents.append(s);
        contents.push_back('\n');
      } else {
        contents.append(s.data(), s.length() - 9);
        break;
      }
  }
  return {Stream{std::move(dict), std::move(contents)}};
}

Object readArray(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "[");
  std::vector<Object> array{};
  while(ts.peek() != "]") {
    Object o;
    if(!(ts >> o))
      return {Invalid{"Array: unable to read object"}};
    array.push_back(std::move(o));
  }
  ts.consume();
  return {Array{std::move(array)}};
}

Object readDict(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "<<");
  std::map<std::string, Object> dict{};
  while(ts.peek() != ">>") {
    Object o1, o2;
    if(!(ts >> o1) || !(ts >> o2))
      return {Invalid{"Dictionary: unable to read key-value"}};
    if(!std::holds_alternative<Name>(o1.contents))
      return {Invalid{"Dictionary: key not a name"}};
    std::string name = std::get<Name>(o1.contents).val;
    dict[name] = std::move(o2);
  }
  ts.consume();
  if(ts.peek() == "stream")
    return readStream(ts, {std::move(dict)});
  else
    return {Dictionary{std::move(dict)}};
}

Object readStringLiteral(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "(");
  assert(ts.empty());
  std::istream& is = ts.istream();
  std::string ret{};
  unsigned parens = 0;
  while(is) {
    char c;
    is >> c;
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
      is >> c;
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
          return {Invalid{"Invalid characters in string"}};
      }
    } else
      ret.push_back(c);
  }
  return {String{std::move(ret), false}};
}

Object readStringHex(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "<");
  assert(ts.empty());
  std::istream& is = ts.istream();
  std::string ret{};
  unsigned odd = 0;
  char d = 0;
  while(is) {
    char c;
    is >> c;
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
    else
      return {Invalid{"Invalid character in hex string"}};
  }
  return {String{std::move(ret), true}};
}

void skipComment(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "%");
  assert(ts.empty());
  std::istream& is = ts.istream();
  std::getline(is, s);
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
    obj = {Invalid{"Unexpected keyword: " + t}};
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
  void dump(std::ostream& os, unsigned off) const {
    print_offset(os, off, "");
    os << num << ' ' << gen << " obj\n";
    contents.dump(os, off+1);
    os << '\n';
    print_offset(os, off, "endobj");
  }
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
};

struct StartXRef {
  int val;
  void dump(std::ostream& os, unsigned) const {
    os << "startxref\n" << val << "\n%%EOF";
  }
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
};

TopLevelObject readNamedObject(TokenStream& ts) {
  std::string t = ts.read();
  if(!toInt(t))
    return {Invalid{"Misshaped named object header (gen)"}};
  int num = *toInt(t);
  t = ts.read();
  if(!toInt(t))
    return {Invalid{"Misshaped named object header (gen)"}};
  int gen = *toInt(t);
  if(ts.read() != "obj")
    return {Invalid{"Misshaped named object header (obj)"}};
  Object contents;
  if(!(ts >> contents))
    return {Invalid{"Misshaped named object header (contents)"}};
  if(ts.read() != "endobj")
    return {Invalid{"Misshaped named object header (endobj)"}};
  return {NamedObject{num, gen, std::move(contents)}};
}

TopLevelObject readXRefTable(TokenStream& ts) {
  std::string s = ts.read();
  assert(s == "xref");
  std::istream& is = ts.istream();
  std::getline(is, s);
  std::vector<XRefTableSection> sections{};
  while(is) {
    int start, count;
    s = ts.read();
    if(s == "trailer")
      break;
    if(!toInt(s))
      return {Invalid{"Broken xref subsection header (start)"}};
    start = *toInt(s);
    s = ts.read();
    if(!toInt(s))
      return {Invalid{"Broken xref subsection header (count)"}};
    count = *toInt(s);
    std::getline(is, s);
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
    return {Invalid{"Broken startxref"}};
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
    obj = {Invalid{"Unexpected keyword: " + t}};
  }
  return true;
}

[[maybe_unused]] std::ostream& operator<< (std::ostream& os, const TopLevelObject& obj) {
  obj.dump(os, 0);
  return os;
}

}

#endif
