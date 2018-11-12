.PHONY: tigine
tigine:
	g++ -Wall -Wextra -pedantic -Wno-class-memaccess -g main.cpp -o tigine \
            -lglfw -ldl -lassimp
