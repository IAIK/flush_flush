all: spy
clean:
	rm -f *.o spy
spy: spy.cpp ../../cacheutils.h
	g++ -std=gnu++11 -O3 -o $@ $< -I/home/dgruss/openssl -L/home/dgruss/openssl -lcrypto
