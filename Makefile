CXXFLAGS = -std=c++17 -g -Wall -Wextra -pedantic -fno-diagnostics-show-caret -Izstr/src/ -lz
LDFLAGS = -lstdc++
HEADERS = pdf.h
SOURCES = pdfbreak.cpp pdf.cpp parser.cpp pdfreader.cpp
OBJECTS = $(patsubst %.cpp,%.o,$(SOURCES))

pdfbreak: $(OBJECTS)
	$(CXX) $(LDFLAGS) $^ -o $@

$(OBJECTS): %.o: %.cpp $(HEADERS)
	$(CXX) -c $(CXXFLAGS) $< -o $@

all: pdfbreak

.PHONY: all
