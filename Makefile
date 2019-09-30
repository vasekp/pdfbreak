CXXFLAGS = -std=c++17 -g -Wall -Wextra -pedantic -fno-diagnostics-show-caret -Izstr/src/ -lz
LDFLAGS = -lstdc++
HEADERS = pdf.h

pdfbreak: pdfbreak.o pdf.o

pdfbreak.o: pdfbreak.cpp $(HEADERS)
#	$(CXX) $(CXXFLAGS) $< -o $@

pdf.o: pdf.cpp $(HEADERS)

all: pdfbreak

.PHONY: all
