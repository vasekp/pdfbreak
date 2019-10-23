CXXFLAGS = -std=c++17 -g -Wall -Wextra -pedantic -fno-diagnostics-show-caret -Izstr/src/ -lz -fdiagnostics-color=auto
LDFLAGS = -lstdc++ -lz
PROGRAMS = pdfbreak pdfassemble
HEADERS = pdfobjects.h pdfparser.h pdffilter.h
SOURCES_COMMON = pdfobjects.cpp pdfparser.cpp pdffilter.cpp
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
