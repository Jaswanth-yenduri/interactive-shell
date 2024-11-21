CC		= cc -std=c99
# ASAN mode
#CC		= clang -std=c99 -fsanitize=address -fno-omit-frame-pointer
CXXFLAGS	= -O0 -Wall -pedantic -g3      # Debug mode
#CXXFLAGS	= -O3 -Wall -pedantic -DNDEBUG # Production mode
LEX	        = lex
LEXSRC		= lex.yy.c
LEXLIB		= -lfl
YACC		= yacc -d
YACCSRC		= y.tab.c y.tab.h
LDFLAGS		= $(LEXLIB)
OBJS		= \
	y.tab.o \
	lex.yy.o \
	main.o \
	err.o \
	utils.o \
	array.o \
	cmd.o \
	bltin.o \
	jobs.o \
	env.o

PROGNAME	= ish

$(PROGNAME): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

$(LEXSRC): ish.l
	$(LEX) $<
	make depend

$(YACCSRC): ish.y
	$(YACC) $<
	make depend

.SUFFIXES : .o .c
.c.o:
	$(CC) $(CXXFLAGS) -c $<

depend:
	$(CC) -E -MM *.c > .depend

clean:
	rm -f *.o *.core *~ $(YACCSRC) $(LEXSRC) $(PROGNAME)
