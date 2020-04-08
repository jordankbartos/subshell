smallsh: smallsh.c
	gcc -o smallsh -g -Wall smallsh.c

debug:
	valgrind -v --show-leak-kinds=all --leak-check=full ./smallsh

clean:
	rm smallsh
