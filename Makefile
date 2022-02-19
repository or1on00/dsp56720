.PHONY: all dsp56300

all: dsp56720emu

dsp56720emu: $(patsubst %.cpp,%.o,$(shell find -name '*.cpp' ! -path './dsp56300/*')) | dsp56300
	g++ -o $@ $^ \
                -L dsp56300/source/dsp56kEmu \
                -L dsp56300/source/asmjit \
                -ldsp56kEmu \
                -lasmjit \
                -lrt \
                -lpthread \
                -lreadline \
		$(shell pkg-config fuse3 --libs)

-include $(shell find -name '*.d')

%.o: %.cpp
	g++ -g -c -MMD -MP -I dsp56300/source -I dsp56300/source/asmjit/src $< -o $@ \
		$(shell pkg-config fuse3 --cflags)

dsp56300:
	$(MAKE) -C $@
