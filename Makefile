LDLIBS+=-lasound

all: daalarm

daalarm: daalarm.o

clean:
	rm -f daalarm
	rm -f *.o *.d
