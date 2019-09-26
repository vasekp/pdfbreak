#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <array>
#include <iterator>
#include <algorithm>
#include <cctype>
#include "zstr.hpp"

enum class TokenType {
  delim,
  regular,
  invalid
};

void check(bool cond) {
  if(!cond)
    throw 1;
}

struct Token {
  TokenType type;
  std::string contents;

  Token(TokenType type_ = TokenType::invalid, const std::string& contents_ = "") : type(type_), contents(contents_) { }

  int toInt() {
    std::size_t pos;
    int ret = std::stoi(contents, &pos);
    check(contents[pos] == '\0');
    return ret;
  }
};

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

bool ispdfnum(char c) {
  return std::isdigit(c) || c == '+' || c == '-' || c == '.';
}

std::istream& operator>> (std::istream& is, Token& tok) {
  is >> std::ws;
  tok = [&is]() -> Token {
    char c = is.peek();
    if(!is)
      return { TokenType::invalid, "" };
    switch(charType(c)) {
      case CharType::delim:
        is.get();
        if(c == '<' || c == '>') {
          if(is.peek() == c) {
            is.get();
            return { TokenType::delim, std::string{c, c} };
          }
        }
        // All other cases
        return { TokenType::delim, std::string{c} };
      case CharType::regular:
        {
          std::ostringstream oss{};
          while(charType(c = is.get()) == CharType::regular)
            oss << c;
          is.unget();
          return { TokenType::regular, oss.str() };
        }
      case CharType::ws:
      default:
        is.get();
        return { TokenType::invalid, "" };
    }
  }();
  return is;
}

std::ostream& operator<< (std::ostream& os, TokenType type) {
  switch(type) {
    case TokenType::delim:
      os << "delim";
      break;
    case TokenType::regular:
      os << "regular";
      break;
    case TokenType::invalid:
      os << "invalid";
      break;
  }
  return os;
}

struct Object {
  std::string contents;

  friend std::istream& operator>> (std::istream& is, Object& obj);
};

struct NamedObject {
  int num;
  int ver;
  Object contents;

  static constexpr int invalid = -1;

  NamedObject() : num(invalid), ver(invalid), contents() { }

  operator bool() { return num != invalid; }

  friend std::istream& operator>> (std::istream& is, NamedObject& obj);
};

void skipName(std::istream& is) {
  std::clog << "[name]\n";
  Token t;
  is >> t >> std::ws;
  check(t.type == TokenType::regular);
}

void skipStream(std::istream& is) {
  std::clog << "[stream]\n";
  std::string s;
  while(std::getline(is, s))
    if(!strncmp(s.data(), "endstream", 9))
      break;
}

void skipDict(std::istream& is) {
  std::clog << "[dict]\n";
  while(is && is.peek() != '>') {
    Object o;
    is >> o >> o;
  }
  Token t;
  is >> t >> std::ws;
  check(t.contents == ">>");
  if(is.peek() == 's') {
    is >> t >> std::ws;
    check(t.contents == "stream");
    skipStream(is);
  }
}

void skipString(std::istream& is, bool hex) {
  std::clog << "[string]\n";
  char closing = hex ? '>' : ')';
  while(is) {
    char c;
    is >> c;
    if(c == closing)
      break;
    if(c == '\\') {
      is >> c;
      switch(c) {
        case 'n': case 'r': case 't': case 'b': case 'f': case '(': case ')': case '\\':
          break;
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
          c = is.peek();
          if(c >= '0' && c <= '7') {
            is.get();
            c = is.peek();
            if(c >= '0' && c <= '7')
              is.get();
          }
          break;
        default:
          throw 1;
      }
    }
  }
  is >> std::ws;
}

void skipArray(std::istream& is) {
  std::clog << "[array]\n";
  while(is && is.peek() != ']') {
    Object o;
    is >> o;
  }
  Token t;
  is >> t >> std::ws;
  check(t.contents == "]");
}

std::istream& operator>> (std::istream& is, Object&) {
  Token t;
  is >> t;
  switch(t.type) {
    case TokenType::delim:
      if(t.contents == "/")
        skipName(is);
      else if(t.contents == "(" || t.contents == "<")
        skipString(is, t.contents == "<");
      else if(t.contents == "<<")
        skipDict(is);
      else if(t.contents == "[")
        skipArray(is);
      else //if(t.contents == "%")
        throw 1;
      break;
    case TokenType::regular:
      if(t.contents == "stream")
        skipStream(is);
      else if(t.contents == "true" || t.contents == "false")
        std::clog << "[boolean]\n";
      else if(t.contents == "R")
        std::clog << "[REF]\n";
      else if(ispdfnum(t.contents[0]))
        std::clog << "[numeric]\n";
      break;
    case TokenType::invalid:
      throw 1;
  };
  return is;
}

std::istream& operator>> (std::istream& is, NamedObject& obj) {
  Token t1, t2, t3;
  is >> t1 >> t2 >> t3 >> std::ws;
  try {
    obj.num = t1.toInt();
    obj.ver = t2.toInt();
  } catch(const std::invalid_argument& e) {
    std::cerr << "Broken object" << "\n";
    obj = {};
  }
  check(t3.contents == "obj");
  is >> obj.contents;
  is >> t1 >> std::ws;
  if(t1.contents != "endobj") {
    std::cerr << "Broken object: read \"" << t1.contents << "\"\n";
  }
  return is;
}


int main(int argc, char* argv[]) {
  if(argc != 2) {
    std::cerr << "Usage: " << argv[0] << " filename.pdf\n";
    return 1;
  }

  std::ifstream infs{argv[1]};
  if(!infs) {
    std::cerr << "Can't open " << argv[1] << " for reading.\n";
    return 1;
  }

  try {
    std::string line;
    std::getline(infs, line);
    if(!strncmp(line.data(), "%PDF1.", 6)) {
      std::cerr << "Not a PDF 1.x file.\n";
      return 1;
    }
    std::getline(infs, line);
    if(line[0] != '%') {
      std::cerr << "Not a PDF 1.x file.\n";
      return 1;
    }

    NamedObject obj{};
    while(infs >> obj)
      std::clog << obj.num << ' ' << obj.ver << '\n';
  } catch(const std::ios_base::failure& err) {
    std::cerr << err.what() << '\n';
    return 1;
  }
}
