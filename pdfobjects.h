#ifndef PDF_OBJECTS_H
#define PDF_OBJECTS_H

#include <ostream>
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <utility>
#include <optional>

namespace pdf {

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

  bool failed() const override {
    return std::visit([](auto&& arg) { return arg.failed(); }, contents);
  }

  void dump(std::ostream& os, unsigned off) const override {
    std::visit([&os, off](auto&& arg) { arg.dump(os, off); }, contents);
  }
};

} // namespace pdf::internal

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

  void dump(std::ostream& os, unsigned off) const override;
};

class Array : public internal::ObjBase {
  std::vector<Object> val;
  std::string error;

  public:
  template<typename V, typename E>
  Array(V&& val_, E&& err_)
    : val(std::forward<V>(val_)), error(std::forward<E>(err_)) { }

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

  std::optional<Object> lookup(const std::string& key) const;

  bool failed() const override { return !error.empty(); }
  void dump(std::ostream& os, unsigned off) const override;
};

class Stream : public internal::ObjBase {
  Dictionary dict;
  std::string data;
  std::string error;

  public:
  template<typename D, typename V, typename E>
  Stream(D&& dict_, V&& data_, E&& err_)
    : dict(std::forward<D>(dict_)),
      data(std::forward<V>(data_)),
      error(std::forward<E>(err_)) { }

  bool failed() const override { return dict.failed() || !error.empty(); }
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
  Invalid() : error{} { }
  Invalid(std::string&& error_) : error(std::move(error_)) { }

  const std::string& get_error() const { return error; }

  bool failed() const override { return true; }
  void dump(std::ostream& os, unsigned off) const override;
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

class NamedObject : public internal::ObjBase {
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

  std::pair<unsigned long, unsigned long> numgen() const { return {num, gen}; }

  bool failed() const override { return contents.failed() || !error.empty(); }
  void dump(std::ostream& os, unsigned off) const override;
};

class XRefTable : public internal::ObjBase {
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

  const Object& trailer() const { return _trailer; }

  void dump(std::ostream& os, unsigned off) const override;
};

class StartXRef : public internal::ObjBase {
  unsigned long val;

  public:
  StartXRef(unsigned long val_) : val(val_) { }

  void dump(std::ostream& os, unsigned off) const override;
};


inline std::ostream& operator<< (std::ostream& os, const internal::ObjBase& obj) {
  obj.dump(os, 0);
  return os;
}

} // namespace pdf

#endif
