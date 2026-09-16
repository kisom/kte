.PHONY: configure build test gate

configure:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_GUI=ON -DBUILD_TESTS=ON

build:
	cmake --build build

test:
	./build/kte_tests

gate: configure build test
