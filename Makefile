CXXFLAGS = -std=c++17 -g -Wall -Wextra -pedantic -fno-diagnostics-show-caret -Izstr/src/ -lz -fdiagnostics-color=auto
LDFLAGS = -lstdc++ -lz
PROGRAMS = pdfbreak pdfassemble
HEADERS = pdf.h pdfbase.h pdffile.h pdfparser.h pdffilter.h pdfobjstream.h
SOURCES_COMMON = pdfbase.cpp pdffile.cpp pdfparser.cpp pdffilter.cpp pdfobjstream.cpp
OBJECTS_COMMON = $(patsubst %.cpp,%.o,$(SOURCES_COMMON))
SOURCES_SPEC = $(patsubst %,%.cpp,$(PROGRAMS))
OBJECTS_SPEC = $(patsubst %.cpp,%.o,$(SOURCES_SPEC))
OBJECTS_ALL = $(OBJECTS_COMMON) $(OBJECTS_SPEC)

all: $(PROGRAMS)

$(PROGRAMS): %: %.o $(OBJECTS_COMMON)
	$(CXX) $(LDFLAGS) $^ -o $@

$(OBJECTS_ALL): %.o: %.cpp $(HEADERS)
	$(CXX) -c $(CXXFLAGS) $< -o $@

.PHONY: all
