all: \
	tap_mcrypt \
	tap_copy \
	vethify \
	udptap_tunnel

udptap_tunnel: udptap.c
		${CC} ${CFLAGS}  -Wall udptap.c -lmcrypt ${LDFLAGS} -o udptap_tunnel
		
tap_mcrypt: tap_mcrypt.c
		${CC} ${CFLAGS} -Wall tap_mcrypt.c -lmcrypt ${LDFLAGS} -o tap_mcrypt
		
tap_copy: tap_copy.c
		${CC} ${CFLAGS} -Wall tap_copy.c ${LDFLAGS} -o tap_copy
		
vethify: vethify.c
		${CC} ${CFLAGS} -Wall vethify.c ${LDFLAGS} -o vethify
