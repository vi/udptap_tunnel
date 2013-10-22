udptap_tunnel: udptap.c
		${CC} ${CFLAGS}  udptap.c -lmcrypt ${LDFLAGS} -o udptap_tunnel
