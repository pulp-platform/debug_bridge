CXX=g++


all: debug_bridge

debug_bridge: main.cpp
	$(CXX) -o $@ $^
