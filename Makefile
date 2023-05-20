CXX = g++
CXXFLAGS = -g
LDFLAGS =

SRCS = $(wildcard *.cpp)
OBJS = $(patsubst %.cpp, %.o, $(SRCS))

kvstoreraftsystem: $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ -ljsoncpp -lzmq -lpthread

.PHONY: clean
clean:
	rm -f kvstoreraftsystem $(OBJS)
