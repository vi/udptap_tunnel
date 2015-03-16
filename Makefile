udptap_tunnel: udptap.c
		${CC} ${CFLAGS}  -Wall udptap.c -lmcrypt ${LDFLAGS} -o udptap_tunnel
		
tap_mcrypt: tap_mcrypt.c
		${CC} ${CFLAGS} -Wall tap_mcrypt.c -lmcrypt ${LDFLAGS} -o tap_mcrypt
