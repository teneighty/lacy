
PREFIX = ${HOME}/local/

EXE = lacy
CFLAGS = -g -Wall -Imarkdown 
LDFLAGS = -g -Lmarkdown -lmarkdown
SRC = lacy.c
OBJ = ${SRC:.c=.o}

SPLINTFLAGS = -Imarkdown +posixlib 

all: ${EXE}

${EXE}: markdown/libmarkdown.a ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

# this rule should only be executed once
markdown/libmarkdown.a: 
	@echo "Fetching and building discount"
	@ git submodule init
	@ git submodule update
	@ cd markdown; ./configure.sh > /dev/null; make > /dev/null;

clean: 
	@echo cleaning
	@rm -f ${EXE} *.o 

fullclean: clean
	@cd markdown; make clean

install: all
	@mkdir -p ${PREFIX}/bin
	@cp -f ${EXE} ${PREFIX}/bin
	@chmod 755 ${PREFIX}/bin/${EXE}

uninstall:
	@echo removing executable file from ${PREFIX}/bin
	@rm -f ${PREFIX}/bin/dwm

splint:
	splint ${SPLINTFLAGS} ${SRC}

.PHONY: all clean fullclean install uninstall splint
