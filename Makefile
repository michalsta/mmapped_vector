headers: allocators.gch mmapped_vector.gch

MYCXX=$(CXX) -std=c++20 -Wall -Wextra -fmax-errors=1
OPT=$(MYCXX) -O3
DBG=$(MYCXX) -Og -g -D_GLIBCXX_DEBUG
ASAN=$(DBG) -fsanitize=address
%.gch: %.h
	$(CXX) -std=c++20 -Wall -Wextra -fmax-errors=1 $< -o $@
performance: performance.cpp performance_threaded.cpp headers
	#$(CXX) -std=c++20 -Wall -Wextra -fmax-errors=1 -Og -g performance.cpp -o performance
	#$(CXX) -std=c++20 -Wall -Wextra -fmax-errors=1 -Og -g -fsanitize=address performance.cpp -o performance
	$(OPT)  performance.cpp -o performance
	#$(OPT) performance_threaded.cpp -o performance
correctness: correctness.cpp headers
	#$(DBG) correctness.cpp -o correctness
	$(ASAN) correctness.cpp -o correctness
test: performance
	./performance
clean:
	rm -f performance *.gch
