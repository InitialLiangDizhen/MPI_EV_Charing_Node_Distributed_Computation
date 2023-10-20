all: assignment2

assignment2: a2.c
	mpicc -lpthread a2.c -o a2.o -lm

run: 
	mpirun -oversubscribe -np 5 a2.o 2 2
clean:
	/bin/rm -f *.txt