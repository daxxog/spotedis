spotedis: example.c
	gcc -o spotedis example.c spotify_appkey.c spotify_login.c -lhiredis -lspotify