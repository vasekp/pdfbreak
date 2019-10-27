#ifndef PDF_BASE_H
#define PDF_BASE_H

#include <ostream>
#include <string>
#include <vector>
#include <map>
#include <variant>

namespace pdf {

struct ObjRef {
  unsigned long num;
  unsigned long gen;
};

inline bool operator< (const ObjRef& lhs, const ObjRef& rhs) {
  return lhs.num < rhs.num ||
    (lhs.num == rhs.num && lhs.gen < rhs.gen);
}

inline bool operator== (const ObjRef& lhs, const ObjRef& rhs) {
  return lhs.num == rhs.num && lhs.gen == rhs.gen;
}

namespace internal {

class ObjBase {
  public:
  virtual void dump(std::ostream& os, unsigned off) const = 0;
  virtual bool failed() const { return false; }

  protected:
  virtual ~ObjBase() { }
};

template<typename... Ts>
class tagged_union : public ObjBase {
  std::variant<Ts...> contents;

  public:

  tagged_union() = default;
  tagged_union(const tagged_union&) = default;
  tagged_union(tagged_union&&) = default;
  tagged_union(std::variant<Ts...>&& contents_)
    : contents(std::move(contents_)) { }
  tagged_union& operator=(const tagged_union&) = default;
  tagged_union& operator=(tagged_union&&) = default;
  ~tagged_union() = default;

  template<typename T>
  bool is() const { return std::holds_alternative<T>(contents); }

  template<typename T>
  const T& get() const { return std::get<T>(contents); }

  template<typename T>
  T&& get() && { return std::move(std::get<T>(contents)); }

  bool failed() const override {
    return std::visit([](auto&& arg) { return arg.failed(); }, contents);
  }

  void dump(std::ostream& os, unsigned off) const override {
    std::visit([&os, off](auto&& arg) { arg.dump(os, off); }, contents);
  }
};

} // namespace pdf::internal

/***** PDF object types *****/

struct Object;

class Null : public internal::ObjBase {
  public:
  void dump(std::ostream& os, unsigned off) const override;
};

class Boolean : public internal::ObjBase {
  bool val;

  public:
  Boolean(bool val_) : val(val_) { }

  operator bool() const { return val; }

  void dump(std::ostream& os, unsigned off) const override;
};

class Numeric : public internal::ObjBase {
  long val_s;
  int dp;

  public:
  Numeric(long val) : val_s(val), dp(0) { }
  Numeric(std::string str);

  bool integral() const { return dp == 0; }
  bool uintegral() const { return integral() && val_s >= 0; }
  bool failed() const override { return dp < 0; }
  bool valid() const { return !failed(); }

  long val_long() const;
  unsigned long val_ulong() const;

  void dump(std::ostream& os, unsigned off) const override;
};

class String : public internal::ObjBase {
  std::string val;
  bool hex;
  std::string error;

  public:
  template<typename V, typename E>
  String(V&& val_, bool hex_, E&& err_)
    : val(std::forward<V>(val_)), hex(hex_), error(std::forward<E>(err_)) { }

  operator const std::string&() const { return val; }

  bool failed() const override { return !error.empty(); }
  void dump(std::ostream& os, unsigned off) const override;
};

class Name : public internal::ObjBase {
  std::string val;

  public:
  template<typename V>
  Name(V&& val_) : val(std::forward<V>(val_)) { }

  operator const std::string&() const { return val; }
  bool operator== (const char* cstr) const { return val == cstr; }

  void dump(std::ostream& os, unsigned off) const override;
};

class Array : public internal::ObjBase {
  std::vector<Object> val;
  std::string error;

  public:
  template<typename V, typename E>
  Array(V&& val_, E&& err_)
    : val(std::forward<V>(val_)), error(std::forward<E>(err_)) { }

  const std::vector<Object>& items() const { return val; }

  bool failed() const override { return !error.empty(); }
  void dump(std::ostream& os, unsigned off) const override;
};

class Dictionary : public internal::ObjBase {
  std::map<std::string, Object> val;
  std::string error;

  public:
  template<typename V, typename E>
  Dictionary(V&& val_, E&& err_)
    : val(std::forward<V>(val_)), error(std::forward<E>(err_)) { }

  const Object& lookup(const std::string& key) const;

  bool failed() const override { return !error.empty(); }
  void dump(std::ostream& os, unsigned off) const override;
};

class Stream : public internal::ObjBase {
  Dictionary _dict;
  std::string _data;
  std::string _error;

  public:
  template<typename D, typename V, typename E>
  Stream(D&& dict_, V&& data_, E&& err_)
    : _dict(std::forward<D>(dict_)),
      _data(std::forward<V>(data_)),
      _error(std::forward<E>(err_)) { }

  const Dictionary& dict() const { return _dict; }
  const std::string& data() const { return _data; }

  bool failed() const override { return _dict.failed() || !_error.empty(); }
  void dump(std::ostream& os, unsigned off) const override;
};

class Indirect : public internal::ObjBase {
  unsigned long num;
  unsigned long gen;

  public:
  Indirect(unsigned long num_, unsigned long gen_) : num(num_), gen(gen_) { }

  void dump(std::ostream& os, unsigned off) const override;
};

class Invalid : public internal::ObjBase {
  std::string error;

  public:
  Invalid(std::string&& error_) : error(std::move(error_)) { }

  const std::string& get_error() const { return error; }

  bool failed() const override { return true; }
  void dump(std::ostream& os, unsigned off) const override;
};

using _Object = internal::tagged_union<
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

struct Object : public _Object {
  using _Object::_Object;
  operator bool() const { return !is<Null>() && !is<Invalid>(); }
};

/***** iostream interface *****/

inline std::ostream& operator<< (std::ostream& os, const internal::ObjBase& obj) {
  obj.dump(os, 0);
  return os;
}

} // namespace pdf

#endif
