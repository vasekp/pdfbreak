#ifndef PDF_H
#define PDF_H

#include <ostream>
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <utility>
#include <optional>

namespace pdf {

namespace internal {

template<typename... Ts>
class tagged_union {
  std::variant<Ts...> contents;

public:
  template<typename T>
  tagged_union(const T& t) : contents(t) { }

  template<typename T>
  tagged_union(T&& t) : contents(std::move(t)) { }

  tagged_union(const tagged_union&) = default;
  tagged_union(tagged_union&&) = default;
  tagged_union& operator=(const tagged_union&) = default;
  tagged_union& operator=(tagged_union&&) = default;
  ~tagged_union() = default;

  template<typename T>
  bool is() { return std::holds_alternative<T>(contents); }

  template<typename T>
  T& get() { return std::get<T>(contents); }
  
  void dump(std::ostream& os, unsigned off) const {
    std::visit([&os, off](auto&& arg) { arg.dump(os, off); }, contents);
  }

  bool failed() const {
    return std::visit([](auto&& arg) { return arg.failed(); }, contents);
  }

};

} // namespace internal

/***** PDF object types *****/

class Null;
class Boolean;
class Numeric;
class String;
class Name;
class Array;
class Dictionary;
class Stream;
class Indirect;
class Invalid;

using Object = internal::tagged_union<
    Null,
    Boolean,
    Numeric,
    String,
    Name,
    Array,
    Dictionary,
    Stream,
    Indirect,
    Invalid>;

struct Null {
  bool failed() const { return false; }
  void dump(std::ostream& os, unsigned off) const;
};

class Boolean {
  bool val;
  
public:
  Boolean(bool val_) : val(val_) { }

  bool failed() const { return false; }
  void dump(std::ostream& os, unsigned off) const;

  operator bool() const { return val; }
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

class String {
  std::string val;
  bool hex;
  std::string error;

public:
  template<typename V, typename E>
  String(V&& val_, bool hex_, E&& err_)
    : val(std::forward<V>(val_)), hex(hex_), error(std::forward<E>(err_)) { }

  bool failed() const { return !error.empty(); }
  void dump(std::ostream& os, unsigned off) const;

  operator const std::string&() const { return val; }
};

class Name {
  std::string val;

public:
  template<typename V>
  Name(V&& val_) : val(std::forward<V>(val_)) { }

  bool failed() const { return false; }
  void dump(std::ostream& os, unsigned off) const;

  operator const std::string&() const { return val; }
};

class Array {
  std::vector<Object> val;
  std::string error;

public:
  template<typename V, typename E>
  Array(V&& val_, E&& err_) : val(std::forward<V>(val_)), error(std::forward<E>(err_)) { }

  bool failed() const { return !error.empty(); }
  void dump(std::ostream& os, unsigned off) const;
};

class Dictionary {
  std::map<std::string, Object> val;
  std::string error;

public:
  template<typename V, typename E>
  Dictionary(V&& val_, E&& err_) : val(std::forward<V>(val_)), error(std::forward<E>(err_)) { }
  
  bool failed() const { return !error.empty(); }
  void dump(std::ostream& os, unsigned off) const;
  std::optional<Object> lookup(const std::string& key) const;
};

class Stream {
  Dictionary dict;
  std::string data;
  std::string error;

public:
  template<typename D, typename V, typename E>
  Stream(D&& dict_, V&& data_, E&& err_)
    : dict(std::forward<D>(dict_)),
      data(std::forward<V>(data_)),
      error(std::forward<E>(err_)) { }

  bool failed() const { return dict.failed() || !error.empty(); }
  void dump(std::ostream& os, unsigned off) const;
};

class Indirect {
  unsigned long num;
  unsigned long gen;

public:
  Indirect(unsigned long num_, unsigned long gen_) : num(num_), gen(gen_) { }

  bool failed() const { return false; }
  void dump(std::ostream& os, unsigned off) const;
};

class Invalid {
  std::string error;

public:
  Invalid() : error{} { }
  Invalid(std::string&& error_) : error(std::move(error_)) { }
  Invalid(std::string&& error_, std::size_t offset);

  const std::string& get_error() const { return error; }
  bool failed() const { return true; }
  void dump(std::ostream& os, unsigned off) const;
};


/***** "Top-level" objects *****/

class NamedObject;
class XRefTable;
class StartXRef;

using TopLevelObject = internal::tagged_union<
    NamedObject,
    XRefTable,
    StartXRef,
    Invalid>;

class NamedObject {
  unsigned long num;
  unsigned long gen;
  Object contents;
  std::string error;

public:
  template<typename T>
  NamedObject(unsigned long num_, unsigned long gen_, Object&& contents_, T&& err_)
    : num(num_), gen(gen_), contents(std::move(contents_)), error(std::forward<T>(err_)) { }
  NamedObject(unsigned long num_, unsigned long gen_, Object&& contents_)
    : NamedObject(num_, gen_, std::move(contents_), "") { }

  bool failed() const { return contents.failed() || !error.empty(); }
  void dump(std::ostream& os, unsigned off) const;

  std::pair<unsigned long, unsigned long> numgen() const { return {num, gen}; }
};

class XRefTable {
public:
  struct Section {
    unsigned long start;
    unsigned long count;
    std::string data;
  };

private:
  std::vector<Section> _table;
  Object _trailer;

public:
  XRefTable(std::vector<Section>&& table_, Object&& trailer_)
    : _table(std::move(table_)), _trailer(std::move(trailer_)) { }

  bool failed() const { return false; }
  void dump(std::ostream& os, unsigned off) const;

  const Object& trailer() const { return _trailer; }
};

class StartXRef {
  unsigned long val;

public:
  StartXRef(unsigned long val_) : val(val_) { }

  bool failed() const { return false; }
  void dump(std::ostream& os, unsigned off) const;
};


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
