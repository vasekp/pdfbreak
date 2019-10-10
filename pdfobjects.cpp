#include <cstdio>
#include <cassert>

#include "pdfobjects.h"

namespace pdf {

/***** Implementation of PDF object classes *****/

namespace {

void print_offset(std::ostream& os, unsigned off, const std::string& text) {
  os << std::string(2*off, ' ') << text;
}

} // anonymous namespace

void Null::dump(std::ostream& os, unsigned off) const {
  print_offset(os, off, "null");
}

void Invalid::dump(std::ostream& os, unsigned off) const {
  print_offset(os, off, "null");
  os << '\n';
  print_offset(os, off, "% !!! " + error);
}

void Boolean::dump(std::ostream& os, unsigned off) const {
  print_offset(os, off, val ? "true" : "false");
}

Numeric::Numeric(std::string str) {
  if(str.empty()) {
    dp = -1; //fail
    return;
  }
  auto off = str.find('.');
  char *last;
  int dp_;
  if(off != std::string::npos) {
    str.erase(off, 1);
    dp_ = str.length() - off;
  } else
    dp_ = 0;
  int res = (int)strtol(str.data(), &last, 10);
  if(last != str.data() + str.length()) {
    dp = -1; //fail
    return;
  } else {
    val_s = res;
    dp = dp_;
  }
}

long Numeric::val_long() const {
  assert(integral());
  long ret = val_s;
  int d = dp;
  while(d > 0) {
    ret /= 10;
    d -= 1;
  }
  return ret;
}

unsigned long Numeric::val_ulong() const {
  assert(uintegral());
  unsigned long ret = val_s;
  int d = dp;
  while(d > 0) {
    ret /= 10;
    d -= 1;
  }
  return ret;
}

void Numeric::dump(std::ostream& os, unsigned off) const {
  assert(!failed());
  char buf[20];
  std::snprintf(buf, 20, "%0*li", dp + (val_s < 0 ? 1 : 0) + 1, val_s);
  std::string str{buf};
  if(dp > 0)
    str.insert(str.length() - dp, 1, '.');
  print_offset(os, off, str);
}

void String::dump(std::ostream& os, unsigned off) const {
  if(hex) {
    print_offset(os, off, "< ");
    for(char c : val) {
      char buf[4];
      sprintf(buf, "%02X ", (unsigned char)c);
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

void Name::dump(std::ostream& os, unsigned off) const {
  print_offset(os, off, "/" + val);
}

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

std::optional<Object> Dictionary::lookup(const std::string& key) const {
  auto it = val.find(key);
  if(it == val.end())
    return {};
  else
    return {it->second};
}

void Stream::dump(std::ostream& os, unsigned off) const {
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

void Indirect::dump(std::ostream& os, unsigned off) const {
  print_offset(os, off, "");
  os << num << ' ' << gen << " R";
}

/***** Implementation of PDF top level object classes *****/

void NamedObject::dump(std::ostream& os, unsigned off) const {
  print_offset(os, off, "");
  os << num << ' ' << gen << " obj\n";
  contents.dump(os, off+1);
  os << '\n';
  if(!error.empty()) {
    print_offset(os, off, "% !!! " + error + "\n");
  }
  print_offset(os, off, "endobj");
}

void XRefTable::dump(std::ostream& os, unsigned off) const {
  print_offset(os, off, "xref\n");
  for(const auto& section : _table)
    os << section.start << ' ' << section.count << '\n'
      << section.data /* << '\n' */;
  print_offset(os, off, "trailer\n");
  _trailer.dump(os, off+1);
}

void StartXRef::dump(std::ostream& os, unsigned) const {
  os << "startxref\n" << val << "\n%%EOF";
}

} //namespace pdf
