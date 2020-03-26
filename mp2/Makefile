CC=/usr/bin/g++
CC_OPTS=-g3 -std=c++11
CC_LIBS=
CC_DEFINES=
CC_INCLUDES=
CC_ARGS=${CC_OPTS} ${CC_LIBS} ${CC_DEFINES} ${CC_INCLUDES}

#target "all" depends on all others
all: vec_router ls_router

vec_router: vec_router.cpp
	@${CC} ${CC_ARGS} -o vec_router vec_router.cpp

ls_router: ls_router.cpp
	@${CC} ${CC_ARGS} -o ls_router ls_router.cpp

manager_send: manager_send.c
	gcc -o manager_send manager_send.c

clean:
	@rm -rf vec_router ls_router ls_router.dSYM vec_router.dSYM manager_send