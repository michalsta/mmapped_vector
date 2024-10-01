all: allocators.gch mmapped_vector.gch
%.gch: %.h
	g++ -Wall -Wextra -fmax-errors=1 $< -o $@
