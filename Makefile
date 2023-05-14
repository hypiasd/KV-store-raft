CXX = g++
CXXFLAGS =
LDFLAGS =

SRCS = $(wildcard *.cpp)
OBJS = $(patsubst %.cpp, %.o, $(SRCS))

kvstoreraftsystem: $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ -ljsoncpp

.PHONY: clean
clean:
	rm -f kvstoreraftsystem $(OBJS)
