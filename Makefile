CXXFLAGS = -std=c++17 -g -Wall -Wextra -pedantic -fno-diagnostics-show-caret -Izstr/src/ -lz -fdiagnostics-color=auto
LDFLAGS = -lstdc++ -lz
HEADERS = pdfobjects.h pdfparser.h pdffilter.h
SOURCES = pdfbreak.cpp pdfobjects.cpp pdfparser.cpp pdffilter.cpp
OBJECTS = $(patsubst %.cpp,%.o,$(SOURCES))

pdfbreak: $(OBJECTS)
	$(CXX) $(LDFLAGS) $^ -o $@

$(OBJECTS): %.o: %.cpp $(HEADERS)
	$(CXX) -c $(CXXFLAGS) $< -o $@

all: pdfbreak

.PHONY: all
