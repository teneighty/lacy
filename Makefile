
SRC = lacy.c
OBJ = ${SRC:.c=.o}

all: lacy

lacy: ${OBJ}

${OBJ}: config.h

clean: 
	@echo cleaning
	@rm -f lacy *.o 

.PHONY: all clean
