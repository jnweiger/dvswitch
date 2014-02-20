build : build/Makefile
	$(MAKE) -C build

build/Makefile :
	mkdir -p build
	cd build && cmake ..

clean :
	rm -rf build

% : build/Makefile
	make -C build $*

.PHONY : build clean
