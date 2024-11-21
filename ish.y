%{
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <pwd.h>

#include "cmd.h"

// The abstract syntax tree root.
cmd_t *root;

int yylex(void);
int yyerror(char *);
%}

%union
{
    char		*string;
    int			integer;
    struct cmd		*cmd;
}

%token 	<string>	WORD
%token 	<string>	COMMAND
%token 	<string>	FILENAME
%token	<int>           BACKGROUND
%token	<int>           PIPE
%token	<int>		PIPE_ERROR
%token	<int>           SEMICOLON
%token	<int>		REDIRECT_IN
%token	<int>		REDIRECT_OUT
%token	<int>		REDIRECT_ERROR
%token	<int>		APPEND
%token	<int>		APPEND_ERROR
%token	<string>	OPTION
%token	<string>	STRING
%token	<int>		LOGICAL_AND
%token	<int>		LOGICAL_OR
%type   <cmd>     	parameters
%type   <cmd>		cmd_line
%type   <integer>       separator

%%

cmd_line 	: cmd_line separator COMMAND parameters
                {
                        if ($1) {
                        	cmd_t *last = cmd_last($1);
                        	$4->name = $3;
                                last->next = $4;
                                switch($2) {
                                case SEMICOLON:
                                	last->mode = C_SEQ;
                                        break;
                                case BACKGROUND:
                                	last->mode = C_BGRD;
                                        break;
                                case PIPE:
                                	last->mode = C_PIPE;
                                        break;
                                case PIPE_ERROR:
                                	last->mode = C_PIPEERR;
                                        break;
                                default:
                                	cmd_free($1);
                                        cmd_free($4);
                                        yyerror(NULL);
                                        break;
                                }

                                $$ = $1;
                                root = $$;
                        } else if ($2 == SEMICOLON) {
                        	$4->name = $3;
                                $$ = $4;
                                root = $$;
                        } else {
                        	cmd_free($4);
                                yyerror(NULL);
                        }
                }
		| COMMAND parameters 
		{
		        $2->name = $1;        
		        $$ = $2;
                        root = $$;                        
		}
		| cmd_line BACKGROUND
                {
                	if ($1 == NULL)
                        	yyerror(NULL);
                        cmd_t *last = cmd_last($1);
                        last->mode = C_BGRD;
                        $$ = $1;
                        root = $$;
                }
		| cmd_line SEMICOLON
		| { $$ = NULL; }
		| error { $$ = NULL; }
		;

separator 	: BACKGROUND { $$ = BACKGROUND; };
		| PIPE { $$ = PIPE; }
		| PIPE_ERROR { $$ = PIPE_ERROR; }
		| SEMICOLON { $$ = SEMICOLON; }
		;

parameters	: parameters OPTION
		| parameters STRING { array_append($1->args, $2); }
		| parameters WORD { array_append($1->args, $2); }
		| parameters REDIRECT_IN FILENAME { $1->filein = $3; }
                | parameters REDIRECT_OUT FILENAME { $1->fileout = $3; }
		| parameters REDIRECT_ERROR FILENAME
                {
                	$1->fileout = $3; 
                        $1->redirerr = 1;
                }
		| parameters APPEND FILENAME
                {
                	$1->fileout = $3;
                        $1->append = 1;
                }
		| parameters APPEND_ERROR FILENAME
                {
                	$1->fileout = $3;
                        $1->append = 1;
                        $1->redirerr = 1;
                }
                | { $$ = cmd_new(); }         
		;

%%

int yyerror(char *s)
{
    fprintf(stderr, "syntax error\n");
    return 0;
}
