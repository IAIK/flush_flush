all: spy
clean:
	rm -f *.o spy
spy: spy.cpp ../../cacheutils.h
	g++ -std=gnu++11 -O2 -o $@ $< -I/home/dgruss/openssl -L/home/dgruss/openssl -lcrypto
