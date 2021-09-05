# Run this via `FILTER_EXTENSIONS=contrib/filter-extensions/rand_cpp/rand_cpp.a make`
# from the main git directory. That way we inherit useful variables.

ifneq ($(findstring s,$(MAKEFLAGS)),s)
ifndef V
	QUIET_CC       = @echo '   ' CC $@;
	QUIET_CXX       = @echo '   ' CXX $@;
	QUIET_AR       = @echo '   ' AR $@;
endif
endif

FILTER_STATIC_LIB = rand_cpp.a

ALL_CXXFLAGS += -std=c++11

all: $(FILTER_STATIC_LIB)
ifeq ($(MAKELEVEL),0)
	$(error "Run via parent git make")
endif
	@:

$(FILTER_STATIC_LIB): rand.o adapter_functions.o
	$(QUIET_AR)$(AR) $(ARFLAGS) $@ $^

rand.o: rand.cpp
	$(QUIET_CXX)$(CXX) -c $(ALL_CFLAGS) $(ALL_CXXFLAGS) $<

adapter_functions.o: adapter_functions.c
	$(QUIET_CC)$(CC) -c $(ALL_CFLAGS) $<

clean:
	$(RM) $(FILTER_STATIC_LIB) rand.o

.PHONY: all clean
