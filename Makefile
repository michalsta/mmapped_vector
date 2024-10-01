headers: allocators.gch mmapped_vector.gch
%.gch: %.h
	$(CXX) -std=c++20 -Wall -Wextra -fmax-errors=1 $< -o $@
performance: performance.cpp headers
	$(CXX) -std=c++20 -Wall -Wextra -fmax-errors=1 -O3 performance.cpp -o performance
test: performance
	./performance
clean:
	rm -f performance *.gch
