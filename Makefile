headers: allocators.gch mmapped_vector.gch
%.gch: %.h
	g++ -Wall -Wextra -fmax-errors=1 $< -o $@
performance: performance.cpp headers
	g++ -Wall -Wextra -fmax-errors=1 -Og -fsanitize=address -g performance.cpp -o performance
test: performance
	./performance
