#ifndef PDF_H
#define PDF_H

#include <istream>
#include <ostream>
#include <stack>
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <cassert>

namespace pdf {

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
    return !stack.empty() || is.good();
  }

  std::istream& istream() const {
    return is;
  }

  void reset() {
    stack = {};
    is.clear();
  }

  private:
  std::string underflow();
};


struct Object;


struct Null {
  bool failed() const { return false; }
  void dump(std::ostream& os, unsigned off) const;
};

struct Boolean {
  bool val;
  
  bool failed() const { return false; }
  void dump(std::ostream& os, unsigned off) const;
};

class Numeric {
  long val_s;
  int dp;

public:
  Numeric(long val) : val_s(val), dp(0) { }
  Numeric(std::string str);

  bool integral() const { return dp == 0; }
  bool uintegral() const { return integral() && val_s >= 0; }
  bool failed() const { return dp < 0; }
  bool valid() const { return !failed(); }

  long val_long() const;
  unsigned long val_ulong() const;
  void dump(std::ostream& os, unsigned off) const;
};

struct String {
  std::string val;
  bool hex;
  std::string error;

  bool failed() const { return !error.empty(); }
  void dump(std::ostream& os, unsigned off) const;
};

struct Name {
  std::string val;
  bool failed() const { return false; }
  void dump(std::ostream& os, unsigned off) const;
};

struct Array {
  std::vector<Object> val;
  std::string error;
  bool failed() const { return !error.empty(); }
  void dump(std::ostream& os, unsigned off) const;
};

struct Dictionary {
  std::map<std::string, Object> val;
  std::string error;
  bool failed() const { return !error.empty(); }
  void dump(std::ostream& os, unsigned off) const;
};

struct Stream {
  Dictionary dict;
  std::string data;
  std::string error;
  bool failed() const { return dict.failed() || !error.empty(); }
  void dump(std::ostream& os, unsigned off) const;
};

struct Indirect {
  unsigned long num;
  unsigned long gen;
  bool failed() const { return false; }
  void dump(std::ostream& os, unsigned off) const;
};

class Invalid {
  std::string error;

  public:
  Invalid() : error{} { }
  Invalid(std::string&& error_) : error(std::move(error_)) { }
  Invalid(const TokenStream&, std::string&&);

  const std::string& get_error() const { return error; }
  bool failed() const { return true; }
  void dump(std::ostream& os, unsigned off) const;
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


struct NamedObject {
  unsigned long num;
  unsigned long gen;
  Object contents;
  std::string error = "";
  bool failed() const { return contents.failed() || !error.empty(); }
  void dump(std::ostream& os, unsigned off) const;
};

struct XRefTable {
  struct Section {
    unsigned long start;
    unsigned long count;
    std::string data;
  };

  std::vector<Section> table;
  Object trailer;
  bool failed() const { return false; }
  void dump(std::ostream& os, unsigned off) const;
};

struct StartXRef {
  unsigned long val;
  bool failed() const { return false; }
  void dump(std::ostream& os, unsigned off) const;
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

Object readObject(TokenStream&);
TopLevelObject readTopLevelObject(TokenStream&);

inline std::ostream& operator<< (std::ostream& os, const Object& obj) {
  obj.dump(os, 0);
  return os;
}

inline std::ostream& operator<< (std::ostream& os, const TopLevelObject& obj) {
  obj.dump(os, 0);
  return os;
}

} // namespace pdf

#endif
