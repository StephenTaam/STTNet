all:main

main:main.cpp src/sttnet.cpp
	g++ -std=c++17 -o main main.cpp src/sttnet.cpp -ljsoncpp -lssl -lcrypto -lpthread

clean:
	rm -f main
