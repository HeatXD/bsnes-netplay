gekkonet.path ?= .

gekkonet.flags := \
	-I$(gekkonet.path)/include \
	-I$(gekkonet.path)/thirdparty \
	-I$(gekkonet.path)/thirdparty/asio \
	-DGEKKONET_STATIC

gekkonet.objects := \
	obj/gekkonet-gekkonet.o \
	obj/gekkonet-backend.o \
	obj/gekkonet-event.o \
	obj/gekkonet-game_session.o \
	obj/gekkonet-input.o \
	obj/gekkonet-net.o \
	obj/gekkonet-player.o \
	obj/gekkonet-spectator_session.o \
	obj/gekkonet-storage.o \
	obj/gekkonet-stress_session.o \
	obj/gekkonet-sync.o

obj/gekkonet-gekkonet.o:          $(gekkonet.path)/src/gekkonet.cpp
obj/gekkonet-backend.o:           $(gekkonet.path)/src/backend.cpp
obj/gekkonet-event.o:             $(gekkonet.path)/src/event.cpp
obj/gekkonet-game_session.o:      $(gekkonet.path)/src/game_session.cpp
obj/gekkonet-input.o:             $(gekkonet.path)/src/input.cpp
obj/gekkonet-net.o:               $(gekkonet.path)/src/net.cpp
obj/gekkonet-player.o:            $(gekkonet.path)/src/player.cpp
obj/gekkonet-spectator_session.o: $(gekkonet.path)/src/spectator_session.cpp
obj/gekkonet-storage.o:           $(gekkonet.path)/src/storage.cpp
obj/gekkonet-stress_session.o:    $(gekkonet.path)/src/stress_session.cpp
obj/gekkonet-sync.o:              $(gekkonet.path)/src/sync.cpp

gekkonet.options :=
ifeq ($(platform),windows)
gekkonet.options += -lws2_32
endif