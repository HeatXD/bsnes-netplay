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
	obj/gekkonet-gekko.o \
	obj/gekkonet-input.o \
	obj/gekkonet-net.o \
	obj/gekkonet-player.o \
	obj/gekkonet-storage.o \
	obj/gekkonet-sync.o

obj/gekkonet-gekkonet.o: $(gekkonet.path)/src/gekkonet.cpp
obj/gekkonet-backend.o:  $(gekkonet.path)/src/backend.cpp
obj/gekkonet-event.o:    $(gekkonet.path)/src/event.cpp
obj/gekkonet-gekko.o:    $(gekkonet.path)/src/gekko.cpp
obj/gekkonet-input.o:    $(gekkonet.path)/src/input.cpp
obj/gekkonet-net.o:      $(gekkonet.path)/src/net.cpp
obj/gekkonet-player.o:   $(gekkonet.path)/src/player.cpp
obj/gekkonet-storage.o:  $(gekkonet.path)/src/storage.cpp
obj/gekkonet-sync.o:     $(gekkonet.path)/src/sync.cpp

gekkonet.options :=
ifeq ($(platform),windows)
gekkonet.options += -lws2_32
endif