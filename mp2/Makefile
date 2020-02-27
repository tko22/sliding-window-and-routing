CC=/usr/bin/gcc
CC_OPTS=-g3 -std=gnu11
CC_LIBS=
CC_DEFINES=
CC_INCLUDES=
CC_ARGS=${CC_OPTS} ${CC_LIBS} ${CC_DEFINES} ${CC_INCLUDES}

#target "all" depends on all others
all: vec_router ls_router

vec_router: vec_router.c
	@${CC} ${CC_ARGS} -o vec_router vec_router.c

ls_router: ls_router.c
	@${CC} ${CC_ARGS} -o ls_router ls_router.c

clean:
	@rm -rf vec_router ls_router ls_router.dSYM vec_router.dSYM