#ifndef PDF_H
#define PDF_H

#include <stack>
#include <string>
#include <optional>
#include <cstdlib>

namespace {

void check(bool cond) {
  if(!cond)
    throw 1;
}


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


struct Object {
};

bool operator>> (TokenStream& ts, Object&);

/*struct NamedObject {
  int num;
  int gen;
  Object contents;

  static constexpr int invalid = -1;

  NamedObject() : num(invalid), gen(invalid), contents() { }

  operator bool() { return num != invalid; }
};*/

void skipName(TokenStream& ts) {
  std::clog << "[name]\n";
  check(ts.read() == "/");
  ts.consume();
  //check(t.type == TokenType::regular);
}

void skipStream(TokenStream& ts) {
  std::clog << "[stream]\n";
  std::istream& is = ts.istream();
  std::string s;
  check(ts.read() == "stream");
  check(ts.empty());
  while(std::getline(is, s))
    if(!std::strncmp(s.data(), "endstream", 9))
      break;
}

void skipDict(TokenStream& ts) {
  std::clog << "[dict]\n";
  check(ts.read() == "<<");
  while(ts.peek() != ">>") {
    Object o;
    if(!(ts >> o)) return;
    if(!(ts >> o)) return;
  }
  ts.consume();
  if(ts.peek() == "stream") {
    skipStream(ts);
  }
}

void skipString(TokenStream& ts) {
  std::clog << "[string]\n";
  bool hex = (ts.read() == "<");
  check(ts.empty());
  char closing = hex ? '>' : ')';
  std::istream& is = ts.istream();
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
}

void skipArray(TokenStream& ts) {
  std::clog << "[array]\n";
  check(ts.read() == "[");
  while(ts.peek() != "]") {
    Object o;
    if(!(ts >> o)) return;
  }
  ts.consume();
}

void skipComment(TokenStream& ts) {
  check(ts.read() == "%");
  check(ts.empty());
  std::istream& is = ts.istream();
  std::string s;
  std::getline(is, s);
}

std::optional<long> toInt(const std::string& s) {
  char *last;
  if(s.empty())
    return {};
  long res = strtol(s.data(), &last, 10);
  if(last != s.data() + s.length())
    return {};
  else
    return {res};
}

bool operator>> (TokenStream& ts, Object&) {
  auto t = ts.peek();
  if(t == "")
    return false;
  else if(t == "/")
    skipName(ts);
  else if(t == "(" || t == "<")
    skipString(ts);
  else if(t == "<<")
    skipDict(ts);
  else if(t == "[")
    skipArray(ts);
  else if(t == "%") {
    skipComment(ts);
    Object o;
    return ts >> o;
  }
  else if(t == "stream")
    skipStream(ts);
  else if(t == "true" || t == "false") {
    std::clog << "[boolean]\n";
    ts.consume();
  } else if(toInt(t)) {
    ts.consume();
    auto t2 = ts.read();
    if(toInt(t2)) {
      auto t3 = ts.read();
      if(t3 == "R") {
        std::clog << "[REF: " << t << ' ' << t2 << "]\n";
        return true;
      } else if(t3 == "obj") {
        std::clog << "[Named: " << t << ' ' << t2 << "]\n";
        return true;
      }
      ts.unread(t3);
    }
    ts.unread(t2);
    std::clog << "[numeric]\n";
  } else {
    std::clog << "Unexpected keyword: " << t << '\n';
    ts.consume();
  }
  return true;
}

}

#endif
