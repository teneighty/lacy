
CFLAGS = -Lmarkdown -Imarkdown
LDFLAGS = -Lmarkdown -lmarkdown
SRC = lacy.c
OBJ = ${SRC:.c=.o}

all: lacy

lacy: markdown/libmarkdown.a ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

# this rule should only be executed once
markdown/libmarkdown.a: 
	@echo "Fetching and building discount"
	@ git submodule init
	@ cd markdown; ./configure.sh > /dev/null; make > /dev/null;

clean: 
	@echo cleaning
	@rm -f lacy *.o 

fullclean: clean
	@cd markdown; make clean

.PHONY: all clean fullclean
