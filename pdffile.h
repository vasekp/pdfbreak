#ifndef PDF_FILE_H
#define PDF_FILE_H

#include <istream>

#include "pdfbase.h"

namespace pdf {

struct Version {
  unsigned major;
  unsigned minor;
};

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
  const Object& object() const { return contents; }

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

  public:
  XRefTable(std::vector<Section>&& table_)
    : _table(std::move(table_)) { }

  void dump(std::ostream& os, unsigned off) const override;
};

class Trailer : public internal::ObjBase {
  Object _dict;
  std::streamoff _start;

public:
  Trailer(Object&& dict_, std::streamoff start_)
    : _dict(std::move(dict_)), _start(start_) { }

  const Object& dict() const { return _dict; }
  std::streamoff start() const { return _start; }

  bool failed() const override { return _dict.failed(); }
  void dump(std::ostream& os, unsigned off) const override;
};

class StartXRef : public internal::ObjBase {
  std::streamoff val;

  public:
  StartXRef(std::streamoff val_) : val(val_) { }

  void dump(std::ostream& os, unsigned off) const override;
};

using _TopLevelObject = internal::tagged_union<
    Null, // EOF
    NamedObject,
    XRefTable,
    Trailer,
    StartXRef,
    Invalid>;

struct TopLevelObject : public _TopLevelObject {
  using _TopLevelObject::_TopLevelObject;
  operator bool() const { return !is<Null>() && !is<Invalid>(); }
};

/***** iostream interface *****/

std::ostream& operator<< (std::ostream& os, Version version);
std::istream& operator>> (std::istream& is, Version& version);

} // namespace pdf

#endif
