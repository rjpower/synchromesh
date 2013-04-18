$(shell mkdir -p build)

SRC := $(wildcard src/*.cc)
HEADERS := $(wildcard */*.h) Makefile
INCLUDE := -I. -Isrc -Impi-rpc 
OBJ := $(patsubst src/%.cc,build/%.o,$(SRC))

TEST_SRC := $(wildcard test/*.cc)
TESTS := $(patsubst test/%.cc,build/%,$(TEST_SRC))

CFLAGS := -Wall -ggdb2 -pthread
CXXFLAGS := $(CFLAGS) -std=c++11 
CXX := mpic++

LDFLAGS := 

build/% : test/%.cc build/libsync.a $(HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDE) $< -o $@ -Lbuild/ $(LDFLAGS) -lsync -lpth -lboost_thread

build/%.o : src/%.cc $(HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDE) $< -c -o $@

all: build/libsync.a $(TESTS)

test: $(TESTS)
	for t in $(TESTS); do echo Running $$t; mpirun -n 1 $$t; done

clean:
	rm -rf build/*

build/libsync.a : $(OBJ) $(HEADERS)
	ar rcs $@ $(OBJ)
	ranlib $@
