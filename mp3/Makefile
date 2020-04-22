CC=/usr/bin/g++
CC_OPTS=-g3 -std=c++11 -pthread
CC_LIBS=
CC_DEFINES=
CC_INCLUDES=
CC_ARGS=${CC_OPTS} ${CC_LIBS} ${CC_DEFINES} ${CC_INCLUDES}

#target "all" depends on all others
all: reliable_sender reliable_receiver

reliable_sender: reliable_sender.cpp
	@${CC} ${CC_ARGS} -o reliable_sender reliable_sender.cpp

reliable_receiver: reliable_receiver.cpp
	@${CC} ${CC_ARGS} -o reliable_receiver reliable_receiver.cpp

clean:
	@rm -rf reliable_receiver reliable_sender reliable_receiver.dSYM reliable_sender.dSYM