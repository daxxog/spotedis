#include <pthread.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hiredis/hiredis.h>
#include <libspotify/api.h>

#include "spotify_appkey.h"
#include "spotify_login.h"
#include "spotedis.h"

int main(int argc, char **argv) {
    unsigned int j;
    const char *hostname = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = (argc > 2) ? atoi(argv[2]) : 6379;

    struct timeval timeout = { 1, 500000 }; // 1.5 seconds
    g_context = redisConnectWithTimeout(hostname, port, timeout);
    if(g_context == NULL || g_context->err) {
        if(g_context) {
            printf("Connection error: %s\n", g_context->errstr);
            redisFree(g_context);
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

    //disconnect
    redisFree(g_context);

    return 0;
}

void dbg(const char* fx) {
    log("\x1b[35m%s();\x1b[0m\n", fx);
}

/* START CUSTOM CALLBACKS */
static void grab_track() {
    dbg("grab_track");
    reset_md();
    g_reply = redisCommand(g_context, "BLPOP %s %d", REDIS_LIST, 0);
    if(g_reply->type == REDIS_REPLY_ARRAY) {
        if(strcmp(g_reply->element[0]->str, REDIS_LIST) == 0) {
            if(!is_playing) {
                play_track(g_reply->element[1]->str);
                notify_main_thread(g_session);
            }
        }
    }
    freeReplyObject(g_reply);
}

static void play_track(const char *uri) {
    dbg("play_track");
    if((int)strlen(uri) < URI_MAX_SIZE) {
        strcpy(g_uri, uri);
        g_link = sp_link_create_from_string(g_uri);
        g_uri_set = 1;
        if(sp_link_type(g_link) == SP_LINKTYPE_TRACK){
            if(!is_playing) {
                notify_player_load = 1;
            }
        }
    } 
}

static void player_load(sp_session *session) {
    int sspl = -1;
    int sspp = -1;

    dbg("player_load");
    is_playing = 1;

    if(g_sspl != SP_ERROR_IS_LOADING) {
        //dbg("sp_session_player_load");
        sspl = (int)sp_session_player_load(session, sp_link_as_track(g_link));
        //dbg("sp_session_player_play");
        sspp = (int)sp_session_player_play(session, 1);
        //dbg("continue");
    }

    if((sspl == SP_ERROR_OK) && (sspp == SP_ERROR_OK)) {
        notify_player_load = 0;
        log("notify_player_load:%d\n", notify_player_load);
    } else {
        if(g_sspl == -1) {
            g_sspl = sspl;
        }

        log("sspl:%d\n", sspl);
        log("sspp:%d\n", sspp);
        log("g_sspl:%d\n", g_sspl);
    }
}

static void reset_md() {
    dbg("reset_md");
    md_flag = 0;
    md_sent_format = 0;
}
/* END CUSTOM CALLBACKS */


/* START CALLBACKS */
static void logged_in(sp_session *session, sp_error error) {
    dbg("logged_in");
    sp_session_set_private_session(session, 1);
    sp_session_preferred_bitrate(session, SP_BITRATE_320k);
    sp_session_preferred_offline_bitrate(session, SP_BITRATE_320k, 1);

    grab_track();

    //sp_session_logout(session);
}

static void logged_out(sp_session *session) {
    dbg("logged_out");
    notify_logout = 1;
    notify_main_thread(session);
}

static void metadata_updated(sp_session *session) {
    dbg("metadata_updated");
    if(!md_flag && g_uri_set && notify_player_load) {
        player_load(session);
    }
}

static void connection_error(sp_session *session, sp_error error) {
    dbg("connection_error");
}

static void message_to_user(sp_session *session, const char *message) {
    fprintf(stderr,"\x1b[36m%s\x1b[0m", message);
}

void notify_main_thread(sp_session *session) {
    dbg("notify_main_thread");

    g_sspl = -1;

    pthread_mutex_lock(&notify_mutex);
    notify_events = 1;
    pthread_cond_signal(&notify_cond);
    pthread_mutex_unlock(&notify_mutex);
}

static int music_delivery(sp_session *session, const sp_audioformat *format, const void *frames, int num_frames) {
    size_t frames_size;

    dbg("music_delivery");

    frames_size = num_frames * sizeof(int16_t) * format->channels;

    log("sample_rate:%d\n", format->sample_rate);
    log("channels:%d\n", format->channels);
    log("num_frames:%d\n", num_frames);
    log("frames_size:%d\n", (int)frames_size);

    if(is_playing) {
        md_flag = 1;

        if(!md_sent_format) {
            md_sent_format = 1;
            g_reply = redisCommand(g_context, "PUBLISH %s:format %d_%d_%d_%d", g_uri, format->sample_rate, format->channels, num_frames, (int)frames_size);
            freeReplyObject(g_reply);
        }
    }

    g_reply = redisCommand(g_context, "PUBLISH %s:data %b", g_uri, frames, frames_size);
    freeReplyObject(g_reply);

    return num_frames;
}

static void play_token_lost(sp_session *session) {
    dbg("play_token_lost");
}

static void log_message(sp_session *session, const char *data) {
    fprintf(stderr,"\x1b[36m%s\x1b[0m", data);
}

static void end_of_track(sp_session *session) {
    dbg("end_of_track");
    is_playing = 0;
    g_uri_set = 0;
    sp_session_player_unload(session);
    g_reply = redisCommand(g_context, "PUBLISH %s:end 1", g_uri);
    freeReplyObject(g_reply);
    grab_track();
}

static void streaming_error(sp_session *session, sp_error error) {
    dbg("streaming_error");
}

static void userinfo_updated(sp_session *session) {
    dbg("userinfo_updated");
    if(!md_flag && g_uri_set && notify_player_load) {
        player_load(session);
    }
}

static void start_playback(sp_session *session) {
    dbg("start_playback");
}

static void stop_playback(sp_session *session) {
    dbg("stop_playback");
}

static void get_audio_buffer_stats(sp_session *session, sp_audio_buffer_stats *stats) {
    //dbg("get_audio_buffer_stats");
}

static void offline_status_updated(sp_session *session) {
    dbg("offline_status_updated");
}

static void offline_error(sp_session *session, sp_error error) {
    dbg("offline_error");
}

static void credentials_blob_updated(sp_session *session, const char *blob) {
    dbg("credentials_blob_updated");
}
/* END CALLBACKS */

void spotify() {
    dbg("spotify");
    spotify_debug();
    spotify_main();
}

void spotify_debug() {
    dbg("spotify_debug");
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
    config.cache_location = CACHE_LOCATION;

    // The path of the directory to store the settings. 
    // This must be specified.
    // Please read the documentation on preferred values.
    config.settings_location = CACHE_LOCATION;

    // The key of the application. They are generated by Spotify,
    // and are specific to each application using libspotify.
    config.application_key = g_appkey;
    config.application_key_size = g_appkey_size;

    // This identifies the application using some
    // free-text string [1, 255] characters.
    config.user_agent = USER_AGENT;

    // Register the callbacks.
    config.callbacks = &callbacks;

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