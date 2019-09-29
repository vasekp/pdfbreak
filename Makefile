CXXFLAGS = -std=c++17 -g -Wall -Wextra -pedantic -fno-diagnostics-show-caret -Izstr/src/ -lz

HEADERS = pdf.h

pdfbreak: pdfbreak.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $< -o $@

all: pdfbreak

.PHONY: all
