
linux:
	cc -O2 -std=gnu11 -fPIC -Wall -shared -o libdatetimeformatter.so datetimeformatter.c -llua

macos:
	cc -O2 -std=c11 -fPIC -Wall -dynamiclib -o libdatetimeformatter.dylib datetimeformatter.c -llua

mingw:
	cc -O2 -std=gnu11 -fPIC -Wall -shared -o libdatetimeformatter.dll datetimeformatter.c -I/usr/local/include/ -L/usr/local/lib -llua54

install:
	mkdir -p /usr/local/lib	# just for ensuring that the dest dir exists
	mkdir -p /usr/local/include	# just for ensuring that the dest dir exists
	mv libdatetimeformatter.so /usr/local/lib
	cp datetimeformatter.h /usr/local/include

install-macos:
	mkdir -p /usr/local/lib	# just for ensuring that the dest dir exists
	mkdir -p /usr/local/include	# just for ensuring that the dest dir exists
	mv libdatetimeformatter.dylib /usr/local/lib/
	cp datetimeformatter.h /usr/local/include

install-mingw:
	mkdir -p /usr/local/lib	# just for ensuring that the dest dir exists
	mkdir -p /usr/local/include	# just for ensuring that the dest dir exists
	mv libdatetimeformatter.dll /usr/local/lib/
	cp datetimeformatter.h /usr/local/include
