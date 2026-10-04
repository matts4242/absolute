CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

tank: tank.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

run: tank
	./tank

clean:
	rm -f tank

.PHONY: run clean
