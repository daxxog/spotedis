spotedis: spotedis.c
	gcc -o spotedis spotedis.c spotify_appkey.c spotify_login.c -lhiredis -lspotify