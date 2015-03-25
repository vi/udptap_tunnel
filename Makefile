all: \
	tap_mcrypt \
	tap_copy \
	vethify \
	ipvampire \
	seqpackettool \
	udpjump \
	udptap_tunnel

udptap_tunnel: udptap.c
		${CC} ${CFLAGS}  -Wall udptap.c -lmcrypt ${LDFLAGS} -o udptap_tunnel
		
tap_mcrypt: tap_mcrypt.c
		${CC} ${CFLAGS} -Wall tap_mcrypt.c -lmcrypt ${LDFLAGS} -o tap_mcrypt
		
tap_copy: tap_copy.c
		${CC} ${CFLAGS} -Wall tap_copy.c ${LDFLAGS} -o tap_copy
		
vethify: vethify.c
		${CC} ${CFLAGS} -Wall vethify.c ${LDFLAGS} -o vethify
		
ipvampire: ipvampire.c
		${CC} ${CFLAGS} -ggdb -Wall ipvampire.c ${LDFLAGS} -o ipvampire
		
seqpackettool: seqpackettool.c
		${CC} ${CFLAGS} -ggdb -Wall seqpackettool.c ${LDFLAGS} -o seqpackettool
		
udpjump: udpjump.c
		${CC} ${CFLAGS} -ggdb -Wall udpjump.c ${LDFLAGS} -o udpjump
