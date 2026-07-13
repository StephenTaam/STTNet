CXX ?= g++
CPPFLAGS ?= $(shell pkg-config --cflags jsoncpp openssl 2>/dev/null)
CXXFLAGS ?= -std=c++17 -O3 -DNDEBUG -Wall -Wextra -Wpedantic
LDLIBS ?= -ljsoncpp -lssl -lcrypto -lpthread

.PHONY: all clean debug sanitize test

all: main

main: main.cpp src/sttnet.cpp include/sttnet.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ main.cpp src/sttnet.cpp $(LDLIBS)

debug: CXXFLAGS = -std=c++17 -O0 -g3 -Wall -Wextra -Wpedantic
debug: clean main

sanitize: CXXFLAGS = -std=c++17 -O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined
sanitize: LDLIBS += -fsanitize=address,undefined
sanitize: clean main

test:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build --parallel
	ctest --test-dir build --output-on-failure

clean:
	rm -f main
	rm -rf build
