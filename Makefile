CXX ?= g++

a.out: src/main.cpp
	$(CXX) $< -o $@

