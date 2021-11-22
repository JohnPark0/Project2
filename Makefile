all : RR.out

RR.out:RR.o
	gcc -o RR.out RR.o

RR.o:RR.c
	gcc -c -o RR.o RR.c

clean :
	rm *.o
