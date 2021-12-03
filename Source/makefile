all : VM.out

VM.out:VM.o
	gcc -o VM.out VM.o

VM.o:VM.c
	gcc -c -o VM.o VM.c

clean :
	rm *.o
