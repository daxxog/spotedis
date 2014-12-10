#include <pthread.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hiredis/hiredis.h>
#include <libspotify/api.h>

#include "spotify_appkey.h"
#include "spotify_login.h"
#include "example.h"

int main(int argc, char **argv) {
    unsigned int j;
    redisContext *c;
    redisReply *reply;
    const char *hostname = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = (argc > 2) ? atoi(argv[2]) : 6379;

    struct timeval timeout = { 1, 500000 }; // 1.5 seconds
    c = redisConnectWithTimeout(hostname, port, timeout);
    if (c == NULL || c->err) {
        if (c) {
            printf("Connection error: %s\n", c->errstr);
            redisFree(c);
        } else {
            printf("Connection error: can't allocate redis context\n");
        }
        exit(1);
    }

    /* START SPOTIFY */
    log("\x1b[32mSTART SPOTIFY\n");
    log("----------------------------------------\x1b[0m\n");

    spotify();

    log("\x1b[32m----------------------------------------\n");
    log("END SPOTIFY\x1b[0m\n\n");
    /* END SPOTIFY */

    /* PING server */
    reply = redisCommand(c,"PING");
    printf("PING: %s\n", reply->str);
    freeReplyObject(reply);

    /* Set a key */
    reply = redisCommand(c,"SET %s %s", "foo", "hello world");
    printf("SET: %s\n", reply->str);
    freeReplyObject(reply);

    /* Set a key using binary safe API */
    reply = redisCommand(c,"SET %b %b", "bar", (size_t) 3, "hello", (size_t) 5);
    printf("SET (binary API): %s\n", reply->str);
    freeReplyObject(reply);

    /* Try a GET and two INCR */
    reply = redisCommand(c,"GET foo");
    printf("GET foo: %s\n", reply->str);
    freeReplyObject(reply);

    reply = redisCommand(c,"INCR counter");
    printf("INCR counter: %lld\n", reply->integer);
    freeReplyObject(reply);
    /* again ... */
    reply = redisCommand(c,"INCR counter");
    printf("INCR counter: %lld\n", reply->integer);
    freeReplyObject(reply);

    /* Create a list of numbers, from 0 to 9 */
    reply = redisCommand(c,"DEL mylist");
    freeReplyObject(reply);
    for (j = 0; j < 10; j++) {
        char buf[64];

        snprintf(buf,64,"%d",j);
        reply = redisCommand(c,"LPUSH mylist element-%s", buf);
        freeReplyObject(reply);
    }

    /* Let"s check what we have inside the list */
    reply = redisCommand(c,"LRANGE mylist 0 -1");
    if (reply->type == REDIS_REPLY_ARRAY) {
        for (j = 0; j < reply->elements; j++) {
            printf("%u) %s\n", j, reply->element[j]->str);
        }
    }
    freeReplyObject(reply);

    /* Disconnects and frees the context */
    redisFree(c);

    return 0;
}

void spotify_debug() {
    log("\x1b[33m");

    log("g_appkey: ");
    for(int i = 0; i<g_appkey_size; i++) {
        log("%02X ", g_appkey[i]);
    }
    log("\n");
    log("g_appkey_size: %d\n", (int)g_appkey_size);
    log("g_username: %s\n", g_username);
    log("USER_AGENT: %s\n", USER_AGENT);
    log("SPOTIFY_API_VERSION: %d\n", SPOTIFY_API_VERSION);

    log("\x1b[0m");
}

void spotify() {
    spotify_debug();
    spotify_main();
}

void dbg(const char* fx) {
    log("\x1b[35m%s();\x1b[0m\n", fx);
}

static void connection_error(sp_session *session, sp_error error) {
    dbg("connection_error");
}

static void logged_in(sp_session *session, sp_error error) {
    dbg("logged_in");
    sp_session_set_private_session(session, 1);
    sp_session_logout(session);
}

static void logged_out(sp_session *session) {
    dbg("logged_out");
    notify_logout = 1;
    notify_main_thread(session);
}

static void log_message(sp_session *session, const char *data) {
    fprintf(stderr,"\x1b[36m%s\x1b[0m",data);
}

void notify_main_thread(sp_session *session) {
    dbg("notify_main_thread");
    pthread_mutex_lock(&notify_mutex);
    notify_events = 1;
    pthread_cond_signal(&notify_cond);
    pthread_mutex_unlock(&notify_mutex);
}

int spotify_init(const char *username,const char *password) {
    dbg("spotify_init");
    sp_session_config config;
    sp_error error;
    sp_session *session;
    
    /// The application key is specific to each project, and allows Spotify
    /// to produce statistics on how our service is used.
    //extern const char g_appkey[];
    /// The size of the application key.
    //extern const size_t g_appkey_size;

    // Always do this. It allows libspotify to check for
    // header/library inconsistencies.
    config.api_version = SPOTIFY_API_VERSION;

    // The path of the directory to store the cache. This must be specified.
    // Please read the documentation on preferred values.
    config.cache_location = "tmp";

    // The path of the directory to store the settings. 
    // This must be specified.
    // Please read the documentation on preferred values.
    config.settings_location = "tmp";

    // The key of the application. They are generated by Spotify,
    // and are specific to each application using libspotify.
    config.application_key = g_appkey;
    config.application_key_size = g_appkey_size;

    // This identifies the application using some
    // free-text string [1, 255] characters.
    config.user_agent = USER_AGENT;

    // Register the callbacks.
    config.callbacks = &callbacks;

    log(""); //prevents SEGFAULT ??

    error = sp_session_create(&config, &session);
    if (SP_ERROR_OK != error) {
        fprintf(stderr, "failed to create session: %s\n",
                        sp_error_message(error));
        return 2;
    }

    // Login using the credentials given on the command line.
    error = sp_session_login(session, username, password, 0, g_blob);

    if (SP_ERROR_OK != error) {
        fprintf(stderr, "failed to login: %s\n",
                        sp_error_message(error));
        return 3;
    }

    g_session = session;
    return 0;
}

int spotify_main() {
    dbg("spotify_main");
    int next_timeout = 0;

    pthread_mutex_init(&notify_mutex, NULL);
    pthread_cond_init(&notify_cond, NULL);

    if(spotify_init(g_username, g_password) != 0) {
        fprintf(stderr,"Spotify failed to initialize\n");
        exit(-1);
    }

    pthread_mutex_lock(&notify_mutex);
    for (;;) {
        if (next_timeout == 0) {
            while(!notify_events)
                pthread_cond_wait(&notify_cond, &notify_mutex);
        } else {
            struct timespec ts;

    #if _POSIX_TIMERS > 0
            clock_gettime(CLOCK_REALTIME, &ts);
    #else
            struct timeval tv;
            gettimeofday(&tv, NULL);
            TIMEVAL_TO_TIMESPEC(&tv, &ts);
    #endif

            ts.tv_sec += next_timeout / 1000;
            ts.tv_nsec += (next_timeout % 1000) * 1000000;

            while(!notify_events) {
                if(pthread_cond_timedwait(&notify_cond, &notify_mutex, &ts))
                    break;
            }
        }

        if(notify_logout) {
            break;
        }

        // Process libspotify events
        notify_events = 0;
        pthread_mutex_unlock(&notify_mutex);

        do {
            sp_session_process_events(g_session, &next_timeout);
        } while (next_timeout == 0);

        pthread_mutex_lock(&notify_mutex);
    }
    return 0;
}