all: allocators.gch mmapped_vector.gch
%.gch: %.h
	g++ -Wall -Wextra -fmax-errors=1 $< -o $@
performance: performance.cpp
	g++ -Wall -Wextra -fmax-errors=1 -O3 performance.cpp -o performance
test: performance
	./performance
